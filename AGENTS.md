# Project guidance

## Start here each session

- Read [hardware.md](docs/hardware.md) for the exact board, display geometry,
  pin mappings, orientation calibration, memory layout, and USB quirks.
- Read [decisions.md](docs/decisions.md) for the user's choices and the failures
  already resolved. These notes describe the current design, not new feature work.
- Use [development.md](docs/development.md) for builds, device checks, diagnostic
  protocol, and recovery. [architecture.md](docs/architecture.md) explains the
  firmware/backend contract; [privacy.md](docs/privacy.md) explains storage.
- If `.local/project-context.md` exists, read it for this workstation's backend,
  original backup, and private verification locations. Keep it out of Git.
- Check the current branch/worktree and connected port before making changes.
  The recorded hardware baseline is dated; it is not a live device status check.

## Established constraints

- Canonical source is `chantastic/stopwatch`: firmware at the repository root,
  browser installer in `web-flasher/`, and Alto website under `site/`. Tested
  changes go directly to `main`; do not open pull requests unless the user asks.
  Read `CONTRIBUTING.md` for checks and `docs/site-promotion.md` for publication.
  Alto's managed `stopwatch` Git repository remains a publication target. The
  retained `scripts/promote-site.sh` belongs to the former WorkOS review workflow
  and cannot publish this repo; a replacement publisher is pending. A source
  push or device flash does not deploy the website.
- Current approved public firmware files remain in the original GitHub release
  repository, pinned by exact hashes in `site/src/release-config.json`. Moving
  source ownership does not change those files or their download URLs. New firmware
  needs an explicitly qualified release before updating the website pins; never
  promote an untested binary because its host checks pass.
- Active firmware is the native ESP-IDF project in `firmware/factory_badge/`.
  Read [factory-stack.md](docs/factory-stack.md) for the LVGL/Smooth/Mooncake
  architecture, per-page views, hardware boundary and pinned dependencies.
  `firmware/devices_badge/` is the retained Arduino implementation and shared
  portable helpers; do not implement new views there.
- Current product is the **offline conference badge**. Read
  [conference-badge.md](docs/conference-badge.md) and
  [conference-clock.md](docs/conference-clock.md). The six-page UI supersedes
  the connected badge/voice controls below; that previous application is retained
  in `legacy_connected_app.h` and Git history and is not compiled into this build.
- The September 17 supplied init() designs use exact exported brand masks in
  `main/ui/assets/` and the supplied intro clip through native LVGL GIF playback.
  Keep these shapes intact. Follow the Figma text roles in `ui/fonts/README.md`:
  IBM Plex Mono Medium names, Regular company, Medium agenda metadata, SemiBold
  captions, and Inter Medium for Suisse Intl Medium. Use the pinned
  native font converter in `scripts/generate-design-fonts.py`.
  Company is optional manual profile data. Schedule bookmarks are removed from
  the UI; retained legacy storage is not read into a visible bookmark control. A configured profile hides chrome but retains
  physical/horizontal paging and tap-to-QR. Do not substitute the mockup's sample
  QR for real phone credentials or a pending invitation destination.
- Normal operation has Wi-Fi and Bluetooth off. Temporary setup is AP-only,
  with manual name/social fields and image upload. Never load authenticated
  identity caches into the manual conference model or require a cloud login.
- Physical screen-left/right pushers are previous/next at rotations 0 and 2;
  loop-up rotation 2 means blue previous / yellow next. Both open setup; either
  exits setup. Preserve completed taps, drag arbitration, and orientation filtering.
