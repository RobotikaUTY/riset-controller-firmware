#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ===================== TB6612FNG PINS =====================
#define PWMA 13
#define AIN1 26
#define AIN2 27

#define PWMB 32
#define BIN1 25
#define BIN2 33

#define CHANNEL_A 0
#define CHANNEL_B 1

#define PWM_FREQ 1000
#define PWM_RESOLUTION 8

// ===================== BATTERY SENSE (ROBOT) =====================
// Pin ADC untuk baca baterai robot lewat voltage divider (belum dipakai fungsi lain)
#define BATTERY_ADC_PIN 34

// Rasio divider = (R1+R2)/R2. Contoh untuk LiPo 3S (maks ~12.6V):
// R1=10k (ke B+), R2=3.3k (ke GND) -> (10000+3300)/3300 = 4.03
// WAJIB dikalibrasi ulang pakai multimeter sesuai resistor asli yang dipasang,
// dan sesuaikan kalau jenis/jumlah sel baterainya berbeda.
const float robotDividerRatio = 4.03f;
const float adcRefVoltage = 3.3f;
const float adcResolution = 4095.0f;

float robotBatteryVoltage = 12.0f;

unsigned long lastTelemetrySentAt = 0;
const unsigned long telemetryIntervalMs = 500;

// ===================== PACKET TYPES =====================
enum PacketType : uint8_t {
  PACKET_TYPE_CONTROL = 1,
  PACKET_TYPE_DISCOVERY_REQUEST = 2,
  PACKET_TYPE_DISCOVERY_RESPONSE = 3,
  PACKET_TYPE_TELEMETRY = 4
};

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

typedef struct {
  uint8_t type;
  uint8_t senderMac[6];
  char name[16];
} DiscoveryPacket;

// Dikirim BALIK ke controller, isinya tegangan baterai robot saat ini.
// Struct ini harus identik (urutan & tipe field) dengan yang ada di kode transmitter.
typedef struct {
  uint8_t type;
  float batteryVoltage;
} TelemetryPacket;

ControlPacket packet;

// ===================== DISCOVERY / SAFETY =====================
uint8_t ownMac[6] = {0};
uint8_t pendingDiscoveryMac[6] = {0};
bool discoveryPending = false;
unsigned long lastControlAt = 0;
const unsigned long controlTimeoutMs = 300;
char deviceName[16] = "Katyusha-RX";

// MAC controller yang terakhir kali mengirim ControlPacket - tujuan kirim telemetry
uint8_t controllerMac[6] = {0};
bool controllerMacKnown = false;

// ===================== MOTOR CONTROL =====================
void motorA(int speed) {
  speed = constrain(speed, -255, 255);

  if (speed > 0) {
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
    ledcWrite(CHANNEL_A, speed);
  } else if (speed < 0) {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
    ledcWrite(CHANNEL_A, -speed);
  } else {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
    ledcWrite(CHANNEL_A, 0);
  }
}

void motorB(int speed) {
  speed = constrain(speed, -255, 255);

  if (speed > 0) {
    digitalWrite(BIN1, HIGH);
    digitalWrite(BIN2, LOW);
    ledcWrite(CHANNEL_B, speed);
  } else if (speed < 0) {
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, HIGH);
    ledcWrite(CHANNEL_B, -speed);
  } else {
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, LOW);
    ledcWrite(CHANNEL_B, 0);
  }
}

void drive(int leftSpeed, int rightSpeed) {
  motorA(leftSpeed);
  motorB(rightSpeed);
}

void applyMode(uint8_t mode, uint8_t speed) {
  switch (mode) {
    case MODE_FORWARD:
      drive(speed, speed);
      break;
    case MODE_BACKWARD:
      drive(-speed, -speed);
      break;
    case MODE_TURN_LEFT:
      drive(-speed, speed);
      break;
    case MODE_TURN_RIGHT:
      drive(speed, -speed);
      break;
    case MODE_FORWARD_LEFT:
      drive(speed / 2, speed);
      break;
    case MODE_FORWARD_RIGHT:
      drive(speed, speed / 2);
      break;
    case MODE_BACKWARD_LEFT:
      drive(-(speed / 2), -speed);
      break;
    case MODE_BACKWARD_RIGHT:
      drive(-speed, -(speed / 2));
      break;
    case MODE_SPIN_LEFT:
      drive(-speed, speed);
      break;
    case MODE_SPIN_RIGHT:
      drive(speed, -speed);
      break;
    default:
      drive(0, 0);
      break;
  }
}

