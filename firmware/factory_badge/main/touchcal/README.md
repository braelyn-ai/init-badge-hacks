# ESPtember calibration core

Imported from `chantastic/esptember`, commit
`687e62a48d2e34339c42506f31221d041184aecd`,
`drafts/grok-bot/grok_bot/touch_calibration_core.{h,cpp}`. One formatting change
separates the early return in smoothRing for GCC's misleading-indentation check.
The Day 07 contract suite is retained in `tests/touch-mapping/contract.cpp`.

This badge reads validated 40-byte v1 affine and 440-byte v2 affine/local-warp
records from `espt-touch/record`. Sensor coordinates are transformed in native
468×466 space before LVGL applies display rotation, exactly once. USB simulated
coordinates bypass calibration. No device coefficients are built into firmware.

The badge's compact calibration wizard is **not the full Day 07 exercise**:
it collects nine stable raw target contacts, fits an affine transform, and checks
five independent targets against Day 07's 12px mean / 22px maximum thresholds.
It writes a compatible v1 record only after fit and holdout verification pass.
Existing v2 records are applied in full; choosing to recalibrate replaces one
only after the compact affine check succeeds. Cancellation/failure before save
leaves the active record untouched. Calibration is fixed at native rotation 0;
the saved orientation preference is restored on exit.
