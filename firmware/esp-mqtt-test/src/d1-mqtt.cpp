/*
  ESP8266 capacitive soil moisture node — Zone 4 office-test firmware
  Water Process project

  Stage 1: continuous Wi-Fi and MQTT operation.
  Stage 2: receive AUTO/MANUAL commands and indicate MANUAL mode on onboard LED.

  Preserved:
  - Soil calibration and sensor power control.
  - WiFiManager portal and LittleFS broker configuration.
  - Existing telemetry topic and payload: zone4/soil01/data,
    {"moisture": <number>}.
  - Optional flow input and latching valve remain disabled.

  Important:
  - LED flashes only in MANUAL mode; it stays OFF in AUTO mode.
  - The LED indicates firmware mode only, not physical valve/pump operation.
  - This office-test version stays powered and connected; no sleep.
  - Plain MQTT is intended only for a trusted private LAN.
  - LED_BUILTIN on common ESP8266 D1/D1 mini boards is active-low.
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <LittleFS.h>

// ---- MQTT settings ----
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "zone4/soil01/data";
const char* MQTT_COMMAND_TOPIC = "zone4/soil01/command";
const char* MQTT_STATUS_TOPIC = "zone4/soil01/status";
const char* MQTT_RESULT_TOPIC = "zone4/soil01/command_result";
const char* NODE_ID = "soil01";

// ---- Continuous office-test timing ----
const unsigned long TELEMETRY_INTERVAL_MS = 15000UL;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000UL;

// ---- LED mode indication ----
const uint8_t MODE_AUTO = 0;
const uint8_t MODE_MANUAL = 1;
const unsigned long MANUAL_LED_INTERVAL_MS = 500UL;
const uint8_t LED_ON = LOW;   // Active-low onboard LED on common D1/D1 mini
const uint8_t LED_OFF = HIGH;

const uint8_t MAX_FAILS_BEFORE_PORTAL = 5;

// ---- Existing pins: unchanged ----
const uint8_t SENSOR_POWER_PIN = D5;
const uint8_t SENSOR_ADC_PIN = A0;
const uint8_t CONFIG_PIN = D3;  // Hold to GND during reset for portal

#define ENABLE_FLOW_INPUT 0
#define ENABLE_LATCHING_VALVE 0

#if ENABLE_FLOW_INPUT
  #ifndef FLOW_PULSE_PIN
    #error "Define FLOW_PULSE_PIN using the verified flow-meter GPIO."
  #endif
  const uint32_t FLOW_SAMPLE_MS = 1000;
  volatile uint32_t flowPulseCount = 0;
  uint32_t lastFlowSampleMs = 0;
  float flowLpm = 0.0f;

  void ICACHE_RAM_ATTR onFlowPulse() {
    flowPulseCount++;
  }
#endif

#if ENABLE_LATCHING_VALVE
  #error "Valve control requires verified driver GPIOs, polarity and pulse timing."
#endif

const int ADC_DRY = 780;
const int ADC_WET = 360;

WiFiClient espClient;
PubSubClient mqtt(espClient);

String brokerHost;
unsigned long lastTelemetryMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastLedToggleMs = 0;
uint8_t operatingMode = MODE_AUTO;
bool ledState = false;

String loadBrokerHost() {
  if (!LittleFS.exists("/broker.txt")) return "";

  File f = LittleFS.open("/broker.txt", "r");
  if (!f) return "";

  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  return s;
}

void saveBrokerHost(const String& h) {
  File f = LittleFS.open("/broker.txt", "w");
  if (!f) {
    Serial.println("ERROR: could not open /broker.txt for writing");
    return;
  }
  f.print(h);
  f.close();
}

float readMoisturePercent() {
  digitalWrite(SENSOR_POWER_PIN, HIGH);
  delay(50);

  long total = 0;
  for (int i = 0; i < 8; i++) {
    total += analogRead(SENSOR_ADC_PIN);
    delay(10);
  }

  digitalWrite(SENSOR_POWER_PIN, LOW);

  const int raw = total / 8;
  float pct = (float)(ADC_DRY - raw) /
              (float)(ADC_DRY - ADC_WET) * 100.0f;

  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

#if ENABLE_FLOW_INPUT
void setupFlowInput() {
  pinMode(FLOW_PULSE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PULSE_PIN),
                  onFlowPulse, FALLING);
  lastFlowSampleMs = millis();
}

void updateFlowMeasurement() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - lastFlowSampleMs;
  if (elapsed < FLOW_SAMPLE_MS) return;

  noInterrupts();
  const uint32_t pulses = flowPulseCount;
  flowPulseCount = 0;
  interrupts();

  const float elapsedSeconds = elapsed / 1000.0f;
  const float frequencyHz = elapsedSeconds > 0.0f
      ? pulses / elapsedSeconds : 0.0f;

  flowLpm = frequencyHz / 7.5f;
  lastFlowSampleMs = now;
  Serial.printf("Flow: %.2f L/min (%.2f Hz)\n", flowLpm, frequencyHz);
}
#endif

bool runSetupPortal() {
  char hostBuf[40];
  strncpy(hostBuf, brokerHost.c_str(), sizeof(hostBuf) - 1);
  hostBuf[sizeof(hostBuf) - 1] = 0;

  WiFiManagerParameter pHost(
      "broker", "MQTT broker (IP or hostname)", hostBuf, 40);

  WiFiManager wm;
  wm.addParameter(&pHost);
  wm.setConfigPortalTimeout(180);

  const String apName = "SoilNode-" + String(ESP.getChipId(), HEX);
  Serial.println("Opening setup portal: " + apName);
  const bool ok = wm.startConfigPortal(apName.c_str());

  String h = pHost.getValue();
  h.trim();
  if (h.length() > 0 && h != brokerHost) {
    brokerHost = h;
    saveBrokerHost(h);
  }
  return ok;
}

bool connectStored() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000UL) {
    delay(100);
    yield();
  }
  return WiFi.status() == WL_CONNECTED;
}

void updateModeLed() {
  // AUTO is indicated by a steady OFF LED.
  if (operatingMode == MODE_AUTO) {
    ledState = false;
    digitalWrite(LED_BUILTIN, LED_OFF);
    return;
  }

  // MANUAL is indicated by a 500 ms ON/OFF interval.
  const unsigned long now = millis();
  if (now - lastLedToggleMs >= MANUAL_LED_INTERVAL_MS) {
    lastLedToggleMs = now;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? LED_ON : LED_OFF);
  }
}

const char* modeName() {
  return operatingMode == MODE_AUTO ? "AUTO" : "MANUAL";
}

// Extract a simple quoted JSON string field for the fixed command schema.
String jsonStringField(const String& json, const char* key) {
  const String token = String("\"") + key + "\"";
  int pos = json.indexOf(token);
  if (pos < 0) return "";

  pos = json.indexOf(':', pos + token.length());
  if (pos < 0) return "";

  int firstQuote = json.indexOf('\"', pos + 1);
  if (firstQuote < 0) return "";

  int secondQuote = json.indexOf('\"', firstQuote + 1);
  if (secondQuote < 0) return "";

  return json.substring(firstQuote + 1, secondQuote);
}

String safeCommandId(String id) {
  String safe;
  for (unsigned int i = 0; i < id.length() && safe.length() < 48; i++) {
    const char c = id.charAt(i);
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      safe += c;
    }
  }
  return safe;
}

void publishDeviceStatus() {
  if (!mqtt.connected()) return;

  char payload[96];
  snprintf(payload, sizeof(payload),
           "{\"online\":true,\"mode\":\"%s\"}", modeName());
  mqtt.publish(MQTT_STATUS_TOPIC, payload, true);
  Serial.print("Published device status: ");
  Serial.println(payload);
}

void publishCommandResult(const String& commandId, bool accepted,
                          const char* message) {
  if (!mqtt.connected()) return;

  char payload[180];
  snprintf(payload, sizeof(payload),
           "{\"command_id\":\"%s\",\"accepted\":%s,"
           "\"mode\":\"%s\",\"message\":\"%s\"}",
           commandId.c_str(), accepted ? "true" : "false",
           modeName(), message);

  mqtt.publish(MQTT_RESULT_TOPIC, payload);
  Serial.print("Published command result: ");
  Serial.println(payload);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Log every MQTT packet delivered to this client before topic filtering.
  String topicName(topic);
  String message;
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }

  Serial.println();
  Serial.println("========== MQTT MESSAGE RECEIVED ==========");
  Serial.print("Topic: ");
  Serial.println(topicName);
  Serial.print("Payload length: ");
  Serial.println(length);
  Serial.print("Payload: ");
  Serial.println(message);
  Serial.println("============================================");

  if (topicName != MQTT_COMMAND_TOPIC) {
    Serial.println("Message ignored: topic is not the command topic");
    return;
  }

  Serial.println("Processing Zone 04 command...");

  const String commandId =
      safeCommandId(jsonStringField(message, "command_id"));
  const String command = jsonStringField(message, "command");
  const String requestedMode = jsonStringField(message, "mode");

  if (command != "SET_MODE") {
    publishCommandResult(commandId, false, "Unsupported command");
    return;
  }

  if (requestedMode == "AUTO") {
    operatingMode = MODE_AUTO;
  } else if (requestedMode == "MANUAL") {
    operatingMode = MODE_MANUAL;
  } else {
    publishCommandResult(commandId, false, "Invalid mode");
    return;
  }

  // Restart the LED pattern from OFF after every accepted mode change.
  lastLedToggleMs = millis();
  ledState = false;
  digitalWrite(LED_BUILTIN, LED_OFF);

  Serial.print("Operating mode changed to ");
  Serial.println(modeName());

  publishDeviceStatus();
  publishCommandResult(commandId, true, "Mode updated");
}

void publishMoisture() {
  if (!mqtt.connected()) return;

  const float moisture = readMoisturePercent();
  char payload[64];
  snprintf(payload, sizeof(payload),
           "{\"moisture\":%.1f}", moisture);

  if (mqtt.publish(MQTT_TOPIC, payload)) {
    Serial.print("Published moisture: ");
    Serial.println(payload);
  } else {
    Serial.println("Moisture publish FAILED");
  }
}

void maintainWifiConnection() {
  if (WiFi.status() == WL_CONNECTED) return;

  const unsigned long now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_INTERVAL_MS) return;

  lastWifiAttemptMs = now;
  Serial.println("Wi-Fi disconnected; attempting reconnection");
  WiFi.disconnect();
  WiFi.begin();
}

void maintainMqttConnection() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;

  const unsigned long now = millis();
  if (now - lastMqttAttemptMs < MQTT_RETRY_INTERVAL_MS) return;

  lastMqttAttemptMs = now;
  if (brokerHost.length() == 0) {
    Serial.println("MQTT broker address is empty; configure the device");
    return;
  }

  Serial.print("Connecting to MQTT broker ");
  Serial.print(brokerHost);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  if (mqtt.connect(NODE_ID)) {
    Serial.println("MQTT connected");

    if (mqtt.subscribe(MQTT_COMMAND_TOPIC)) {
      Serial.print("Subscribed to ");
      Serial.println(MQTT_COMMAND_TOPIC);
    } else {
      Serial.println("WARNING: MQTT command subscription failed");
    }

    publishDeviceStatus();
    lastTelemetryMs = millis() - TELEMETRY_INTERVAL_MS;
  } else {
    Serial.print("MQTT connection failed, state = ");
    Serial.println(mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);
  pinMode(CONFIG_PIN, INPUT_PULLUP);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_OFF);

  if (!LittleFS.begin()) {
    Serial.println(
        "WARNING: LittleFS mount failed; broker config may be unavailable");
  }

  brokerHost = loadBrokerHost();

#if ENABLE_FLOW_INPUT
  setupFlowInput();
#endif

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  const bool forceConfig = (digitalRead(CONFIG_PIN) == LOW);
  const bool noConfig =
      (WiFi.SSID().length() == 0) || (brokerHost.length() == 0);

  bool connected = false;
  if (forceConfig || noConfig) {
    connected = runSetupPortal();
  } else {
    connected = connectStored();
  }

  if (connected || WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi not connected; automatic retries will continue");
  }

  Serial.println("Broker: " + brokerHost);

  mqtt.setServer(brokerHost.c_str(), MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(3);
  mqtt.setBufferSize(256);

  lastWifiAttemptMs = millis();
  lastMqttAttemptMs = millis() - MQTT_RETRY_INTERVAL_MS;
  lastTelemetryMs = millis() - TELEMETRY_INTERVAL_MS;
  lastLedToggleMs = millis();
}

void loop() {
  maintainWifiConnection();
  maintainMqttConnection();

  if (mqtt.connected()) {
    mqtt.loop();

    const unsigned long now = millis();
    if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
      lastTelemetryMs = now;
      publishMoisture();
    }
  }

  updateModeLed();

#if ENABLE_FLOW_INPUT
  updateFlowMeasurement();
#endif

  yield();
}
