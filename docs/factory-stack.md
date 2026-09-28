# Factory stack migration

September 17, 2026. The active firmware is the native ESP-IDF project at
`firmware/factory_badge/`. The earlier Arduino conference application remains
under `firmware/devices_badge/` for rollback reference and regression fixtures.

## Structure

| Area | Files | Responsibility |
| --- | --- | --- |
| Runtime | `main/main.cpp` | Main loop, physical buttons, orientation policy, preferences, bounded USB diagnostics |
| Hardware | `main/board.*`, `board_rotation.h`, `vendor/` | Factory CO5300 display, raw CST820 touch, PMIC, IMU and checked RTC access |
| UI lifecycle | `main/badge_ui.*` | One Mooncake app, Smooth RAII scene ownership, model and callbacks |
| Views | `main/ui/page_*.cpp`, `modal_*.cpp` | One native LVGL view per page/modal |
| Shared UI | `main/ui/widgets.*`, `chrome.*`, `ui_internal.h`, `brand_assets.*` | Typography/colors, buttons/QR/branding, clock, fixed pagers and dots |
| Profile/setup | `main/services.*`, `portal_page.h` | Compatible atomic records, photo decoding, temporary AP, HTTP/DNS workers |
| Clock | `main/clock_service.*` | UTC, persisted display offset, computer/phone provisioning and RTC readback |
| Agenda | `main/schedule.h` | Published blocks and daily local-time current/passed/upcoming rules |
| Invitation reveal | `main/after_dark_unlock.h`, `morse_unlock.h`, `ui/page_invite.cpp` | Persistent/timed policy and bounded touch-Morse recognition on the visible locked page; main owns persistence |

Views receive a `UiModel` and invoke callbacks. They must not read hardware,
open NVS, start Wi-Fi, or write profile records. Update labels and styles in
place; recreate a page on navigation or a structural change. Native LVGL owns
widgets, scrolling, snap behavior, QR generation, input hit testing and animations.
Shared brand images preserve the supplied conference PNG masks exactly; portraits remain
dynamic and never enter source or compiled assets.

All LVGL and board calls run on the main task. HTTP and DNS run separately;
profile snapshots hold immutable image memory. Phone clock requests are queued
back to main, so RTC/I2C access cannot race display/input polling.

After Dark retains page ID 2 and its indicator while locked; all six pages remain
available. Its whole-page native LVGL target says `tap the code to reveal a secret
invitation` and measures touch press/release durations with the existing
`MorseUnlock` helper. Chrome arrows retain navigation. Long holds
are intentional dashes. Drag, press loss, navigation, modals and
rotation cancel progress. A complete `init` invokes a UI callback; main owns the
persistent `conference_ui/after_dark_v1` unlock (`0xA1`, cleared by confirmed badge
reset) and fixed October 7, 2026 at 13:30 local clock policy. Ordinary pushers are
only navigation/setup controls.
The unlocked invitation QR opens https://luma.com/developers-after-dark, with
`Invite details coming soon` and a white quiet zone of at least four modules. It is hidden
while locked and during the three-word reveal. This supersedes
the earlier hidden-page/physical-pusher interaction without changing saved unlocks.
Successful touch code uses the existing native intro GIF once behind a native
LVGL word sequence (`You're`, `Invited`, `To`), then reveals the invitation heading.
Page lifetime owns the decoder and animations; no second player is introduced.
Registered dots/dashes use native LVGL objects with flex letter groups and
wrapping. A 200 ms unit defines the standard 1/3/7 Morse timing, with tolerant
400 ms dot/dash and letter-gap decisions and a 1400 ms word boundary. The final
correct dash submits on release.
The recognizer reports a 2.5-second released-idle reset, and the view shakes for
220 ms then fades for 160 ms using a native animation. A new press cancels that
animation without discarding the new contact; page destruction cancels both
feedback and reveal callbacks.
Press/release haptic callbacks go through main to the existing board M5IOE1 PWM
driver. The board owns motor state, verified writes, bounded holds and stop retries;
pages never issue I2C calls or create a motor worker.

## Factory baseline and deliberate adaptations

