# Conference clock and batch flashing

The active native implementation is `firmware/factory_badge/main/clock_service.*`
with checked RTC access in `board.*` and `vendor/rx8130/`. The HTTP endpoint and
USB acknowledgment contract remain compatible. Arduino helper names below
describe the retained implementation; see [factory-stack.md](factory-stack.md)
for current task ownership and build details. Native unit readiness requires
`conference-factory-3`.

The conference firmware keeps **UTC in the StopWatch's RX8130CE hardware RTC**
and sets the ESP32 system clock from it on boot. The display adds a saved UTC
offset. It can adopt the flashing computer's clock over USB or the phone/browser
clock through its temporary local setup portal. There is no internet time
service, compile-time clock, or saved timestamp used to guess elapsed time after
power loss. Visible times use **12-hour `h:mm AM/PM`**, with no leading zero on
the hour: midnight is `12:00 AM` and noon is `12:00 PM`. An invalid clock displays
`--:--`.

## No on-device editor

September 30: the Settings → Date / time editor (Calendar, rollers, UTC offset)
was removed in favor of phone setup. The captive portal's automatic browser clock
sync, and USB provisioning during flashing, are the only ways to set the clock;
both use the same checked RTC/system/NVS clock service. The earlier `manual`
source and its `set_clock` UI callback no longer exist.

## Phone synchronization and Settings

Opening the authorized temporary portal sends the browser's current clock to
the badge. The portal has a separate clock result and Retry action; success is
shown only after hardware RTC and system-clock readback pass. This uses local
Wi-Fi between the browser and badge and requires no internet or account sign-in.

**Clock synchronization is independent of profile saving.** A successful clock
sync takes effect immediately and survives closing setup or canceling profile
edits. Cancel continues to discard uncommitted name/social/photo edits; it does
not roll back a verified clock. A failed clock sync does not save the profile or
close setup. The phone/browser is the time authority, so an incorrect phone
clock or timezone produces an incorrect badge clock; there is no external clock
service to correct it automatically.

The dedicated `/clock` request passes the portal's session/nonce and bounded JSON
checks before invoking `conferenceClockSyncPhone(input, response)`. Required
values are integer Unix **UTC seconds** in 2024–2099 and integer `offset_minutes`
from -840 through +840 **east of UTC**. JavaScript supplies
`Math.floor(Date.now() / 1000)` and `-new Date().getTimezoneOffset()`; the minus
sign matters because JavaScript's native offset uses the opposite convention.
An optional timezone name is informational and is neither persisted nor used
to calculate time. Missing, string, fractional, boolean, or out-of-range values
are rejected before clock writes.

The clock helper checks RTC writes/readback, offset persistence, system-clock
readback, and agreement between the current system clock and RTC. Its response
contains only `ok`, `valid`, `source`, UTC `epoch`, `rtc_epoch`, `offset_minutes`,
and an error code on failure. It performs no profile writes or AP teardown.
The last successful source is `phone` during that boot; a later reboot reports
`rtc` after restoring the retained clock. Repeated synchronization with the same
offset does not rewrite that NVS value.

Native views receive the shared local-time helpers: `badge_clock::timeText()`
(`h:mm AM/PM`) and `badge_clock::dateText()` (`YYYY-MM-DD h:mm AM/PM`). Both apply
the same saved offset, including day/year rollover. For example, Settings can
show `2026-10-07 1:30 PM`. An invalid date/time remains `Date / time not set`.
AM/PM text is fixed English, independent of the C library locale.

`badge_clock::epoch()` still returns UTC seconds (zero while invalid), and
`badge_clock::offset()` exposes the saved display offset. Agenda state and the
After Dark reveal use those numeric values, without parsing display strings.
The published agenda's labels also use `h:mm AM/PM`; its start minutes, daily
boundaries, UTC storage and USB/phone clock contracts are unchanged by display
formatting.

## Flash one device

Verify the flashing computer's time and timezone, close other serial monitors,
and discover the currently connected StopWatch port. For a development flash:

