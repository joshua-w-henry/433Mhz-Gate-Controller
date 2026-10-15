# RF Gate Actuator

Custom MM571 gate interface and vehicle remotes, using classic ESP32 boards,
433 MHz CC1101 modules and SSD1306 OLEDs. Target: reliable operation around 600 feet,
with direct and optional house-relayed paths. Existing LiftMaster remains independent.

## Current status
- `firmware/gate-bench`: preserved GateNode_Bench_v1, for the first gate hardware test.
- `firmware/mobile`: new vehicle remote v0.1, OLED/buttons/radio-SPI commissioning.
- Neither firmware sends RF packets or drives the physical gate. USB power for bench.
- `docs/ROADMAP.md`: staged remaining work and completion criteria.
- `docs/ARCHITECTURE.md`: interfaces, identity, safety boundaries and logging design.

Open one firmware folder at a time as a PlatformIO project. Gate and mobile use the
same radio/OLED pin mapping; switch/button/analog pins differ by role. Read each
project's README before wiring. Use `pio run -d firmware/gate-bench` or
`pio run -d firmware/mobile` from repository root to build.

## Development workflow
Keep known-working milestones tagged. Implement features on topic branches; include
build status and hardware observations in PRs. Never commit Wi-Fi credentials,
MQTT credentials, pairing secrets or private keys. Display names are not credentials.
Every command source must converge at one gate command processor before any relay
can be enabled. No software update may enable live relay operation implicitly.

Run portable button tests:
`g++ -std=c++11 tests/button_test.cpp -o /tmp/gate-buttons && /tmp/gate-buttons`

Completion standard: the system solves the intended gate-control problem reliably
without repeated manual intervention. Hardware commissioning is still outstanding.
