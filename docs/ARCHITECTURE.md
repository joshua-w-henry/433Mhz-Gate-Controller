# Design baseline

## Hardware roles
Gate: ESP-WROOM-32, CC1101 433, SSD1306, closed reed (toggle in prototype), hinge
10k potentiometer, local service button, future dry-contact relay. Charger-backed
12V gate battery, with a separate protected regulated electronics supply planned.
Buzzer motion input remains an optional future observation, not yet wired.
Mobile: same MCU/radio/OLED; ACTIVATE and STATUS buttons, one 18650 with USB charging
and suitable regulator/protection after bench tests. USB power now. Display remote
name, battery, path/RSSI, gate status/freshness and transition notifications.
House: optional forwarding node; may bridge ESP-NOW and 433. Does not originate
an independent duplicate of a forwarded command or independently drive the gate.

## Planned command protocol (not implemented in bench firmware)
Authenticated identity per remote; separate human-readable name. One press creates
one command ID preserved through all routes/retries. Gate verifies freshness and
credentials before the single serialized command processor. Replay/dedup state must
survive reset without repeating an uncertain activation. Pair and revoke individual
remotes. Acknowledgments must be authenticated and bound to the original command.

Start with two-way 433; add ESP-NOW with measured fallback deadlines. BLE remains
optional: it shares the ESP32 2.4GHz radio with Wi-Fi. Select paths by recent delivery
success and latency, using per-hop RSSI as supporting evidence. No raw cross-band
RSSI ranking. Relay duplicates are suppressed even if multiple transports succeed.
Forwarding must be bounded, expire old packets and preserve source identity.

## Gate states
Closed sensor is authoritative for the closed endpoint, subject to angle disagreement.
Angle slope estimates motion direction; calibrated open angle estimates fully open.
Initial firmware uses thresholds and linear angle conversion, not safety-grade position
measurement. Missing/broken pot detection needs hardware and software validation.
Never treat a sent command as proof of motion. MM571 handles motor/entrapment safety.
The dry contact means ACTIVATE (open-stop-close-stop), not independent open/close.

## History
Eventually retain a bounded local event log and archive on the home Pi over MQTT.
Record identity, command ID, timestamp/clock-valid flag, route, acknowledgment,
relay result and independently observed state changes. Boot-time uptime is not wall
clock time. External LiftMaster actions are unattributed; never infer guest identity.
Current bench builds only print events over serial; no persistent event history yet.

## Power
Validate charge termination under load, USB switchover, low-load cutoff, idle draw,
cell polarity/protection and battery-temperature behavior on the chosen board.
Display actual battery measurement; do not infer percentage from USB/5V supply.
Test one-press wake/activation before enabling MCU deep sleep.
