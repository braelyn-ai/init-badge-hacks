# Offline conference badge

Factory-stack migration, September 17, 2026. Active entrypoint is
`firmware/factory_badge/main/main.cpp`; see [factory-stack.md](factory-stack.md)
for the current modules and migration verification. The behavior below remains
the product contract. Arduino-specific rendering/input details and the dated
verification reports describe the prior implementation, not the migrated build.
The prior connected AuthKit/voice application is retained in
`legacy_connected_app.h` and Git history. It is not initialized or linked into
the conference application. No gateway changes are required.

## Pages and controls

The five primary pages always wrap in this order, including the locked After Dark
page:

1. **init()**: the supplied 10-second cross-pattern loop behind the exact init() mark.
2. **Schedule**: vertically scrollable published init() agenda, repeating daily
   in the badge's local time. The current block says **On now**; passed blocks
   are dimmed. Times, wrapped titles and available speaker details stay visible.
3. **Party**: titled Party even while locked; its locked page says `tap the code to reveal a secret
   invitation`, with the event name hidden until reveal. Enter Morse `init` by
   tapping/holding anywhere on the page except
   its navigation arrows, or wait for the timed reveal below. Successful code
   plays the intro loop once with `You're` → `Invited` → `To`, then reveals the
   rounded event artwork. The invitation QR below opens https://luma.com/developers-after-dark.
4. **Badge**: square manual photo, name and optional company. The Badge title stays visible on configured and QR views; a configured face
   hides navigation chrome, as in the supplied design. Tap it for the selected
   social QR (or setup for an empty account); swipe the face vertically through
   GitHub, X/Twitter and LinkedIn, including empty slots.
5. **Settings**: includes the Hack this device QR (`https://workos.com/init/badge`), battery percentage, brightness, local date/time, phone setup,
   touch test, reset badge, and orientation. Each category opens a native LVGL submenu.

The factory 468×466 framebuffer uses the supplied pixel chevrons and small init()
mark, with five square page indicators whether the invitation is locked or
unlocked. There is no page-name footer or clock overlay. Settings retains the actual clock/battery;
useful content and QR quiet zones remain inside the round aperture. Name display truncates at UTF-8 boundaries; the complete accepted value
remains editable in setup. Standard firmware fonts have limited glyph coverage.

Screen-left/right pushers page backward/forward at rotations 0 and 2. In the
lanyard-up rotation 2, **blue is previous and yellow is next**. In rotation 0,
yellow is previous and blue next. At quarter turns, the pushers are top/bottom;
use the stock yellow-previous/blue-next convention. Side arrows also accept a
completed tap, and a horizontal swipe pages. Both pushers open setup from the
primary pages. Either pusher or the on-screen Back/cancel button closes setup.
Inside Touch test, either pusher or both together instead return to Settings.

Schedule drag scrolling is clipped above the footer; the last row is reachable.
Badge vertical swipes change only the network slot. The selected network is saved
after a short debounce; primary pages start on init() at reboot. Empty accounts
never inherit another slot's QR. Tap an empty badge to configure; tap a configured
badge to open its QR, then tap the QR to close it. Both pushers can edit any configured profile.

Ordinary gestures dispatch on release. A drag that returns to its start is still
not a tap. Long holds do not activate ordinary taps; the locked After Dark code
surface deliberately accepts them as Morse dashes. Setup is modal: drags cannot
change the underlying page, network, or schedule position. Rotation uses the established IMU
axis mapping/filter and stays stable throughout touch.

## After Dark code and timed reveal

The September 17 follow-up replaces the earlier hidden page and physical-pusher
code. After Dark always appears in navigation. The subsequent usability update
replaces the small square target with the whole page and a prominent prompt:
`tap the code to reveal a secret invitation`. Its navigation arrows retain their
normal actions. The rest of the locked page accepts `init` in Morse:
`.. / -. / .. / -` (two taps; hold then tap; two taps; hold). Use 200 ms taps,
600 ms holds as a comfortable starting point; pauses only affect displayed spacing.
Each registered symbol appears as a dot or dash above the prompt; a recognized
letter gap reserves the next letter's space, and a word gap is wider again. Only
the entered symbols are shown, never decoded letters or the answer. The correct
final hold reveals the invitation immediately on release, without a final pause.
An incorrect attempt ends after 2.5 seconds with no contact; a correct partial
sequence waits indefinitely. Its
visualization shakes for 220 ms and fades for 160 ms, then clears. The next press
can interrupt this feedback immediately and starts a fresh attempt. Physical
pushers retain their normal paging and setup behavior and cannot enter the code.

On successful code entry, the front-page GIF backs the native words `You're`,
`Invited`, `To` for about 700 ms each. After the final word, its decoder/timer is
released and the supplied event artwork/QR viewport slides upward over 450 ms.
The intact 300×300 artwork is centered horizontally with its top at y=100, clipped to 16px rounded
corners, with `Tap for QR code` beneath it. A completed tap on artwork or caption
replaces the poster with a centered 240×240 live-event QR and `Tap to return`.
Another tap restores the poster. There is no vertical scrolling; drags and long
holds cannot toggle the QR. Model updates retain the current view. Re-entering
an already unlocked page shows the poster without replaying the code-success
sequence. Navigation destroys all page-owned animations and temporary QR state.

Recognition matches the seven symbols `..-...-` without checking pauses or
letter boundaries. Holds shorter than 400ms are dots; all longer holds are dashes.
There is no total-attempt deadline and a correct prefix does not time out.
Displayed spaces still appear after 400ms and widen after 1400ms; these are purely
visual and never invalidate the sequence. Short or long pauses are equally valid.
Incorrect prefixes keep displaying later registered symbols and
cannot accept a correct-looking suffix before the inactivity reset. Input is
bounded to 32 ASCII symbols/spaces; overflow cannot unlock. Each release renews
the inactivity deadline, and a held contact never triggers the idle restart.
Native LVGL flex layout spaces/wraps the marks and owns the feedback animation.
A drag more than 10 pixels on either axis, lost press, navigation,
setup, Touch test, reset or rotation discards progress. A contact already held
when entering the page must lift before it can start a new code. Long holds are
accepted here rather than cancelled by the ordinary completed-tap helper.
These are monotonic timers, unaffected by phone/USB clock sync. USB `button`
actions cannot enter Morse; simulated touch sequences exercise software behavior
without proving physical sensor alignment.

