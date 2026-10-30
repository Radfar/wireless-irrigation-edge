# Wireless Irrigation Edge

Embedded firmware and edge connectivity for the Industrial Water Management testbed.

This repository contains two distinct workstreams:

1. **Wireless irrigation control:** an Arduino UNO main controller and ESP8266 RF valve nodes for an offline-first control concept.
2. **Zone 04 IoT edge node:** an ESP8266 soil-moisture device with Wi-Fi provisioning, MQTT telemetry, broker discovery and bidirectional AUTO/MANUAL mode messaging.

These workstreams are separate firmware applications. The Zone 04 MQTT node does not currently actuate the physical latching valve.

## System overview

### Wireless RF irrigation control

```text
Arduino UNO main controller
  LCD / buttons / EEPROM / pump output / SIM800
             |
             | 433 MHz RF
             v
       ESP8266 valve node
             |
             v
      Valve driver circuit
             |
             v
      6 V bi-stable valve
```

The RF controller and valve-node firmware are maintained separately from the Zone 04 MQTT telemetry firmware. The RF protocol uses fixed command codes and does not provide delivery acknowledgement or sender authentication.

### Zone 04 MQTT edge path

```text
ESP8266 soil node
      |
      | Wi-Fi / MQTT
      v
Mosquitto broker
      |
      v
Node.js MQTT client in building-irrigation-scada
      |
      +----> Live Zone 04 HMI data
      |
      +----> PostgreSQL tag_values historian
```

The current SCADA integration receives MQTT directly in its Node.js backend. The repository also retains Node-RED flow assets from an earlier integration path; those assets are not required for the direct MQTT-to-Node.js path shown above.

## Repository layout

| Path | Purpose |
|---|---|
| `firmware/main-controller/` | Arduino UNO irrigation controller |
| `firmware/valve-node/` | ESP8266 RF receiver and valve-driver firmware |
| `firmware/soil-node/` | ESP8266 soil-node firmware assets |
| `firmware/esp-mqtt-test/` | Current Zone 04 continuous Wi-Fi/MQTT test firmware |
| `node-red/` | Retained Node-RED flow assets for the earlier MQTT-to-PostgreSQL route |
| `docs/` | Architecture, RF protocol, hardware and commissioning documentation |

## Zone 04 soil node

### Hardware and firmware

- ESP8266 / Wemos D1 board.
- Capacitive soil-moisture sensor input on A0.
- Sensor power control on D5.
- Configuration input on D3; hold D3 to GND during reset to open the setup portal.
- WiFiManager captive portal for Wi-Fi credentials and MQTT broker address.
- LittleFS stores the configured broker address in `/broker.txt`.
- MQTT client library: PubSubClient.
- PlatformIO project: `firmware/esp-mqtt-test/platformio.ini`.
- Main firmware: `firmware/esp-mqtt-test/src/d1-mqtt.cpp`.

### Telemetry

The node publishes every 15 seconds while connected:

- Topic: `zone4/soil01/data`
- Payload: `{"moisture": 42.0}`

The value is a percentage derived from the configured dry and wet ADC calibration points. The current source uses `ADC_DRY = 780` and `ADC_WET = 360`; calibrate these values against the actual sensor and installation before relying on the readings.

### Bidirectional mode messaging

The device subscribes to:

- `zone4/soil01/command`

The current firmware accepts the `SET_MODE` command with `AUTO` or `MANUAL` mode and publishes:

- Retained status: `zone4/soil01/status`
- Command acknowledgement: `zone4/soil01/command_result`

The status payload reports the device online state and selected mode. The command result includes the command ID, acceptance state, mode and message.

The onboard LED is steady OFF in AUTO and flashes in MANUAL on common active-low ESP8266 D1/D1 mini boards. This is a firmware mode indication only. It does not confirm that a valve, pump or other field actuator has operated.

### Automatic MQTT broker discovery

The device first attempts to connect to its stored broker address. After five consecutive MQTT connection failures, it broadcasts a discovery request over UDP:

- ESP8266 discovery listener: UDP 1885
- SCADA discovery responder: UDP 1884
- Request: `IRRIGATION_MQTT_DISCOVER_V1`
- Response format: `IRRIGATION_MQTT_HERE_V1|<broker-ip>|1883`

