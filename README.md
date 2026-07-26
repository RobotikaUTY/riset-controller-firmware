# ESP NOW Controller 🚀

> **Kendali robot berbasis ESP-NOW** — dikembangkan oleh **Divisi Pengembangan Teknologi**, Robotika UTY.

ESP NOW Controller adalah firmware ESP32 universal yang mengubah papan ESP32 Anda menjadi kendali jarak jauh robot nirkabel menggunakan protokol **ESP-NOW** (koneksi peer-to-peer tanpa router WiFi). Dilengkapi dengan layar OLED 128x64, 8 tombol kontrol, Layar Utama, Pengaturan, Pemasangan Perangkat, Kecepatan Turbo, dan Indikator Baterai untuk kendali maupun robot.

---

## Daftar Isi 📑

- [Fitur](#fitur)
- [Struktur Proyek](#struktur-proyek)
- [Kebutuhan Perangkat Keras](#kebutuhan-perangkat-keras)
- [Instalasi](#instalasi)
    - [1. Prasyarat](#1-prasyarat)
    - [2. Upload Firmware Transmitter (Kendali)](#2-upload-firmware-transmitter-kendali)
    - [3. Upload Firmware Receiver (Robot)](#3-upload-firmware-receiver-robot)
- [Penggunaan](#penggunaan)
    - [Transmitter — Kendali](#transmitter--kendali)
    - [Receiver — Robot](#receiver--robot)
- [Protokol ESP-NOW](#protokol-esp-now)
- [Kontribusi](#kontribusi)
- [Lisensi](#lisensi)
- [Kontak](#kontak)

---

## Fitur ✨

### ✅ Fitur Tersedia

| Fitur                    | Deskripsi                                                                                                                     |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| **8 Tombol Kontrol**     | D-pad (ATAS/BAWAH/KIRI/KANAN) + Y, X, A, B — debounce perangkat lunak.                                                        |
| **Layar OLED 128x64**    | Menampilkan mode gerakan, status koneksi, dan nama receiver yang aktif.                                                        |
| **Layar Utama**          | Menampilkan mode gerakan, status LINK (ONLINE/SEARCH), nama receiver, status Turbo, dan indikator Baterai.                    |
| **Pengaturan**           | Navigasi menu pengaturan dengan menekan **ATAS + Y** secara bersamaan.                                                         |
| **Pemasangan Perangkat** | Pemindai ESP-NOW yang menemukan receiver terdekat dalam 6 detik dan memungkinkan Anda memilih salah satu untuk dipasangkan.    |
| **Turbo Speed**          | Aktifkan/nonaktifkan mode turbo untuk meningkatkan kecepatan robot.                                                            |
| **Indikator Baterai**    | Menampilkan level baterai untuk kendali dan robot.                                                                            |
| **Teks Berjalan**        | Nama receiver yang panjang secara otomatis bergulir di layar.                                                                 |
| **10 Mode Gerakan**      | MAJU, MUNDUR, BELOK_KIRI, BELOK_KANAN, MAJU_KIRI, MAJU_KANAN, MUNDUR_KIRI, MUNDUR_KANAN, PUTAR_KIRI, PUTAR_KANAN, BERHENTI. |

### 🔄 Dalam Pengembangan

| Fitur             | Deskripsi                                             |
| ----------------- | ----------------------------------------------------- |
| **Set Button**    | Konfigurasi pemetaan tombol khusus.                   |
| **Set Movement**  | Konfigurasi perilaku dan preferensi gerakan robot.    |

---

## Struktur Proyek 📂

```
esp-now-controller/
├── esp-transmitter/              # Firmware transmitter (kendali)
│   ├── src/main.cpp              # Kode utama transmitter
│   ├── platformio.ini            # Konfigurasi PlatformIO
│   └── ...
├── esp-receiver/                 # Firmware receiver (robot)
│   ├── src/main.cpp              # Kode utama receiver
│   ├── platformio.ini            # Konfigurasi PlatformIO
│   └── ...
├── esp32-getmacaddress/          # Utilitas alamat MAC
│   ├── src/main.cpp
│   └── platformio.ini
└── README.md                     # Dokumentasi proyek
```

---

## Kebutuhan Perangkat Keras 🔧

### Transmitter (Kendali) 🎮

| Komponen        | Spesifikasi                                          |
| --------------- | ---------------------------------------------------- |
| Mikrokontroler  | ESP32 (DOIT ESP32 DEVKIT V1 atau kompatibel)         |
| Layar           | OLED 128x64, I2C (alamat 0x3C) — SSD1306             |
| Tombol          | 8x Saklar tactile, internal pull-up (LOW = ditekan)  |
| Koneksi I2C     | SDA = GPIO21, SCL = GPIO22                           |

**Pemetaan Tombol:**

| Tombol  | GPIO | Fungsi            |
| ------- | ---- | ----------------- |
| ATAS    | 32   | Maju              |
| BAWAH   | 23   | Mundur            |
| KIRI    | 19   | Belok Kiri        |
| KANAN   | 18   | Belok Kanan       |
| Y 🟡    | 27   | Menu              |
| X 🔵    | 26   | Pemindaian / Pilih|
| A 🟢    | 25   | (Mendatang)       |
| B 🔴    | 33   | Kembali / Batal   |

### Receiver (Robot) 🤖

| Komponen        | Spesifikasi                               |
| --------------- | ----------------------------------------- |
| Mikrokontroler  | ESP32 (DOIT ESP32 DEVKIT V1 atau kompatibel) |
| Driver Motor    | TB6612FNG                                 |
| Motor           | 2x Motor DC                               |

**Pemetaan Pin Driver Motor:**

| Fungsi          | Pin ESP32 |
| --------------- | --------- |
| PWMA (Motor A)  | 13        |
| AIN1            | 26        |
| AIN2            | 27        |
| PWMB (Motor B)  | 32        |
| BIN1            | 25        |
| BIN2            | 33        |

---

## Instalasi 📥

### Prasyarat 📋

- [PlatformIO](https://platformio.org/) (ekstensi VS Code atau CLI)
- [Git](https://git-scm.com/)

### 1. Kloning Repository 📦

```bash
git clone https://github.com/RobotikaUTY/riset-controller-firmware.git
cd riset-controller-firmware
```

### 2. Upload Firmware Transmitter (Kendali) 📤

```bash
cd esp-transmitter
pio run --target upload
```

Setelah upload, buka serial monitor untuk verifikasi:

```bash
pio device monitor
```

### 3. Upload Firmware Receiver (Robot) 📥

Pertama, dapatkan alamat MAC receiver ESP32 Anda:

```bash
cd esp32-getmacaddress
pio run --target upload
pio device monitor
# Catat alamat MAC WiFi Station (contoh: 78:1C:3C:2B:DF:B4)
```

Kemudian salin alamat MAC tersebut ke `esp-transmitter/src/main.cpp`:

```cpp
// Ganti dengan alamat MAC receiver Anda
uint8_t receiverMAC[] = {0x78, 0x1C, 0x3C, 0x2B, 0xDF, 0xB4};
```

Upload firmware receiver:

```bash
cd esp-receiver
pio run --target upload
```

Upload ulang transmitter dengan MAC yang telah diperbarui:

```bash
cd esp-transmitter
pio run --target upload
```

> **Catatan:** Jika Anda menggunakan fitur _pemindaian_, Anda tidak perlu mengatur MAC secara manual — transmitter akan secara otomatis menemukan receiver melalui penemuan ESP-NOW.

---

## Penggunaan 🎯

### Transmitter — Kendali 🎮

1. Nyalakan transmitter — **Layar Utama** akan muncul.
2. Tekan **ATAS + Y** secara bersamaan untuk membuka **Pengaturan**.
3. Pilih **Pemindaian** menggunakan ATAS/BAWAH, lalu tekan **X** 🔵 untuk memulai pemindaian.
4. Transmitter akan mencari receiver selama 6 detik dan menampilkan daftarnya.
5. Pilih receiver dengan ATAS/BAWAH, lalu tekan **X** 🔵 untuk terhubung.
6. Setelah terhubung, gunakan **D-pad** untuk mengendalikan robot:
    - **ATAS** → Maju
    - **BAWAH** → Mundur
    - **KIRI** → Belok kiri
    - **KANAN** → Belok kanan
    - **ATAS + KIRI** → Diagonal maju-kiri
    - **ATAS + KANAN** → Diagonal maju-kanan
    - **BAWAH + KIRI** → Diagonal mundur-kiri
    - **BAWAH + KANAN** → Diagonal mundur-kanan
    - **B** 🔴 → Kembali / Batal
    - **X** 🔵 → Pilih
7. Layar menampilkan mode gerakan, status koneksi, status Turbo, dan indikator baterai secara _real-time_.

### Receiver — Robot 🤖

1. Nyalakan receiver — receiver akan langsung siap menerima perintah.
2. Saat permintaan _penemuan_ diterima dari transmitter, receiver membalas dengan nama perangkatnya.
3. Robot bergerak sesuai perintah yang diterima dari transmitter.
4. Jika tidak ada perintah yang diterima selama 300 ms, robot secara otomatis berhenti (_timeout pengamanan_).

---

## Protokol ESP-NOW 📡

### Tipe Paket

| Tipe                             | Nilai | Deskripsi                         |
| -------------------------------- | ----- | --------------------------------- |
| `PACKET_TYPE_CONTROL`            | 1     | Perintah kendali robot            |
| `PACKET_TYPE_DISCOVERY_REQUEST`  | 2     | Permintaan penemuan dari transmitter |
| `PACKET_TYPE_DISCOVERY_RESPONSE` | 3     | Respon penemuan dari receiver     |

### Paket Kontrol (6 byte)

```c
typedef struct {
  uint8_t buttons;   // Bitmask tombol yang ditekan
  uint8_t speed;     // Kecepatan motor (0-255)
  uint8_t mode;      // Mode gerakan (MAJU, MUNDUR, dll.)
} ControlPacket;
```

### Paket Penemuan (23 byte)

```c
typedef struct {
  uint8_t type;          // Tipe paket (REQUEST / RESPONSE)
  uint8_t senderMac[6];  // Alamat MAC pengirim
  char     name[16];     // Nama perangkat (diakhiri null)
} DiscoveryPacket;
```

---

## Kontribusi 🤝

Kami menyambut kontribusi dari siapa pun — baik itu perbaikan bug, fitur baru, atau peningkatan dokumentasi.

### Panduan Kontribusi 📋

1. **Fork** repository ini ke akun GitHub Anda.
2. **Clone** fork Anda secara lokal:
    ```bash
    git clone https://github.com/username/riset-controller-firmware.git
    ```
3. Buat **branch** baru untuk fitur/perbaikan Anda:
    ```bash
    git checkout -b feat/fitur-keren
    ```
4. Lakukan perubahan, lalu **commit**:
    ```bash
    git add .
    git commit -m "feat: tambah fitur keren"
    ```
5. **Push** ke fork Anda:
    ```bash
    git push origin feat/fitur-keren
    ```
6. Buka **Pull Request** ke repository utama — jelaskan perubahan Anda secara detail.

### Pedoman Kontribusi

- Ikuti _gaya penulisan kode_ yang ada (C++ dengan PlatformIO).
- Gunakan _conventional commits_: `feat:`, `fix:`, `docs:`, `refactor:`, dll.
- Berikan deskripsi yang jelas di PR Anda.
- Jika menambahkan fitur baru, sertakan dokumentasi yang sesuai.
- Pastikan kode dapat dikompilasi tanpa _peringatan_.

### Laporan Bug / Permintaan Fitur

Buka [GitHub Issue](https://github.com/RobotikaUTY/riset-controller-firmware/issues) dan gunakan template yang tersedia.

---

## Lisensi ⚖️

Proyek ini dilisensikan di bawah **Lisensi MIT** — lihat file [LICENSE](LICENSE) untuk detailnya.

```
MIT License

Copyright (c) 2026 Divisi Pengembangan Teknologi, Robotika UTY

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

## Kontak 📬

**Divisi Pengembangan Teknologi**  
Robotika Universitas Teknologi Yogyakarta (UTY)

- GitHub: [github.com/RobotikaUTY](https://github.com/RobotikaUTY)
- Email: psrobotika.uty@gmail.com / robotika@uty.ac.id

---