Morse entry has no vibration and no pressed-background color change. Registered
dots/dashes provide the contact feedback. The unused board motor safety adapter
and its hardware diagnostics remain available, but this UI never activates it.

The automatic reveal has a fixed date/time cutoff: **October 7, 2026 at 1:30 PM
in the badge's configured local time**. Any valid clock reading at or after that
instant unlocks it, including a fresh badge first started the next morning,
midnight, or a later date. Earlier dates never trigger it, even after 1:30 PM.
The agenda still repeats daily; this reveal does not. An unset clock keeps the
invitation locked and its code surface visible, so Morse still works.
The timed reveal changes the invitation content without changing the current
page, scroll position, or modal. Settings remains the last page. Stable internal
IDs remain 0–5; ID 2 and its page indicator are present before and after reveal.

Either reveal latches in the separate versioned `conference_ui/after_dark_v1`
NVS byte, with the exact unlocked value `0xA1` (`0xA0` is locked; missing or unknown
values also stay locked). It is saved promptly; failures keep the in-memory
invitation unlocked and retry after five seconds. A committed unlock survives
restart, midnight and clock corrections. Confirmed Settings → Reset badge clears
it, including any pending unlock save. Ordinary reflashing preserves it.
The reset keeps the clock, so the automatic reveal still applies at or after
the cutoff. A loss of power before a successful save can
lose a Morse unlock. This is an Easter egg, not a security boundary.

The policy lives in `after_dark_unlock.h` and recognition in `morse_unlock.h`.
The invitation view feeds native LVGL press/release durations to the recognizer
and invokes a callback only for the complete word. Main applies the persistent
unlock, checked clock policy, NVS writes and UI model updates. Views never read
the wall clock or storage directly.

## Settings and current schedule item

September 28: Settings uses native LVGL menu navigation with six buttons:
Brightness, Orientation, Date / time, Hack this device, Connect phone, and Reset.
Brightness and orientation changes retain delayed persistence. Touch test lives
under Orientation. Date / time opens a Calendar, hour/minute/AM-PM rollers, and
15-minute UTC offset controls; only Save writes the checked main-owned clock
service. Leaving the menu discards unsaved edits. Phone clock sync remains available.
Reset opens the existing confirmation modal. Both Hack QR views use
`https://workos.com/init/badge`.


Brightness applies immediately in ten-percentage-point steps, bounded to 10–100%
with a 60% default. Existing saved brightness remains valid. The minimum maps to a nonzero display level. Brightness and
orientation share one versioned NVS value in `conference_ui`; writes coalesce
after 1.2 seconds without another change. Settings says `Saving settings...` above the pagination dots
while pending, and a failed write stays pending with a five-second retry.
The existing selected-network preference remains separate. Changes made just
before power loss may not have reached the delayed save yet.

All displayed clock times use `h:mm AM/PM`, including midnight (`12:00 AM`) and
noon (`12:00 PM`). Settings displays `YYYY-MM-DD h:mm AM/PM`; its date and
agenda calculations still use the same local offset from UTC.

### Reset badge

The Settings reset row opens an explanation and **Reset badge / Cancel** buttons.
Only a fresh completed confirmation tap queues the reset; drags, holds, opening
the screen or pressing a pusher cannot confirm it. Either pusher cancels before
confirmation. While the worker is running, navigation and duplicate requests are
blocked; the reset view holds the current orientation.

The existing service worker atomically saves an empty manual profile, clearing
name, company, photo and social URLs. Main then writes default conference settings:
60% brightness, Default orientation, first network, no saved agenda bookmarks
and a cleared After Dark unlock. It also removes any saved Wi-Fi override, so the
badge returns to the built-in event network. It discards any pending unlock save so it cannot
restore the old invitation state. The clock, legacy records and partition map
remain. A clock at or after the automatic reveal cutoff can unlock it again.
This is a logical badge reset, not a secure erase or factory firmware restoration.

A failed profile write preserves the previous record and requires an explicit
Retry. NVS preference keys are separate writes: if a later setting write fails,
the already-cleared profile and any partial preference changes remain. The screen
reports that partial result and offers Retry to finish; it does not report success.
After success, Done returns to the empty badge. No reset occurs during startup,
ordinary flashing or opening/cancelling the confirmation.

The [meeting follow-up verification](meeting-followup-verification-2026-09-17.md)
records reset/clock/photo tests and the preserved-data device check.

September 17 reset follow-up: the production coordinator host fixture verifies
clearing saved and pending unlocks, all four NVS key failures and commit failure,
explicit retry, and preserved clock/legacy records. The native LVGL sanitizer
fixture verifies the updated explanation fits and revisiting After Dark after
reset shows its locked prompt with no event title. These are host simulations;
the full destructive reset is not exercised on the user's badge for verification.
The guarded flash and live open/cancel check passed with saved indicators intact,
no preference writes, a valid clock and offline radios. Application SHA-256:
`161924e41bd70152d40650618d0d1d4c33ea912581e0045df4a158789d8b6fee`.
Private evidence is under `.build/reset-invitation/`.