```sh
./scripts/flash.sh /dev/cu.usbmodemYOUR_PORT
```

This builds, uploads normal application components, sets a fresh clock over USB,
and verifies both system and hardware RTC readback. It then waits and checks that
the hardware clock actually advances. A final fresh status must also confirm the
conference build, StopWatch board, expected memory, available profile storage,
valid RTC, and both radios off outside setup. A successful unit ends with
`UNIT_READY` and exits zero. A successful upload without all those checks
**fails the command** and must not count as a ready unit.

Before **every upload**, including both prebuilt modes, the script uses the
pinned ESP32 core 3.3.10/esptool 5.3.0 to read only the current partition-table
sector at `0x8000` (4096 bytes). The compiled table must contain the established
six partitions and a valid checksum; the entire device sector must match it,
including padding. A missing, corrupt, blank, factory, or otherwise different
table blocks the upload before flash mutation. Read logs and the sector remain
in ignored `.build/partition-preflight/`; hardware identifiers are not printed.

The run binds to the selected USB serial number, VID, and PID from Arduino
discovery. After the preflight reset and after upload, it locates that same
identity even if the port name changes. Missing or ambiguous identity stops the
command; it does not choose another attached board. Rediscover the intended
device and repeat the full command after resolving the connection. The read
operation may reset the existing application but never writes flash.

The script preserves the established 16 MiB flash / OPI PSRAM /
`app3M_fat9M_16MB` layout. By default it does not erase the chip, upload a
filesystem/NVS image, format storage, or clear saved profiles. `--artifact-dir` assumes artifacts from this
project’s pinned build target; it is not a general-purpose image flasher.

**Factory devices can need a separate partition migration before this script
can be used at all.** The manufacturer's layout differs from this project's
established layout: its NVS/OTA boundaries and filesystem location differ.
Do not treat normal component upload as a safe factory conversion. First obtain
an appropriate private backup and explicit migration authorization, then use a
separately reviewed migration procedure. This scaffold does not implement that
migration, and `--initialize-profile-storage` cannot bypass the partition check.

**Devices already using the established partition map can need a separate
filesystem preparation step.**
The conference profile store expects the existing `ffat` partition to contain a
mountable LittleFS filesystem. An unformatted device or a factory FAT filesystem
within that same verified partition map can accept the application and clock
but still be unable to save a profile.
The default station command rejects `store_ready: false` and does not format or
erase anything. For a **batch with the verified expected partition map whose
filesystem data may be discarded**, explicitly opt in during initialization:

```sh
./scripts/flash.sh --no-build --initialize-profile-storage /dev/cu.usbmodemYOUR_PORT
```

This option can erase **all data in the 9.875 MiB `ffat` filesystem partition**,
including any old cached images or other factory filesystem files. It leaves
NVS, application slots, and the partition table unchanged. It is not a complete
credential wipe. Do not use the option to recover an unreadable personalized
device without first deciding that its filesystem data can be discarded.

The helper first verifies a nonce-bound status for the expected firmware,
hardware, clock, and offline radio state. It sends the explicit initialization
command only when `store_ready` is exactly false; a ready store is never
formatted. Firmware checks the exact partition address/size before formatting.
A matching successful storage acknowledgment and another fresh ready status
are required. Missing acknowledgment or failed initialization fails the unit.
The current development board has not been formatted by this workflow.

## Unattended batch station

October 1, 2026: for the ~400-unit batch the user authorized converting factory
units without prompts or per-unit backups (the stock image is not attendee data).
`scripts/flash-station.py` watches `/dev/cu.usbmodem*`, runs up to 8 units at
once, and classifies each one by its exact partition sector, the same allowlist
as [web-flasher/src/factory.js](../web-flasher/src/factory.js):

| Device | Action |
| --- | --- |
| Recognized factory sector | Erase app1/ffat/coredump, write bootloader, target sector, blank settings, app0; then provision |
| Target sector, app0 equals this build | Provision only (clock; storage init only when not ready) |
| Target sector, other app, ledger says install started | Resume the full install |
| Target sector, other app (e.g. the development badge) | Skip, no write |
| Anything else | Skip, no write |

