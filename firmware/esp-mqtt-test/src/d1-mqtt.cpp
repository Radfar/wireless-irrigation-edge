
/*
  ESP8266 capacitive soil moisture node — Zone 4 interim WiFi path
  Water Process project

  Incremental compatibility update:
  - Preserves existing soil calibration, WiFiManager portal, LittleFS broker
    configuration, MQTT node ID/topic/payload, and reconnect/sleep behavior.
  - Optional flow input is scaffolded but DISABLED until the physical pin is
    confirmed. Flow formula: L/min = frequency_Hz / 7.5.
  - Latching/bistable valve control is deliberately NOT enabled because the
    GPIOs, driver circuit, pulse duration, and set/reset polarity are unknown.
  - Existing MQTT payload remains {"moisture": <number>}.

  Power notes:
  - Deep sleep requires GPIO16 (D0) wired to RST; disabled by default.
  - Plain MQTT is intended only for a trusted private LAN.
  - Battery voltage monitoring is not included; A0 is used by the sensor.
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <LittleFS.h>

// ---- Fixed settings: preserve existing integration ----
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "zone4/soil01/data";
const char* NODE_ID = "soil01";
const uint64_t SLEEP_SECONDS = 15;
#define USE_DEEP_SLEEP 0
const uint8_t MAX_FAILS_BEFORE_PORTAL = 5;

// ---- Existing pins: unchanged ----
const uint8_t SENSOR_POWER_PIN = D5;
const uint8_t SENSOR_ADC_PIN = A0;
const uint8_t CONFIG_PIN = D3;  // Hold to GND while resetting for portal

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

struct RtcData {
  uint32_t magic;
  uint32_t failCount;
};
RtcData rtcData;

WiFiClient espClient;
PubSubClient mqtt(espClient);
String brokerHost;

void loadRtc() {
  ESP.rtcUserMemoryRead(0, (uint32_t*)&rtcData, sizeof(rtcData));
  if (rtcData.magic != 0xA5A5A5A5) {
    rtcData.magic = 0xA5A5A5A5;
    rtcData.failCount = 0;
  }
}

void saveRtc() {
  ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtcData, sizeof(rtcData));
}

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

void goToSleep() {
  saveRtc();

#if USE_DEEP_SLEEP
  ESP.deepSleep(SLEEP_SECONDS * 1000000ULL);
#else
  delay(SLEEP_SECONDS * 1000UL);
  ESP.restart();
#endif
}

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
  WiFi.begin();

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000UL) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
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
  loadRtc();

#if ENABLE_FLOW_INPUT
  setupFlowInput();
#endif

  WiFi.mode(WIFI_STA);

  const bool forceConfig = (digitalRead(CONFIG_PIN) == LOW);
  const bool noConfig =
      (WiFi.SSID().length() == 0) || (brokerHost.length() == 0);
  const bool tooManyFails =
      (rtcData.failCount >= MAX_FAILS_BEFORE_PORTAL);

  bool connected;
  if (forceConfig || noConfig || tooManyFails) {
    rtcData.failCount = 0;
    connected = runSetupPortal();
  } else {
    connected = connectStored();
  }

  if (!connected) {
    rtcData.failCount++;
    Serial.println("No WiFi, sleeping");
    goToSleep();
    return;
  }

  Serial.print("WiFi OK, IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Broker: " + brokerHost);

  const float moisture = readMoisturePercent();
  mqtt.setServer(brokerHost.c_str(), MQTT_PORT);

  if (mqtt.connect(NODE_ID)) {
    // Keep the legacy topic and payload unchanged for Node.js compatibility.
    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"moisture\":%.1f}", moisture);

    const bool ok = mqtt.publish(MQTT_TOPIC, payload);
    Serial.println(ok ? "Published moisture telemetry"
                      : "Publish FAILED");

    mqtt.loop();
    delay(100);
    mqtt.disconnect();

    if (ok) rtcData.failCount = 0;
    else rtcData.failCount++;
  } else {
    Serial.print("MQTT FAILED, state = ");
    Serial.println(mqtt.state());
    rtcData.failCount++;
  }

  goToSleep();
}

void loop() {
  // Intentionally remains a wake-read-publish-sleep node.
  // Do not enable continuous valve control until hardware and local
  // control requirements have been verified.
}
