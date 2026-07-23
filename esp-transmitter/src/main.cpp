#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

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
  SCREEN_SCAN
};

enum PacketType : uint8_t {
  PACKET_TYPE_CONTROL = 1,
  PACKET_TYPE_DISCOVERY_REQUEST = 2,
  PACKET_TYPE_DISCOVERY_RESPONSE = 3
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

// ===================== STATUS =====================
bool connected = false;
unsigned long lastSuccess = 0;
UiScreen uiScreen = SCREEN_HOME;
uint8_t previousButtons = 0;
uint8_t menuIndex = 0;
char activeReceiverName[16] = "SEARCH";

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

const char* menuItems[] = {
  "Scanning",
  "Exit"
};

const uint8_t menuItemCount = sizeof(menuItems) / sizeof(menuItems[0]);

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

struct DebouncedButton {
  uint8_t pin;
  uint8_t mask;
  bool stablePressed;
  bool lastRawPressed;
  unsigned long lastChangeAt;
};

const unsigned long debounceIntervalMs = 15;

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

void drawHomeScreen(uint8_t mode) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.drawLine(0, 16, 127, 16, SSD1306_WHITE);
  display.drawLine(64, 16, 64, 63, SSD1306_WHITE);

  display.setCursor(8, 4);
  display.print("Katyusha V1.0");

  display.setCursor(8, 24);
  display.print("MODE");
  display.setCursor(8, 35);
  display.println(modeToString(mode));

  display.setCursor(72, 24);
  display.print("LINK");
  display.setCursor(72, 35);
  display.println(connected ? "ONLINE" : "SEARCH");

  display.setCursor(72, 44);
  drawMarqueeText(72, 44, 48, connected ? activeReceiverName : "-");

  display.setCursor(8, 50);
  display.println("Y: MENU");

  display.display();
}

void drawMenuScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.drawLine(0, 16, 127, 16, SSD1306_WHITE);

  display.setCursor(8, 4);
  display.print("MENU");

  for (uint8_t i = 0; i < menuItemCount; i++) {
    uint8_t y = 26 + (i * 14);

    if (i == menuIndex) {
      display.fillRect(6, y - 1, 116, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(12, y);
    display.println(menuItems[i]);
  }

  display.display();
}

void drawScanScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.drawLine(0, 16, 127, 16, SSD1306_WHITE);

  const char* scanLabel = "SCANNED";
  char scanText[16];

  if (scanningActive) {
    uint8_t dotCount = (millis() / 300) % 4;
    strcpy(scanText, "SCANNING");

    for (uint8_t i = 0; i < dotCount; i++) {
      strlcat(scanText, ".", sizeof(scanText));
    }

    scanLabel = scanText;
  }

  display.setCursor(8, 4);
  display.print(scanLabel);

  display.setCursor(84, 4);
  display.print(discoveredReceiverCount);
  display.print(" FOUND");

  if (discoveredReceiverCount == 0) {
    display.setCursor(8, 26);
    display.print("WAITING RECEIVER");
  } else {
    uint8_t windowSize = 4;
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

      uint8_t y = 25 + (row * 10);

      if (itemIndex == selectedReceiverIndex) {
        display.fillRect(6, y - 1, 116, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(10, y);
      display.print(itemIndex == selectedReceiverIndex ? ">" : " ");
      display.print(discoveredReceivers[itemIndex].name);

      if (itemIndex == selectedReceiverIndex) {
        display.setTextColor(SSD1306_WHITE);
      }
    }
  }

  display.display();
}

void handleUi(uint8_t buttons) {
  if (uiScreen == SCREEN_HOME) {
    if (isButtonJustPressed(buttons, previousButtons, BTN_MASK_Y)) {
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
    if (menuIndex == 0) {
      uiScreen = SCREEN_SCAN;
      beginScan();
    } else if (menuIndex == menuItemCount - 1) {
      uiScreen = SCREEN_HOME;
    } else {
      Serial.print("Menu item selected: ");
      Serial.println(menuItems[menuIndex]);
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

  handleUi(buttons);

  if (uiScreen == SCREEN_SCAN) {
    updateScan();
  }

  if (uiScreen == SCREEN_MENU || uiScreen == SCREEN_SCAN) {
    packet.buttons = 0;
    packet.speed = 0;
    packet.mode = MODE_STOP;
  } else {
    packet.buttons = buttons & ~BTN_MASK_Y;
    packet.speed = 220;
    packet.mode = resolveMovementMode(packet.buttons);
  }

  esp_now_send(receiverMAC, (uint8_t *)&packet, sizeof(packet));

  if (millis() - lastSuccess > 1000) {
    connected = false;
  }

  if (uiScreen == SCREEN_MENU) {
    drawMenuScreen();
  } else if (uiScreen == SCREEN_SCAN) {
    drawScanScreen();
  } else {
    drawHomeScreen(packet.mode);
  }

  previousButtons = buttons;

  delay(5);
}