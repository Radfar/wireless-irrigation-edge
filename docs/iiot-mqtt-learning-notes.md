# IIoT and MQTT Learning Journal — Zone 4

**Project:** Wireless Irrigation Edge  
**Repository:** https://github.com/Radfar/wireless-irrigation-edge  
**Application:** Industrial water management and irrigation telemetry  
**Purpose:** Build practical knowledge of IoT, IIoT, MQTT, edge computing, messaging, data integration, and industrial telemetry.

## 1. Learning objectives

By completing this learning path, I want to be able to:

- Explain the differences between IoT, IIoT, SCADA, edge computing, and cloud computing.
- Design and troubleshoot an MQTT-based telemetry system.
- Understand MQTT brokers, publishers, subscribers, topics, payloads, QoS, retained messages, and Last Will and Testament.
- Integrate ESP8266 sensor data with Mosquitto, Node-RED, PostgreSQL, and a Node.js Web SCADA.
- Handle unreliable networks, stale data, reconnection, duplicate messages, and invalid sensor readings.
- Apply appropriate cybersecurity practices to connected industrial devices.
- Understand where MQTT fits alongside OPC UA, Modbus TCP, BACnet/IP, and other industrial communication technologies.

## 2. IoT versus IIoT

### Internet of Things (IoT)

IoT describes connected physical devices that measure conditions, exchange data, or perform actions over a network.

Examples include smart-home sensors, connected lighting, and consumer environmental monitors.

### Industrial Internet of Things (IIoT)

IIoT applies connected-device and data-processing concepts to industrial environments, including manufacturing, utilities, building automation, water treatment, and process monitoring.

Industrial requirements typically include:

- Reliability and fault handling
- Data quality and traceability
- Operational cybersecurity
- Integration with existing automation systems
- Clear separation between monitoring and control
- Maintainability and commissioning procedures

**Zone 4 example:** An ESP8266 reads soil moisture and publishes measurements using MQTT. The data is processed by Node-RED, stored in PostgreSQL, and displayed by the Web SCADA.

This is an IIoT-style telemetry pipeline. The complete application is a learning testbed, not yet a validated production-grade industrial system.

## 3. What is MQTT?

MQTT stands for Message Queuing Telemetry Transport. It is a lightweight messaging protocol commonly used to exchange telemetry between connected devices and software applications.

MQTT uses a publish/subscribe model.

### Main components

**Publisher:** An application or device that sends a message. In Zone 4, the ESP8266 soil node is the publisher.

**Broker:** The messaging server that receives published messages and distributes them to matching subscribers. Mosquitto is the broker in this project.

**Subscriber:** An application that subscribes to topics and receives matching messages. Node-RED is a subscriber in Zone 4.

**Topic:** A hierarchical string used to organize messages. The current topic is `zone4/soil01/data`.

**Payload:** The message content. The current example is:

```json
{
  "moisture": 42.0
}
```

The value `42.0` is an example payload, not evidence of a calibrated physical soil-moisture measurement.

### Current message path

1. The ESP8266 reads its sensor input.
2. The device creates a moisture payload.
3. It publishes to `zone4/soil01/data`.
4. Mosquitto receives the message.
5. Node-RED subscribes to the topic and processes the payload.
6. Node-RED maps the value to the testbed tag `Z04_MOISTURE`.
7. The value is inserted into PostgreSQL.
8. The Node.js Web SCADA presents the stored measurement.

## 4. MQTT concepts to learn hands-on

### 4.1 Publish and subscribe

A publisher sends messages without needing to know which applications will consume them. Subscribers receive messages according to their subscriptions.

This decouples the ESP8266 firmware from Node-RED and the Web SCADA.

### 4.2 Quality of Service (QoS)

MQTT defines three delivery levels:

- **QoS 0 — At most once:** The message is sent without protocol-level delivery confirmation.
- **QoS 1 — At least once:** Delivery is acknowledged, but duplicates can occur.
- **QoS 2 — Exactly once:** MQTT uses a more extensive exchange to provide exactly-once delivery within the relevant MQTT protocol flow.

QoS does not guarantee that a sensor measurement is accurate, that PostgreSQL committed a database transaction, or that a physical valve moved.

The current Node-RED MQTT input requests QoS 2. This should be understood as a messaging setting, not as end-to-end proof that every reading has been stored correctly.

### 4.3 Retained messages

A retained message allows the broker to provide the latest retained value to a new subscriber. This can help a monitoring application obtain an initial value without waiting for the next publication.

Retained telemetry must be accompanied by timestamps and freshness checks. A retained reading may be old.

### 4.4 Last Will and Testament (LWT)

LWT allows a client to register a message that the broker publishes if the client disconnects unexpectedly.

It can help indicate that a device has gone offline. It does not by itself prove that a sensor, actuator, or entire application is functioning correctly.

### 4.5 Keepalive and reconnection

The MQTT client uses a keepalive mechanism to detect an unresponsive connection. The ESP8266 must also implement a suitable reconnection strategy and avoid losing its main application responsiveness while reconnecting.

A device that reconnects successfully should not automatically be considered healthy: its sensor may be disconnected, its readings may be invalid, or its clock may be incorrect.

## 5. Understanding MQTT topics

The current topic is:

```text
zone4/soil01/data
```

Its components mean:

- `zone4`: the logical zone
- `soil01`: the soil-sensor device
- `data`: telemetry messages

Possible future topics include:

```text
zone4/soil01/status
zone4/soil01/availability
zone4/soil01/diagnostics
```

