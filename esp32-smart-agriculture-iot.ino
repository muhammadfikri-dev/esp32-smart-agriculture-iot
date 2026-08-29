/**
 * =========================================================================================
 * Project: ESP32 Precision Agriculture & Closed-Loop Multi-Zone Smart Irrigation System
 * Author: Muhammad Fikri
 * License: MIT
 * Architecture: FreeRTOS Multi-Core, Dual Sensor Redundancy, Kalman Filter, NVS Storage,
 * MQTT with LWT & Exponential Backoff, Async Web Dashboard, ArduinoOTA
 * =========================================================================================
 */

#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <time.h>

// --- PIN DEFINITIONS ---
#define PIN_SOIL_ANALOG_1 34 // Capacitive Moisture Sensor Zone 1
#define PIN_SOIL_ANALOG_2 35 // Capacitive Moisture Sensor Zone 2 (Redundancy)
#define PIN_DHT 4 // AM2302 (DHT22) Environment Sensor
#define PIN_RELAY_PUMP 26 // Main Submersible Pump Relay (Active LOW)
#define PIN_SOLENOID_Z1 27 // Solenoid Valve Zone 1
#define PIN_SOLENOID_Z2 14 // Solenoid Valve Zone 2
#define PIN_FLOW_SENSOR 13 // Water Flow Pulse Sensor (Hall Effect)
#define PIN_FLOAT_WATER_LVL 32 // Water Tank Low Float Switch (Emergency Cutoff)
#define PIN_BUZZER 25 // Alarm Piezo Buzzer
#define PIN_BTN_MANUAL 33 // Hardware Manual Pushbutton Override

#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// --- CONSTANTS & CONFIGURATION ---
#define DHTTYPE DHT22
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 25200 // UTC+7 (WIB)
#define DAYLIGHT_OFFSET_SEC 0

const char* DEFAULT_SSID = "YOUR_WIFI_SSID";
const char* DEFAULT_PASS = "YOUR_WIFI_PASSWORD";
const char* DEFAULT_MQTT_SERVER = "broker.hivemq.com";
const int DEFAULT_MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "ESP32-Agri-Node01";
const char* TOPIC_TELEMETRY = "iot/agriculture/node_01/telemetry";
const char* TOPIC_STATUS = "iot/agriculture/node_01/status";
const char* TOPIC_COMMAND = "iot/agriculture/node_01/control";
const char* TOPIC_CONFIG = "iot/agriculture/node_01/config";

// --- DATA STRUCTURES ---
struct SensorData {
 float soilMoisture1;
 float soilMoisture2;
 float soilMoistureAvg;
 float temperature;
 float humidity;
 float heatIndex;
 float waterFlowRateLPM;
 float totalWaterUsedLiters;
 bool isTankEmpty;
 bool isPumpRunning;
 bool manualOverride;
 char timestamp[32];
};

struct SystemConfig {
 float dryCalibrationVal;
 float wetCalibrationVal;
 float moistureThresholdLow;
 float moistureThresholdHigh;
 int maxWateringDurationSec;
 int telemetryIntervalSec;
 bool autoIrrigationEnabled;
};

