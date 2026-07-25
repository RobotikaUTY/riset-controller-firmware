#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/TomThumb.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===================== BATTERY SENSE (CONTROLLER) =====================
// Pin ADC untuk baca baterai remote lewat voltage divider (belum dipakai fungsi lain)
#define BATTERY_ADC_PIN 34

// Rasio divider = (R1+R2)/R2. Contoh: R1=1k (ke B+), R2=3k (ke GND) -> (1000+3000)/3000 = 1.33
// WAJIB dikalibrasi ulang pakai multimeter sesuai resistor asli yang dipasang.
const float remoteDividerRatio = 1.33f;
const float adcRefVoltage = 3.3f;
const float adcResolution = 4095.0f;

// ===================== BUTTON PINS =====================
// 8-button keypad layout: D-pad + Y/X/A/B
#define BTN_UP 32
#define BTN_DOWN 23
#define BTN_LEFT 19
#define BTN_RIGHT 18
#define BTN_Y 27
#define BTN_X 26
#define BTN_A 25
#define BTN_B 33

// ===================== RECEIVER MAC =====================
uint8_t receiverMAC[] = {0x78, 0x1C, 0x3C, 0x2B, 0xDF, 0xB4};

const uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ===================== CONTROL PACKET =====================
enum ButtonMask : uint8_t {
  BTN_MASK_UP = 1 << 0,
  BTN_MASK_DOWN = 1 << 1,
  BTN_MASK_LEFT = 1 << 2,
  BTN_MASK_RIGHT = 1 << 3,
  BTN_MASK_Y = 1 << 4,
  BTN_MASK_X = 1 << 5,
  BTN_MASK_A = 1 << 6,
  BTN_MASK_B = 1 << 7
};

enum ControlMode : uint8_t {
  MODE_STOP = 0,
  MODE_FORWARD,
  MODE_BACKWARD,
  MODE_TURN_LEFT,
  MODE_TURN_RIGHT,
  MODE_FORWARD_LEFT,
  MODE_FORWARD_RIGHT,
  MODE_BACKWARD_LEFT,
  MODE_BACKWARD_RIGHT,
  MODE_SPIN_LEFT,
  MODE_SPIN_RIGHT
};

enum UiScreen : uint8_t {
  SCREEN_HOME = 0,
  SCREEN_MENU,
  SCREEN_SCAN,
  SCREEN_ABOUT,
  SCREEN_TURBO_SELECT,
  SCREEN_COMING_SOON
};

enum PacketType : uint8_t {
  PACKET_TYPE_CONTROL = 1,
  PACKET_TYPE_DISCOVERY_REQUEST = 2,
  PACKET_TYPE_DISCOVERY_RESPONSE = 3,
  PACKET_TYPE_TELEMETRY = 4
};

typedef struct {
  uint8_t buttons;
  uint8_t speed;
  uint8_t mode;
} ControlPacket;

ControlPacket packet;

typedef struct {
  uint8_t type;
  uint8_t senderMac[6];
  char name[16];
} DiscoveryPacket;

typedef struct {
  uint8_t mac[6];
  char name[16];
} ReceiverEntry;

// Dikirim BALIK oleh receiver (robot) ke controller, isinya tegangan baterai robot saat ini.
// Struct ini harus identik (urutan & tipe field) dengan yang ada di kode receiver.
typedef struct {
  uint8_t type;
  float batteryVoltage;
} TelemetryPacket;

// ===================== STATUS =====================
bool connected = false;
unsigned long lastSuccess = 0;
UiScreen uiScreen = SCREEN_HOME;
uint8_t previousButtons = 0;
uint8_t menuIndex = 0;
char activeReceiverName[16] = "No Device";
float remoteBatteryVoltage = 4.2f;
float robotBatteryVoltage = 12.0f;

// Asumsi chemistry baterai untuk hitung persentase ikon - sesuaikan kalau beda
const float remoteBatteryMinV = 3.3f; // Li-ion 1 sel
const float remoteBatteryMaxV = 4.2f;
const float robotBatteryMinV = 9.0f;  // LiPo 3S
const float robotBatteryMaxV = 12.6f;

