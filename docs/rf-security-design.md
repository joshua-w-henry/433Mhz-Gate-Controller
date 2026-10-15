# Gate RF protocol: security design (proposal, not enabled)

## Scope
The stationary gate-bench hub authorizes all actuation. Portable mobile nodes request actions; the hub observes position and independently enforces safety interlocks. Bench builds must leave relay output and RF command transmission disabled.

## Cryptography
- Use a well-maintained authenticated-encryption implementation, preferably AES-128-GCM from ESP32 Mbed TLS, subject to on-device library and packet-size validation.
- Give each mobile a unique, provisioned 128-bit key and stable device ID. No hard-coded shared household key and no credentials in Git.
- Nonce uniqueness under a key is mandatory. Design a persisted, power-loss-safe counter scheme (or challenge-response with fresh hub nonce) before enabling authenticated commands. Never reuse a GCM nonce.
- Hub validates authentication, sender authorization, message type, expected session/challenge, and replay counter before taking action.
- Reject malformed, unauthenticated, duplicate, stale, or revoked requests without actuation.
- Authenticated ACK/NACK responses include the device ID, request ID, current state, and fault status.
- Provisioning requires physical local action at the gate controller. Support per-device revocation and re-keying. Protect stored keys and consider flash encryption/secure boot in production.

## Candidate application messages
HELLO/CHALLENGE, ACTUATE_REQUEST, ACK, STATUS, FAULT_EVENT. Keep messages versioned and length-checked.
Event snapshots report angle_deg, position_pct, closed_switch, open_switch=null (no independent open switch installed), expected_direction, elapsed_ms, and a monotonic uptime. Add wall-clock time only if an RTC or trusted time source is available.

## RF engineering and safety
- CC1101 is a packet radio, not the cryptographic engine; crypto is implemented on the ESP32.
- Check final RF frequency, power, airtime, antenna and regulatory compliance before activating transmissions.
- Never infer authorization merely from knowledge of the RF frequency, address or device ID.
- Add deliberate retries and duplicate suppression so lost acknowledgements cannot cause a second gate pulse.
- RF security does not replace physical entrapment/obstruction safety protection.

## Verification gates
1. Host-test vectors for encryption/decryption, tampering, and nonce/counter behavior.
2. Simulated multi-mobile provisioning, replay, out-of-order, revoked-device, loss-of-ACK and reboot tests.
3. Bidirectional CC1101 packet reception, RSSI and latency tests without relay actuation.
4. Controlled actuation tests only after independent safety checks.
