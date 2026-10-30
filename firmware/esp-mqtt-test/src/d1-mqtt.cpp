/*
  ESP8266 capacitive soil moisture node — Zone 4 office-test firmware
  Water Process project

  Stage 1:
  - Continuous Wi-Fi and MQTT operation for bidirectional development.
  - Preserves soil calibration, WiFiManager portal, LittleFS broker
    configuration, MQTT node ID/topic/payload and sensor power control.
  - Existing telemetry remains {"moisture": <number>}.
  - MQTT command subscription and LED indication will be added in Stage 2.
  - Latching/bistable valve control remains disabled until the physical
    driver, GPIOs, polarity and pulse timing are verified.

  Power notes:
  - This office-test version stays powered and connected; it does not sleep.
  - Battery wake-up operation will be designed separately.
  - Plain MQTT is intended only for a trusted private LAN.
  - Battery voltage monitoring is not included; A0 is used by the sensor.
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <LittleFS.h>

// ---- MQTT settings: preserve existing integration ----
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "zone4/soil01/data";
const char* NODE_ID = "soil01";

// ---- Continuous office-test timing ----
const unsigned long TELEMETRY_INTERVAL_MS = 15000UL;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000UL;

const uint8_t MAX_FAILS_BEFORE_PORTAL = 5;

// ---- Existing pins: unchanged ----
const uint8_t SENSOR_POWER_PIN = D5;
const uint8_t SENSOR_ADC_PIN = A0;
const uint8_t CONFIG_PIN = D3;  // Hold to GND during reset for portal

// Optional hardware remains disabled until wiring is verified.
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

// ---- Soil calibration: unchanged ----
const int ADC_DRY = 780;
const int ADC_WET = 360;

WiFiClient espClient;
PubSubClient mqtt(espClient);

String brokerHost;
unsigned long lastTelemetryMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
uint8_t wifiConnectFailures = 0;

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

// Frequency in Hz = pulses / elapsed seconds.
// Supplied meter formula: flow L/min = frequency Hz / 7.5.
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

  Serial.printf("Flow: %.2f L/min (%.2f Hz)\n",
                flowLpm, frequencyHz);
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

  const String apName =
      "SoilNode-" + String(ESP.getChipId(), HEX);

  Serial.println("Opening setup portal: " + apName);
  const bool ok = wm.startConfigPortal(apName.c_str());

  // Preserve the original behavior: save a submitted broker address
  // even if Wi-Fi connection fails or the portal times out.
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

void publishMoisture() {
  if (!mqtt.connected()) return;

  const float moisture = readMoisturePercent();

  // Keep the legacy topic and JSON payload unchanged for Node.js.
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
    wifiConnectFailures = MAX_FAILS_BEFORE_PORTAL;
  }

  Serial.println("Broker: " + brokerHost);

  mqtt.setServer(brokerHost.c_str(), MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(3);

  lastWifiAttemptMs = millis();
  lastMqttAttemptMs = millis() - MQTT_RETRY_INTERVAL_MS;
  lastTelemetryMs = millis() - TELEMETRY_INTERVAL_MS;
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

#if ENABLE_FLOW_INPUT
  updateFlowMeasurement();
#endif

  yield();
}
