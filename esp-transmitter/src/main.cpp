#include <WiFi.h>
#include <esp_now.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===================== PIN =====================
#define BTN1 32
#define BTN2 33

// ===================== RECEIVER MAC =====================
uint8_t receiverMAC[] = {0x8C, 0x4F, 0x00, 0x2E, 0x99, 0x08};

// ===================== DATA =====================
typedef struct {
  bool btn1;
  bool btn2;
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

void setup() {

  Serial.begin(115200);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);

  // OLED
  Wire.begin(21,22);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  display.clearDisplay();
  display.display();

  // WiFi
  WiFi.mode(WIFI_STA);

  // ESP NOW
  if (esp_now_init() != ESP_OK) {

    Serial.println("ESP NOW INIT FAILED");

    while (true);

  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer = {};

  memcpy(peer.peer_addr, receiverMAC, 6);

  peer.channel = 0;

  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {

    Serial.println("Peer Failed");

    while (true);

  }

  Serial.println("TRANSMITTER READY");
}

void loop() {

  packet.btn1 = !digitalRead(BTN1);
  packet.btn2 = !digitalRead(BTN2);

  esp_now_send(receiverMAC,(uint8_t*)&packet,sizeof(packet));

  if (millis() - lastSuccess > 1000)
      connected = false;

  // OLED
  display.clearDisplay();

  display.setTextSize(1);

  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0,0);

  display.println("ROBOT CONTROLLER");

  display.println();

  display.print("BTN1 : ");
  display.println(packet.btn1 ? "ON":"OFF");

  display.print("BTN2 : ");
  display.println(packet.btn2 ? "ON":"OFF");

  display.println();

  display.print("STATUS : ");

  display.println(connected ? "CONNECTED":"SEARCHING");

  display.display();

  delay(20);
}