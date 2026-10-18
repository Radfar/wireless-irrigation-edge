# Valve Node Hardware Notes

## Current target

- Board: full-size WEMOS D1 based on ESP8266 for current bench testing.
- RF: 433 MHz receiver module using RCSwitch.
- Valve: 6 V DC bi-stable / latching valve.
- Driver: external circuit controlled by ESP8266 logic outputs.

## Pin assignments in `src/rf.cpp`

| Function | Board pin | ESP8266 GPIO |
|---|---|---:|
| Driver output 1 | D3 | GPIO0 |
| Driver output 2 | D1 | GPIO5 |
| Driver power enable | D5 | GPIO14 |
| RF receiver data | D6 | GPIO12 |
| On-board LED control | GPIO2 | GPIO2 |

GPIO0 is a boot-strapping pin on ESP8266 boards. The connected driver must not force an invalid boot state; verify the actual circuit during reset and power-up.

## Electrical requirements to confirm

- Valve manufacturer's rated voltage and permitted pulse duration.
- Coil current and driver current rating.
- Whether the valve requires polarity reversal between open and close.
- Appropriate inductive transient suppression for the actual driver topology.
- Shared reference/grounding requirements and supply decoupling.
- Safe output state during boot, reset, initialization, and brownout.

Do not connect the valve coil directly to ESP8266 GPIO pins. This document does not replace the electrical schematic or the valve manufacturer's datasheet.