- Settings uses native LVGL menus: Brightness, Orientation, Date / time, Hack this device,
  Connect phone, and Reset. Calendar/time roller/UTC-offset edits apply only on Save,
  through the main-owned checked clock service. Hack QR URL is https://workos.com/init/badge.
  Settings is the final page, after Hack this device. Brightness defaults to 60%
  with a visible minimum and delayed persistent saves. Display clocks use h:mm
  AM/PM; UTC storage and schedule calculations remain unchanged. Orientation is exactly Free (automatic),
  Default (rotation 0), or 180° (rotation 2); new/reset badges use Default.
  Preserve explicitly saved modes and apply changes after touch release.
  Phone setup synchronizes a fresh browser clock independently of profile Save
  and Cancel. Settings-launched setup returns to Settings. The published agenda
  in `factory_badge/main/schedule.h` repeats daily using the badge's local clock:
  current rows say On now, passed rows are dimmed, and invalid time marks neither.
  Schedule opens with the current event centered between the arrows (first event
  if none is current); native LVGL center snapping settles swipes on an event.
  Clock updates preserve the reader's position.
  Schedule cards use 18px times and 24px wrapped titles, a 9px time-to-title layout gap,
  no bylines/location rows, no bookmarks and no subtitle. The active card has a stepped
  outline and right-aligned On now. Use RGB565-neutral card levels: #181818 for
  current, #101010 upcoming, and #101010 before the past-state 50% opacity.
  Times use #E7E7E7, slightly dimmer than white titles.
  Keep input/setup/save deadlines on monotonic time, independent of clock changes.
- Settings → Reset badge requires a fresh confirmation tap. Clear only the manual
  conference profile/photo/links, bookmarks, UI preferences and After Dark unlock;
  retain clock and legacy records. Never exercise confirmed reset
  on a personalized device merely to test it. Use synthetic storage fixtures.
- After Dark is always a visible page; retain all six pages/dots and stable IDs.
  Its locked page hides the event name and says
  `tap the code to reveal a secret invitation`. Enter Morse
  `init` (`.. -. .. -`) with taps/holds across the page; arrows remain navigation
  and pushers never enter Morse. A successful code plays the existing intro GIF
  once with `You're` → `Invited` → `To`, then slides the supplied event artwork upward into the invitation.
  Match the seven symbols `..-...-` regardless of pause timing. Holds shorter
  than 400ms are dots; longer holds are dashes, with no upper duration limit.
  Keep displayed spaces at 400ms (letter) and 1400ms (word), for visualization only.
  Correct prefixes do not expire. Incorrect attempts clear after 2.5 seconds
  released, with the existing brief shake/fade. No vibration or pressed-background
  color changes during Morse entry. Accept the correct final dash on release.
  Dragging more than 10 pixels, a cancelled press, navigation, modals or rotation
  cancel code entry. Long holds on this surface are Morse dashes, not ordinary
  taps. Valid local date/time at or after October 7, 2026 at 13:30 also reveals it,
  including later mornings on a fresh badge. Main owns the persistent versioned
  NVS unlock; timed reveal must not move the current page/scroll. The actual invite
  URL is `https://luma.com/developers-after-dark`. The unlocked page centers the
  supplied 300px artwork with 16px rounded corners and `Tap for QR code` below.
  Completed taps on artwork/caption toggle a centered 240px live QR with
  `Tap to return`. No vertical scrolling; drags/holds must not toggle it. The native 450ms slide follows the
  three-word reveal, with both artwork and QR hidden during those words. See
  `docs/conference-badge.md` for timing and persistence rules.
  USB `after_dark_reset` may clear only this latch when the user explicitly asks
  to retry it without clearing their profile. Confirmed Reset badge also clears
  the latch; the unchanged clock cutoff can still reveal it again when due.
  `observe_taps` is an explicit, at-most-120-second physical-contact RAM recorder,
  limited to After Dark. Its timestamps describe sensor samples, not exact LVGL
  decisions; keep captures/logs private and stop recording after the requested trial.
- Settings → Orientation → Touch test is an observation-only modal: five white crosshairs,
  live purple sensor marker retained on release, and current rotation frozen
  without changing the saved mode. Either pusher or the chord returns to Settings.
  Never apply calibration, save touch data, or start networking from this test.
  Settings → Orientation → Calibrate touch is a separate measurement wizard.
  It collects real raw contacts at nine fit and five holdout targets in rotation 0,
  saves an ESPtember-compatible affine map only after validation, and preserves
  the prior map on cancellation. The board loads validated espt-touch/record v1/v2
  records and applies their full mapping before LVGL owns touch rotation.
  Never calibrate from USB-injected contacts or compile per-device coefficients.
  The factory board adapter translates M5 rotation numbering once and keeps
  its 468×466 native display geometry. Do not reintroduce the retired Arduino
  scale/offset trial. See hardware.md for its historical fit limitations.
  Label simulated inputs; they cannot verify physical sensor alignment.
