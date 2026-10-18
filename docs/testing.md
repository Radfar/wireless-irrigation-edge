# Test and Commissioning Checklist

Record actual results and hardware revisions before presenting the system as customer-ready.

## A. Controller bench test

- [ ] Confirm Arduino UNO board and firmware revision.
- [ ] Confirm LCD, button ladder, EEPROM settings, RF transmitter, pump output, and SIM800 wiring.
- [ ] Verify valve IDs 0–3 are intended for the current configuration.
- [ ] Confirm watering duration. The current source sets `WATERING_DURATION = 2000` ms although the adjacent comment says one minute.
- [ ] Verify pump output behavior at startup, normal stop, and interrupted operation.
- [ ] Check that any safety/protection function being relied upon is implemented and tested; commented-out code is not active protection.

## B. Valve-node bench test

- [ ] Confirm the exact WEMOS D1 board variant and pin mapping.
- [ ] Confirm RF receiver output is connected to D6 / GPIO12.
- [ ] Verify driver-control signals D3 / GPIO0, D1 / GPIO5, and D5 / GPIO14 against the schematic.
- [ ] Test with the valve disconnected or a safe test load.
- [ ] Verify boot-time output pulses are safe and intentional.
- [ ] Confirm valve voltage, current, polarity, and pulse duration from its datasheet.
- [ ] Verify the 6 V supply and driver can handle the coil pulse without resetting the ESP8266.
- [ ] Confirm transient suppression and protection suit the actual driver topology.

## C. RF integration test

- [ ] Confirm controller and node use the same valve ID.
- [ ] Verify open and close codes independently.
- [ ] Confirm repeated commands do not cause unsafe behavior.
- [ ] Test at intended distances and with typical obstructions.
- [ ] Record missed commands and range; do not claim guaranteed RF delivery.
- [ ] Verify physical valve movement separately from RF reception.

## D. Water commissioning

- [ ] Start with water isolated and verify the valve sequence.
- [ ] Verify pump and valve sequencing before connecting the water supply.
- [ ] Test for leakage and confirm the valve returns to the expected state.
- [ ] Test power loss and restart behavior.
- [ ] Establish a safe maximum watering time and a manual stop procedure.

## E. Customer handover evidence
## E. Zone 4 soil node and edge pipeline (WiFi / MQTT)

Bench-tested 2026-10-18 on a Wemos D1 R1.

- [x] Node joins WiFi through the setup portal; WiFi and broker address are saved in flash.
- [x] Node publishes to `zone4/soil01/data` every 15 s (test interval) and Mosquitto receives it.
- [x] Node-RED maps the payload to `Z04_MOISTURE` and inserts it into `tag_values`.
- [x] Web SCADA Zone 04 card reads `/api/zone4`.
- [ ] Sensor wired and calibrated (`ADC_DRY` / `ADC_WET` measured in air and in water).
- [ ] ONLINE, STALE and NO DATA transitions verified (unplug the node, stop the server).
- [ ] Node moved to a different network (portal reopens after 5 failed wakes, or with D3 held to GND at reset).
- [ ] Deep sleep wake verified with D2 (GPIO16) wired to RST and `USE_DEEP_SLEEP 1`.
- [ ] Battery runtime measured (sleep current and wake duration).
- [ ] Broker authentication and TLS added (currently anonymous, private LAN only).

Record the hardware BOM, wiring diagram, valve model, driver schematic, firmware revision, test date, RF range conditions, known limitations, and outstanding corrective actions. Mark untested items as pending rather than complete.