void addPeerIfNeeded(const uint8_t *peerMac) {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
    Serial.print("Add peer failed: ");
    Serial.println(result);
  }
}

void sendDiscoveryResponse() {
  if (!discoveryPending) {
    return;
  }

  DiscoveryPacket response = {};
  response.type = PACKET_TYPE_DISCOVERY_RESPONSE;
  memcpy(response.senderMac, ownMac, 6);
  strncpy(response.name, deviceName, sizeof(response.name) - 1);
  response.name[sizeof(response.name) - 1] = '\0';

  addPeerIfNeeded(pendingDiscoveryMac);

  esp_err_t result = esp_now_send(pendingDiscoveryMac, (uint8_t *)&response, sizeof(response));
  if (result == ESP_OK) {
    Serial.print("Discovery response sent to ");
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
                  pendingDiscoveryMac[0], pendingDiscoveryMac[1], pendingDiscoveryMac[2],
                  pendingDiscoveryMac[3], pendingDiscoveryMac[4], pendingDiscoveryMac[5]);
  } else {
    Serial.print("Discovery response failed: ");
    Serial.println(result);
  }

  discoveryPending = false;
}

// Baca tegangan baterai robot lewat ADC + voltage divider
float readRobotBatteryVoltage() {
  int raw = analogRead(BATTERY_ADC_PIN);
  float vAdc = (raw / adcResolution) * adcRefVoltage;
  return vAdc * robotDividerRatio;
}

// Kirim status baterai ke controller terakhir yang diketahui, dibatasi interval biar tidak spam
void sendTelemetryIfDue() {
  if (!controllerMacKnown) {
    return;
  }

  unsigned long now = millis();
  if (now - lastTelemetrySentAt < telemetryIntervalMs) {
    return;
  }
  lastTelemetrySentAt = now;

  float rawV = readRobotBatteryVoltage();
  robotBatteryVoltage = (robotBatteryVoltage * 0.9f) + (rawV * 0.1f); // smoothing

  TelemetryPacket telemetry = {};
  telemetry.type = PACKET_TYPE_TELEMETRY;
  telemetry.batteryVoltage = robotBatteryVoltage;

  esp_now_send(controllerMac, (uint8_t *)&telemetry, sizeof(telemetry));
}

// ===================== ESP-NOW CALLBACK =====================
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == (int)sizeof(ControlPacket)) {
    memcpy(&packet, incomingData, sizeof(packet));
    lastControlAt = millis();
    applyMode(packet.mode, packet.speed);

    // Catat MAC controller supaya telemetry baterai bisa dikirim balik ke dia
    if (!controllerMacKnown || memcmp(controllerMac, mac, 6) != 0) {
      memcpy(controllerMac, mac, 6);
      controllerMacKnown = true;
      addPeerIfNeeded(controllerMac);
    }
    return;
  }

  if (len == (int)sizeof(DiscoveryPacket)) {
    DiscoveryPacket discoveryPacket = {};
    memcpy(&discoveryPacket, incomingData, sizeof(discoveryPacket));

    if (discoveryPacket.type != PACKET_TYPE_DISCOVERY_REQUEST) {
      return;
    }

    memcpy(pendingDiscoveryMac, mac, 6);
    discoveryPending = true;

    Serial.print("Discovery request from ");
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print(" name=");
    Serial.println(discoveryPacket.name);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(BATTERY_ADC_PIN, INPUT);

  ledcSetup(CHANNEL_A, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PWMA, CHANNEL_A);

  ledcSetup(CHANNEL_B, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PWMB, CHANNEL_B);

  drive(0, 0);

  WiFi.mode(WIFI_STA);
  esp_wifi_get_mac(WIFI_IF_STA, ownMac);

  snprintf(deviceName, sizeof(deviceName), "Katyusha-%02X%02X", ownMac[4], ownMac[5]);
  deviceName[sizeof(deviceName) - 1] = '\0';

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) {}
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.print("RECEIVER READY: ");
  Serial.println(deviceName);
}

void loop() {
  if (discoveryPending) {
    sendDiscoveryResponse();
  }

  sendTelemetryIfDue();

  if (millis() - lastControlAt > controlTimeoutMs) {
    drive(0, 0);
  }

  delay(10);
}