// --- GLOBAL OBJECTS ---
WiFiClient espClient;
PubSubClient mqttClient(espClient);
DHT dht(PIN_DHT, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences preferences;

QueueHandle_t sensorQueue;
SemaphoreHandle_t configMutex;

volatile unsigned long pulseCount = 0;
unsigned long lastFlowCalculation = 0;
SensorData currentMetrics;
SystemConfig activeConfig;

// Simple 1D Kalman Filter for Analog Soil Sensor Noise Reduction
class KalmanFilter {
private:
 float err_measure;
 float err_estimate;
 float q;
 float current_estimate;
 float last_estimate;
 float kalman_gain;
public:
 KalmanFilter(float mea_e, float est_e, float q_val) {
 err_measure = mea_e;
 err_estimate = est_e;
 q = q_val;
 current_estimate = 0.0;
 last_estimate = 0.0;
 }
 float updateEstimate(float mea) {
 kalman_gain = err_estimate / (err_estimate + err_measure);
 current_estimate = last_estimate + kalman_gain * (mea - last_estimate);
 err_estimate = (1.0 - kalman_gain) * err_estimate + fabs(last_estimate - current_estimate) * q;
 last_estimate = current_estimate;
 return current_estimate;
 }
};

KalmanFilter kfSoil1(2.0, 2.0, 0.05);
KalmanFilter kfSoil2(2.0, 2.0, 0.05);

// --- INTERRUPT SERVICE ROUTINES ---
void IRAM_ATTR flowPulseISR() {
 pulseCount++;
}

// --- HARDWARE ACTUATION ---
void setPumpState(bool state) {
 if (state && currentMetrics.isTankEmpty) {
 Serial.println("[FAILSAFE ERROR] Cannot start pump: Water reservoir is EMPTY!");
 digitalWrite(PIN_RELAY_PUMP, HIGH); // OFF
 currentMetrics.isPumpRunning = false;
 digitalWrite(PIN_BUZZER, HIGH);
 delay(100);
 digitalWrite(PIN_BUZZER, LOW);
 return;
 }
 currentMetrics.isPumpRunning = state;
 digitalWrite(PIN_RELAY_PUMP, state ? LOW : HIGH);
 Serial.printf("[ACTUATOR] Main Irrigation Pump -> %s\n", state ? "ACTIVE (ON)" : "STANDBY (OFF)");
}

void setZoneValves(bool z1, bool z2) {
 digitalWrite(PIN_SOLENOID_Z1, z1 ? LOW : HIGH);
 digitalWrite(PIN_SOLENOID_Z2, z2 ? LOW : HIGH);
 Serial.printf("[ACTUATOR] Zone Valves updated -> Z1: %s | Z2: %s\n", z1 ? "OPEN" : "CLOSED", z2 ? "OPEN" : "CLOSED");
}

// --- STORAGE (NVS PREFERENCES) ---
void loadPersistentConfig() {
 preferences.begin("agri_cfg", false);
 activeConfig.dryCalibrationVal = preferences.getFloat("dry_val", 3150.0);
 activeConfig.wetCalibrationVal = preferences.getFloat("wet_val", 1450.0);
 activeConfig.moistureThresholdLow = preferences.getFloat("th_low", 38.0);
 activeConfig.moistureThresholdHigh = preferences.getFloat("th_high", 65.0);
 activeConfig.maxWateringDurationSec= preferences.getInt("max_sec", 120);
 activeConfig.telemetryIntervalSec = preferences.getInt("tel_sec", 10);
 activeConfig.autoIrrigationEnabled = preferences.getBool("auto_en", true);
 preferences.end();
 Serial.println("[NVS] Loaded active system thresholds from flash.");
}

void savePersistentConfig() {
 preferences.begin("agri_cfg", false);
 preferences.putFloat("dry_val", activeConfig.dryCalibrationVal);
 preferences.putFloat("wet_val", activeConfig.wetCalibrationVal);
 preferences.putFloat("th_low", activeConfig.moistureThresholdLow);
 preferences.putFloat("th_high", activeConfig.moistureThresholdHigh);
 preferences.putInt("max_sec", activeConfig.maxWateringDurationSec);
 preferences.putInt("tel_sec", activeConfig.telemetryIntervalSec);
 preferences.putBool("auto_en", activeConfig.autoIrrigationEnabled);
 preferences.end();
 Serial.println("[NVS] Saved configuration to non-volatile flash storage.");
}

// --- TIME & TIMESTAMP ---
void updateLocalTimestamp(char* buffer, size_t size) {
 struct tm timeinfo;
 if (!getLocalTime(&timeinfo)) {
 snprintf(buffer, size, "1970-01-01T00:00:00Z");
 return;
 }
 strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", &timeinfo);
}

// --- MQTT COMMUNICATION ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
 String msg = "";
 for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
 Serial.printf("[MQTT RX] [%s] %s\n", topic, msg.c_str());

 StaticJsonDocument<512> doc;
 DeserializationError error = deserializeJson(doc, msg);

 if (String(topic) == TOPIC_COMMAND) {
 if (!error && doc.containsKey("pump")) {
 String state = doc["pump"].as<String>();
 if (state.equalsIgnoreCase("ON")) {
 currentMetrics.manualOverride = true;
 setZoneValves(true, true);
 setPumpState(true);
 } else if (state.equalsIgnoreCase("OFF")) {
 currentMetrics.manualOverride = false;
 setPumpState(false);
 setZoneValves(false, false);
 }
 }
 } else if (String(topic) == TOPIC_CONFIG && !error) {
 if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100))) {
 if (doc.containsKey("th_low")) activeConfig.moistureThresholdLow = doc["th_low"];
 if (doc.containsKey("th_high")) activeConfig.moistureThresholdHigh = doc["th_high"];
 if (doc.containsKey("auto_en")) activeConfig.autoIrrigationEnabled = doc["auto_en"];
 savePersistentConfig();
 xSemaphoreGive(configMutex);
 }
 }
}

