#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

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

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(ControlPacket)) {
    memcpy(&packet, incomingData, sizeof(packet));
    applyMode(packet.mode, packet.speed);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  ledcSetup(CHANNEL_A, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PWMA, CHANNEL_A);

  ledcSetup(CHANNEL_B, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PWMB, CHANNEL_B);

  drive(0, 0);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) {}
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("RECEIVER READY");
}

void loop() {
  delay(10);
}