// Set Button (swap Y<->A): fiturnya untuk sementara "Coming Soon" dan tidak dipanggil dari menu.
// Fungsi & variabelnya tetap disimpan supaya gampang diaktifkan lagi nanti.
bool buttonsSwapped = false;

// ===================== KECEPATAN =====================
// Default Speed: kecepatan normal saat jalan biasa (tanpa kombinasi A). Fixed di 180 (tidak bisa di-set user).
const uint8_t defaultSpeed = 180;

// Turbo Speed: aktif kalau salah satu arah (UP/DOWN/LEFT/RIGHT) ditekan BERSAMAAN dengan A.
// Nilainya di-custom terpisah, rentang 181-255 (di atas batas atas Default Speed).
uint8_t turboSpeedValue = 255;
const uint8_t turboSpeedMin = 181;
const uint8_t turboSpeedMax = 255;

const uint8_t speedStep = 5;

// Label yang ditampilkan di layar "Coming Soon" (diisi saat masuk dari menu)
const char* comingSoonLabel = "";

const uint8_t maxReceivers = 8;
ReceiverEntry discoveredReceivers[maxReceivers];
uint8_t discoveredReceiverCount = 0;
uint8_t selectedReceiverIndex = 0;
bool scanningActive = false;
unsigned long scanStartedAt = 0;
unsigned long lastDiscoverySendAt = 0;
const unsigned long discoveryIntervalMs = 400;
const unsigned long scanDurationMs = 6000;

char scannerStatus[32] = "Ready";

// Menu utama SETTING - tiap item membuka submenu sendiri saat ditekan A
// (Mode SUMO/SOCCER dihapus - remote sekarang Universal)
const char* menuItemsBase[] = {
  "Pair Device",
  "Set Button",
  "Set Movement",
  "Turbo Speed",
  "About",
  "Exit"
};

const uint8_t menuItemCount = sizeof(menuItemsBase) / sizeof(menuItemsBase[0]);
const uint8_t visibleMenuItems = 3;

// ===================== CALLBACK =====================
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  (void)mac_addr;

  if (status == ESP_NOW_SEND_SUCCESS) {
    connected = true;
    lastSuccess = millis();
  }
}

void onDataReceived(const uint8_t *mac, const uint8_t *incomingData, int len) {
  (void)mac;

  // Telemetry dari robot (baterai) - dicek lebih dulu karena ukurannya paling kecil & khas
  if (len == (int)sizeof(TelemetryPacket)) {
    TelemetryPacket telemetry = {};
    memcpy(&telemetry, incomingData, sizeof(telemetry));

    if (telemetry.type == PACKET_TYPE_TELEMETRY) {
      robotBatteryVoltage = telemetry.batteryVoltage;
    }
    return;
  }

  if (len < (int)sizeof(DiscoveryPacket)) {
    return;
  }

  DiscoveryPacket discoveryPacket = {};
  memcpy(&discoveryPacket, incomingData, sizeof(discoveryPacket));

  if (discoveryPacket.type != PACKET_TYPE_DISCOVERY_RESPONSE) {
    return;
  }

  for (uint8_t i = 0; i < discoveredReceiverCount; i++) {
    if (memcmp(discoveredReceivers[i].mac, discoveryPacket.senderMac, 6) == 0) {
      strncpy(discoveredReceivers[i].name, discoveryPacket.name, sizeof(discoveredReceivers[i].name) - 1);
      discoveredReceivers[i].name[sizeof(discoveredReceivers[i].name) - 1] = '\0';
      return;
    }
  }

  if (discoveredReceiverCount >= maxReceivers) {
    return;
  }

  memcpy(discoveredReceivers[discoveredReceiverCount].mac, discoveryPacket.senderMac, 6);
  strncpy(discoveredReceivers[discoveredReceiverCount].name, discoveryPacket.name, sizeof(discoveredReceivers[discoveredReceiverCount].name) - 1);
  discoveredReceivers[discoveredReceiverCount].name[sizeof(discoveredReceivers[discoveredReceiverCount].name) - 1] = '\0';
  discoveredReceiverCount++;
}

