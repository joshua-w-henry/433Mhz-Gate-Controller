# RF Gate Actuator — checkpoint 2026-10-16

## Status
Bench communications operational. Gate OTA works at 192.168.86.58; TCP read-only diagnostics on port 2323. Physical relay actuation remains disabled. Development PR #3 on branch feature/rf-bench-link (not merged).

## Hardware / RF
Gate: ESP32, CC1101 (433.92 MHz), OLED, potentiometer GPIO34, closed switch GPIO32, service button GPIO13; relay GPIO33 intentionally disabled.
Mobile: ESP32, CC1101, OLED, local buttons GPIO32 and GPIO33. USB serial COM16. Both use supplied approximately 2-inch SMA stubby antennas.
Both radios: 2-FSK, 2.4 kbps experimental profile, approximately 58 kHz RX bandwidth and 9.5 kHz deviation (previous 4.8 kbps baseline). No intentional changes to TX power, bandwidth, or deviation during recent tests.
RF functions remain harmless BENCHPING/BENCHACK; no remote actuation.
Gate CC1101 state after tests: MARCSTATE 0x0D (RX), RXBYTES 0, no FIFO overflow, no reported receiver recoveries.

## Field measurements
4.8 kbps initial: 100 requests, 98 success, 70 first-attempt success, 45 retries; avg ACK 155 ms.
2.4 kbps initial: 105 cumulative requests, 104 success, 77 first-attempt success, 31 retries; avg ACK 185 ms.
2.4 kbps both vertical: increment 102 requests, 101 success, 82 first-attempt success, 23 retries.
2.4 kbps mobile antenna rotated 90 degrees: increment 100 requests, 99 success, 89 first-attempt success, 13 retries.
Final mobile cumulative: requests=307 success=304 failed=3 retries=67 first_ok=248 retry_ok=56 tx_error=0 rx_arm_error=0 crc=1 invalid_ack=0 ack_timeout=70 avg_ack_ms=188 max_ack_ms=208.
Final gate cumulative: rx=339 ack=339 duplicates=32 rejected=0 unique=307 crc=0 empty=0 oversize=0 rx_error=0 recoveries=0 ack_error=0 rearm_error=0 begin_code=0 arm_code=0 link_ok=1 rssi=-78.5 irq=679. diag: spi=1 marc=0x0D rxbytes=0x00 fifo_count=0 overflow=0 spurious=1 overflow_count=0 discarded=0.
Important caveats: cumulative snapshots, unsynchronized single-packet RSSI; no proof of antenna superiority from one run; 'ack' counts transmit operations rather than successful mobile reception. All observed request IDs reached gate, so mobile timeouts may reflect ACK path losses.

## Architecture decision
Prefer direct Wi-Fi when mobile can reach gate on KOTH/Bald Bunny; fallback to independent direct CC1101 RF when gate Wi-Fi cannot be reached/acknowledged. Optional high-house CC1101 relay extends independent RF coverage, even when Wi-Fi is unavailable; not needed for direct Wi-Fi path. Wi-Fi, direct RF, and relayed RF must share one authenticated, replay-resistant, cross-transport duplicate-safe command handler using a common request ID. No physical actuation until safety/authentication and tests pass.

## Next session — priority
1. Preserve working 2.4 kbps firmware and log this baseline; no hardware/firmware change yet.
2. Add receive RSSI/LQI summary (min/avg/max) to gate RF diagnostics and mobile ACK diagnostics to distinguish forward vs reverse link quality. Avoid disrupting stable CC1101 RX state.
3. Repeat controlled A/B/A antenna orientation tests and compare packet metrics; identify SMA vs RP-SMA and consider tuned 433 MHz whip with ground plane if warranted.
4. Implement harmless direct Wi-Fi BENCHPING/BENCHACK gate-mobile transport, test reachability and measured round trips.
5. Add Wi-Fi-preferred fallback to independent RF using same request ID and duplicate-safe gate-side handler. Authentication/replay protection before actuation.
6. Optional high-house RF relay after direct transport is validated.
Gate update: gate-ota from gate-bench directory (PowerShell profile function); mobile via PlatformIO on COM16. Pull feature/rf-bench-link. Preserve local ignored wifi_secrets.h and OTA credentials. Do not expose secrets.
