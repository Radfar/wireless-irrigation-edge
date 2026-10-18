# Wireless Irrigation Edge

Wireless irrigation control and an IIoT edge path in one repository:

1. **RF valve control (offline-first):** an Arduino UNO controller sends 433 MHz RF commands to
   ESP8266 valve nodes that drive 6 V DC bi-stable (latching) valves.
2. **Zone 4 edge path:** an ESP8266 soil-moisture node publishes over MQTT. Node-RED maps the
   data to the testbed's tag convention and logs it to PostgreSQL, and the web SCADA shows it.

The local watering sequence does not depend on the Internet or MQTT. The two paths are separate programs.

## Repository layout

| Folder | Contents |
|---|---|
| `firmware/main-controller/` | Arduino UNO sketch: LCD, buttons, EEPROM settings, RF transmitter, pump output, SIM800 |
| `firmware/valve-node/` | ESP8266 (PlatformIO): RCSwitch receiver that pulses the valve driver |
| `firmware/soil-node/` | ESP8266 (PlatformIO): soil-moisture node with WiFi setup portal and MQTT publish |
| `node-red/` | Exported Node-RED flow (MQTT to `Z04_MOISTURE` to PostgreSQL) |
| `docs/` | Architecture, hardware, RF protocol, test and commissioning checklist |

## Architecture

RF valve control:

```text
Arduino UNO controller (LCD, EEPROM, pump output, SIM800)
        |  433 MHz RF
ESP8266 valve node --> valve driver --> 6 V DC bi-stable valve
```

Zone 4 edge path:

```text
ESP8266 soil node --MQTT--> Mosquitto --> Node-RED --> PostgreSQL --> Node.js web SCADA
```

## Status

| Item | Status |
|---|---|
| Soil node: WiFi setup portal, MQTT publish | Bench-tested (Wemos D1 R1) |
| Mosquitto to Node-RED to PostgreSQL (`Z04_MOISTURE`) | Bench-tested, one row per reading |
| Web SCADA Zone 04 card | Bench-tested |
| Soil sensor wiring and calibration | Pending |
| Soil node deep sleep and battery runtime | Pending |
| Main controller sketch | Existing source; bench checklist (docs/testing.md, section A) not yet recorded |
| Valve node | Builds; hardware checklist (section B) not yet recorded |
| RF range and reliability | Not yet measured |

## Controller details (from the current source)

| Item | Current value |
|---|---|
| Main controller | Arduino UNO |
| RF library | RCSwitch |
| RF transmitter signal | Digital pin 12 |
| Pump/main-valve relay output | Digital pin 13 |
| SIM800 interface | SoftwareSerial RX 10, TX 11 |
| LCD | 16 x 2 LiquidCrystal |
| Button ladder | Analog input A0 |
| Settings | EEPROM |
| Valve IDs | 0-3 (`VALVE = 4`) |
| RF command | 24 bits, each command sent twice |
| Watering delay constant | `WATERING_DURATION = 2000` ms |

**Commissioning note:** the source comment says the watering delay is one minute, but the configured
value is 2,000 ms. Confirm the intended duration before live irrigation.

## RF command mapping

| Action | Code for valve ID `n` |
|---|---:|
| Open / ON | `2560 + n` |
| Close / OFF | `1280 + n` |

This fixed-code protocol has no acknowledgement, encryption, sender authentication, or proof that a
valve moved. See `docs/rf-protocol.md`.

## Zone 4 soil node

- Publishes `{"moisture": 42.0}` to `zone4/soil01/data`
- On first boot, or when D3 is held to GND during reset, the node opens a setup portal
  (`SoilNode-xxxxxx`) for WiFi and the MQTT broker address; both are saved in flash
- After 5 failed wakes in a row it reopens the portal, so it can be moved to another network
- Sensor power is switched by a GPIO and read as an 8-sample average

To run the edge path: start Mosquitto, import `node-red/flows.json` into Node-RED, install the
`node-red-contrib-postgresql` palette node, and configure your PostgreSQL connection.
The tag `Z04_MOISTURE` belongs to equipment `Z-04` in the testbed's `building_irrigation` database.

## Known limitations

- Soil sensor not yet wired or calibrated; readings are not meaningful until it is
- No battery voltage monitoring (the ESP8266 has one ADC pin, used by the sensor)
- MQTT broker is anonymous and unencrypted: private LAN only
- Broker address is entered per network; a fixed-hostname broker is planned
- RF link has no acknowledgement or authentication
- RF repeaters are not confirmed as implemented in the current controller sketch

## Safety and commissioning

- Do not connect a valve coil directly to an ESP8266 GPIO; use a driver rated for the valve's pulse current and inductive load.
- Verify valve voltage, polarity and maximum pulse duration against the datasheet.
- Confirm pump relay ratings and electrical isolation/protection.
- Do not rely on commented-out protection code as an active safety function.
- Test RF reception and physical valve movement separately.

## Related repositories

- [building-irrigation-scada](https://github.com/Radfar/building-irrigation-scada): Node.js web SCADA
- [opcua-bacnet-gateway](https://github.com/Radfar/opcua-bacnet-gateway): OPC UA to BACnet gateway
- [water-process-integration-testbed](https://github.com/Radfar/water-process-integration-testbed): zone overview and architecture

## License

MIT, see [LICENSE](LICENSE).
