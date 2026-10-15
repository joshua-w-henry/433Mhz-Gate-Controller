# Vehicle remote bench package v0.1

Classic AITRIP ESP-WROOM-32 + CC1101 433 MHz + SSD1306 128x64 I2C OLED.
Open THIS folder in PlatformIO, build/upload, monitor at 115200 baud.
No battery circuit is needed yet: use USB. Do not apply raw cell voltage to 3V3.
Do not connect external VIN power and USB simultaneously without verified isolation.

## Wiring
| Device | Connection | ESP32 GPIO/label |
|---|---|---|
| CC1101 | 1 GND | GND |
| CC1101 | 2 VCC | 3V3 |
| CC1101 | 3 GDO0 | 26 |
| CC1101 | 4 CSN | 27 |
| CC1101 | 5 SCK | 18 |
| CC1101 | 6 MOSI | 23 |
| CC1101 | 7 MISO | 19 |
| CC1101 | 8 GDO2 | 25 |
| OLED | GND, VCC | GND, 3V3 |
| OLED | SDA, SCL | 21, 22 |
| ACTIVATE momentary | between pin and ground | 32, GND |
| STATUS momentary | between pin and ground | 33, GND |

Wire with power disconnected. Radio antenna attached. Both buttons use internal
pull-ups; no external resistors needed for short bench wiring. GPIO34 reserved for
future battery measurement through a suitable divider/protection circuit, currently
unconnected. No pot or reed switch on the mobile unit.

## Behavior
- ACTIVATE produces a local BENCH_REQUEST in serial and an OLED confirmation marked
  TEST ONLY. It does NOT transmit, claim acknowledgment, or operate the gate.
- STATUS also logs a request and cycles display pages. Gate stays UNKNOWN.
- Holding does not repeat. A button held at boot is ignored until debounced release.
- If both presses debounce in the same loop, STATUS takes priority.
- Screen blanks after 60 seconds; either button wakes the screen and performs its
  normal action on the same press. MCU is still awake; battery life is NOT optimized.
- Serial `a` / `s` perform the corresponding bench tests; `r` reprobes CC1101.
- Radio probe reads PART/VERSION and tests register write/read/restore, with a timeout.
  It does not measure antenna performance. RSSI remains unavailable until real packets.
- `src/config.h`: customize REMOTE_NAME (keep <=21 characters), screen idle time.
- Battery voltage/percentage, charging status, link and gate telemetry are intentionally
  unavailable rather than fabricated.

## Validation on your bench
Startup should show OLED OK and CC1101 SPI PASS. Tap each button, hold ACTIVATE,
reset while holding it, and wait for screen timeout then wake it. Confirm each
normal ACTIVATE press logs one request, a hold never repeats, and boot-held causes
none. Repeat on the second board before enclosure/battery work.

Next increment will connect this UI to authenticated two-way radio packets and a
matching gate receiver, with acknowledgments and reported gate state. Existing
GateNode_Bench_v1 is preserved unchanged in firmware/gate-bench.
