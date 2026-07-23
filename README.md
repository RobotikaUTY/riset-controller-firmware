# Katyusha Controller 🚀

> **ESP-NOW based robot remote control** — developed by **Technology Development Division**, Robotika UTY.

Katyusha Controller is an ESP32 firmware that turns your ESP32 board into a wireless robot remote control using the **ESP-NOW** protocol (peer-to-peer connection without a WiFi router). It features a 128x64 OLED display, 8 control buttons, and a receiver scanning system to automatically discover and connect to robots.

---

## Table of Contents 📑

- [Features](#features)
- [Project Structure](#project-structure)
- [Hardware Requirements](#hardware-requirements)
- [Installation](#installation)
    - [1. Prerequisites](#1-prerequisites)
    - [2. Upload Transmitter Firmware (Controller)](#2-upload-transmitter-firmware-controller)
    - [3. Upload Receiver Firmware (Robot)](#3-upload-receiver-firmware-robot)
- [Usage](#usage)
    - [Transmitter — Controller](#transmitter--controller)
    - [Receiver — Robot](#receiver--robot)
- [ESP-NOW Protocol](#esp-now-protocol)
- [Future Development](#future-development)
- [Contributing](#contributing)
- [License](#license)
- [Contact](#contact)

---

## Features ✨

### Transmitter (Controller) 🎮

| Feature | Description |
|---------|-------------|
| **8 Control Buttons** | D-pad (UP/DOWN/LEFT/RIGHT) + Y, X, A, B — software-debounced. |
| **OLED 128x64 Display** | Shows movement mode, connection status, and active receiver name. |
| **Home Screen** | Displays robot mode (FORWARD, BACKWARD, TURN, SPIN, etc.), LINK status (ONLINE/SEARCH), receiver name. |
| **Menu System** | Navigate Scanning/Exit menu using UP/DOWN and X. |
| **ESP-NOW Scanner** | Discovers nearby ESP-NOW receivers within 6 seconds, lists them, and lets you select one to control. |
| **Marquee Text** | Long receiver names automatically scroll across the display. |
| **10 Movement Modes** | FORWARD, BACKWARD, TURN_LEFT, TURN_RIGHT, FORWARD_LEFT, FORWARD_RIGHT, BACKWARD_LEFT, BACKWARD_RIGHT, SPIN_LEFT, SPIN_RIGHT, STOP. |

### Receiver (Sumo Robot) 🤖

| Feature | Description |
|---------|-------------|
| **TB6612FNG Motor Driver** | Controls 2 DC motors with PWM (8-bit, 1 kHz). |
| **ESP-NOW Discovery** | Responds to discovery requests from the transmitter with the device name. |
| **Safety Timeout** | Motors automatically stop if no command is received for 300 ms. |

### Utility — MAC Address Reader 🔧

| Feature | Description |
|---------|-------------|
| **esp32-getmacaddress** | Reads MAC addresses from various interfaces (WiFi STA, Soft-AP, Bluetooth, Ethernet, IEEE802154). Useful for obtaining the receiver MAC before pairing. |

---

## Project Structure 📂

```
katyusha-controller/
├── esp-transmitter/              # Transmitter firmware (controller)
│   ├── src/main.cpp              # Main transmitter code
│   ├── platformio.ini            # PlatformIO configuration
│   └── ...
├── esp-receiver-sumo/            # Receiver firmware (sumo robot)
│   ├── src/main.cpp              # Main receiver code
│   ├── platformio.ini            # PlatformIO configuration
│   └── ...
├── esp32-getmacaddress/          # MAC address utility
│   ├── src/main.cpp
│   └── platformio.ini
└── README.md                     # Project documentation
```

---

## Hardware Requirements 🔧

### Transmitter (Controller) 🎮

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32 (DOIT ESP32 DEVKIT V1 or compatible) |
| Display | OLED 128x64, I2C (address 0x3C) — SSD1306 |
| Buttons | 8x Tactile switches, internal pull-up (LOW = pressed) |
| I2C Connection | SDA = GPIO21, SCL = GPIO22 |

**Button Mapping:**

| Button | GPIO | Function |
|--------|------|----------|
| UP | 32 | Forward |
| DOWN | 23 | Backward |
| LEFT | 19 | Turn Left |
| RIGHT | 18 | Turn Right |
| Y 🟡 | 27 | Menu |
| X 🔵 | 26 | Scanning / Select |
| A 🟢 | 25 | (Future) |
| B 🔴 | 33 | Back / Cancel |

### Receiver (Sumo Robot) 🤖

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32 (DOIT ESP32 DEVKIT V1 or compatible) |
| Motor Driver | TB6612FNG |
| Motors | 2x DC Motor |

**Motor Driver Pin Mapping:**

| Function | ESP32 Pin |
|----------|-----------|
| PWMA (Motor A) | 13 |
| AIN1 | 26 |
| AIN2 | 27 |
| PWMB (Motor B) | 32 |
| BIN1 | 25 |
| BIN2 | 33 |

---

## Installation 📥

### Prerequisites 📋

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- [Git](https://git-scm.com/)

### 1. Clone Repository 📦

```bash
git clone https://github.com/RobotikaUTY/riset-controller-firmware.git
cd riset-controller-firmware
```

### 2. Upload Transmitter Firmware (Controller) 📤

```bash
cd esp-transmitter
pio run --target upload
```

After uploading, open the serial monitor to verify:

```bash
pio device monitor
```

### 3. Upload Receiver Firmware (Robot) 📥

First, obtain the MAC address of your ESP32 receiver:

```bash
cd esp32-getmacaddress
pio run --target upload
pio device monitor
# Note the WiFi Station MAC address (e.g., 78:1C:3C:2B:DF:B4)
```

Then copy that MAC address into `esp-transmitter/src/main.cpp`:

```cpp
// Replace with your receiver's MAC address
uint8_t receiverMAC[] = {0x78, 0x1C, 0x3C, 0x2B, 0xDF, 0xB4};
```

Upload the receiver firmware:

```bash
cd esp-receiver-sumo
pio run --target upload
```

Re-upload the transmitter with the updated MAC:

```bash
cd esp-transmitter
pio run --target upload
```

> **Note:** If you use the _scanning_ feature, you don't need to set the MAC manually — the transmitter will automatically discover receivers via ESP-NOW discovery.

---

## Usage 🎯

### Transmitter — Controller 🎮

1. Power on the transmitter — the **Home Screen** will appear.
2. Press **Y** 🟡 to open the **Menu**.
3. Select **Scanning** using UP/DOWN, then press **X** 🔵 to start scanning.
4. The transmitter will search for receivers for 6 seconds and display the list.
5. Select a receiver with UP/DOWN, then press **X** 🔵 to connect.
6. Once connected, use the **D-pad** to control the robot:
    - **UP** → Forward
    - **DOWN** → Backward
    - **LEFT** → Turn left
    - **RIGHT** → Turn right
    - **UP + LEFT** → Forward-left diagonal
    - **UP + RIGHT** → Forward-right diagonal
    - **DOWN + LEFT** → Backward-left diagonal
    - **DOWN + RIGHT** → Backward-right diagonal
    - **Y** 🟡 → Menu
    - **B** 🔴 → Back
    - **X** 🔵 → Scanning / Select
7. The display shows the movement mode and connection status in _real-time_.

### Receiver — Robot 🤖

1. Power on the receiver — it will immediately be ready to receive commands.
2. When a _discovery request_ is received from the transmitter, the receiver replies with its device name.
3. The robot moves according to the commands received from the transmitter.
4. If no command is received for 300 ms, the robot automatically stops (_safety timeout_).

---

## ESP-NOW Protocol 📡

### Packet Types

| Type | Value | Description |
|------|-------|-------------|
| `PACKET_TYPE_CONTROL` | 1 | Robot control command |
| `PACKET_TYPE_DISCOVERY_REQUEST` | 2 | Discovery request from transmitter |
| `PACKET_TYPE_DISCOVERY_RESPONSE` | 3 | Discovery response from receiver |

### Control Packet (6 bytes)

```c
typedef struct {
  uint8_t buttons;   // Bitmask of pressed buttons
  uint8_t speed;     // Motor speed (0-255)
  uint8_t mode;      // Movement mode (FORWARD, BACKWARD, etc.)
} ControlPacket;
```

### Discovery Packet (23 bytes)

```c
typedef struct {
  uint8_t type;          // Packet type (REQUEST / RESPONSE)
  uint8_t senderMac[6];  // Sender MAC address
  char     name[16];     // Device name (null-terminated)
} DiscoveryPacket;
```

---

## Future Development 🚧

Upcoming development plans:

- [ ] **Custom Button Mapping** — Users can reassign button functions.
- [ ] **Robot Ability System** — Configure robot-specific capabilities (max speed, acceleration, tank-drive / mecanum mode).
- [ ] **PID / Stabilization** — Control algorithms to maintain stable movement at high speeds.
- [ ] **Multiple Robot Support** — Switch between multiple robots without re-scanning.
- [ ] **Telemetry Feedback** — Receive telemetry data from the robot (battery, sensors, temperature).
- [ ] **Profile System** — Save configurations for different robots.
- [ ] **Receiver Firmware for Other Robots** — Beyond sumo robots (mecanum, rover, battlebot, etc.).
- [ ] **Robot Soccer Compatibility** — Receiver firmware for soccer robots with precise and responsive control.

We are open to ideas and contributions from anyone!

---

## Contributing 🤝

We welcome contributions from everyone — whether it's a bug fix, a new feature, or documentation improvements.

### Contribution Guide 📋

1. **Fork** this repository to your GitHub account.
2. **Clone** your fork locally:
    ```bash
    git clone https://github.com/username/riset-controller-firmware.git
    ```
3. Create a new **branch** for your feature/fix:
    ```bash
    git checkout -b feat/awesome-feature
    ```
4. Make your changes, then **commit**:
    ```bash
    git add .
    git commit -m "feat: add awesome feature"
    ```
5. **Push** to your fork:
    ```bash
    git push origin feat/awesome-feature
    ```
6. Open a **Pull Request** to the main repository — describe your changes in detail.

### Contribution Guidelines

- Follow the existing _coding style_ (C++ with PlatformIO).
- Use _conventional commits_: `feat:`, `fix:`, `docs:`, `refactor:`, etc.
- Provide a clear description in your PR.
- If adding a new feature, include appropriate documentation.
- Ensure the code compiles without _warnings_.

### Bug Reports / Feature Requests

Open a [GitHub Issue](https://github.com/RobotikaUTY/riset-controller-firmware/issues) and use the available template.

---

## License ⚖️

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

```
MIT License

Copyright (c) 2026 Technology Development Division, Robotika UTY

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Contact 📬

**Technology Development Division**  
Robotika Universitas Teknologi Yogyakarta (UTY)

- GitHub: [github.com/RobotikaUTY](https://github.com/RobotikaUTY)
- Email: psrobotika.uty@gmail.com / robotika@uty.ac.id

---