// Baca tegangan baterai remote lewat ADC + voltage divider
float readRemoteBatteryVoltage() {
  int raw = analogRead(BATTERY_ADC_PIN);
  float vAdc = (raw / adcResolution) * adcRefVoltage;
  return vAdc * remoteDividerRatio;
}

void formatMac(const uint8_t *mac, char *buffer, size_t bufferSize) {
  snprintf(buffer, bufferSize, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void clearDiscoveredReceivers() {
  discoveredReceiverCount = 0;
  selectedReceiverIndex = 0;
  scannerStatus[0] = '\0';
}

void sendDiscoveryRequest() {
  DiscoveryPacket discoveryPacket = {};
  discoveryPacket.type = PACKET_TYPE_DISCOVERY_REQUEST;
  memcpy(discoveryPacket.senderMac, receiverMAC, 6);
  strncpy(discoveryPacket.name, "Katyusha", sizeof(discoveryPacket.name) - 1);
  esp_now_send(broadcastMAC, (uint8_t *)&discoveryPacket, sizeof(discoveryPacket));
}

void beginScan() {
  clearDiscoveredReceivers();
  scanningActive = true;
  scanStartedAt = millis();
  lastDiscoverySendAt = 0;
  strncpy(scannerStatus, "Scanning...", sizeof(scannerStatus) - 1);
  scannerStatus[sizeof(scannerStatus) - 1] = '\0';
}

void updateScan() {
  if (!scanningActive) {
    return;
  }

  unsigned long now = millis();

  if (now - lastDiscoverySendAt >= discoveryIntervalMs) {
    sendDiscoveryRequest();
    lastDiscoverySendAt = now;
  }

  if (now - scanStartedAt >= scanDurationMs) {
    scanningActive = false;
    if (discoveredReceiverCount == 0) {
      strncpy(scannerStatus, "No receiver found", sizeof(scannerStatus) - 1);
    } else {
      strncpy(scannerStatus, "Scan complete", sizeof(scannerStatus) - 1);
    }
    scannerStatus[sizeof(scannerStatus) - 1] = '\0';
  }
}

void connectToReceiverIndex(uint8_t index) {
  if (index >= discoveredReceiverCount) {
    return;
  }

  esp_now_del_peer(receiverMAC);
  memcpy(receiverMAC, discoveredReceivers[index].mac, 6);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) == ESP_OK) {
    connected = false;
    lastSuccess = 0;
    strncpy(activeReceiverName, discoveredReceivers[index].name, sizeof(activeReceiverName) - 1);
    activeReceiverName[sizeof(activeReceiverName) - 1] = '\0';
    snprintf(scannerStatus, sizeof(scannerStatus), "Selected %s", discoveredReceivers[index].name);
  } else {
    strncpy(scannerStatus, "Connect failed", sizeof(scannerStatus) - 1);
    scannerStatus[sizeof(scannerStatus) - 1] = '\0';
  }
}

void drawKeypadIndicator(int16_t centerX, int16_t centerY, bool up, bool down, bool left, bool right) {
  const int16_t radius = 3;
  const int16_t offset = 8;

  struct KeyDot {
    int16_t x;
    int16_t y;
    bool active;
  };

  KeyDot dots[] = {
    {static_cast<int16_t>(centerX), static_cast<int16_t>(centerY - offset), up},
    {static_cast<int16_t>(centerX - offset), static_cast<int16_t>(centerY), left},
    {static_cast<int16_t>(centerX + offset), static_cast<int16_t>(centerY), right},
    {static_cast<int16_t>(centerX), static_cast<int16_t>(centerY + offset), down}
  };

  for (KeyDot &dot : dots) {
    if (dot.active) {
      display.fillCircle(dot.x, dot.y, radius, SSD1306_WHITE);
    } else {
      display.drawCircle(dot.x, dot.y, radius, SSD1306_WHITE);
    }
  }
}

struct DebouncedButton {
  uint8_t pin;
  uint8_t mask;
  bool stablePressed;
  bool lastRawPressed;
  unsigned long lastChangeAt;
};

const unsigned long debounceIntervalMs = 15;

