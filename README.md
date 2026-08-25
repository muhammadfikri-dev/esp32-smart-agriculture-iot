# 🌱 ESP32 Smart Agriculture & Irrigation System

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://espressif.com/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-teal.svg)](https://www.arduino.cc/)
[![Portfolio: Laksanasoft](https://img.shields.io/badge/Portfolio-Laksanasoft-green.svg)](#)

An intelligent, automated precision agriculture and soil management solution powered by the **ESP32** microcontroller. This system continuously monitors soil moisture, ambient temperature, and humidity, and autonomously triggers irrigation pumps while publishing telemetry data to an MQTT broker / IoT dashboard.

---

## 📌 Features

- **Automated Irrigation Control:** Relays automatically activate the water pump when soil moisture drops below a configurable threshold.
- **Environmental Telemetry:** Real-time temperature and relative humidity tracking via DHT22 sensor.
- **MQTT & Cloud Integration:** Publishes JSON-formatted sensor metrics every 10 seconds to any standard MQTT broker (ThingsBoard, Node-RED, EMQX, Adafruit IO).
- **Manual Remote Override:** Allows operators to remotely control the water pump via MQTT command topic.
- **Failsafe & Deep Sleep Support:** Robust reconnect logic for Wi-Fi and MQTT drops.

---

## 🛠️ Hardware Requirements & Bill of Materials (BOM)

| Component | Quantity | Description / Interface |
| :--- | :---: | :--- |
| **ESP32 Development Board** (NodeMCU-32S / ESP-WROOM-32) | 1 | Main MCU with Wi-Fi & BLE |
| **Capacitive Soil Moisture Sensor v1.2** | 1 | Analog input (corrosion-resistant) |
| **DHT22 (AM2302) Temperature & Humidity Sensor** | 1 | Digital 1-Wire protocol |
| **5V Single-Channel Relay Module** (Optocoupler isolated) | 1 | Controls 12V / 220V mini submersible water pump |
| **Mini Submersible Water Pump & Tubing** | 1 | 5V - 12V DC |
| **0.96\" I2C OLED Display (SSD1306)** *(Optional)* | 1 | On-site status display |
| **Power Supply** | 1 | 5V 2A DC Adapter |

---

## 🔌 Pinout Diagram & Wiring Table

```
+---------------------+-------------------+-------------------+
| ESP32 GPIO Pin      | Module Pin        | Description       |
+---------------------+-------------------+-------------------+
| GPIO 34 (ADC1_CH6)  | Moisture AOUT     | Soil Analog Read  |
| GPIO 4              | DHT22 DATA        | 10k Pull-up to 3V3|
| GPIO 26             | Relay IN          | Active LOW trigger|
| GPIO 21 (SDA)       | OLED SDA          | I2C Data          |
| GPIO 22 (SCL)       | OLED SCL          | I2C Clock         |
| 3V3 / GND           | Sensor VCC / GND  | Power rails       |
+---------------------+-------------------+-------------------+
```

---

## 📁 Repository Structure

```
├── esp32-smart-agriculture-iot.ino  # Main Arduino sketch
├── config.h.example                 # Wi-Fi & MQTT credentials template
├── .gitignore                       # Standard git ignore rules
└── README.md                        # Documentation
```

---

## 🚀 Quick Start Guide

### 1. Prerequisites
- [Arduino IDE](https://www.arduino.cc/en/software) or [PlatformIO (VS Code)](https://platformio.org/)
- ESP32 Board package installed (`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`)

### 2. Install Required Libraries
Install the following libraries via Arduino Library Manager:
- `DHT sensor library` by Adafruit
- `PubSubClient` by Nick O'Leary
- `ArduinoJson` by Benoit Blanchon
- `Adafruit SSD1306` & `Adafruit GFX Library`

### 3. Configuration
Copy `config.h.example` to `config.h` and update your Wi-Fi and MQTT credentials:

```cpp
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define MQTT_SERVER     "broker.hivemq.com"
#define MQTT_PORT       1883
```

### 4. Upload
Select board **ESP32 Dev Module**, choose the correct COM port, and click **Upload**.

---

## 📊 MQTT Topic Architecture

- **Publish Telemetry:** `laksanasoft/agriculture/node_01/telemetry`
  ```json
  {
    "device_id": "esp32-agri-01",
    "temperature": 28.4,
    "humidity": 65.2,
    "soil_moisture_percent": 42.0,
    "pump_status": "OFF",
    "uptime_seconds": 3600
  }
  ```
- **Subscribe Command:** `laksanasoft/agriculture/node_01/pump/set` (`ON` / `OFF` / `AUTO`)

---

## 📄 License
Distributed under the **MIT License**. See `LICENSE` for more information.

Developed with ❤️ by **Muhammad Fikri** for **Laksanasoft Portfolio**.
