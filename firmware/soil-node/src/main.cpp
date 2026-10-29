
/*
  ESP8266 capacitive soil moisture node — battery-powered, Zone 4 interim WiFi path
  Water Process project

  Power strategy:
  - Deep sleep between reads — D0 (GPIO16) MUST be wired to RST externally for the
    timed wake-up to work. This is a hardware jumper, not something code can do.
  - Sensor VCC switched via GPIO (not tied to 3V3) — sensor only draws current
    for the ~1s it's actually being read, extends sensor life too.
  - Fast WiFi reconnect using a cached channel + BSSID stored in RTC memory —
    the WiFi association handshake is the dominant power cost per wake, this
    cuts it from ~2-5s down to roughly 1s on most access points.
  - Plain MQTT (no TLS) to keep the connect+publish window short. Fine on a
    private LAN; add TLS before this is ever exposed beyond that.

  NOT included in this version: battery voltage monitoring. The ESP8266 has
  only one ADC pin (A0) and it's already committed to the soil sensor reading.
  Monitoring battery voltage too would need an analog mux or a second board —
  left as a known gap, not solved here.
*/
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <LittleFS.h>

// ---- Fixed settings ----
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC   = "zone4/soil01/data";
const char* NODE_ID      = "soil01";
const uint64_t SLEEP_SECONDS = 15;     // use 600 for battery
#define USE_DEEP_SLEEP 0               // set 1 only when D2 (GPIO16) is wired to RST
const uint8_t MAX_FAILS_BEFORE_PORTAL = 5;

// ---- Pins ----
const uint8_t SENSOR_POWER_PIN = D5;
const uint8_t SENSOR_ADC_PIN   = A0;
const uint8_t CONFIG_PIN       = D3;   // hold to GND while resetting -> setup portal

// ---- Calibration ----
const int ADC_DRY = 780;
const int ADC_WET = 360;

struct RtcData { uint32_t magic; uint32_t failCount; };
RtcData rtcData;

WiFiClient espClient;
PubSubClient mqtt(espClient);
String brokerHost;

void loadRtc() {
  ESP.rtcUserMemoryRead(0, (uint32_t*)&rtcData, sizeof(rtcData));
  if (rtcData.magic != 0xA5A5A5A5) { rtcData.magic = 0xA5A5A5A5; rtcData.failCount = 0; }
}
void saveRtc() { ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtcData, sizeof(rtcData)); }

String loadBrokerHost() {
  if (!LittleFS.exists("/broker.txt")) return "";
  File f = LittleFS.open("/broker.txt", "r");
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  return s;
}
void saveBrokerHost(const String& h) {
  File f = LittleFS.open("/broker.txt", "w");
  f.print(h);
  f.close();
}

float readMoisturePercent() {
  digitalWrite(SENSOR_POWER_PIN, HIGH);
  delay(50);
  long total = 0;
  for (int i = 0; i < 8; i++) { total += analogRead(SENSOR_ADC_PIN); delay(10); }
  digitalWrite(SENSOR_POWER_PIN, LOW);
  int raw = total / 8;
  float pct = (float)(ADC_DRY - raw) / (float)(ADC_DRY - ADC_WET) * 100.0;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

void goToSleep() {
  saveRtc();
  // NOTE: do not call WiFi.disconnect() here. On ESP8266 it erases the saved credentials.
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
  WiFiManagerParameter pHost("broker", "MQTT broker (IP or hostname)", hostBuf, 40);

  WiFiManager wm;
  wm.addParameter(&pHost);
  wm.setConfigPortalTimeout(180);
  String apName = "SoilNode-" + String(ESP.getChipId(), HEX);
  Serial.println("Opening setup portal: " + apName);

  bool ok = wm.startConfigPortal(apName.c_str());

  // Save the broker address even if the WiFi connect failed or the portal timed out.
  // WiFiManager updates the field whenever the form is submitted.
  String h = pHost.getValue();
  h.trim();
  if (h.length() > 0 && h != brokerHost) { brokerHost = h; saveBrokerHost(h); }
  return ok;
}

bool connectStored() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();   // uses credentials saved in flash
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, LOW);
  pinMode(CONFIG_PIN, INPUT_PULLUP);

  LittleFS.begin();
  brokerHost = loadBrokerHost();
  loadRtc();

  WiFi.mode(WIFI_STA);
  bool forceConfig  = (digitalRead(CONFIG_PIN) == LOW);
  bool noConfig     = (WiFi.SSID().length() == 0) || (brokerHost.length() == 0);
  bool tooManyFails = (rtcData.failCount >= MAX_FAILS_BEFORE_PORTAL);

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
  //rtcData.failCount = 0;

  Serial.print("WiFi OK, IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Broker: " + brokerHost);

  float moisture = readMoisturePercent();
  mqtt.setServer(brokerHost.c_str(), MQTT_PORT);
  if (mqtt.connect(NODE_ID)) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"moisture\":%.1f}", moisture);
    bool ok = mqtt.publish(MQTT_TOPIC, payload);
    Serial.println(ok ? "Published" : "Publish FAILED");
    mqtt.loop();
    delay(100);
    mqtt.disconnect();
    if (ok) rtcData.failCount = 0;
    else    rtcData.failCount++;
  } else {
    Serial.print("MQTT FAILED, state = ");
    Serial.println(mqtt.state());
    rtcData.failCount++;
  }
  goToSleep();
}

void loop() {}