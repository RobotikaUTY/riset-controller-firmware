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
#define BTN_FORWARD 4
#define BTN_BACKWARD 5
#define BTN_LEFT 18
#define BTN_RIGHT 19
#define BTN_SPIN_LEFT 23
#define BTN_SPIN_RIGHT 15

// ===================== RECEIVER MAC =====================
uint8_t receiverMAC[] = {0x78, 0x1C, 0x3C, 0x2B, 0xDF, 0xB4};

// ===================== CONTROL PACKET =====================
enum ButtonMask : uint8_t {
  BTN_MASK_FORWARD = 1 << 0,
  BTN_MASK_BACKWARD = 1 << 1,
  BTN_MASK_LEFT = 1 << 2,
  BTN_MASK_RIGHT = 1 << 3,
  BTN_MASK_SPIN_LEFT = 1 << 4,
  BTN_MASK_SPIN_RIGHT = 1 << 5
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

typedef struct {
  uint8_t buttons;
  uint8_t speed;
  uint8_t mode;
} ControlPacket;

ControlPacket packet;

// ===================== STATUS =====================
bool connected = false;
unsigned long lastSuccess = 0;

// ===================== CALLBACK =====================
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  (void)mac_addr;

  if (status == ESP_NOW_SEND_SUCCESS) {
    connected = true;
    lastSuccess = millis();
  }
}

bool readButton(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

uint8_t resolveMovementMode(uint8_t buttons) {
  bool fwd = buttons & BTN_MASK_FORWARD;
  bool bwd = buttons & BTN_MASK_BACKWARD;
  bool left = buttons & BTN_MASK_LEFT;
  bool right = buttons & BTN_MASK_RIGHT;
  bool spinLeft = buttons & BTN_MASK_SPIN_LEFT;
  bool spinRight = buttons & BTN_MASK_SPIN_RIGHT;

  if (spinLeft && !spinRight) return MODE_SPIN_LEFT;
  if (spinRight && !spinLeft) return MODE_SPIN_RIGHT;

  if (fwd && left) return MODE_FORWARD_LEFT;
  if (fwd && right) return MODE_FORWARD_RIGHT;
  if (bwd && left) return MODE_BACKWARD_LEFT;
  if (bwd && right) return MODE_BACKWARD_RIGHT;

  if (fwd) return MODE_FORWARD;
  if (bwd) return MODE_BACKWARD;
  if (left) return MODE_TURN_LEFT;
  if (right) return MODE_TURN_RIGHT;

  return MODE_STOP;
}

const char* modeToString(uint8_t mode) {
  switch (mode) {
    case MODE_FORWARD: return "FORWARD";
    case MODE_BACKWARD: return "BACKWARD";
    case MODE_TURN_LEFT: return "TURN LEFT";
    case MODE_TURN_RIGHT: return "TURN RIGHT";
    case MODE_FORWARD_LEFT: return "FWD+LEFT";
    case MODE_FORWARD_RIGHT: return "FWD+RIGHT";
    case MODE_BACKWARD_LEFT: return "BWD+LEFT";
    case MODE_BACKWARD_RIGHT: return "BWD+RIGHT";
    case MODE_SPIN_LEFT: return "SPIN LEFT";
    case MODE_SPIN_RIGHT: return "SPIN RIGHT";
    default: return "STOP";
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_FORWARD, INPUT_PULLUP);
  pinMode(BTN_BACKWARD, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SPIN_LEFT, INPUT_PULLUP);
  pinMode(BTN_SPIN_RIGHT, INPUT_PULLUP);

  // OLED
  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  // WiFi
  WiFi.mode(WIFI_STA);

  // ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP NOW INIT FAILED");
    while (true) {}
  }

  esp_now_register_send_cb(onDataSent);

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
  packet.buttons = 0;
  packet.speed = 220;

  if (readButton(BTN_FORWARD)) packet.buttons |= BTN_MASK_FORWARD;
  if (readButton(BTN_BACKWARD)) packet.buttons |= BTN_MASK_BACKWARD;
  if (readButton(BTN_LEFT)) packet.buttons |= BTN_MASK_LEFT;
  if (readButton(BTN_RIGHT)) packet.buttons |= BTN_MASK_RIGHT;
  if (readButton(BTN_SPIN_LEFT)) packet.buttons |= BTN_MASK_SPIN_LEFT;
  if (readButton(BTN_SPIN_RIGHT)) packet.buttons |= BTN_MASK_SPIN_RIGHT;

  packet.mode = resolveMovementMode(packet.buttons);

  esp_now_send(receiverMAC, (uint8_t *)&packet, sizeof(packet));

  if (millis() - lastSuccess > 1000) {
    connected = false;
  }

  // OLED display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.println("ROBOT CONTROLLER");
  display.println();
  display.print("FWD:");
  display.println((packet.buttons & BTN_MASK_FORWARD) ? "ON" : "OFF");
  display.print("BWD:");
  display.println((packet.buttons & BTN_MASK_BACKWARD) ? "ON" : "OFF");
  display.print("LFT:");
  display.println((packet.buttons & BTN_MASK_LEFT) ? "ON" : "OFF");
  display.print("RGT:");
  display.println((packet.buttons & BTN_MASK_RIGHT) ? "ON" : "OFF");
  display.print("SL:");
  display.println((packet.buttons & BTN_MASK_SPIN_LEFT) ? "ON" : "OFF");
  display.print("SR:");
  display.println((packet.buttons & BTN_MASK_SPIN_RIGHT) ? "ON" : "OFF");
  display.println();
  display.print("MODE: ");
  display.println(modeToString(packet.mode));
  display.print("STATUS: ");
  display.println(connected ? "CONNECTED" : "SEARCHING");
  display.display();

  delay(20);
}