// Index tetap: 0=UP 1=DOWN 2=LEFT 3=RIGHT 4=Y 5=X 6=A 7=B
// toggleButtonSwap() menukar field .mask milik index 4 (Y) dan 6 (A) - belum dipakai (Coming Soon)
DebouncedButton debouncedButtons[] = {
  {BTN_UP, BTN_MASK_UP, false, false, 0},
  {BTN_DOWN, BTN_MASK_DOWN, false, false, 0},
  {BTN_LEFT, BTN_MASK_LEFT, false, false, 0},
  {BTN_RIGHT, BTN_MASK_RIGHT, false, false, 0},
  {BTN_Y, BTN_MASK_Y, false, false, 0},
  {BTN_X, BTN_MASK_X, false, false, 0},
  {BTN_A, BTN_MASK_A, false, false, 0},
  {BTN_B, BTN_MASK_B, false, false, 0}
};

void toggleButtonSwap() {
  buttonsSwapped = !buttonsSwapped;

  uint8_t temp = debouncedButtons[4].mask; // slot fisik BTN_Y
  debouncedButtons[4].mask = debouncedButtons[6].mask; // ambil mask dari slot BTN_A
  debouncedButtons[6].mask = temp;
}

void initializeDebouncedButtons() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < 8; i++) {
    bool pressed = digitalRead(debouncedButtons[i].pin) == LOW;
    debouncedButtons[i].stablePressed = pressed;
    debouncedButtons[i].lastRawPressed = pressed;
    debouncedButtons[i].lastChangeAt = now;
  }
}

uint8_t readDebouncedButtons() {
  uint8_t mask = 0;
  unsigned long now = millis();

  for (uint8_t i = 0; i < 8; i++) {
    bool rawPressed = digitalRead(debouncedButtons[i].pin) == LOW;

    if (rawPressed != debouncedButtons[i].lastRawPressed) {
      debouncedButtons[i].lastRawPressed = rawPressed;
      debouncedButtons[i].lastChangeAt = now;
    }

    if ((now - debouncedButtons[i].lastChangeAt) >= debounceIntervalMs) {
      debouncedButtons[i].stablePressed = rawPressed;
    }

    if (debouncedButtons[i].stablePressed) {
      mask |= debouncedButtons[i].mask;
    }
  }

  return mask;
}

bool isButtonJustPressed(uint8_t buttons, uint8_t previous, uint8_t mask) {
  return ((buttons & mask) != 0) && ((previous & mask) == 0);
}

bool isButtonPressed(uint8_t buttons, uint8_t mask) {
  return (buttons & mask) != 0;
}

uint8_t resolveMovementMode(uint8_t buttons) {
  bool up = buttons & BTN_MASK_UP;
  bool down = buttons & BTN_MASK_DOWN;
  bool left = buttons & BTN_MASK_LEFT;
  bool right = buttons & BTN_MASK_RIGHT;

  if (up && left) return MODE_FORWARD_LEFT;
  if (up && right) return MODE_FORWARD_RIGHT;
  if (down && left) return MODE_BACKWARD_LEFT;
  if (down && right) return MODE_BACKWARD_RIGHT;

  if (up) return MODE_FORWARD;
  if (down) return MODE_BACKWARD;
  if (left) return MODE_TURN_LEFT;
  if (right) return MODE_TURN_RIGHT;

  return MODE_STOP;
}

const char* modeToString(uint8_t mode) {
  switch (mode) {
    case MODE_FORWARD: return "FORWARD";
    case MODE_BACKWARD: return "BACKWARD";
    case MODE_TURN_LEFT: return "LEFT";
    case MODE_TURN_RIGHT: return "RIGHT";
    case MODE_FORWARD_LEFT: return "FWL";
    case MODE_FORWARD_RIGHT: return "FWR";
    case MODE_BACKWARD_LEFT: return "BWL";
    case MODE_BACKWARD_RIGHT: return "BWR";
    case MODE_SPIN_LEFT: return "SPIN LEFT";
    case MODE_SPIN_RIGHT: return "SPIN RIGHT";
    default: return "STOP";
  }
}