A separate command topic should only be introduced if remote commands are genuinely required. Remote actuation needs explicit authorization, validation, safe-state behavior, and a reliable way to verify the resulting physical state.

## 6. Payload design

The current payload is intentionally simple:

```json
{
  "moisture": 42.0
}
```

A more informative future payload could be:

```json
{
  "device_id": "soil01",
  "zone_id": "Z04",
  "moisture_pct": 42.0,
  "timestamp": "2026-10-27T08:00:00Z",
  "sequence": 1042
}
```

This is a proposed schema, not the current firmware format.

Potential fields:

- `device_id`: identifies the publishing device
- `zone_id`: identifies the monitored zone
- `moisture_pct`: calibrated moisture percentage, once calibration is complete
- `timestamp`: measurement time in UTC
- `sequence`: a monotonically increasing counter that helps identify missing or repeated messages

Before adopting the schema, define units, timestamp ownership, sensor calibration, missing-value handling, and backward compatibility.

## 7. Node-RED's role

Node-RED connects messaging systems, transforms data, and coordinates processing using flows.

In Zone 4, its responsibilities include:

1. Subscribing to the MQTT topic.
2. Receiving and validating the payload.
3. Mapping `moisture` to `Z04_MOISTURE`.
4. Preparing the database insert.
5. Sending the measurement to PostgreSQL.
6. Reporting errors and unexpected input.

A critical engineering task is to validate the payload before database insertion. Missing fields, non-numeric values, out-of-range values, stale readings, and malformed JSON should not silently become valid measurements.

## 8. PostgreSQL and data integrity

PostgreSQL provides durable storage and allows the telemetry to be queried, analyzed, and presented later.

For the current testbed, Node-RED inserts into `tag_values` using the tag ID, measurement value, and quality.

Before relying on this flow, verify that the hard-coded tag ID `75` really identifies `Z04_MOISTURE` in the active `building_irrigation` database.

Future improvements should include:

- Device and measurement timestamps
- Database constraints and type validation
- Appropriate indexes for time-series queries
- Clear data-quality states
- Duplicate-message handling where required
- Error handling for failed database writes
- A documented retention and backup strategy

A successful MQTT delivery is not the same as a successful database commit.

## 9. Reliability and cybersecurity

### Current limitations

The repository documents that the MQTT broker is currently anonymous and unencrypted, intended for a private LAN. The soil sensor is not yet calibrated, and deep-sleep behavior and battery runtime remain unverified.

These limitations should remain visible in the project documentation.

### Improvement priorities

1. Validate sensor wiring and calibration.
2. Record timestamps and data freshness.
3. Test Wi-Fi and MQTT disconnection and recovery.
4. Test broker and database restart scenarios.
5. Introduce broker authentication and suitable access-control lists.
6. Use TLS where supported and appropriate to the deployment.
7. Protect credentials and avoid committing secrets to Git.
8. Separate telemetry permissions from command permissions.
9. Define safe local behavior if the network becomes unavailable.
10. Test physical actuation and safety independently of telemetry.

## 10. Hands-on experiment log

Record each experiment with the following template.

### Experiment

**Date:**  
**Objective:**  
**Hardware and software versions:**  
**Configuration changes:**  
**Procedure:**  
**Expected result:**  
**Observed result:**  
**Evidence:** Logs, screenshots, database query results, or test output  
**Problems encountered:**  
**Root cause:**  
**Resolution:**  
**What I learned:**  
**Next step:**  

### Initial experiment sequence

- [ ] Publish a test message from an MQTT client.
- [ ] Subscribe to `zone4/soil01/data` and inspect the payload.
- [ ] Trace one message through Mosquitto and Node-RED.
- [ ] Verify that one valid measurement is inserted into PostgreSQL.
- [ ] Verify the corresponding `Z04_MOISTURE` tag in the database.
- [ ] Confirm the Web SCADA displays the stored value.
- [ ] Disconnect Wi-Fi and observe reconnection behavior.
- [ ] Restart Mosquitto and verify recovery.
- [ ] Restart Node-RED and verify subscription recovery.
- [ ] Simulate malformed and out-of-range measurements.
- [ ] Test stale-data indication in the SCADA.
- [ ] Enable MQTT authentication and test permitted and rejected connections.

## 11. Further topics

After the basic MQTT path is understood, study:

- Edge computing and store-and-forward buffering
- MQTT retained messages, LWT, QoS, and session behavior
- MQTT over TLS and broker access control
- MQTT 5 features
- Sparkplug B for industrial MQTT data models and state management
- OPC UA information models and client/server communication
- Modbus TCP and BACnet/IP integration
- REST APIs and WebSockets for web applications
- Time-series data, SQL analysis, and data-quality management
- Docker-based deployment, backups, logging, and monitoring
- Digital twins and analytics after reliable data collection is established

## 12. Project references

- Repository: https://github.com/Radfar/wireless-irrigation-edge
- Architecture: https://github.com/Radfar/wireless-irrigation-edge/blob/main/docs/architecture.md
- Firmware: https://github.com/Radfar/wireless-irrigation-edge/tree/main/firmware
- Node-RED flow: https://github.com/Radfar/wireless-irrigation-edge/tree/main/node-red
- Web SCADA: https://github.com/Radfar/building-irrigation-scada

**Learning principle:** Every new concept should be connected to a real Zone 4 test, with the configuration, evidence, result, and lessons recorded. The objective is not merely to make the demonstration work once, but to understand why it works, how it fails, and how to improve it safely.