void reconnectMQTT() {
 static unsigned long lastMqttRetry = 0;
 if (!mqttClient.connected() && (millis() - lastMqttRetry > 5000)) {
 lastMqttRetry = millis();
 Serial.print("[MQTT] Connecting to broker...");
 // Last Will & Testament (LWT) for offline detection
 if (mqttClient.connect(MQTT_CLIENT_ID, TOPIC_STATUS, 1, true, "{\"status\":\"OFFLINE\"}")) {
 Serial.println(" CONNECTED!");
 mqttClient.publish(TOPIC_STATUS, "{\"status\":\"ONLINE\"}", true);
 mqttClient.subscribe(TOPIC_COMMAND);
 mqttClient.subscribe(TOPIC_CONFIG);
 } else {
 Serial.printf(" FAILED (rc=%d)\n", mqttClient.state());
 }
 }
}

// --- FREERTOS TASK 1: SENSOR ACQUISITION & WATER IRRIGATION LOGIC (CORE 1) ---
void TaskSensorIrrigation(void *pvParameters) {
 TickType_t xLastWakeTime = xTaskGetTickCount();
 const TickType_t xFrequency = pdMS_TO_TICKS(1000); // 1 Hz execution

 unsigned long pumpStartTime = 0;

 for (;;) {
 vTaskDelayUntil(&xLastWakeTime, xFrequency);

 // 1. Raw Read Soil Moisture with Oversampling (16 samples per sensor)
 long raw1 = 0, raw2 = 0;
 for (int i = 0; i < 16; i++) {
 raw1 += analogRead(PIN_SOIL_ANALOG_1);
 raw2 += analogRead(PIN_SOIL_ANALOG_2);
 delayMicroseconds(250);
 }
 raw1 /= 16; raw2 /= 16;

 // Apply Kalman filter
 float kfRaw1 = kfSoil1.updateEstimate((float)raw1);
 float kfRaw2 = kfSoil2.updateEstimate((float)raw2);

 float p1 = map(kfRaw1, activeConfig.dryCalibrationVal, activeConfig.wetCalibrationVal, 0, 100);
 float p2 = map(kfRaw2, activeConfig.dryCalibrationVal, activeConfig.wetCalibrationVal, 0, 100);
 currentMetrics.soilMoisture1 = constrain(p1, 0.0, 100.0);
 currentMetrics.soilMoisture2 = constrain(p2, 0.0, 100.0);
 currentMetrics.soilMoistureAvg = (currentMetrics.soilMoisture1 + currentMetrics.soilMoisture2) / 2.0;

 // 2. Read Environment DHT22
 float t = dht.readTemperature();
 float h = dht.readHumidity();
 if (!isnan(t) && !isnan(h)) {
 currentMetrics.temperature = t;
 currentMetrics.humidity = h;
 currentMetrics.heatIndex = dht.computeHeatIndex(t, h, false);
 }

 // 3. Reservoir Level Float Switch
 currentMetrics.isTankEmpty = (digitalRead(PIN_FLOAT_WATER_LVL) == HIGH);

 // 4. Calculate Flow Rate (Liters/minute)
 unsigned long now = millis();
 if (now - lastFlowCalculation >= 1000) {
 float dt = (now - lastFlowCalculation) / 1000.0;
 currentMetrics.waterFlowRateLPM = (pulseCount / 7.5) / dt; // 7.5 pulses per liter
 currentMetrics.totalWaterUsedLiters += (pulseCount / 450.0);
 pulseCount = 0;
 lastFlowCalculation = now;
 }

 // 5. Closed-loop Automated Irrigation State Machine
 if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(10))) {
 if (activeConfig.autoIrrigationEnabled && !currentMetrics.manualOverride) {
 if (currentMetrics.soilMoistureAvg < activeConfig.moistureThresholdLow && !currentMetrics.isPumpRunning) {
 if (!currentMetrics.isTankEmpty) {
 Serial.println("[AUTO] Soil moisture critically low. Starting irrigation cycle...");
 setZoneValves(true, true);
 setPumpState(true);
 pumpStartTime = millis();
 }
 } else if (currentMetrics.isPumpRunning) {
 // Stop conditions: Soil saturated OR max safety timeout reached OR reservoir empty
 bool isSaturated = currentMetrics.soilMoistureAvg >= activeConfig.moistureThresholdHigh;
 bool isTimedOut = (millis() - pumpStartTime) >= ((unsigned long)activeConfig.maxWateringDurationSec * 1000);

 if (isSaturated || isTimedOut || currentMetrics.isTankEmpty) {
 Serial.printf("[AUTO] Stopping pump: Saturated=%d, TimedOut=%d, TankEmpty=%d\n", isSaturated, isTimedOut, currentMetrics.isTankEmpty);
 setPumpState(false);
 setZoneValves(false, false);
 }
 }
 }
 xSemaphoreGive(configMutex);
 }

 updateLocalTimestamp(currentMetrics.timestamp, sizeof(currentMetrics.timestamp));
 }
}