The baseline is M5Stack's `M5StopWatch-UserDemo` at
`6b4aa125288b6fe9dca661f10159f6e1e5ee785c`. Dependencies are pinned in
`frameworks.json` and `dependencies.lock`: ESP-IDF 5.5.4, LVGL 9.5.0,
Smooth UI Toolkit 2.12.1, Mooncake 2.3.3 and M5GFX 0.2.19.
Source-derived drivers and patches retain the factory MIT notices.

The CO5300 native framebuffer is 468×466, matching the factory HAL. Visible
panel specification remains 466×466. Quarter turns swap framebuffer dimensions.
Raw CST820 coordinates enter LVGL; LVGL applies rotation once. The board adapter
translates M5's quarter-turn numbering into LVGL's convention. Observation markers
use the same transform; diagnostic injection reverses it before entering LVGL.
The Arduino scale/offset experiments are not compiled into this application.

Display flushes use bounded `pushImage` row chunks, not `setAddrWindow` followed
by `writePixels`. In pinned M5GFX 0.2.19, the latter updates framebuffer pixels
but records reversed or unswapped dirty bounds when rotated; the physical panel
can remain frozen even though captures and button dispatch work. The factory
demo's rotation-0 path did not expose this. Keep the driver-level rotated-flush
regression, and do not treat framebuffer captures as proof of panel refresh.

The application preserves Free/Default/180° settings, stable IMU orientation,
release-based controls, all six pages, and observation-only Touch test. The
factory demo's audio, automatic NVS erase, unrelated apps, and filesystem layout
are not imported. RTC reads are validated and do not silently invent a date.

## Storage, flashing and batch compatibility

The existing six-partition map is retained exactly. In particular, `ffat` stays
at `0x610000`, size `0x9e0000`, holding LittleFS. Profile records retain the
`INITCF01` container, SHA-256 verification, native RGB565 pixels and atomic rename.
The reader accepts original v1 metadata and v2 metadata with an optional company;
company-free writes remain v1. Agenda bookmarks use a separate versioned NVS u32.
Brightness/orientation/network keys in `conference_ui` and clock offset in
`conf-clock` retain their names and types. Neither boot nor mount failure formats
storage or erases NVS.

```sh
./scripts/setup-factory.sh       # install SDK/fetch pinned dependencies once
./scripts/build.sh               # native ESP-IDF build
./scripts/test.sh                # host regressions, native driver/services/UI tests
./scripts/flash.sh --no-build /dev/cu.usbmodemYOUR_PORT
```

Native build output is under `.build/factory/`. The build stages three artifact
aliases under `.build/firmware/` for the existing reviewed upload wrapper.
Their `.ino` filenames are compatibility names, not an Arduino build. The wrapper
still uses Arduino CLI/ESP32 core 3.3.10 for USB discovery, partition preflight and
esptool upload; M5Unified and Arduino are absent from the native application.

Every upload verifies the current partition sector before writing, follows the
same USB identity, provisions a fresh computer clock, verifies an advancing RTC,
and requires `conference-factory-3` with offline radios and available storage.
The explicit first-install filesystem option remains guarded and is never used
on the provisioned development board. See [the batch runbook](conference-clock.md).

The old Arduino build is available through `scripts/build-arduino-legacy.sh`.
Its output is historical and does not satisfy the native readiness build check;
do not mix artifacts from the two builds in a batch.

## Verification and limits

Host checks exercise the actual native profile record code against the old
writer/reader, corruption and failed writes, guarded initialization, the actual
portal browser script, the production RTC helper, and the production rotation
adapter against real LVGL. Simulated input and framebuffer captures validate
software behavior; they cannot establish physical fingertip alignment.

Device installation and acceptance results are recorded separately in
[factory verification](factory-verification-2026-09-17.md). Do not transfer
measurements from the retired calibrated Arduino build to the new raw-input path.
The published daily agenda and `conference-factory-3` checks are recorded in
[schedule verification](schedule-verification-2026-09-17.md).

Framework reuse, remaining integration issues and justified custom adapters are
documented in the [framework audit](framework-audit-2026-09-17.md). Its findings
are recommendations and have not yet been applied.