Provisioning is `provision-clock.py --initialize-profile-storage`; only
`UNIT_READY` counts as ready. Probing resets every attached board, so keep the
development badge unplugged or pass `--ignore-port`. A MAC-keyed ledger and
per-unit logs are written to `.build/station/` (private: they hold hardware IDs).
`--dry-run` classifies without writing; `--offset-minutes` sets the event offset.
macOS port names follow the physical hub position, so a slot keeps its name.

Verified October 1 only as a dry run on the development badge (it was classified
as running this build, and verify-flash matched without writing), plus host
classification checks. The factory-unit path has **not** yet run on hardware.
Qualify it on a few units from the delivered batch before running the full batch.

```sh
python3 scripts/flash-station.py --artifact-dir /absolute/path/to/frozen-release
```

## One compiled artifact, many devices

Build once from the reviewed conference source:

```sh
./scripts/build.sh
```

For each of approximately 400 devices, identify its live port and run:

```sh
./scripts/flash.sh --no-build /dev/cu.usbmodemYOUR_PORT
```

To keep an immutable local release separate from later development builds, copy
the **entire build output directory** to an ignored/private release directory
and use:

```sh
./scripts/flash.sh --artifact-dir /absolute/path/to/release /dev/cu.usbmodemYOUR_PORT
```

Each call samples the computer clock after upload. The firmware binary remains
identical across devices; time is never baked into it. Use one serial owner per
device and treat exit status as the station's pass/fail signal. Retry a failed
clock provision without reflashing using:

```sh
python3 scripts/provision-clock.py /dev/cu.usbmodemYOUR_PORT
```

The same helper accepts `--initialize-profile-storage` for a deliberately
approved filesystem preparation without re-uploading firmware. Its firmware
guard still requires the exact `ffat` partition; it cannot migrate the partition
table of a factory application.

The Python helper requires only Python 3's standard library on macOS/Linux. It
does not require pyserial, change accounts, inspect profile data, or print
unrelated USB diagnostics. It waits for boot/USB availability, uses a new nonce
and fresh time on each retry, and does not silently skip a missing clock reply.
If USB re-enumerates under a different port name, rediscover the port and retry.

The default display offset is the computer's current local UTC offset, including
its current daylight-saving adjustment. An event in another timezone can use an
explicit offset, in minutes east of UTC:

```sh
./scripts/flash.sh --no-build --offset-minutes -420 /dev/cu.usbmodemYOUR_PORT
```

Offsets are fixed until the next USB or phone sync; this scaffold does **not** contain
an IANA timezone database or automatically apply a future DST transition. Check
the event's offset and resync if the devices cross a DST change. The accepted
clock range is 2024 through 2099; offsets are -840 through +840 minutes.

Only reviewed source-built application components should be distributed.
Never distribute a full flash dump from a personalized board: it can contain
legacy credentials and private photos. New conference profile storage and the
clock's `conf-clock` NVS namespace are distinct from authenticated badge data.
The clock namespace contains only `offset`, not an identity or saved time.

## Separate factory migration procedure — design only

**This procedure is not implemented as a migration tool and has not been tested
on factory-layout hardware. Do not migrate the current development board.** It
already has the expected layout. The normal flash command intentionally has no
partition-check bypass, and a filesystem-initialization flag is not migration
authorization.

For a future factory batch, use this staged procedure:

1. **Inventory one sacrificial unit before writing.** Establish its physical
   StopWatch revision, flash size, USB identity, existing partition table,
   firmware version if available, and security/encryption state using read-only
   tools. Never assume all factory batches share the example layout below.
2. **Make a private recovery set.** Read the entire 16 MiB flash, its partition
   sector, and nonsecret tool/version metadata. Hash the files, confirm exact
   lengths and parsed boundaries, and retain a second read/hash comparison.
   A Git source backup does not substitute for this data backup. Restrict access
   because the image may include factory configuration, Wi-Fi data, or other
   private state. Do not read/export secret eFuse keys or change security fuses.