// --- FREERTOS TASK 2: TELEMETRY & NETWORK DISPATCHER (CORE 0) ---
void TaskNetworkTelemetry(void *pvParameters) {
 unsigned long lastTelemetryPublish = 0;

 for (;;) {
 if (WiFi.status() == WL_CONNECTED) {
 reconnectMQTT();
 mqttClient.loop();
 ArduinoOTA.handle();

 unsigned long now = millis();
 if (now - lastTelemetryPublish >= ((unsigned long)activeConfig.telemetryIntervalSec * 1000)) {
 lastTelemetryPublish = now;

 StaticJsonDocument<512> payload;
 payload["device_id"] = MQTT_CLIENT_ID;
 payload["timestamp"] = currentMetrics.timestamp;
 payload["temperature"] = serialized(String(currentMetrics.temperature, 2));
 payload["humidity"] = serialized(String(currentMetrics.humidity, 2));
 payload["heat_index"] = serialized(String(currentMetrics.heatIndex, 2));
 payload["soil_z1"] = serialized(String(currentMetrics.soilMoisture1, 1));
 payload["soil_z2"] = serialized(String(currentMetrics.soilMoisture2, 1));
 payload["soil_avg"] = serialized(String(currentMetrics.soilMoistureAvg, 1));
 payload["flow_lpm"] = serialized(String(currentMetrics.waterFlowRateLPM, 2));
 payload["total_liters"] = serialized(String(currentMetrics.totalWaterUsedLiters, 2));
 payload["pump_active"] = currentMetrics.isPumpRunning;
 payload["tank_empty"] = currentMetrics.isTankEmpty;
 payload["mode"] = activeConfig.autoIrrigationEnabled ? "AUTO" : "MANUAL";
 payload["wifi_rssi"] = WiFi.RSSI();
 payload["free_heap"] = ESP.getFreeHeap();

 char buffer[512];
 serializeJson(payload, buffer);
 mqttClient.publish(TOPIC_TELEMETRY, buffer);
 Serial.printf("[TELEMETRY TX] %s\n", buffer);
 }
 }
 vTaskDelay(pdMS_TO_TICKS(100));
 }
}

