# Roadmap

1. Gate bench: verify compile, OLED, CC1101 SPI, toggle, angle calibration and service
   gesture on physical hardware. Already supplied v1 remains the commissioning baseline.
2. Mobile bench: compile and verify OLED, radio-SPI probe, ACTIVATE/STATUS buttons,
   no repeat on hold, boot-held suppression and screen blank/wake.
3. Implement authenticated two-way 433 packets, gate-origin acknowledgments, status
   telemetry and per-remote identity; bench-only output throughout development.
4. Add reset-safe dedup/replay handling and bounded retries. Inject dropped packets,
   duplicates, resets, stale packets and corrupt payloads; prove no extra actuation.
5. Add tested relay hardware with idle/boot/reset off behavior. Confirm input pulse
   duration on MM571. Preserve LiftMaster parallel contacts and all OEM safety inputs.
6. Add ESP-NOW fallback/house forwarding without changing command identities.
7. Battery hardware, calibrated voltage reporting, low-power wake and enclosure.
8. Persistent history + MQTT/HA; timestamp quality and unattributed external movements.
9. Field tests at actual approach routes/600 feet, inside both vehicles, gate moving,
   mains/network outages, weather, and simultaneous path deliveries. Close out when
   normal operation no longer needs manual workarounds.
