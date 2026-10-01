# Hardware and interaction notes

> **Factory-stack migration, September 17, 2026:** active code is now
> `firmware/factory_badge/`. Its factory CO5300 framebuffer is **468×466** and
> CST820 readings pass through any validated ESPtember map before LVGL, with rotation applied once. The Arduino
> 468×468 geometry and scale/offset experiments below are historical; do not
> transfer those corrections to the new board adapter. See
> [factory-stack.md](factory-stack.md) for current integration and verification.

> **Current application: offline conference badge (September 17, 2026).**
> Read [conference-badge.md](conference-badge.md) for the current six-page UI,
> manual setup, radio policy and storage model, and
> [conference-clock.md](conference-clock.md) for repeatable flashing.
> The connected badge/voice behavior below is the retained historical application.
> Hardware geometry, pin assignments, calibrated IMU mapping, dependency pins,
> and partition-preservation constraints remain applicable.

This project targets the **M5Stack StopWatch**, not an arbitrary ESP32 development
board. These notes preserve findings from the September 2026 device sessions.
Current firmware and the pinned dependencies in [README](../README.md) are the
implementation reference; historical measurements are not guarantees for a new
build or another device.

Manufacturer references supplied for this project (SKU **C152**):

- [Stopwatch Dev Kit product page](https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3?variant=48862593515777)
- [StopWatch hardware documentation](https://docs.m5stack.com/en/core/StopWatch)

Checked September 11, 2026. Use these for schematics, pin maps, expansion, and
hardware-revision notes; use the project observations below for firmware behavior.

## Board and display

| Item | Established configuration |
| --- | --- |
| Controller | ESP32-S3R8, dual-core Xtensa LX7 |
| Flash | 16 MiB / 16,777,216 bytes, confirmed on the development device |
| PSRAM | 8 MiB / 8,388,608 bytes; use **OPI** mode |
| Screen | Round 1.75-inch AMOLED, CO5300 over QSPI |
| Display coordinates | **468 × 466** in the native factory HAL; quarter turns swap dimensions |
| Touch | CST820B; factory controller driver → LVGL |
| Orientation sensor | BMI270 six-axis IMU; the badge uses acceleration |
| Network | 2.4 GHz Wi-Fi |
| Power | USB-C, M5PM1 power management, nominal 450 mAh battery; separate power button |
| NFC | No built-in NFC on this StopWatch |

The manufacturer's nominal display resolution is **466 × 466**. The active
factory adapter retains its HAL's 468×466 framebuffer with an X offset of 6.
The following describes the previous Arduino integration: M5GFX configured a
468 × 468 coordinate system with an internal panel
offset. Its StopWatch driver also explicitly requires OPI PSRAM. The same
driver's board detection distinguishes StopWatch's touch controller and absence
of NFC from the PaperMono family. Relevant upstream source:
[M5GFX `src/M5GFX.cpp`](https://github.com/m5stack/M5GFX/blob/0.2.26/src/M5GFX.cpp)
(`Panel_StopWatch` and the StopWatch detection/configuration block).

The chip, panel, touch controller and radio specifications above agree with the
[official StopWatch hardware reference](https://docs.m5stack.com/en/core/StopWatch).
The framebuffer size and installed memory were also observed on the development
device. A framebuffer capture includes corners hidden by the round screen: QR
verification has used both the full image and a 232-pixel-radius circular
aperture. Preserve quiet zones and keep useful content inside the visible circle.

**Native rotation refresh finding, September 17, 2026:** the first native build
could leave the physical screen frozen at 180° while USB, button dispatch and
framebuffer capture remained responsive. Pinned M5GFX 0.2.19's framebuffer
`writePixels` reverses dirty-rectangle endpoints without normalizing them at
180°, and fails to swap dirty axes at quarter turns. The active board adapter
uses bounded `pushImage` row chunks through the correctly rotated `writeImage`
path. This is a display-transfer issue, independent of touch calibration.

The product documents ES8311 audio, an AW8737A speaker amplifier, RX8130CE RTC,
and vibration hardware. Current conference firmware enables the internal IMU
and RTC; microphone and speaker initialization are disabled. The retained
connected application configures its microphone for requested voice recording
and did not initialize the RTC. Battery runtime is not validated.

**Vibration experiment, September 18, 2026:** the motor uses M5IOE1 IO9 / PWM
channel 0 at the factory's 5 kHz frequency. Locked-page Morse contacts request
40% duty through the existing driver; release/cancellation and a 1400 ms hold
limit request 0%. The main-owned board adapter retains startup motor clearing,
checks touch/deadline on polling and retries failed stops. This software bound
cannot guarantee shutdown during a stalled main task or persistent I2C failure.
Driver register verification is distinct from physical motor/perceived-strength
verification. See the current dated record in [conference-badge.md](conference-badge.md).
The board's power controller handles its separate power button. See
[voice replies](voice-replies.md) for recording limits and verification status.

The pinned M5Unified StopWatch driver uses **I2S1**, with MCLK 18, BCLK 17,
LRCK 15, and microphone data-in 16. ES8311 is at I²C address `0x18` on SDA 47 /
SCL 48; the library enables its rail through M5IOE1 GPIO 3. Speaker data-out 21
and amplifier GPIO 10 are separate. Keep M5Unified's codec callback and channel
selection intact. `M5.begin()` configures audio; explicit `Mic.begin/record`
starts capture. Creating the recorder worker alone does not record sound.

## Pins and controls

The native `board` module owns hardware initialization. Use that single adapter;
do not add another display, touch, or IMU driver in a view. M5Unified owned this
boundary in the retained Arduino implementation described below.

| Connection | Mapping |
| --- | --- |
| Yellow pusher | `M5.BtnA`, GPIO 2, active low |
| Blue pusher | `M5.BtnB`, GPIO 1, active low |
| Internal I²C | SDA GPIO 47, SCL GPIO 48 |
| BMI270 | I²C address `0x68` |
| Touch interrupt | GPIO 13 |

These pin assignments are in the
[manufacturer's pin map](https://docs.m5stack.com/en/core/StopWatch#pinmap) and
the pinned M5Unified/M5GFX implementations. Display and touch resets go through
the board's I/O expander; do not assume direct ESP32 reset GPIOs. The physical
board revision has not been established. The official reference identifies a
v1.0 sticker error: a pin marked `BAT` can actually be a **5V input**, where a
battery must not be connected. On v1.0.1 that pin is a battery connection.
Confirm the revision and pin map before wiring rear expansion power.

The historical connected application's interaction contract was:

- Blue advances LinkedIn → X → GitHub, skipping unavailable accounts.
- Yellow is reserved and leaves the badge unchanged.
- One simultaneous press of both pushers opens Settings. This is a **chord**, not
  a double-click. Either pusher returns from Settings to the badge.
- A completed short touch toggles the expanded QR. There is no settings cog.

[`button_gesture.h`](../firmware/devices_badge/button_gesture.h) allows 125 ms for
the second pusher to join a chord. A single action commits on release or after
that interval, then both buttons must be released before another action. This
sits above M5Unified's normal button debouncing (10 ms in version 0.2.19).

Touch dispatch in
[`devices_badge.ino`](../firmware/devices_badge/devices_badge.ino) uses
`wasClicked()`, **after release**. An earlier change to `wasPressed()` caused
unwanted activations and was reverted at the user's request. Keep completed-tap
handling. M5Unified's unchanged defaults recognize holds after 500 ms and drags
using an 8-pixel threshold; those recognized gestures do not become badge taps.
Do not confuse the pusher chord grace interval with touch sensitivity.

## Lanyard orientation

The original stopwatch orientation has the loop at the bottom. Hanging from the
lanyard puts the loop at the top and needs **rotation 2 / 180 degrees**. The
device's loop-up physical calibration produced native accelerometer X near
`+1 g`; physical movement subsequently produced 2 → 0 → 2 rotations, and the
user confirmed the display looked correct.

The important axis mapping is **native Y → screen X, native X → screen Y**.
In the current Settings menu, **Default** selects stock rotation 0 and **180°**
selects this lanyard-up rotation 2, following the user's September 17 correction.
These labels do not change the automatic orientation calibration.
`updateOrientation()` deliberately calls:

```cpp
orientation.update(data.accel.y, data.accel.x, data.accel.z, now, touching);
```

This matches the axis swap in the
[factory IMU implementation](https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/main/hal/hal_imu.cpp).
The accelerometer's stationary support-force reading points opposite gravity;
do not reverse the mapping by assuming it is a gravity vector.

[`orientation_filter.h`](../firmware/devices_badge/orientation_filter.h) keeps
rotation stable while the device hangs or is handled:

- Poll no faster than every 50 ms; require 700 ms of a stable candidate.
- Smooth X/Y with a 0.25 low-pass coefficient. Require a dominant axis of at
  least 0.65 g and 1.35 times the other axis.
- Reject nonfinite readings and acceleration magnitude outside 0.7–1.3 g.
- Cancel settling after a missing sample, a gap over 250 ms, an ambiguous fresh
  direction, or touch contact/release detail. A flat or diagonal pose keeps the
  last orientation.
- Change display rotation only after touch dispatch and full release. In the
  active native runtime, LVGL transforms touch coordinates once; do not apply
  another manual rotation. M5GFX performed this step in the Arduino build.

All four cardinal orientations are supported. Rotation redraws local cached
data and does not trigger a profile request or save rotation to flash.

## Physical touch alignment finding — September 17, 2026

On conference commit `f17048f`, a 60-second live USB observation used the real
touch sensor in Default/rotation 0. The user confirmed aiming directly at the
bottom crosshair `(234, 362)`: 12 captured release positions had Y=390–410,
median 401, a median downward error of 39 pixels. Side-region release positions
had median X=92 near the left target X=112, and X=386 near the right target
X=356. Those side targets were inferred from regions, not individually labeled
by the sampling tool; the user also reported the other axis feeling displaced.
This established the baseline before the scale trial below.
Private samples: `.build/touch-test-verification/live-20260917-090738.jsonl`.

Pinned M5GFX 0.2.26 source explains the observed raw-to-screen factor 239/233:
`Panel_AMOLED_Framebuffer` calls `setTouch()` while panel geometry still has the
240×240 defaults, before `initPanelFb()` copies the 468×468 configuration.
StopWatch's touch configuration declares maxima 233, but live raw coordinates
exceeded that value. Merely recalculating with the larger panel size would
roughly double coordinates and is not an established fix. Collect deliberately
labeled targets across the screen and validate both fixed orientations before
choosing or persisting a calibration. The source issue alone does not explain
the full measured offset, and injected display-coordinate tests cannot qualify
physical alignment.

### Continuous scale trial

The user requested a scale correction derived from the recorded test, rather
than per-target mapping or treating the suggested 1.2 factor as exact. Grouping
the released samples by target region gives these median display coordinates
in rotation 0 (all five regions receive equal weight in the fit):

| Region | Reported median | Target |
| --- | --- | --- |
| Top | 234, 119 | 234, 120 |
| Left | 92, 264 | 112, 234 |
| Center | 249, 257 | 234, 234 |
| Right | 386, 257 | 356, 234 |
| Bottom | 238, 401 | 234, 362 |

Separate least-squares scale/offset fits produce `X = 0.82758047844*x +
35.54620127` and `Y = 0.85758081377*y + 14.17202075`. The implied overscaling
is about 20.8% horizontally and 16.6% vertically. The vertical equation has a
fixed point near Y=100, consistent with the user confirming the upper target
was already close. Fitted median residuals are at most about 8 pixels; this is
a prediction on the old samples, not a fresh physical verification.

`conference_touch_scale.h` applies these continuous equations once after
M5GFX conversion, to both normal physical input and the live touch-test marker.
For rotated displays, it transforms the correction axes/origin, preserving the
same physical correction. Raw readouts and injected UI-coordinate diagnostics
are unchanged. There is no target lookup, edge clamp, saved calibration, or
library modification. Existing M5Unified click/drag eligibility still uses its
original coordinates and thresholds; application release semantics are retained.

This is a provisional trial for the observed development board. Upper/bottom
aiming was user-confirmed; the side/center region labels were inferred. The
Default-orientation follow-up below supports the correction; the subsequent
180° comparison exposes a remaining downward bias. These coefficients are not
a validated default for the roughly 400-unit batch.

### Physical follow-up with the scale correction

On firmware `213a527`, the user reported improvement and completed a further
60-second observation in Default/rotation 0. Excluding initial stale samples and
unpaired releases yielded 54 new physical taps versus 37 baseline taps. Targets
were assigned by nearest crosshair; fingertip locations were not independently
measured. Median target errors (right/down positive) changed as follows:

| Target | Horizontal error before → after | Vertical error before → after |
| --- | --- | --- |
| Top | -1 → -5 px | -2 → +3 px |
| Center | +15 → -4 px | +23 → +5 px |
| Bottom | +4 → +5 px | +39 → 0 px |
| Left | -20 → +1 px | +30 → -5 px |
| Right | +30 → +4 px | +23 → -9 px |

Median distance from the inferred target fell from 31.1 to 10.0 pixels. Bottom
vertical readings spanned -6 to +7 pixels, previously +28 to +48. Keep the
current correction: residual scatter and the right target's 9-pixel upward bias
do not yet warrant another adjustment without a controlled labeled-target test.
No firmware/settings changed during observation; the serial observer closed.
Private evidence: `.build/touch-test-verification/live-20260917-092455.jsonl`
and `tap-comparison-20260917-092455.json` in that directory.

### Physical 180° follow-up

On the same firmware `213a527`, the user reported that all targets felt low in
180°/rotation 2. Two initial captures contained only retained samples; the next
60-second capture collected 58 new physical press/release pairs, all in rotation
2 with the scale correction enabled. The initial retained sample and one
unpaired changed release were excluded. Nearest-crosshair assignment gives:

| Target | Taps | Median horizontal error | Median vertical error |
| --- | --- | --- | --- |
| Top | 7 | +5 px | +22 px |
| Center | 18 | -7 px | +16.5 px |
| Bottom | 9 | +6 px | +29 px |
| Left | 11 | -9 px | +19 px |
| Right | 13 | -2 px | +6 px |

Positive errors are right/down. All five vertical medians confirm a downward
bias, but its magnitude varies by target and individual tap. Median target
distance is 18.2 pixels versus 10.0 in the corrected Default pass; per-target
comparisons are more informative because tap counts differ. Targets remain
inferred and fingertip locations were not independently measured. These results
do not establish a new calibration or its cause. Keep the current coefficients
for comparison while investigating the residual; physical quarter-turn passes
were still outstanding at that point. Neither firmware nor settings changed during this capture,
and the observer closed afterward.

Private evidence: `.build/touch-test-verification/live-20260917-094235.jsonl`
and `orientation-comparison-20260917-094235.json` in the same directory. The
earlier `093231` and `093346` captures contain no fresh physical taps and are
not alignment evidence.

### Controlled center test across four orientations

On September 17 with unchanged firmware `213a527`, a guided test collected five
physical center presses in each of these passes: 180°, Default, loop-left
sideways (rotation 3), loop-right sideways (rotation 1), then 180° again. The
user was instructed to keep the support, finger, viewing angle and approach
consistent and hold/lift for about one second. The USB observer required the
expected frozen rotation, recorded raw/corrected sensor positions, and closed
after five press/release pairs. An initial attempt mixed short taps and missed
presses; it was excluded and the first pass repeated. All 25 formal presses
passed the capture checks, with observed holds of 0.45–0.88 seconds.

Every intended target was the center `(234, 234)`, explicitly prescribed rather
than inferred from nearest position. Each press contributes its median held
position; each pass summarizes those five independent contacts. Medians below
are relative to the displayed center, with positive directions right/down:

| Pass | Rotation | Median horizontal error | Median vertical error |
| --- | --- | --- | --- |
| Initial 180° | 2 | +5 px | +23 px |
| Default | 0 | -3 px | +5 px |
| Loop left | 3 | +17 px | +19 px |
| Loop right | 1 | -7 px | +20 px |
| Repeated 180° | 2 | +1 px | +25 px |

The repeated 180° median differs by (-4, +2) pixels, supporting a repeatable
orientation-related effect at the center. The downward error is already
present at first contact and during the hold; lifting the finger does not
explain it. The sideways horizontal error changes sign while the vertical
error stays downward. Thus neither one constant screen-relative vector nor
one constant sensor-relative vector describes all four poses well.

A descriptive fit giving each orientation equal weight (averaging the two
180° pass medians) separates a screen-relative vector `(2.5, 17)` from a native
vector `(-1.75, -10.75)` rotated with the display. Vector RMS residual is 3.95
pixels, versus 11.59 for a single screen-relative vector or 17.63 for a single
native vector. This combined model has more parameters and only four orientation
medians; it does not establish cause,
edge scaling, or a new calibration. Contact technique and device effects remain
possible contributors. The approximately one-pixel movement of the displayed
center under rotation is handled by transforming both points and targets.

No firmware or calibration changed during the test; orientation settings were
changed by the user for the prescribed passes. Observation finished with the
test open in 180° and the serial port closed. Private evidence is under
`.build/touch-test-verification/`: formal `center-*` JSONL/summary files at
`100010`, `100045`, `100132`, `100208`, and `100254`, and the independent
`controlled-center-independent-final.json` analysis. All 25 held-position
summaries were independently reconstructed from the captured polls. The `095911` short-tap
attempt is excluded. The read-only capture helper is `.build/observe-center-pass.py`.

### Combined-offset trial

The next implementation retains the original scales and separates the fitted
native residual `n=(-1.75, -10.75)` from the screen residual `d=(2.5, 17)`.
Relative to the prior unrounded mapping, the new point is `old - R(n) - d`,
where `R` rotates a vector with the display. In `conference_touch_scale.h`,
subtracting `n` is folded into the native offsets before rotating the affine
correction, and `d` is subtracted after rotation. Both coordinates round once
at the end. The already-rotated driver input is not rotated a second time.

The resulting changes from the old unrounded mapping are rotation 0
`(-0.75, -6.25)`, rotation 1 `(+8.25, -18.75)`, rotation 2 `(-4.25, -27.75)`,
and rotation 3 `(-13.25, -15.25)` pixels. These follow one shared model; they
are not independently fitted per-orientation or per-button adjustments. Scales,
gesture thresholds, release handling, raw readouts, and synthetic UI inputs
remain unchanged. Normal input and the test marker share the same mapping.
`touch_test_status` exposes `touch_model: "scale-offset-2"` to distinguish new
physical captures from the preceding scale-only observations.

Host checks exercise the offset changes over and outside the screen, unchanged
scales, monotonicity, no clamping, and rotation covariance after restoring the
screen term. These verify the mathematical implementation, not physical
alignment. The physical follow-up below covers the center; edges remain
unchecked. No batch-wide validity is inferred from this one board/user fit.

The September 17 candidate passed the complete host suite, pinned firmware
build, and independent math/source review. The 1,307,375-byte application has
SHA-256 `9987f153696f41e7fe638e15dc8d11773504e4952dbe306567a19cbe83e4a09a`.
The ordinary uploader verified the existing partition layout and programmed
data, then freshly provisioned/verified the RTC and returned `UNIT_READY` with
`storage_initialized: false`. Live diagnostics confirmed `scale-offset-2`,
mounted storage, valid time, both radios off, and a freshly opened 180° test.
At installation, the physical repeat was pending; upload and synthetic modal
entry do not qualify alignment. Private logs: `.build/touch-offset-{tests,build,flash}.log`.

### Physical center follow-up with the combined correction

Firmware `dec9905` / `scale-offset-2` was tested with the same five-pose center
sequence on September 17. The observer required the new model identifier in
every reply. Coefficients were fixed throughout; these new observations were
not used to refit them. Each pose has five complete sensor press/release pairs.
All targets were explicitly the center `(234, 234)`.

The new presses were shorter than prescribed: 21 of the 25 comparison contacts
were below the observer's 0.45-second threshold. Baseline duration range/median
was 0.454–0.881 / 0.654 seconds; follow-up was 0.187–0.687 / 0.334 seconds.
Every new pass therefore retains `clean: false` and its duration flags. These
are descriptive physical results, not matched-duration validation. Each press
had multiple captured sensor polls, and first/held/release positions usually
agreed closely; the largest first-to-release movement was 5 pixels vertically.

Median errors use the held position from each of the five contacts per pose;
positive remains right/down. Distances summarize individual contacts, not the
length of each pass's median error vector.

| Pose | Median error before → after (X, Y) | Median distance before → after |
| --- | --- | --- |
| Initial 180° | (+5, +23) → (+3, +1) px | 24.70 → 4.12 px |
| Default | (-3, +5) → (+4, -8) px | 8.60 → 8.94 px |
| Loop left / rotation 3 | (+17, +19) → (+1, +6) px | 24.04 → 10.30 px |
| Loop right / rotation 1 | (-7, +20) → (+4, +2) px | 22.09 → 7.81 px |
| Repeated 180° | (+1, +25) → (0, +2) px | 26.57 → 8.00 px |

Across the comparison sequence, median contact distance decreased from 22.47
to 7.81 pixels. Default's distance is essentially unchanged, with a modest
upward bias; the other poses improve. The repeated new 180° median changes by
(-3, +1) pixels from its initial new pass. Retain the candidate without further
fitting to this small sample. Timing differences, finger placement, untested
edges and other boards remain limitations.

The first new 180° attempt also had five short contacts and was repeated for
timing. It is retained separately, not relabeled as clean: its median error is
(+2, -5) pixels and median distance is 6.40 pixels, also improved versus the
baseline. Substituting that first attempt into the comparison sequence gives
8.00 pixels overall, so the conclusion does not depend on choosing the better
retry. All outliers are retained. An earlier waiting window contained zero
contacts and is excluded.

Private evidence is under `.build/touch-test-verification/`: comparison files
`center-offset2-*` at `102858`, `102932`, `103024`, `103116`, and `103225`;
the first attempt is `102745`, and the empty window is `102254`. The firmware
remained unchanged, the final pose was 180°, and observation closed afterward.
All 25 contact summaries were independently reconstructed from their sensor
polls; the comparison is `heldout-independent-final.json` in the same directory.

## Build target, USB, and flash layout

[`scripts/build.sh`](../scripts/build.sh) fixes the working target:

```text
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi
```

[`scripts/flash.sh`](../scripts/flash.sh) adds `UploadSpeed=460800`. The sketch
sets `fallback_board` to `board_M5StopWatch`; the pinned M5GFX enum identifies it
as board 30. Keep the explicit target even if Arduino USB discovery offers a
generic or ambiguous board name. USB identification alone is not the board's
display/memory configuration.

The macOS `/dev/cu.usbmodem…` port has changed after reconnecting. Rediscover it
for each hardware session, close any serial monitor before upload, and do not
copy a historical port into automation. Runtime diagnostics use 115200 baud.
USB output must stay nonblocking: unread output previously affected input-loop
responsiveness. Current capture output has a five-second deadline and restores
nonblocking output afterward.

The following table comes from ESP32 Arduino core **3.3.10**,
`tools/partitions/app3M_fat9M_16MB.csv`:

| Partition | Type / subtype | Offset | Length | Capacity |
| --- | --- | --- | --- | --- |
| `nvs` | data / nvs | `0x9000` | `0x5000` | 20 KiB |
| `otadata` | data / ota | `0xE000` | `0x2000` | 8 KiB |
| `app0` | app / ota_0 | `0x10000` | `0x300000` | 3 MiB |
| `app1` | app / ota_1 | `0x310000` | `0x300000` | 3 MiB |
| `ffat` | data / fat | `0x610000` | `0x9E0000` | 9.875 MiB |
| `coredump` | data / coredump | `0xFF0000` | `0x10000` | 64 KiB |

**Preserve this layout.** The profile store mounts **LittleFS** in the existing
partition named `ffat`; that historical label does not mean this firmware uses
a FAT filesystem. Its address and length are explicitly checked in
[`profile_store.h`](../firmware/devices_badge/profile_store.h). A second app slot
exists, but wireless firmware updates are not implemented. Normal component
upload preserves saved user state; a full erase or full-flash restore does not.

Each provider's 400 × 400 RGB565 avatar uses 320,000 pixel bytes. Three retained
avatars use 960,000 bytes, with another 320,000-byte scratch canvas during an
avatar refresh, plus decoder and other allocations. These are accounting facts,
not measured peak RAM. Build size and free-memory snapshots change with the
firmware; use current build output and bounded diagnostics instead of old release
numbers. See [architecture](architecture.md) for cache behavior, startup
measurements, and validation limits.

### ESPtember calibration reference (September 28, 2026)

ESPtember Day 07 (`days/day-07-touch-calibration/SPEC.md` in
`chantastic/esptember`) specifies a device-measured affine transform plus local
warp, stored as `espt-touch/record` with CRC, geometry and rotation validation.
Day 08 consumes that record before delivering coordinates to LVGL. This badge now loads both record versions and applies the map to raw CST820
coordinates before LVGL rotation. Settings → Calibrate touch runs
a compact nine-target affine fit plus five independent verification targets,
saving a compatible v1 record after success. It is not the complete Day 07
perimeter/local-warp collection exercise. Existing v2 maps retain their complete
local warp until the user successfully recalibrates. A full
flash erase removes NVS calibration. Do not copy another device's coefficients
or add offsets without implementing and physically verifying the calibration
workflow against this native board adapter.