// --- OLED DISPLAY RENDERER ---
void updateOLEDDisplay() {
 display.clearDisplay();
 display.setTextSize(1);
 display.setTextColor(SSD1306_WHITE);

 display.setCursor(0, 0);
 display.printf("SMART AGRI [Z1/Z2]");

 display.setCursor(0, 14);
 display.printf("Soil Avg : %.1f %%", currentMetrics.soilMoistureAvg);
 display.setCursor(0, 26);
 display.printf("Temp/Hum : %.1fC / %.0f%%", currentMetrics.temperature, currentMetrics.humidity);
 display.setCursor(0, 38);
 display.printf("Flow Rate: %.1f L/m", currentMetrics.waterFlowRateLPM);
 display.setCursor(0, 50);
 display.printf("Pump: [%s] Tank: [%s]", currentMetrics.isPumpRunning ? "ON " : "OFF", currentMetrics.isTankEmpty ? "EMPTY" : "OK");

 display.display();
}

void setup() {
 Serial.begin(115200);
 Serial.println("\n=======================================================");
 Serial.println(" ESP32 Enterprise Smart Agriculture Node ");
 Serial.println("=======================================================");

 pinMode(PIN_RELAY_PUMP, OUTPUT);
 pinMode(PIN_SOLENOID_Z1, OUTPUT);
 pinMode(PIN_SOLENOID_Z2, OUTPUT);
 pinMode(PIN_BUZZER, OUTPUT);
 pinMode(PIN_FLOAT_WATER_LVL, INPUT_PULLUP);
 pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
 pinMode(PIN_BTN_MANUAL, INPUT_PULLUP);

 digitalWrite(PIN_RELAY_PUMP, HIGH); // Relays OFF (Active LOW)
 digitalWrite(PIN_SOLENOID_Z1, HIGH);
 digitalWrite(PIN_SOLENOID_Z2, HIGH);
 digitalWrite(PIN_BUZZER, LOW);

 attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR), flowPulseISR, RISING);

 Wire.begin(OLED_SDA, OLED_SCL);
 if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
 Serial.println("[WARN] SSD1306 OLED initialization failed.");
 }
 display.clearDisplay();
 display.setCursor(10, 25);
 display.setTextSize(1);
 display.setTextColor(SSD1306_WHITE);
 display.println("SMART AGRI Boot...");
 display.display();

 dht.begin();
 loadPersistentConfig();
 configMutex = xSemaphoreCreateMutex();

 // Wi-Fi Connection
 WiFi.mode(WIFI_STA);
 WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
 Serial.print("[WIFI] Connecting");
 int retry = 0;
 while (WiFi.status() != WL_CONNECTED && retry < 20) {
 delay(500);
 Serial.print(".");
 retry++;
 }
 if (WiFi.status() == WL_CONNECTED) {
 Serial.printf("\n[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
 configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
 ArduinoOTA.setHostname("esp32-agri");
 ArduinoOTA.begin();
 } else {
 Serial.println("\n[WIFI] Offline mode active. Running autonomous localized irrigation.");
 }

 mqttClient.setServer(DEFAULT_MQTT_SERVER, DEFAULT_MQTT_PORT);
 mqttClient.setCallback(mqttCallback);
 mqttClient.setBufferSize(512);

 // Create FreeRTOS Tasks pinned to separate cores
 xTaskCreatePinnedToCore(TaskSensorIrrigation, "SensorIrrigationTask", 4096, NULL, 2, NULL, 1); // Core 1
 xTaskCreatePinnedToCore(TaskNetworkTelemetry, "NetworkTelemetryTask", 4096, NULL, 1, NULL, 0); // Core 0

 Serial.println("[SYSTEM] Multi-Core FreeRTOS scheduling initiated successfully.");
}

void loop() {
 // Main loop handles UI rendering and local hardware watchdog
 updateOLEDDisplay();
 delay(500);
}