void drawMarqueeText(int16_t x, int16_t y, int16_t widthPixels, const char *text) {
  display.setCursor(x, y);

  size_t textLength = strlen(text);
  size_t visibleChars = widthPixels / 6;

  if (textLength <= visibleChars) {
    display.print(text);
    return;
  }

  size_t scrollSpan = textLength + visibleChars;
  size_t offset = (millis() / 250) % scrollSpan;

  for (size_t i = 0; i < visibleChars; i++) {
    size_t sourceIndex = (offset + i) % scrollSpan;
    char character = ' ';

    if (sourceIndex < textLength) {
      character = text[sourceIndex];
    }

    display.print(character);
  }
}

// Header minimalis dipakai di semua layar setting/submenu: judul + garis tipis, tanpa bingkai kotak.
void drawSettingsHeader(const char* title) {
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(6, 3);
  display.print(title);
  display.drawFastHLine(0, 13, 128, SSD1306_WHITE);
}

// Footer kecil untuk hint tombol, dipakai di submenu
void drawHint(const char* hint) {
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(6, 56);
  display.print(hint);
}

uint8_t batteryPercent(float voltage, float minV, float maxV) {
  if (voltage <= minV) return 0;
  if (voltage >= maxV) return 100;
  return (uint8_t)(((voltage - minV) / (maxV - minV)) * 100.0f);
}

// Ikon baterai kecil: body + nub + isi proporsional sesuai persentase
void drawBatteryIcon(int16_t x, int16_t y, int16_t bodyW, int16_t bodyH, uint8_t percent) {
  display.drawRect(x, y, bodyW, bodyH, SSD1306_WHITE);

  int16_t nubH = bodyH / 2;
  display.fillRect(x + bodyW, y + (bodyH - nubH) / 2, 2, nubH, SSD1306_WHITE);

  int16_t fillW = ((bodyW - 2) * percent) / 100;
  if (fillW > 0) {
    display.fillRect(x + 1, y + 1, fillW, bodyH - 2, SSD1306_WHITE);
  }
}

void drawHomeScreen(uint8_t buttons) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // --- Kotak-kotak (grid rapi: kolom kiri title/koneksi, kolom kanan baterai) ---
  display.drawRoundRect(2, 2, 58, 16, 3, SSD1306_WHITE);   // Judul
  display.drawRoundRect(64, 2, 62, 16, 3, SSD1306_WHITE);  // Baterai controller (samakan ukuran dgn baterai robot)
  display.drawRoundRect(36, 21, 56, 20, 3, SSD1306_WHITE); // Status gerak real-time
  display.drawRoundRect(2, 48, 58, 14, 3, SSD1306_WHITE);  // Status koneksi + nama device
  display.drawRoundRect(64, 48, 62, 14, 3, SSD1306_WHITE); // Baterai robot

  // --- Judul: teks statis, TIDAK marquee (sesuai permintaan) ---
  display.setCursor(7, 6);
  display.print("KATYUSHA");

  // --- Baterai controller: label "C" + ikon + voltase ---
  display.setCursor(70, 6);
  display.print("C");

  uint8_t ctrlPercent = batteryPercent(remoteBatteryVoltage, remoteBatteryMinV, remoteBatteryMaxV);
  drawBatteryIcon(80, 6, 10, 6, ctrlPercent);

  char remoteBatteryText[8];
  snprintf(remoteBatteryText, sizeof(remoteBatteryText), "%.1fV", remoteBatteryVoltage);
  display.setCursor(94, 6);
  display.print(remoteBatteryText);

  // --- Status gerak real-time (STOP/FORWARD/BACKWARD/LEFT/RIGHT/dst) ---
  uint8_t liveMode = resolveMovementMode(buttons);
  const char* modeText = modeToString(liveMode);
  int modeW = strlen(modeText) * 6;
  display.setCursor(36 + (56 - modeW) / 2, 27);
  display.print(modeText);

  drawKeypadIndicator(18, 34,
                      isButtonPressed(buttons, BTN_MASK_UP),
                      isButtonPressed(buttons, BTN_MASK_DOWN),
                      isButtonPressed(buttons, BTN_MASK_LEFT),
                      isButtonPressed(buttons, BTN_MASK_RIGHT));

  // Layout Xbox: Y atas, A bawah, X kiri, B kanan (urutan tidak diubah)
  drawKeypadIndicator(110, 34,
                      isButtonPressed(buttons, BTN_MASK_Y),
                      isButtonPressed(buttons, BTN_MASK_A),
                      isButtonPressed(buttons, BTN_MASK_X),
                      isButtonPressed(buttons, BTN_MASK_B));

  char connectionText[40];
  if (connected) {
    snprintf(connectionText, sizeof(connectionText), "%s Connected", activeReceiverName);
  } else {
    snprintf(connectionText, sizeof(connectionText), "Searching for device...");
  }
  drawMarqueeText(6, 51, 54, connectionText);

  display.setCursor(70, 51);
  display.print("R");

  uint8_t robotPercent = batteryPercent(robotBatteryVoltage, robotBatteryMinV, robotBatteryMaxV);
  drawBatteryIcon(80, 51, 10, 6, robotPercent);

  char robotBatteryText[8];
  snprintf(robotBatteryText, sizeof(robotBatteryText), "%.1fV", robotBatteryVoltage);
  display.setCursor(94, 51);
  display.print(robotBatteryText);

  display.display();
}

void drawMenuScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  drawSettingsHeader("SETTINGS");

  uint8_t startIndex = 0;
  if (menuItemCount > visibleMenuItems) {
    if (menuIndex >= 1) {
      startIndex = menuIndex - 1;
    }
    if (startIndex + visibleMenuItems > menuItemCount) {
      startIndex = menuItemCount - visibleMenuItems;
    }
  }

  const uint8_t listTop = 18;
  const uint8_t rowHeight = 15;
  const uint8_t rowWidth = 110; // sisakan ruang kanan untuk chevron + scrollbar

  for (uint8_t row = 0; row < visibleMenuItems; row++) {
    uint8_t itemIndex = startIndex + row;
    if (itemIndex >= menuItemCount) {
      break;
    }

    uint8_t y = listTop + (row * rowHeight);
    bool isSelected = (itemIndex == menuIndex);

    if (isSelected) {
      display.fillRoundRect(4, y - 2, rowWidth, 13, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    char label[24];
    snprintf(label, sizeof(label), "%d. %s", itemIndex + 1, menuItemsBase[itemIndex]);

    display.setCursor(9, y);
    display.print(label);

    // Chevron kecil di kanan tiap baris (indikasi "buka submenu")
    display.setCursor(4 + rowWidth - 6, y);
    display.print(">");
  }

  display.setTextColor(SSD1306_WHITE);

  // --- Scrollbar minimalis: garis tipis + thumb solid ---
  const int16_t trackX = 124;
  const int16_t trackTop = 18;
  const int16_t trackH = 44;

  display.drawFastVLine(trackX, trackTop, trackH, SSD1306_WHITE);

  int16_t thumbH = (trackH * visibleMenuItems) / menuItemCount;
  if (thumbH < 6) {
    thumbH = 6;
  }

  uint8_t maxScroll = menuItemCount - visibleMenuItems;
  int16_t thumbY = trackTop;
  if (maxScroll > 0) {
    thumbY = trackTop + ((trackH - thumbH) * startIndex) / maxScroll;
  }

  display.fillRect(trackX - 1, thumbY, 3, thumbH, SSD1306_WHITE);

  display.display();
}

void drawAboutScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  drawSettingsHeader("ABOUT");

  display.setCursor(6, 30);
  display.print("Info coming soon");

  drawHint("B: Back");

  display.display();
}

void drawComingSoonScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  drawSettingsHeader(comingSoonLabel);

  display.setCursor(6, 30);
  display.print("Coming Soon");

  drawHint("B: Back");

  display.display();
}

// Submenu Turbo Speed: UP/DOWN untuk naik-turun nilai, B untuk kembali
void drawTurboSelectScreen() {
  display.clearDisplay();

  drawSettingsHeader("TURBO SPEED");

  char valueText[6];
  snprintf(valueText, sizeof(valueText), "%d", turboSpeedValue);

  display.setTextSize(2);
  int16_t w = strlen(valueText) * 12;
  display.setCursor((128 - w) / 2, 22);
  display.print(valueText);

  display.setTextSize(1);
  char rangeText[20];
  snprintf(rangeText, sizeof(rangeText), "Range %d - %d", turboSpeedMin, turboSpeedMax);
  display.setCursor((128 - (int16_t)strlen(rangeText) * 6) / 2, 42);
  display.print(rangeText);

  drawHint("Up/Down      B:Back");

  display.display();
}

void drawScanScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  const char* scanLabel = "PAIR DEVICE";
  char scanText[20];

  if (scanningActive) {
    uint8_t dotCount = (millis() / 300) % 4;
    strcpy(scanText, "SCANNING");

    for (uint8_t i = 0; i < dotCount; i++) {
      strlcat(scanText, ".", sizeof(scanText));
    }

    scanLabel = scanText;
  }

  drawSettingsHeader(scanLabel);

  display.setCursor(90, 3);
  display.print(discoveredReceiverCount);

  if (discoveredReceiverCount == 0) {
    display.setCursor(6, 28);
    display.print("Waiting receiver...");
  } else {
    uint8_t windowSize = 3;
    uint8_t startIndex = 0;

    if (discoveredReceiverCount > windowSize) {
      if (selectedReceiverIndex >= 1) {
        startIndex = selectedReceiverIndex - 1;
      }

      if (startIndex + windowSize > discoveredReceiverCount) {
        startIndex = discoveredReceiverCount - windowSize;
      }
    }

    for (uint8_t row = 0; row < windowSize; row++) {
      uint8_t itemIndex = startIndex + row;
      if (itemIndex >= discoveredReceiverCount) {
        break;
      }

      uint8_t y = 20 + (row * 13);
      bool isSelected = (itemIndex == selectedReceiverIndex);

      if (isSelected) {
        display.fillRoundRect(4, y - 2, 118, 12, 3, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(9, y);
      display.print(discoveredReceivers[itemIndex].name);
    }
  }

  display.setTextColor(SSD1306_WHITE);
  drawHint("A: Connect   B: Back");

  display.display();
}

void handleUi(uint8_t buttons) {
  if (uiScreen == SCREEN_HOME) {
    bool upAndYPressed = ((buttons & BTN_MASK_UP) != 0) && ((buttons & BTN_MASK_Y) != 0);
    bool upOrYJustPressed = isButtonJustPressed(buttons, previousButtons, BTN_MASK_Y) || isButtonJustPressed(buttons, previousButtons, BTN_MASK_UP);

    if (upAndYPressed && upOrYJustPressed) {
      uiScreen = SCREEN_MENU;
      menuIndex = 0;
    }
    return;
  }

  if (uiScreen == SCREEN_SCAN) {
    if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_B)) {
      uiScreen = SCREEN_MENU;
      scanningActive = false;
      return;
    }

    if (discoveredReceiverCount > 0) {
      if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_UP)) {
        if (selectedReceiverIndex == 0) {
          selectedReceiverIndex = discoveredReceiverCount - 1;
        } else {
          selectedReceiverIndex--;
        }
      }

      if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_DOWN)) {
        selectedReceiverIndex = (selectedReceiverIndex + 1) % discoveredReceiverCount;
      }

      if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_A)) {
        connectToReceiverIndex(selectedReceiverIndex);
        uiScreen = SCREEN_HOME;
        scanningActive = false;
      }
    }

    return;
  }

  if (uiScreen == SCREEN_ABOUT || uiScreen == SCREEN_COMING_SOON) {
    if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_B)) {
      uiScreen = SCREEN_MENU;
    }
    return;
  }

  if (uiScreen == SCREEN_TURBO_SELECT) {
    if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_B)) {
      uiScreen = SCREEN_MENU;
      return;
    }
    if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_UP)) {
      turboSpeedValue = (turboSpeedValue + speedStep <= turboSpeedMax) ? turboSpeedValue + speedStep : turboSpeedMax;
    }
    if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_DOWN)) {
      turboSpeedValue = (turboSpeedValue >= turboSpeedMin + speedStep) ? turboSpeedValue - speedStep : turboSpeedMin;
    }
    return;
  }

  // --- Dari sini uiScreen == SCREEN_MENU ---
  if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_B)) {
    uiScreen = SCREEN_HOME;
    return;
  }

  if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_UP)) {
    if (menuIndex == 0) {
      menuIndex = menuItemCount - 1;
    } else {
      menuIndex--;
    }
  }

  if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_DOWN)) {
    menuIndex = (menuIndex + 1) % menuItemCount;
  }

  if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_A)) {
    switch (menuIndex) {
      case 0: // Pair Device
        uiScreen = SCREEN_SCAN;
        beginScan();
        break;

      case 1: // Set Button - Coming Soon
        comingSoonLabel = menuItemsBase[1];
        uiScreen = SCREEN_COMING_SOON;
        break;

      case 2: // Set Movement - Coming Soon
        comingSoonLabel = menuItemsBase[2];
        uiScreen = SCREEN_COMING_SOON;
        break;

      case 3: // Turbo Speed
        uiScreen = SCREEN_TURBO_SELECT;
        break;

      case 4: // About
        uiScreen = SCREEN_ABOUT;
        break;

      case 5: // Exit
        uiScreen = SCREEN_HOME;
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_Y, INPUT_PULLUP);
  pinMode(BTN_X, INPUT_PULLUP);
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BATTERY_ADC_PIN, INPUT);
  initializeDebouncedButtons();

  // OLED
  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  display.println("BOOTING...");
  display.println("ESP TRANSMITTER");
  display.display();
  delay(700);

  // WiFi
  WiFi.mode(WIFI_STA);

  // ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP NOW INIT FAILED");
    while (true) {}
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  esp_now_peer_info_t broadcastPeer = {};
  memcpy(broadcastPeer.peer_addr, broadcastMAC, 6);
  broadcastPeer.channel = 0;
  broadcastPeer.encrypt = false;
  esp_now_add_peer(&broadcastPeer);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Peer Failed");
    while (true) {}
  }

  Serial.println("TRANSMITTER READY");
}

