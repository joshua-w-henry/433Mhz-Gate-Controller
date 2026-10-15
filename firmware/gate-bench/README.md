# MM571 GateNode Bench v1

AITRIP classic ESP-WROOM-32, Arduino framework, PlatformIO/VS Code.
This is a bench commissioning project, not deployable gate-control firmware.
No relay output, RF transmission/reception, ESP-NOW, Bluetooth, MQTT, battery sensing,
wall-clock logging or remote authentication is implemented yet. CC1101 stays idle.
Events are printed over serial; request counts are RAM-only. Only calibration persists.

## Open and run
1. Extract ZIP. In VS Code / PlatformIO choose Open Project and select the folder
   containing platformio.ini. Do not paste this into another existing project.
2. Connect ESP32 by USB, click Build, then Upload. PlatformIO downloads pinned
   platform/display dependencies on the first build. If connection fails, hold BOOT
   during Connecting and release when writing starts.
3. Open Serial Monitor at 115200. Press EN once to see startup diagnostics.
4. OLED auto-probes 0x3C then 0x3D. The program continues over serial if OLED/radio fails.

## Wiring (power disconnected)
| Device | Pin | ESP32 |
|---|---|---|
| CC1101 | 1 GND | GND |
| CC1101 | 2 VCC | 3V3 |
| CC1101 | 3 GDO0 | GPIO26 |
| CC1101 | 4 CSN | GPIO27 |
| CC1101 | 5 SCK | GPIO18 |
| CC1101 | 6 MOSI | GPIO23 |
| CC1101 | 7 MISO | GPIO19 |
| CC1101 | 8 GDO2 | GPIO25 |
| SSD1306 OLED | GND / VCC | GND / 3V3 |
| SSD1306 OLED | SDA / SCL | GPIO21 / GPIO22 |
| Closed toggle | two contacts | GPIO32 and GND |
| Service button | two contacts | GPIO13 and GND |
| 10k potentiometer | outer terminals | 3V3 and GND |
| 10k potentiometer | wiper | GPIO34 |

D labels correspond to GPIO numbers, not physical pin numbers. For a 3-terminal
switch use common and one switched terminal. A four-leg tactile button has internally
joined pairs: use contacts that only connect when pressed. Both switches use pull-ups.
Optional 100nF capacitor GPIO34 to GND. Attach antenna. GPIO33 reserved, leave relay
completely disconnected. GDO pins are wired for later packet firmware but unused here.
Do not feed the radio/OLED 5V. Use ESP32 USB for this build.

## Bench calibration
1. Use the middle part of the potentiometer travel: avoid either electrical extreme,
   where the ESP32 ADC can saturate. A disconnected wiper is not reliably detectable.
2. Set the pot to your simulated closed position. Close the toggle. Wait one second.
   Send lowercase `c` in Serial Monitor to save closed ADC value.
3. Move the pot to simulated fully open. Open the toggle. Wait one second.
   Send `o` to save open ADC value.
4. Both endpoints must differ by at least 400 ADC counts. Either pot direction works.
5. `s` prints status, `x` clears calibration, `r` rechecks radio, `h` prints help.
   Calibration survives resets; recalibrate after moving the pot mechanically.

The display assumes a 90-degree sweep (TRAVEL_DEG in src/main.cpp). Percentage uses
saved endpoints. Degrees are an approximate linear conversion, not a precision angular
measurement: ADC nonlinearity, pot geometry and mounting need field calibration.
Angle movement above 0.8% of span sets direction; 900ms without that movement reports
stationary. Very slow motion below this threshold can be missed. Full open is inferred
within 2% of its calibrated angle only when stationary. Closed is the toggle.
A closed switch >5% away from the calibrated closed position, or angle outside -8..108%,
reports SENSOR DISAGREE. This is diagnostic logic, not an entrapment/safety system.
Boot state is observed without issuing any command. Long sensor cables and outdoor
power/input protection have not been designed in this bench version.

## Service button
- Short press cycles 3 OLED pages.
- Hold >=3 seconds, release: arms a 5-second sequence.
- Three short presses (each <600ms) completed within that window: one simulated request.
- Timeout or a medium press cancels. Another long hold re-arms on release.
- All edges are debounced 35ms. Holding during boot is ignored until released.
- The request is only shown/logged. It cannot actuate a physical relay.

## Commissioning checks
- Startup: OLED OK and CC1101 SPI PASS, with PART/VERSION bytes in serial.
- Toggle reliably changes CLOSED vs away; rotate pot both directions after calibration.
- Reboot confirms endpoints persist, without any simulated request.
- One hold alone does nothing. Hold/release + 2 taps then timeout does nothing.
- Hold/release + 3 taps produces exactly one request. Hold at boot produces none.
- Missing radio does not hang UI; missing OLED still leaves serial usable.

Next stage: authenticated two-way packets, duplicate handling, hardware relay interface,
MQTT/history and gate-side electrical integration after this commissioning passes.