- Build once for a batch; each flash provisions and verifies a fresh hardware RTC
  time and storage/radio readiness. Preserve the partition layout and user state.
  Every flash must first compare the current partition sector with the compiled
  map. Factory/different layouts require a separately authorized migration.
  Explicit filesystem initialization applies only to the verified existing map
  and erases ONLY `ffat`; never run it on the provisioned development board or add
  automatic format-on-error.
- Active gateway source is `/Users/chan/Developer/chan-services/apps/devices`.
  The private `chantastic/chan-services` monorepo owns service deployments; read
  its `docs/deployment.md` before gateway work. `gateway/` here and the standalone
  `../devices.chan.dev` repository are retained historical snapshots. Do not
  implement or deploy gateway changes from those copies. Firmware stays here.
- The retained connected prototype intentionally connects to chan.dev Production
  Devices application. Any signed-in Production user uses their own account and
  personal connections. Shared Auth, Pipes integration, Social and the narrow
  Devices gateway share the services monorepo but retain separate Workers and
  private bindings. Do not build a separate authentication stack or move device
  workflows into Auth or Social.
- Hardware is an M5Stack **StopWatch**, ESP32-S3 with 16 MiB flash and 8 MiB OPI
  PSRAM. Use the pinned build configuration; do not trust USB board-name guesses.
- Keep public client IDs and HTTPS endpoint configuration separate from secrets.
  Wi-Fi credentials, session tokens, and cached profiles are provisioned or saved
  on the device, never baked into source.
- Keep full-flash backups, NVS dumps, live diagnostic logs, screenshots, and
  generated binaries outside Git. Build and test output goes in `.build/`.
- `scripts/build.sh` compiles; `scripts/test.sh` runs the host checks on macOS.
  Use the dependency versions in README.md.
  LVGL and hardware state belong to the main task; HTTP/DNS run on workers.
  Phone clock writes are marshalled back to main. Update view models through
  callbacks; pages must not access hardware, NVS, Wi-Fi, or profile storage.
- `scripts/flash.sh PORT` rebuilds and uploads application components. Firmware
  changes should preserve the partition layout and saved user state.
- Avatar and profile behavior must remain dynamic. Avoid static personal assets.
- Historical connected application: one `init()` ASCII layout per account;
  blue changes accounts and yellow is reserved. Both open Settings. Those rules
  are not the conference controls. Touch uses completed taps on release.
- The historical X replies app opens from Settings and owns its blue-hold recording
  gesture. Read [voice-replies.md](docs/voice-replies.md) before changing this flow.
  Sending requires a fresh tap on Send after reviewing every transcript page.
  Preserve unresolved send receipts across Back, sign-in changes, and restart;
  never automatically retry a post or replace an uncertain receipt key.
- Microphone capture and upload stay on workers. No recording at startup; no
  permanent provider keys on the board; no audio or transcripts in Git/logs.
  Tests may transcribe a bounded recording, but a real public reply requires the
  user's exact approved target/content or their deliberate Send on the device.
- Keep HTTPS waits on the worker and display/state ownership on the main task.
  Restore cached profiles before networking; a cache must never authenticate a user.

## Keep this memory useful

- When hardware behavior, controls, integration contracts, or verified limits
  change, update the relevant guide in the same change. Keep this file concise.
- Label measured results with their source version/date and distinguish host
  simulations from device observations. Preserve limitations and failed approaches
  that explain a current implementation choice.
- Do not copy transcripts, historical artifacts, or raw diagnostics into these
  guides. Record the durable finding and link to current code or a private locator.