void loop() {
  uint8_t buttons = readDebouncedButtons();

  // Update baterai remote (dinamis, dengan smoothing biar tidak jitter)
  float rawRemoteV = readRemoteBatteryVoltage();
  remoteBatteryVoltage = (remoteBatteryVoltage * 0.9f) + (rawRemoteV * 0.1f);

  handleUi(buttons);

  if (uiScreen == SCREEN_SCAN) {
    updateScan();
  }

  bool isDrivingScreen = (uiScreen == SCREEN_HOME);

  if (!isDrivingScreen) {
    packet.buttons = 0;
    packet.speed = 0;
    packet.mode = MODE_STOP;
  } else {
    packet.buttons = buttons & ~BTN_MASK_Y;

    // Turbo aktif kalau salah satu arah (UP/DOWN/LEFT/RIGHT) ditekan BERSAMA A
    bool anyDirectionPressed = isButtonPressed(packet.buttons, BTN_MASK_UP) ||
                                isButtonPressed(packet.buttons, BTN_MASK_DOWN) ||
                                isButtonPressed(packet.buttons, BTN_MASK_LEFT) ||
                                isButtonPressed(packet.buttons, BTN_MASK_RIGHT);

    bool turboActive = anyDirectionPressed && isButtonPressed(packet.buttons, BTN_MASK_A);

    packet.speed = turboActive ? turboSpeedValue : defaultSpeed;
    packet.mode = resolveMovementMode(packet.buttons);
  }

  esp_now_send(receiverMAC, (uint8_t *)&packet, sizeof(packet));

  if (millis() - lastSuccess > 1000) {
    connected = false;
  }

  switch (uiScreen) {
    case SCREEN_MENU:
      drawMenuScreen();
      break;
    case SCREEN_SCAN:
      drawScanScreen();
      break;
    case SCREEN_ABOUT:
      drawAboutScreen();
      break;
    case SCREEN_TURBO_SELECT:
      drawTurboSelectScreen();
      break;
    case SCREEN_COMING_SOON:
      drawComingSoonScreen();
      break;
    default:
      drawHomeScreen(buttons);
      break;
  }

  previousButtons = buttons;

  delay(5);
}