Orientation has exactly three choices: **Free** resumes calibrated automatic
rotation in all four directions; **Default** fixes the stock rotation 0;
**180°** fixes rotation 2, the lanyard-up pose. These fixed choices were swapped
after the user's September 17 device feedback. Default is a named fixed pose, not the orientation
at the moment of selection. New badges, missing/invalid preferences and Reset
badge use Default, so the first badge frame boots at rotation 0. A saved mode
survives restart, including a deliberate selection of Free or 180°. Startup in Free
begins at rotation 2 until fresh IMU readings settle. Switching modes clears old
filter candidates and waits until navigation and physical touch are fully
released before rotating. Pusher direction follows the displayed orientation.

September 17 verification (application SHA-256 `0cb6315b411e66a2899db1415e66a3bc9436ae8d9dadf68e76038ed49cdb8d33`):
host checks cover new, invalid-storage and reset defaults, plus preservation of
saved Free/180° choices. The connected badge retained Default at rotation 0 after
restart, with its other saved state intact. No profile reset was performed on
the personalized device. Private evidence: `.build/default-orientation-verification/`.

Battery is the percentage reported by the board driver, refreshed on Settings;
an unavailable reading is labeled honestly. No remaining runtime or charging
claim is inferred. Date/time uses the shared clock with its saved display offset,
or `Date / time not set` when invalid. **Connect phone** opens the same temporary
local setup used from Badge. Setup launched from Settings returns to Settings
after Save, Cancel or timeout; successful setup launched elsewhere retains the
prior return to Badge, and cancellation leaves its launch page selected.

