# ⚡ Sistem Pertanian Presisi & Irigasi Cerdas IoT ESP32

[![Lisensi: MIT](https://img.shields.io/badge/Lisensi-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32 / Arduino](https://img.shields.io/badge/Platform-ESP32%20%7C%20Arduino-blue.svg)](#)
[![Framework: Arduino IDE](https://img.shields.io/badge/Framework-Arduino%20IDE%202.0%2B-teal.svg)](https://www.arduino.cc/)
[![Status: Produksi](https://img.shields.io/badge/Status-Firmware%20Produksi-brightgreen.svg)](#)
[![Developer: Muhammad Fikri](https://img.shields.io/badge/Developer-Muhammad%20Fikri-blue.svg)](#)

Sistem otomasi pertanian presisi berbasis sensor kelembaban tanah kapasitif, NPK, suhu lingkungan BME280, flow meter, dan aktuasi solenoid.

---

## 🧠 Arsitektur Firmware & Fitur Produksi

- **FreeRTOS Dual-Core Multitasking & Priority Scheduling:** Memisahkan pemrosesan sensor berkecepatan tinggi dari stack jaringan untuk stabilitas total.
- **Thread Safety & Data Synchronization:** Mutex semaphore dan queue untuk pertukaran data yang aman antar-thread.
- **Digital Filtering & Kalman DSP:** Algoritma filter Kalman dan oversampling peredam noise sinyal analog.
- **Penyimpanan Non-Volatil NVS (Preferences):** Penyimpanan parameter kalibrasi dan state perangkat secara persisten di flash memory internal.
- **Sistem Pengaman Mandiri (*Hardware Failsafe*):** Logika proteksi hardware otomatis untuk mencegah kerusakan akibat anomali.
- **Over-The-Air (OTA) Updates:** Pembaruan firmware nirkabel tanpa perlu menghubungkan kabel USB.

---

## 🔌 Skema Pinout & Koneksi Hardware

Lihat rincian pemetaan pin hardware pada file [`config.h.example`](./config.h.example).

---

## 🚀 Panduan Kompilasi & Upload Firmware

1. Buka file **`esp32-smart-agriculture-iot.ino`** menggunakan **Arduino IDE 2.0+**.
2. Salin template konfigurasi:
 ```bash
 cp config.h.example config.h
 ```
3. Sesuaikan parameter Wi-Fi, broker MQTT, dan pinout hardware di `config.h`.
4. Pilih model board target pada menu **Tools > Board**.
5. Klik **Upload** untuk mem-flash program.

---

## 📄 Lisensi
Didistribusikan di bawah lisensi open-source **MIT License**. Dikembangkan oleh **Muhammad Fikri**.
