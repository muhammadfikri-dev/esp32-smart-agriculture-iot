/**
 * @file esp32-smart-agriculture-iot.ino
 * @brief ESP32 Smart Agriculture & Automated Irrigation System
 * @author Muhammad Fikri (Laksanasoft)
 * @license MIT
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// Pins
#define PIN_SOIL_ANALOG  34
#define PIN_DHT          4
#define PIN_RELAY_PUMP   26
#define DHTTYPE          DHT22

// Default Credentials (override with config.h if present)
#if __has_include("config.h")
  #include "config.h"
#else
  #define WIFI_SSID        "YOUR_WIFI_SSID"
  #define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"
  #define MQTT_SERVER      "broker.hivemq.com"
  #define MQTT_PORT        1883
  #define MQTT_CLIENT_ID   "ESP32-Agri-Node01"
  #define TOPIC_TELEMETRY  "laksanasoft/agriculture/node_01/telemetry"
  #define TOPIC_COMMAND    "laksanasoft/agriculture/node_01/pump/set"
  #define DRY_VALUE        3200
  #define WET_VALUE        1500
  #define MOISTURE_THRESHOLD 35.0
#endif

WiFiClient espClient;
PubSubClient mqttClient(espClient);
DHT dht(PIN_DHT, DHTTYPE);

unsigned long lastTelemetryTime = 0;
const unsigned long TELEMETRY_INTERVAL = 10000; // 10 seconds

bool isPumpActive = false;
bool isManualMode = false;

void setupWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP address: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("MQTT received [%s]: %s\n", topic, message.c_str());

  if (String(topic) == TOPIC_COMMAND) {
    if (message == "ON") {
      isManualMode = true;
      setPumpState(true);
    } else if (message == "OFF") {
      isManualMode = true;
      setPumpState(false);
    } else if (message == "AUTO") {
      isManualMode = false;
      Serial.println("Switched back to AUTO mode");
    }
  }
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println("connected!");
      mqttClient.subscribe(TOPIC_COMMAND);
    } else {
      Serial.printf("failed, rc=%d. Retrying in 5 seconds...\n", mqttClient.state());
      delay(5000);
    }
  }
}

void setPumpState(bool state) {
  isPumpActive = state;
  // Active LOW Relay
  digitalWrite(PIN_RELAY_PUMP, state ? LOW : HIGH);
  Serial.printf("Pump state updated -> %s\n", state ? "ACTIVE (PUMPING)" : "STANDBY (OFF)");
}

float readSoilMoisturePercent() {
  int rawValue = analogRead(PIN_SOIL_ANALOG);
  float percent = map(rawValue, DRY_VALUE, WET_VALUE, 0, 100);
  return constrain(percent, 0.0, 100.0);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  digitalWrite(PIN_RELAY_PUMP, HIGH); // Turn off by default

  dht.begin();
  setupWiFi();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("ESP32 Smart Agriculture Node Ready.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL) {
    lastTelemetryTime = currentMillis;

    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();
    float soilMoisture = readSoilMoisturePercent();

    if (isnan(temp) || isnan(humidity)) {
      Serial.println("Warning: Failed to read from DHT sensor!");
      temp = 0.0;
      humidity = 0.0;
    }

    // Auto Irrigation Logic
    if (!isManualMode) {
      if (soilMoisture < MOISTURE_THRESHOLD && !isPumpActive) {
        setPumpState(true);
      } else if (soilMoisture >= (MOISTURE_THRESHOLD + 15.0) && isPumpActive) {
        setPumpState(false);
      }
    }

    // Prepare JSON payload
    StaticJsonDocument<256> doc;
    doc["device_id"] = MQTT_CLIENT_ID;
    doc["temperature"] = serialized(String(temp, 1));
    doc["humidity"] = serialized(String(humidity, 1));
    doc["soil_moisture_percent"] = serialized(String(soilMoisture, 1));
    doc["pump_status"] = isPumpActive ? "ON" : "OFF";
    doc["mode"] = isManualMode ? "MANUAL" : "AUTO";
    doc["uptime_seconds"] = millis() / 1000;

    char buffer[256];
    serializeJson(doc, buffer);
    mqttClient.publish(TOPIC_TELEMETRY, buffer);
    Serial.printf("Published Telemetry: %s\n", buffer);
  }
}