3. **Approve a state policy and exact write manifest.** Compare the detected
   table with the target table. Decide which existing settings need logical
   export/migration, which can be discarded, and how the original image will be
   restored if qualification fails. Do not transplant a raw NVS/filesystem dump
   into differently sized partitions or presume it contains only disposable
   data. A manifest must list every destination range, its input artifact hash,
   expected partition checksum, and any intentional NVS/filesystem loss. Obtain
   specific authorization for that manifest before mutation.
4. **Implement and review a dedicated migration command.** It must verify the
   backed-up device identity and source-layout hash, enforce the approved source
   layout and exact destination ranges, use the pinned tools, and fail closed on
   any mismatch. Upload only approved components and explicitly approved
   initialized data. No generic chip erase, security changes, or shortcut that
   skips the normal uploader's guard. Keep the original recovery files separate
   from distributable application artifacts.
5. **Qualify one factory unit.** Apply that separately authorized migration,
   then read and verify its target table and programmed components. Run the
   normal read-only preflight against the resulting device. Only after its
   expected map is verified may an explicitly authorized `ffat` initialization
   occur, if needed. Complete clock/readiness checks, local profile create/edit/
   image upload, reboot persistence, controls, and offline-radio checks. Exercise
   and verify the approved recovery procedure on this sacrificial unit; this is
   destructive test work that also needs its own authorization.
6. **Release the qualified batch process.** Record the tested hardware revision,
   source and target layout hashes, firmware/component hashes, migration-tool
   version, state policy, and qualification outcome. Reject unknown revisions or
   source layouts. Each subsequent device needs its own backup/identity checks,
   fresh clock provisioning, and successful `UNIT_READY`; after migration,
   routine updates always use the guarded normal flash command.

The source-layout example preserved in the guard's host test has NVS
`0x9000/0x4000`, OTA metadata `0xD000/0x2000`, PHY data `0xF000/0x1000`,
applications `0x20000/0x4F0000` and `0x510000/0x4F0000`, filesystem
`0xA00000/0x400000`, and coredump `0xE00000/0x10000` (offset/length).
The target instead has NVS `0x9000/0x5000`, OTA metadata `0xE000/0x2000`,
applications `0x10000/0x300000` and `0x310000/0x300000`, `ffat`
`0x610000/0x9E0000`, and coredump `0xFF0000/0x10000`. These overlaps are why a
partition-table replacement is a data migration, not an ordinary safe update.
The example is a compatibility-test fixture, not a current hardware observation.

## Clock protocol

Commands are newline-delimited JSON at 115200 baud:

```json
{"op":"clock_set","epoch":1789560000,"offset_minutes":-420,"nonce":"0123456789abcdef"}
{"op":"clock_status","nonce":"fedcba9876543210"}
```

Those are protocol examples, not the current time; production provisioning
always samples the computer. Nonces contain 8–64 lowercase hexadecimal
characters. The response prefixes are `CLOCK_ACK ` and `CLOCK_STATUS ` followed
by a JSON object with `protocol: 1`, echoed nonce, `ok`, `valid`, `source`, UTC
`epoch`, `rtc_epoch`, and `offset_minutes`. Failures include a short `error` code.
Neither request nor response contains an attendee profile or credential.

The station accepts only a matching nonce and version, correct integer fields,
the requested offset, and both clocks within three seconds of the current
computer clock and within one second of each other. A separate status request
1.2 seconds later must show the hardware RTC advancing. Stale, malformed,
unrelated, or oversized serial lines cannot satisfy verification.

Finally the helper sends `{"op":"status","nonce":"<new hex nonce>"}` and
requires a matching `CONFERENCE_STATUS` reply. This release expects build
`conference-factory-3`, board `30`, flash `16777216`, PSRAM `8388608`,
`store_ready`, `clock_valid`, and `rtc` all true; `setup` false; and both
`wifi_mode` and `bluetooth` zero. A mounted existing manual profile is never
cleared to satisfy readiness. Update the expected build alongside a future
firmware protocol/version change.

