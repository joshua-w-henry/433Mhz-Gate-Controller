# Gate TCP/IP bench link — phase 1

No actuation. CC1101 at 433.92 MHz / 2.4 kbps and gate OTA continue unchanged.

## Setup
The gate retains its existing ignored firmware/gate-bench/src/wifi_secrets.h, associated to KOTH/BB with the reserved address 192.168.86.58. Diagnostic TCP 2323 is untouched; new BENCHPING-only TCP listener is on 2324. No router port forwarding; this unencrypted bench listener must remain restricted to trusted LAN, and must not be used to actuate anything.

Create the **ignored local-only** file firmware/mobile/src/wifi_secrets.h:

```cpp
#pragma once
#define MOBILE_WIFI_SSID "YOUR_PROPERTY_SSID"
#define MOBILE_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

Do not commit the file. The mobile connects via Wi-Fi STA. Its destination is currently pinned to 192.168.86.58:2324 for this specific field installation. If KOTH and Bald Bunny have separate SSIDs, this initial milestone only joins the configured SSID. Multiple network profiles and route selection come later.

Mobile serial COM16 at 115200:
- i : one independent TCP BENCHPING / BENCHACK test
- T : 100 TCP tests, 2 seconds spacing
- V : IP statistics
- p / t / v : existing independent RF tests and stats; untouched
- z : cancel batch

Gate read-only TCP 2323 commands now include `ip` for IP bench counters alongside `rf`, `diag`, `status`, `logs`.

## Scope and acceptance
Flash gate via existing gate-ota after git pull, then mobile over USB.
1. Confirm gate diagnostic 2323 and OTA still work.
2. Confirm mobile Wi-Fi association and five successful `i` tests, then run `T`.
3. Query gate `ip` and `rf`, run `p` to ensure CC1101 unaffected.
4. Disconnect mobile Wi-Fi: `i` should report failure without any gate action; `p` still works on RF.

This stage deliberately does NOT automatically choose/fallback transports. It does NOT share a cross-transport deduplication identity, authenticate, or issue gate commands; those are separate milestones before real actuation. No mobile BLE app planned until after testing the two-node system and optional independent high-house CC1101 relay. A Home Assistant phone shortcut is the preferred future phone interface.

## Next
Implement shared request envelope + authenticated command/ACK interface and cross-transport deduplication, then bounded Wi-Fi-preferred/RF fallback. Validate failure modes. Independent high-house RF relay after two-node system proves reliable. Keep actuator disabled throughout bench validation.
