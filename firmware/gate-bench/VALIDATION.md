# Validation

2026-10-15: Native C++11 tests extracted the actual Debounced class and tickButton
handler from src/main.cpp and passed these cases: page tap, hold/release plus three
short taps (one simulated request), timeout after two taps, boot-held suppression,
medium-press cancellation, and electrical bounce rejection.

Full PlatformIO build attempted using the pinned configuration. Platform downloaded,
but ESP32 toolchain download failed checksum validation; stopped without bypassing
integrity checks. Full firmware compilation is NOT verified. Run PlatformIO Build
on the ThinkPad and report any compiler errors. Hardware tests remain outstanding.