The firmware validates that the advertised IPv4 address matches the response source and that the advertised port matches its MQTT port. A newly discovered broker address is saved to LittleFS.

Discovery relies on IPv4 broadcast and therefore requires the ESP8266 and broker host to be on the same broadcast-enabled subnet. It does not cross routers or VLAN boundaries. The SCADA host must run the discovery responder and permit the required inbound UDP and MQTT traffic through its firewall.

## Build and upload the Zone 04 firmware

1. Install Visual Studio Code and the PlatformIO IDE extension.
2. Open the `firmware/esp-mqtt-test/` folder as the PlatformIO project.
3. Connect the Wemos D1 / ESP8266 board.
4. Build and upload the firmware using PlatformIO.
5. Open the PlatformIO serial monitor at 115200 baud.
6. On first setup, connect to the device's `SoilNode-xxxxxx` access point and configure Wi-Fi and the MQTT broker address. Hold D3 to GND during reset to reopen the setup portal.

The PlatformIO environment is named `d1` and targets the ESP8266 Arduino framework.

## RF controller details

The main controller source uses an Arduino UNO with:

| Function | Interface |
|---|---|
| RF transmitter | Digital pin 12 |
| Pump / main-valve relay output | Digital pin 13 |
| SIM800 | SoftwareSerial RX 10, TX 11 |
| Button ladder | Analog input A0 |
| Display | 16 × 2 LiquidCrystal |
| Settings | EEPROM |
| RF library | RCSwitch |

The current source defines valve IDs 0–3 and a 24-bit RF command repeated twice.

### RF command mapping

| Action | Code for valve ID `n` |
|---|---:|
| Open / ON | `2560 + n` |
| Close / OFF | `1280 + n` |

This fixed-code RF protocol has no acknowledgement, encryption, sender authentication or proof of physical valve movement. Review [docs/rf-protocol.md](docs/rf-protocol.md) before using it.

**Commissioning note:** the main-controller source sets `WATERING_DURATION = 2000` ms, although an adjacent source comment describes one minute. Confirm the intended duration before live irrigation.

## Current status and verification

| Item | Status |
|---|---|
| Zone 04 Wi-Fi provisioning and stored broker configuration | Bench-tested on Wemos D1 R1 |
| MQTT telemetry publication | Bench-tested; 15-second interval |
| UDP broker discovery and stored broker update | Implemented in current firmware and SCADA responder |
| AUTO/MANUAL MQTT command and acknowledgement | Implemented and user-tested in the target setup |
| Retained device status and LED mode indication | Implemented |
| SCADA direct MQTT ingestion and PostgreSQL historian | Integrated in the related SCADA repository |
| Soil sensor wiring and calibration | Pending |
| Physical valve actuation from Zone 04 MQTT firmware | Not implemented; disabled in source |
| Optional flow input | Disabled in source |
| Deep sleep and battery runtime | Pending |
| RF range and reliability measurements | Pending |
| Full RF controller and valve commissioning checklist | Refer to [docs/testing.md](docs/testing.md) |

## Safety and commissioning

- Do not connect a valve coil directly to an ESP8266 GPIO. Use a driver rated for the valve's pulse current and inductive load.
- Verify valve voltage, polarity and maximum pulse duration against the manufacturer's datasheet.
- Confirm pump relay ratings, electrical isolation and protection.
- Test RF reception separately from physical valve movement.
- Do not treat an RF transmission or an LED indication as proof of actuator position.
- Do not rely on commented-out or disabled protection code as an active safety function.
- The current MQTT broker configuration is anonymous and unencrypted. Use only on a trusted private test network until authentication, TLS and appropriate network controls are implemented.
- Complete sensor calibration, electrical checks and water commissioning before field operation.

## Related repositories

- [Building Irrigation SCADA](https://github.com/Radfar/building-irrigation-scada) — custom web HMI, Node.js backend, multi-zone integrations and Zone 04 MQTT ingestion.
- [Water Process Integration Testbed](https://github.com/Radfar/water-process-integration-testbed) — broader testbed architecture.
- [OPC UA BACnet Gateway](https://github.com/Radfar/opcua-bacnet-gateway) — separate protocol gateway project.

## License

MIT. See [LICENSE](LICENSE).
