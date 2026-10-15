# Validation status

Mobile portable button tests passed locally on 2026-10-15: debounce, one event per
press, release/repress, boot-held suppression, chatter and millis rollover.

Gate v1 was preserved byte-for-byte from the previously delivered bench source.
Its previous button-gesture tests passed; see firmware/gate-bench/VALIDATION.md.

Firmware builds are run by .github/workflows/build.yml for both projects. A local
full embedded build was not yet verified at initial repository publication due to
toolchain download limitations; consult the GitHub Actions run for actual results.
No physical hardware test has been observed or certified by this repository.

Neither project transmits RF commands or drives the gate relay in this milestone.