The active agenda is `firmware/factory_badge/main/schedule.h`, sourced from
[workos.com/init](https://workos.com/init) on September 17, 2026. Its nine blocks
start at 8:00 AM, 9:30 AM, 11:00 AM, 11:30 AM, 12:30 PM, 1:30 PM, 3:00 PM,
3:30 PM and 5:00 PM. Only the keynote currently has an assigned speaker;
unannounced program speakers stay TBA rather than being inferred from the
separate speaker list.

At the user's request, the agenda intentionally repeats **every day** instead
of being restricted to October 7. Current local minute is calculated from UTC
plus the same saved offset as the badge clock. Starts are inclusive; the next
start ends each block. The final Happy hour block stays current until midnight
because no end time is published; its detail says `End time not listed`.
Before 8:00 AM every block is upcoming. Midnight resets all rows to upcoming.
An invalid clock selects no current/passed rows.

On entry, the schedule centers the current block between the navigation arrows;
before the first event or with an invalid clock, it centers the first block.
Vertical swipes use native LVGL center snapping, so the event the reader stops on
settles at that same center, including the first and last events. The original
list clipping bounds remain intact. Subsequent clock changes update the highlight
and dimming in place without moving the user's reading position. Titles and
details wrap; row heights are based on their content.
The older absolute-UTC helper in `firmware/devices_badge/conference_schedule.h`
is retained only for the Arduino application and its historical tests.

## September 17 supplied design implementation

Native LVGL still owns labels, image assets, QR generation, scrolling, hit testing,
buttons and animation. Smooth/Mooncake retain scene and application ownership.
Exact logo/arrow/bookmark exports and their packaging are described in
`firmware/factory_badge/main/ui/assets/README.md`. The intro uses LVGL's built-in
GIF widget for a native 468×466, 60-frame, silent 10-second conversion of the
user's loop, with a small grayscale palette. Only the intro owns the decoder;
leaving it deletes its timer and buffers. No new video player or filesystem
partition is introduced. See the [loop provenance](../firmware/factory_badge/main/ui/intro-loop.md)
and [font provenance](../firmware/factory_badge/main/ui/fonts/README.md).

The supplied Figma file replaces the initial raster-based font guesses: IBM
Plex Mono Medium24 for attendee names, Regular20 for company, Medium12 for agenda
times/details, and SemiBold12 for supporting captions. Inter Medium approximates
Suisse Intl Medium at the source sizes, including 12px orientation options.
Native LVGL padding and line spacing preserve the source line boxes rounded to
whole display pixels; its integer tracking rounds -0.24px/+0.3px to zero. Fonts
use LVGL's standard converter and native rendering, with pinned sources, weights
and Latin-1 subsets. Exported brand marks remain images, independent of typefaces.

Schedule cards show published times in 18px IBM Plex Mono and wrapped titles in
24px Inter, with a 9px layout gap after the time row. They have no bylines, location labels,
bookmark controls or subtitle. Current sessions use the white stepped border
and a muted, right-aligned `On now`; passing time preserves the reader's scroll.
Saved bookmark records from older versions remain unused by this view.
Current backgrounds use #181818, yielding neutral RGB565 output (24,24,24).
Upcoming cards use #101010 (16,16,16). Past cards use #101010 before 50% opacity,
yielding (8,8,8); this avoids
the previous quantization-induced green tint. The viewport starts at Y=100 and
extends to Y=380, reclaiming the removed subtitle's space. Native center snapping
continues to align the selected card with the navigation arrows.

The supplied phone/invitation QR graphics both encode the customization website.
Phone setup continues to generate real local Wi-Fi credentials. The invitation
uses a generated QR for https://luma.com/developers-after-dark after unlock.
The visible locked code surface and persistent reveal by code or clock follow
the current policy above.

See the [September 17 design verification](design-verification-2026-09-17.md)
for host/device results and remaining physical checks.

## Touch alignment test

Open **Settings → Orientation → Touch test** to compare five white crosshair targets with the
live purple position reported by the touch sensor. Touch or drag over the top,
center, bottom, left and right targets. The final marker and coordinate readout
remain visible after release so the finger does not obscure the result.

The current display rotation is held for the duration of the test without
changing the saved orientation mode. Either physical pusher, or both together,
returns to Settings. Choose **Default** or **180°**, let the display rotate, then
reopen the test to compare those poses. The test does not calibrate the sensor,
save touch data, or start Wi-Fi or Bluetooth.

The live marker uses the factory CST820 sample and the same LVGL rotation as
normal input. The retired Arduino scale/offset fit is not applied. See the
[hardware notes](hardware.md#combined-offset-trial) for that historical trial.
The test does not create or save calibration.
Serial `touch` input exercises the test display but is labeled **Simulated input**.
It verifies dispatch and rendering, not sensor alignment. Physical alignment
and any remaining offset still require the user's observation on the device.
`touch_test_status` returns active/pressed/sample flags, sensor-versus-simulated
source, corrected screen/raw coordinates, held rotation, `scale_trial`,
`touch_model` (`factory-native`, with `scale_trial:false`), and an optional valid hex nonce.
It reads only the temporary test state and does not activate the test.

## Local customization

Setup creates a temporary password-protected hotspot with a per-device SSID.
Scan its Wi-Fi QR, then use the captive page or `http://192.168.4.1`.
The confirmed open event network `init() attendee` (blank password) is compiled
into firmware as the default, so every badge can refresh at the event without
setup. The page pre-fills the network in use. Since October 1 there is one
**Save badge** for profile, photo choice and Wi-Fi together (separate Wi-Fi
save/test buttons and `/wifi/*` endpoints were removed after attendees' Wi-Fi
and photo choices were silently dropped). A changed network name, or any typed
password, is stored in this badge's NVS without joining; an unchanged name with
a blank password keeps the saved network, because the page never receives the
saved password. Saving the event network just clears any override. **Use event
Wi-Fi** only refills the fields. Personal and test networks are never compiled
in or committed. The Photo picker defaults to **Automatic**: with no saved photo,
the first handle (GitHub, X, LinkedIn) is fetched after setup. Ordinary badge use keeps the
station radio off. A USB-requested probe currently makes one certificate-checked
HTTPS GET to `https://workos.com/init`, reads at most 1 KiB, then disconnects,
whether the request succeeds or fails. The probe has no unattended timer or
background retry. It verifies the connect/fetch/disconnect path without adding
provider sign-in or profile import. Future refresh actions must use the same
bounded lifecycle. A local USB diagnostic may provide temporary Wi-Fi credentials
in RAM for one probe; they are not saved to NVS, built into firmware, or echoed
by status. Explicit fetches use the saved override or, by default, the event network.
On each page load, the browser automatically submits its current epoch and UTC
offset to a separate authorized clock endpoint. Clock status and Retry are
independent of Save badge. Each retry samples time again. A successful sync is
kept even if the profile edits are cancelled; failed sync shows an error and
does not silently save edits or close setup. See [the clock guide](conference-clock.md).
The committed name, optional company and all three canonical profile URLs prefill
the form. Name/company each accept at most 60 Unicode codepoints / 120 UTF-8
bytes without controls. A blank company remains blank on a configured face.
GitHub, X/Twitter and LinkedIn personal profile handles/HTTPS URLs are accepted;
unrelated hosts, extra paths, control characters, and excessive input are rejected.

Choose a locally available photo in the browser: JPEG, PNG, WebP, or HEIC/HEIF
when that browser supports it. The portal prepares and shows a square preview
before Save. Browser JavaScript converts it locally to JPEG at most 512×512 and
128 KiB. Decode, encoding and network waits have bounded error paths; failed
preparation blocks Save until a new photo is chosen or the change is cancelled.
If the captive Wi-Fi sign-in window cannot open photos, stay on the badge Wi-Fi
and open `http://192.168.4.1` in Safari or Chrome. Cloud-only photos need a local
copy because the badge hotspot supplies no internet connection.
The device independently checks the
JPEG size/dimensions, decodes in PSRAM, and stores a 160×160 center crop. Keep,
replace, and remove are explicit choices. An image upload is only staged until
Save badge commits all fields and the image together.

The phone receives a save/cancel acknowledgment before a three-second grace
period closes the AP. Connection loss during save is an uncertain acknowledgment:
check the badge or reopen setup to see the committed values. Cancel and timeout
discard staged changes. Setup also expires after ten minutes. Normal mode has
both radios off, including after save, cancel, timeout and reboot.

HTTP transport processes at most 2 KiB and one nonblocking socket operation per
loop tick. Headers have a five-second absolute deadline, the complete request
ten seconds, responses five seconds, and idle connections 1.5 seconds. These
bounds keep trickled uploads and stalled response readers from monopolizing the
input loop. Requests are checked before body allocation; multipart, chunked,
oversized, ambiguous, and unauthorized requests are refused. Save acknowledgment
grace begins once the response is queued completely or its connection fails.

## Storage and privacy

The existing `ffat` partition still holds LittleFS at offset `0x610000`, length
`0x9E0000`. A separate conference record stores manual fields plus RGB565 image,
with versioned bounds and SHA-256. Save writes a temporary record, syncs/closes,
reads/verifies it, then atomically renames it. RAM and form prefill update only
after that commit. A staged image is volatile. The loader refuses corrupt records.

Existing version 1 records load unchanged. A nonempty company writes version 2
metadata with the company appended; blank-company saves use version 1. Both
retain SHA verification, atomic replacement and the same photo/URL fields. Older
firmware cannot read company-bearing version 2 records; clear the company on the
new firmware before downgrading if the old reader must display the profile.

Conference source never reads legacy authenticated profiles, Wi-Fi passwords,
sessions, or pending voice receipts. Those previous records are preserved by
ordinary flashing. Manual profiles confer no authentication. Storage remains
unencrypted; images and profile fields belong on the device, not in source,
binaries, public logs, or screenshots committed to Git.

No automatic filesystem formatting occurs on mount errors. A batch unit without
usable LittleFS within the verified expected partition map can use the explicit option documented in
[the clock and batch guide](conference-clock.md). That opt-in initializes `ffat`
and destroys previous filesystem contents in that partition; never use it as
recovery for an attendee's damaged profile. Default flashing fails readiness
instead of reporting such a unit ready.
Factory layouts that differ are blocked before upload and need a separately
authorized migration. Filesystem initialization cannot migrate a partition map.

## Diagnostics and verification

Use one serial owner at 115200 baud. `status` returns only bounded system/state
metadata, plus a supplied valid hex nonce for freshness. `page` ±1, `button`
blue/yellow/both, and `touch` begin/move/end exercise the same navigation dispatch.
They simulate input; they do not constitute physical touch/button testing.
`capture_badge` emits the existing bounded RGB frame protocol and refuses the
setup screen, which contains temporary Wi-Fi credentials. Captures are private.

`setup_test` accepts an input-only ephemeral alphanumeric AP password for
repeatable local verification; it is never saved or echoed. `clear_manual_profile`
with boolean `confirm:true` clears only the conference record. It never erases
the filesystem, NVS, or legacy authenticated records. Do not clear attendee data
as part of routine flashing. These diagnostics have the same physical USB trust
boundary as application flashing.

`observe_taps` has `start`, `read` and `stop` actions and requires a valid nonce.
Start requires `duration_ms` from 1000 through 120000. It records only physical
contacts on After Dark, outside modals, into a 64-contact RAM buffer; it is off
by default, expires automatically and never writes storage. Reads drain at most
eight completed contacts, with a dropped-record count. Contact records include
sensor press/end timestamps, duration, previous-release gap, first/last displayed
coordinates, maximum movement per axis, and release/cancellation reason. Held
contacts at start or scope entry and USB-injected contacts are excluded. A lost,
expired or stopped contact is labelled as such rather than as a finger release.
Sensor polling and LVGL's separate 10 ms input cadence differ, so these records
establish physical timing rather than the decoder's exact internal decisions.
The response's `current_rotation` is the rotation at read time, not per-contact
metadata; use a fixed-orientation trial when interpreting stored coordinates.
Avoid screenshots during recording; stop after the requested trial and retain
the bounded observations only in private `.build/` output.

`after_dark_reset` is a deliberate USB-only test reset, requiring boolean
`confirm:true` and a valid hex nonce. It writes only the locked `after_dark_v1`
byte, then clears the in-memory unlock and any completed code gesture. Profile,
bookmarks, clock and other settings remain unchanged. It refuses active modals,
setup/reset work, or a clock already eligible for the automatic reveal; it never
changes the clock to bypass that policy. `AFTER_DARK_RESET` reports `ok`, current
unlock state, nonce and an error on failure. Use only when the user explicitly
requests relocking their badge without clearing their profile. Confirmed
Settings → Reset badge also clears the unlock along with the profile and settings.

`portal_status` reports transport counters, phases, bounded byte counts and close
reasons, with separate last-POST evidence so captive probe GETs cannot overwrite
it. It does not export request paths, headers, bodies, nonce or profile values.
`status` includes the AP client count, without client identifiers, plus brightness,
orientation mode, pending preference state/write count, page count, and current
schedule index. `page_count` is always five; `after_dark_unlocked` and
`after_dark_save_pending` report reveal/persistence state without user data.
A write count is per boot and is not a flash-wear measurement.

`scripts/verify-factory.py` captures all five pages, including the locked invitation
when applicable, and records their stable IDs in `captured_pages`. It never
changes the clock or enters Morse to increase capture coverage. Ordinary page
captures do not establish code-entry behavior or persistence.

Run `scripts/test.sh` and `scripts/build.sh`. Native checks cover real LVGL
rotation, UI input/rendering, RTC validation, and cross-version profile storage.
Retained Arduino host checks cover gestures,
scroll bounds, network isolation, URL/name/image limits, atomic-storage failures,
portal commit/cancel/timeout behavior, and fresh RTC/batch acknowledgments.
Settings checks cover brightness bounds/debounce, fixed modes, drag rejection,
and schedule UTC boundaries/corrections. The actual rendered portal script is
executed in host fixtures to verify fresh clock requests, offset signs and retry.
The older host suites remain regression checks for retained connected code.
Hardware verification results and limits belong in the dated report below;
compilation/host simulations alone are not proof of physical behavior.

## Verification record

September 18 standard-timing/vibration follow-up: portable Morse sanitizer tests
passed exact 200 ms-unit input, receiver boundary sweeps, single-word enforcement,
word spacing, eager release and the unchanged retry deadline. Native UI tests
passed nominal input and haptic edge/cancellation/hold/reveal cleanup. All five
board checks passed; the production motor controller was also sanitizer-tested
for startup clearing, bounded holds, timer wrap, ambiguous ON and failed-OFF retry.
The build and guarded flash passed. Live status confirmed motor configuration and
idle OFF. The badge was already unlocked, so its state was preserved and a
simulated press on revealed content stayed quiet. Positive motor operation and
subjective strength remain unverified by a physical trial. Saved indicators,
original page, clock/storage and offline radios were preserved; no preference
writes occurred. Application SHA-256:
`76fb766b3209e67b3d32b0d5ed5f83df0059f7120bb61602f78e5bf95e3bf073`.
Private evidence: `.build/morse-standard-haptics/`.

September 18 Morse feedback: sanitized portable tests passed timing boundaries,
all 8,192 seven-symbol/grouping combinations, eager final release, continued
incorrect-input display, idle reset, overflow and wraparound. The native LVGL
sanitizer suite passed actual mark rendering/spacing, mark-area touch pass-through,
shake/fade, retry during either phase, wrapped input, cancellation and cleanup.
The build and guarded flash passed. USB-simulated incorrect input appeared on the
development badge and cleared after its timeout; no successful code was entered.
Saved indicators/unlock were preserved, the original page restored, no preference
writes occurred, and clock/storage/offline-radio checks passed. These simulated
contacts do not establish physical fingertip timing or alignment. Application
SHA-256: `032c4d91eb3dedefd5119d7e5129bc465579358549d8102c4668e7eed5dfd941`.
Private evidence: `.build/morse-feedback/`.

The September 17 schedule-centering follow-up uses LVGL's native center snap at
display Y=233, matching the arrows. Sanitized native UI tests passed for all nine
events on entry, both swipe directions and end boundaries, invalid/pre-agenda
time, idle rendering, bookmarks and clock-update scroll preservation. The device
build and guarded flash passed; public framebuffer captures confirm the centered
card on entry and after USB-simulated swipes in both directions. Pre/post-flash
saved-state indicators matched, radios remained off, and the original page was
restored. Physical fingertip alignment is not established by these simulated
inputs. Application SHA-256:
`759e4eb78368350e3f128f3fe1015772c6aa5de6b58f2b2931a472c64463e591`.
Private evidence: `.build/schedule-center/`.

The September 17 touch-entry follow-up passed native LVGL tests with address and
undefined-behavior sanitizers, portable Morse/reveal tests, capture-script tests,
and the ESP32 build. Coverage includes short taps, long-press dashes, cancellation
on movement/navigation/modals/rotation, held-finger page entry, final-letter
silence, and exactly one unlock callback. All six pages remain available while
locked. Application SHA-256:
`51f57a9835f6d93cee5d201ca72b9611589ccb8b35c42251e1a89c83961c709c`.

That application was flashed and verified on the development badge. The badge
was already unlocked, so its unlock was retained; locked code entry was verified
in the host LVGL fixture, not with a physical finger on this badge. Public-page
capture, USB-simulated button navigation, clock/storage readiness and offline
radios passed. State remained unchanged during the bounded post-flash check.
Profile/settings indicators differed from the earlier pre-task observation.
The user subsequently confirmed using Settings → Reset badge to try code entry;
that earlier firmware cleared the profile/preferences while retaining the unlock.
The later reset follow-up now clears the unlock too.
The upload did not write NVS or FFAT, and the new boot reported zero preference
writes. Private evidence and the unresolved comparison are in `.build/touch-morse/`.

The original hidden-page/physical-pusher behavior and its host/device limits are
recorded in the historical [After Dark verification report](after-dark-verification-2026-09-17.md).
That report predates the visible locked page and touch-code follow-up; it is not
verification of the new touch interaction.

Current native ESP-IDF/LVGL results are in the
[factory verification report](factory-verification-2026-09-17.md).
The populated daily agenda is covered by the
[schedule verification report](schedule-verification-2026-09-17.md).
The reports below describe the retired Arduino implementation and are retained
as historical evidence, not acceptance of the new input or network code.

See the [Settings verification report](conference-verification-2026-09-17.md)
and the [earlier scaffold report](conference-verification-2026-09-16.md).
Private Settings captures/results stay under `.build/settings-verification/`.
Actual Settings persistence, rendering and input dispatch pass on the development
board. Phone clock synchronization and device profile save/edit/photo persistence
remain unverified: the bounded Settings attempt failed at workstation hotspot
association, while the earlier AP/browser failures are documented separately.
Production portal/profile/clock host fixtures pass; they are not a completed
phone or hardware profile test.

See [stock UI source research](stock-ui-reference.md) for the inspected framework,
versions and MIT licensing, and [factory stack](factory-stack.md) for the later
decision to migrate the runtime, board adapter and views to the factory stack.

### September 28 official invitation link

The unlocked Developers After Dark page now displays a generated QR for
`https://luma.com/developers-after-dark` and `Scan to RSVP`. The QR remains
hidden while locked and during the three-word reveal. Its 200-pixel white frame
provides at least four modules of quiet space around the code. Existing Morse,
timed reveal, persistence and navigation behavior are unchanged.

Native LVGL sanitizer checks passed, including locked/reveal visibility and
existing page/input regressions. The native ESP32 build passed. Both full and
circularly masked host frames decoded to the exact official URL. Private
verification artifacts remain in `.build/after-dark-link/`.

The guarded device flash completed with `UNIT_READY` and no storage initialization.
Fresh status preserved all saved profile/settings/unlock indicators and confirmed
clock/storage readiness, offline radios and advancing UI ticks. Full and circular
live framebuffer captures decoded the exact event URL. The badge was left on the
unlocked invitation for review. This is software QR decoding, not a physical
phone-camera scan. Application SHA-256:
`91328ab8bdb5be8e6710f8744a4ad162d3db4837f27be5747aa14fde55185302`.


### September 28 supplied invitation layout

The user supplied `Developers After Dark.png` and requested its layout and only
its text, while retaining the live event destination. The settled invitation
has two title lines, a 156×156 white QR frame at (155,195), and the sole footer
`Invite details coming soon` at label Y=367. Removed `You're invited` and
`Scan to RSVP` from the settled screen. The existing three-word reveal animation
remains transient; its prompt is hidden afterward. The QR still encodes
`https://luma.com/developers-after-dark`, never the reference image's QR.
Native fonts/chrome are retained on the 468×466 framebuffer; the supplied raster
is 466×466, so font rasterization and horizontal pixel geometry are not identical.
Native UI sanitizer tests and the ESP32 build pass. The host-rendered live-event
QR decodes at the new size. Evidence: `.build/after-dark-design/`.

Guarded flashing completed with `UNIT_READY`, without formatting storage. Full
and circular live-device frames decoded to the official event URL at the new QR
size. Clock/storage were ready; the badge was left on the invitation page.


### September 28 supplied event poster

Replaced the standalone text/QR layout with the user's official 800×800 artwork,
converted to a 300×300 native RGB565 asset. A 320×365 viewport at (74,54) centers
the artwork at (234,233); the QR starts at display Y=389, peeking 30px into the
viewport. Swiping up reveals its full 200×200 frame. The image is preserved as
supplied, including its date and sponsor logos; no extra captions were added.

Native sanitizer tests passed for word/reveal sequencing, slide completion,
poster centering, QR peek, actual LVGL swipe scrolling, preserved scroll on
model updates, modal/navigation cleanup and the existing Morse/input regressions.
The ESP32 build passed. The circularly masked host QR frame decoded to the live
event URL after scrolling. Evidence: `.build/after-dark-poster/`.

Guarded flashing completed with `UNIT_READY` and no storage initialization. Fresh
status preserved saved profile/settings/unlock indicators, with clock/storage ready
and radios off. USB-simulated native swipes kept the page selected and revealed a
live framebuffer QR that decoded within the round-screen mask. The badge was left
at the poster/QR peek. No physical-finger claim or hardware unlock replay was made;
this already-unlocked device retained its state. Application SHA-256:
`21df026dbb6e87aa638883bc60dcc72c850e89cab51588a269878dc06b93f8e8`.

September 28 rounded artwork: the 300px poster now uses a native 16px-radius
clipping container. Its source pixels, center, scroll layout and separate QR
remain unchanged. Native UI sanitizer checks passed and host rendering confirms
the clipped corners. Evidence: `.build/after-dark-rounded/`.


September 28 tap-to-QR follow-up supersedes peek-and-scroll. Native sanitizer
checks passed completed artwork/caption taps, return taps, drag rejection,
model-update retention and existing reveal/navigation regressions. The circular
host QR decoded to the official event URL. Evidence: `.build/after-dark-tap/`.
The ESP32 build and guarded flash passed (`UNIT_READY`, no storage initialization).
USB-simulated completed taps opened the official QR, which decoded within the
round-screen mask, then restored the poster framebuffer byte-for-byte. The badge
was left on the rounded poster. Physical finger interaction was not measured.

September 30 legibility update: the poster source is the user-supplied 2400×2400
artwork (same wording, date and venue), downsampled to the unchanged 300×300
RGB565 asset. The expanded QR now occupies the poster's exact 300px, 16px-radius
frame (x=10, y=0 within the invitation) with a 30px quiet zone, instead of a
centered 240px square. Host checks, the ESP32 build and a guarded flash passed
(`UNIT_READY`, no storage initialization). A USB-simulated tap on the device
showed the QR, which decoded within the round-screen mask, and a second tap restored the
poster byte-for-byte. Physical finger interaction and scanning with a phone were
not tested.


### September 28 simplified schedule design

Applied the supplied stepped-card spec with subsequent user refinements: larger
18px times/24px titles, no location data, bookmarks or subtitle, and neutral
RGB565 card backgrounds. Published agenda wording/times and speaker details are
retained. Host sanitizer tests pass all session boundaries, current-card centering,
first/last swipe limits, unchanged row geometry on clock updates, no bookmark
actions/assets, and exact neutral card pixels. Evidence: `.build/schedule-spec/`.
The native firmware build and guarded flash completed with `UNIT_READY` and no
storage initialization. Live framebuffer inspection confirmed (24,24,24) card
background pixels, and a USB-simulated swipe changed the schedule frame while
remaining on the page. Clock/storage readiness passed. The badge was left on
Schedule with the current event centered. This does not measure physical finger
interaction or subjective display color under ambient lighting.

September 28 spacing refinement: increased the time-to-title layout gap from
3px to 9px and removed all secondary bylines (including speaker/TBA/end-time
notes) from the cards. Agenda source details remain available but are not rendered.

September 28 tonal refinement: upcoming cards are one neutral RGB565 step darker
than the current item (#101010 versus #181818); times use #E7E7E7 while titles
remain white. Combined with the 9px title gap and absent bylines.
Native UI sanitizer checks and the ESP32 build passed for the combined spacing,
byline and tonal changes. Guarded flash completed with `UNIT_READY`, without
storage initialization. Live screen capture confirmed both (24,24,24) current
and (16,16,16) upcoming backgrounds. Clock/storage readiness passed; Schedule
was left open. Evidence: `.build/schedule-final/`.

September 28 Settings verification: native UI sanitizer tests passed, including
menu navigation, clock draft/save/failure, leap-day offset conversion, discarded
edits, and reset confirmation. The ESP32-S3 build and guarded flash passed
`UNIT_READY`. Device framebuffer captures verified the six menu buttons,
Calendar and both Hack QR destinations (`https://workos.com/init/badge`). Saved
profile/preferences/unlock were preserved. The all-pages verifier deliberately
refused this personalized board; focused checks captured only Settings/Hack.
Manual clock writes were tested with the host callback; physical touch mapping
was not recalibrated. Private artifacts: `.build/settings-menu/`.

### Touch calibration (September 28)

Settings → Calibrate touch measures nine orange-ring targets and
checks five independent targets. Hold each center briefly and release. The flow
uses real raw sensor contacts, fixes the screen at Default without changing the
saved preference, and ignores simulated touches. Either side button cancels or
exits; it restores the configured orientation. A passing fit/check is saved with
NVS readback and applied immediately. Failed/cancelled measurements retain the
old map. Existing ESPtember v1/v2 records load on boot; a new compact calibration
writes compatible v1 affine data. See `main/touchcal/README.md` for the distinction
from ESPtember's full local-warp exercise. This device data survives normal flashes
and badge-profile reset. USB `calibrate_touch` with a fresh nonce opens the wizard
when inaccurate mapping prevents using Settings; it does not supply measurements.

Validation: 1,052 ESPtember contract assertions, NVS loader/save-failure fixtures,
physical-only session fixtures, native UI sanitizers, and all five board tests
passed. The guarded ESP32-S3 flash passed UNIT_READY. The connected device reported
no saved record (version 0). The user completed the physical nine-target/five-check
run; the device saved and applied version 1. A subsequent reboot reloaded version 1
with profile, preferences and invitation unlock preserved. The holdout acceptance
threshold passed; a separate full-screen physical accuracy survey was not performed.
Private evidence:
`.build/touch-mapping/`.

September 28 forgiving Morse update: recognition compares only the seven dot/dash
symbols. Host tests cover all 128 seven-symbol patterns across all 64 short/long
groupings, zero/short/long pauses, minute-long pauses, extended dashes, visible
spaces, wrong-symbol retries, cancellation, bounds and clock wraparound. Native
UI tests cover visibly spaced entry with deliberately wrong grouping and a pause
longer than the old reset deadline, plus black pressed background and no haptic
callbacks. Existing reveal animation/QR and modal regressions passed.

September 28 page titles: the five-page order is init(), Schedule, Party, Badge,
Settings (IDs 0–4). The standalone Hack page is removed; Settings retains its
Hack submenu and live QR. Party and Badge now have visible titles matching the
Schedule heading. The badge QR panel starts below its title. Home remains the
untitled init() animation; Settings uses its native menu heading. Five dots and
all navigation/setup/reset returns use the new count and Settings ID.
Native UI sanitizer tests and capture-navigation tests passed for the five-page
layout. The resized badge QR still decodes in the host frame. Guarded device
flash passed UNIT_READY; USB navigation verified the five-page wrap and the
Settings Hack QR, preserving calibration, profile flags and invitation unlock.
Private screenshots/logs: `.build/page-titles/`.

September 28 shared page layout: Schedule defines the heading at x=84, y=52,
width=300 with centered 24px text, and the content top at y=100. Party and Badge
use the same shared heading helper and content boundary. Settings keeps native
menu navigation but reserves a fixed 48px header, centers the heading independently
of its back button, and starts every submenu at y=100. Its content ends at y=404.
Party's 300px poster occupies y=100–400 with its caption below; Badge's profile
and expanded QR panels use y=100–404. Home and modal calibration/setup are separate.
Validation: native UI tests assert identical primary heading bounds/alignment and
Settings content starting at y=100; menu, QR and capture tests passed. Guarded
flash passed UNIT_READY and device captures confirmed the new layout and Settings
Hack QR. Calibration, profile flags and invitation unlock were preserved. USB
discovery needed over ten seconds under host load; its timeout is now sixty
seconds with all identity/partition checks retained (eight guard tests passed).
Private evidence: `.build/fixed-layout/`.

### September 30 neutral grays

RGB565 has six green bits but five red/blue bits, so grays rounded per channel
(alpha blends, anti-aliasing, low-alpha masks, `#161616`) land one green step
off neutral and read green on the dark UI. `board::flush` now snaps only pixels
whose red and blue match and whose green is within one step of neutral
(`main/neutral_gray.h`, host-checked in `tests/factory-board`); colors, photos'
non-gray pixels and equal-distance mid-gray ties are unchanged. The shared
`panel()` gray is now `#181818`. Device captures after flashing: the Badge
button and Settings rows render (24,24,24), the empty-portrait frame (16,16,16),
and the former (16,20,16)/(0,4,0) pixels are gone. Max main-loop gap stayed
237 ms during page changes.

### September 30 Settings actions and arrow targets

Settings rows are square (`radius 0`). Inside any submenu the chrome hides the
page arrows and dots, and LVGL's header chevron stays hidden; a fixed white
272×46 action at y=354, aligned with the rendered rows, is the only on-screen
way back. It reads **Save** on Date / time and **Done** on every other page,
including the Date, Time and UTC offset pickers (which return to Date / time).
Save writes the clock only when the draft was edited; a failed save keeps the
page and its error. Submenu content stops above the action. Physical pushers
keep their page navigation.

The previous/next arrows now own the outer 72px strips from y=100 to y=404
instead of 54×126 boxes. Page content stays within x=72..396 and the heading,
footer and dots sit outside that band, so the targets do not overlap them. The
icons keep their original positions; pressing dims the icon instead of filling
the larger area. Host UI checks cover strip edges, hidden submenu arrows, the
action geometry and save/no-op/failure paths.
Follow-up the same day: Settings rows grew to 52px with 20px text and 12px
separation (the 46px white action is unchanged); longer pages such as Settings
home, Orientation and Date / time scroll natively instead of shrinking targets.
Battery status moved from Brightness to Settings home, as muted 12px mono text
directly under the title.
Calibrate touch is now its own Settings row (after Orientation) that opens the
wizard directly; Orientation keeps Free, Default, 180° and Touch test.
Later the same day the Date / time submenu was removed in favor of the captive
portal's browser clock sync (see `conference-clock.md`), so every submenu's white
action now reads **Done**. Settings home is Brightness, Orientation, Calibrate
touch, Hack this device, Connect phone and Reset.

### October 1 photo download

Phone setup's **Photo** picker offers "Use my GitHub / X / LinkedIn photo" beside
upload. The matching handle is required; nothing is uploaded from the phone. Save
badge stores the profile with the current photo and queues one fetch. The
service task starts it only after the setup AP has stopped: it joins the saved or
built-in event network, GETs `https://avatar.chan.dev/v1/<network>/<handle>`
(certificate-checked, at most 128 KiB, no redirects), turns Wi-Fi off, then
replaces only the photo, and only if the profile still has that handle. The
footer shows "Getting your photo..." and then "Photo updated" or a short reason
for 6 seconds on every page. The relay itself is `services/avatar/`.

Device check, same day: after a confirmed Reset badge, the USB `photo_fetch`
diagnostic (public `octocat` handle, temporary Wi-Fi held only in RAM) reached
HTTP 200 with 4,315 bytes; Wi-Fi was off about 4 s after the request, and the
Badge page showed the photo with "Photo updated". A screen capture taken during
the fetch was garbled, consistent with bytes dropped from the USB capture stream
while Wi-Fi was active; the capture afterward was clean. The physical screen
during the fetch was not observed. The badge was reset to stock again afterward.
The phone-driven path was covered by host checks only.