The destructive opt-in uses `op: "initialize_conference_storage"`, the exact
confirmation value `"ERASE_FFAT_FOR_CONFERENCE"`, and a new nonce. Its
`STORAGE_ACK` must echo that nonce with boolean `ok: true` and
`store_ready: true`; a fresh `status` follows. It is never sent implicitly after
a failed upload or a missing clock/status reply.

## Hardware support and retention limits

Sources inspected September 16, 2026:

- [M5Stack StopWatch documentation](https://docs.m5stack.com/en/core/StopWatch)
  identifies RX8130CE at I²C address `0x32` and the integrated 450 mAh battery.
- [Pinned M5Unified 0.2.19 RTC selection](https://github.com/m5stack/M5Unified/blob/0.2.19/src/utility/RTC_Class.cpp)
  selects its RX8130 driver for `board_M5StopWatch`.
- [Pinned RX8130 driver](https://github.com/m5stack/M5Unified/blob/0.2.19/src/utility/rtc/RX8130_Class.cpp)
  supplies calendar access and board initialization. Its `getVoltLow()` exposes
  the backup-voltage flag **VBLF**, which is different from clock-invalid **VLF**.
- [Epson RX8130CE application manual, ETM50E-10](https://download.epsondevice.com/td/pdf/app/RX8130CE_en.pdf),
  sections 14.5 and 18, defines oscillator-loss VLF at register `0x1D`, bit 1,
  and calendar initialization/readback requirements.
- [M5Stack StopWatch schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-SCH_Stopwatch_PRJ_Main_VA_20251201_2026_04_24_17_46_22.pdf),
  RTC block on PDF page 4, shows RTC supply/decoupling circuitry; no separate
  replaceable RTC backup cell is shown in that block. The exact physical unit
  revision has not been established.

Firmware enables `cfg.internal_rtc = true`, reads retained calendar values and
VLF, and rejects stopped clocks, invalid BCD, impossible dates, failed reads, and
out-of-range years. Fresh provisioning checks RTC writes, clears VLF only after
writing a new calendar, and verifies readback before marking the clock valid.
NVS offset writes must succeed too. Power-switch/charging setup remains owned by
the pinned board driver.

The RTC can keep time when the main application restarts while its power supply
remains available. **Do not promise retention after a fully exhausted or
disconnected battery and no USB power.** Loss of RTC power can invalidate its
calendar; a saved flash timestamp cannot repair that. The firmware requires
fresh provisioning after detected loss. Ordinary power-off retention, long-term
drift, and battery runtime require measurements on the actual hardware revision;
they are not established by host simulation or successful USB readback.

## Verification

Host tests exercise production calendar/command handling with simulated RTC,
system-clock and NVS failures, plus real POSIX pseudo-terminal transport and
fake batch uploads. They cover stale/missing replies, fresh retry timestamps,
typed field validation, stopped RTC detection, and failed provisioning causing
the flash command to fail. Final readiness checks reject unavailable storage,
wrong hardware/memory/build, missing status, active setup, or active radios.
Storage opt-in tests verify no initialization by default, none for an already
ready store, none for wrong hardware, and required acknowledgment/readiness.
Partition tests reject factory/corrupt/blank/truncated maps and changed compiled
layouts. Batch orchestration tests prove all build modes and storage opt-in stop
before upload on failed preflight. USB tests cover renumbering, missing/duplicate
identity, and a different board taking the previous device path.
Phone-clock tests cover strict field validation, east/west offset signs, local
day/year rollover with UTC unchanged, reload, unchanged-offset write avoidance,
and rejected RTC/system-clock readback or persistence failures. These are host
simulations; they do not establish real-phone behavior by themselves.
Native clock tests additionally check 12-hour midnight/noon labels, unpadded
hours, two-digit minutes, fractional-hour offsets, both offset limits and invalid
placeholders. Agenda tests verify each displayed 12-hour label matches its
numeric start minute.
Run them through `scripts/test.sh`.
The host tests do not access the board or measure retention, drift, or physical
power behavior. Record actual device checks separately with the source version.
