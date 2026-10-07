# init() badge hack pages

A fork of the WorkOS init() conference badge firmware
([chantastic/stopwatch](https://github.com/chantastic/stopwatch)) for the
**M5Stack StopWatch**, with eight extra full-screen pages after Settings.
Everything the stock firmware does is unchanged, and installing it keeps your
profile, photo and settings.

| Page | What it does | Tap |
| --- | --- | --- |
| **Eyes** | Googly eyes: the pupils fall with gravity and bounce off the rims | Pokes them |
| **Radar** | A sweeping scope with a blip for each nearby Wi-Fi access point | |
| **HAL** | The HAL 9000 lens, breathing | Speaks to you by first name |
| **Third Eye** | A sleeping eye that opens into a psychedelic one | Sleep / wake |
| **Matrix** | Falling green glyphs | Types a message |
| **Bit** | Tron's Bit, floating and tumbling | Answers YES or NO; tap again to clear |
| **Labyrinth** | Hold the badge flat and tilt it to roll a marble to the centre of a generated maze | Restart level; progress is saved |
| **Umbrella** | An Umbrella Corporation employee ID with your name and photo. Your job title is rolled from a hash of your name: 55% common, 25% uncommon, 13% rare, 5.5% epic, 1.5% legendary | |

These pages hide the arrows and dots: swipe sideways or use the side buttons.
From the init() screen, paging left reaches them fastest.

Radar is the only page that uses a radio: while it is showing, it runs a
receive-only (passive) Wi-Fi scan every few seconds. It never joins a network or
transmits, and nothing it hears is stored. Blip distance is signal strength;
bearing is decorative, because Wi-Fi gives no direction.

## Install it

Quick version, with the badge plugged in over a USB-C data cable:

```sh
python3 -m venv .venv && .venv/bin/pip install esptool
gh release download --repo braelyn-ai/init-badge-hacks --pattern conference_badge.bin
.venv/bin/python scripts/hack-flash.py --image conference_badge.bin
```

`scripts/hack-flash.py` writes only the app slot. It first checks the badge has
the conference partition layout, backs up the app that is on it to `backups/`,
and can put it back with `--restore`.

### Install with an AI agent

This repository ships an agent skill at
[`.claude/skills/install-badge-hacks/SKILL.md`](.claude/skills/install-badge-hacks/SKILL.md).
Clone the repo, open it in Claude Code (or any agent that reads skills) with
the badge plugged in, and say **"install the badge hacks"**. For other agents,
paste the skill below as instructions.

<details>
<summary>The skill</summary>

# Install the hack pages on an init() badge

You are helping someone put this firmware on their own badge. It adds eight
full-screen pages after Settings and changes nothing else. The install writes
only the 3 MiB app slot; profile, photo, Wi-Fi override, clock and settings
survive. Never erase flash, never write the bootloader or partition table, and
never use `idf.py flash` or `scripts/flash.sh` here (the first rewrites the
bootloader and table; the second needs tooling this fork does not use).

## 1. Check the starting point

- The badge must already run the WorkOS conference firmware (pages: init(),
  Schedule, Party, Badge, Settings). If it still shows M5Stack's factory demo,
  the owner installs the conference firmware first in Chrome or Edge at
  https://workos.com/init/badge/install, then comes back.
- Connect it with a USB-C **data** cable. If no port appears
  (`ls /dev/cu.usbmodem*` on macOS, `ls /dev/ttyACM*` on Linux), have the owner
  hold PWR for about two seconds until the green LED lights.
- Close anything else using the serial port (serial monitors, the web installer tab).

## 2. Get esptool

```sh
python3 -m venv .venv && .venv/bin/pip install esptool
```

## 3. Get a firmware image: download or build

**Download (minutes):** take `conference_badge.bin` from the latest release of
this repository (`gh release download --pattern conference_badge.bin`, or the
Releases page).

**Build (first time ~20 min, ~3 GB):** needs git, Python 3, cmake and ninja
(`brew install cmake ninja` on macOS).

```sh
./scripts/setup-factory.sh   # once: pinned ESP-IDF 5.5.4 + components into .build/
./scripts/build.sh           # -> .build/factory/conference_badge.bin
```

## 4. Flash

```sh
.venv/bin/python scripts/hack-flash.py                               # the image you built
.venv/bin/python scripts/hack-flash.py --image conference_badge.bin  # a downloaded image
```

The script refuses to write unless the badge has the conference partition
layout and boots from the first app slot, saves the current app to
`backups/app0-<time>.bin`, writes and verifies the new one, restarts the badge
and reports its build and page count. If it prints `STOPPED`, relay the reason
to the owner and do not work around it with raw esptool commands.

## 5. Confirm with the owner

Success looks like `Done: build v1.2.0-hack.N, 13 pages, profile kept.` On the
badge, their Badge page still shows their name and photo. The new pages come
after Settings; from the init() screen, paging left reaches them fastest.

| Page | What it does | Tap |
| --- | --- | --- |
| Eyes | Googly eyes whose pupils fall with gravity | Pokes them |
| Radar | Sweep with blips for nearby Wi-Fi access points | |
| HAL | The HAL 9000 lens, breathing | Speaks to the owner by first name |
| Third Eye | Opens into a psychedelic eye | Sleep / wake |
| Matrix | Falling green glyphs | Types a message |
| Bit | Tron's Bit, floating | Answers YES or NO; tap again to clear |
| Labyrinth | Tilt the badge flat to roll a marble to the centre of the maze | Restarts the level; progress is saved |
| Umbrella | An Umbrella Corporation ID with their name, photo and a job title rolled from their name | |

These pages have no arrows or dots: swipe sideways or use the side buttons.
The Radar page turns the Wi-Fi radio on in receive-only mode for about two
seconds every few seconds while it is showing; it never joins or transmits.

## Going back

```sh
.venv/bin/python scripts/hack-flash.py --restore backups/app0-<time>.bin
```

or reinstall the stock firmware from https://workos.com/init/badge/install.

</details>

## Hack on it

Each page is one file in `firmware/factory_badge/main/ui/page_*.cpp`, built on
the small shared kit in `ui/hack_kit.h` (shapes, colour, frame pacing, custom
painting, gravity, typed captions). To add a page: write `page_<name>.cpp` with a
`make_<name>` factory, declare it in `ui/ui_internal.h` (and bump `PageCount`),
and add it to the factory table in `badge_ui.cpp`.

You can see a page without a badge:

```sh
./scripts/setup-factory.sh               # once
./scripts/hack-preview.sh eyes           # PNG frames in .build/hack-preview/eyes/
python3 scripts/hack-gallery.py          # animated gallery of every previewed page
```

The preview builds one page against real LVGL under ASan/UBSan and reports how
much of the screen it redraws. That number matters: the real badge repaints a
full screen in roughly 180 ms, so pages stay smooth by redrawing only what
moved (about 10% of the screen per frame or less) with opaque shapes.

Known gap: the upstream host UI test (`tests/factory-ui`) and the
`scripts/verify-*.py` device checks still assert the stock page count and have
not been updated for the extra pages.

---

*Everything below is the upstream README.*

# M5Stack StopWatch conference badge

Firmware and the installer website live together in
[chantastic/stopwatch](https://github.com/chantastic/stopwatch), the canonical
source repository. Request improvements in
[Issues](https://github.com/chantastic/stopwatch/issues). Tested changes go
directly to `main`; see [Contributing](CONTRIBUTING.md) for checks and releases.

| Area | Source | Publication |
| --- | --- | --- |
| Badge firmware | `firmware/factory_badge/` | Qualified, versioned firmware release |
| Browser flashing logic | `web-flasher/` | Tested bundle copied into `site/public/stopwatch/install/` |
| Guide and installer website | `site/` | [Alto publication status](docs/site-promotion.md) |

The [live installer](https://drops.workos.cloud/stopwatch) continues to serve the
hardware-tested `conference-factory-3` release. Pushing this repository does not
deploy the website or publish firmware automatically. The former WorkOS promotion
script is retained as legacy; a publisher for this repository is pending.

An offline, locally configurable conference badge for the **M5Stack StopWatch**.
The active firmware uses the factory **ESP-IDF + LVGL + Smooth UI Toolkit +
Mooncake** stack, with the factory CO5300 display and CST820 touch integration.
No internet, attendee account, cloud backend, or sign-in is needed.

Six pages: **init() animation → Schedule → Developers After Dark → Badge →
Hack your Badge → Settings**. Schedule uses the [published init() agenda](https://workos.com/init),
repeats daily in local time, highlights **On now**, and dims passed blocks.
Invite and animation artwork remain temporary.
The Hack QR opens [drop.workos.cloud/stopwatch](https://drop.workos.cloud/stopwatch).

Browser release packaging and publication are documented in
[web releases](docs/web-releases.md). Updates preserve compatible conference
badges. The separate [browser first-install flow](docs/browser-factory-install.md)
backs up factory devices before conversion; physical qualification is pending.

Use the physical left/right pushers or on-screen arrows to page. Swipe vertically
to scroll Schedule or switch between GitHub, X/Twitter and LinkedIn within Badge.
Both pushers open local setup; either pusher returns. Empty accounts say **Tap to
configure**. Scan the setup Wi-Fi QR and edit your name, social handles, and photo
on your phone. Previous values prefill the form. Normal operation has Wi-Fi and
Bluetooth off.

In lanyard-up orientation, blue is previous and yellow is next. Completed taps,
drag rejection and stable auto-rotation remain in place. Tap a configured badge
to expand its QR; press both pushers to edit it.

Settings shows battery percentage, persistent brightness, date/time and a
**Connect phone** action. Orientation choices are **Free** (automatic),
**Default** (stock orientation), and **180°** (lanyard-up). Opening phone setup
automatically synchronizes its fresh clock, independently of saving profile
edits. A successful clock sync is retained even if those edits are cancelled.
The actual phone flow still needs the acceptance check described in the
[Settings verification report](docs/conference-verification-2026-09-17.md).

**Settings → Touch test** shows five white targets and a live purple touch
marker, retained after release. Either pusher returns to Settings. The test
temporarily holds the current rotation; it changes no saved mode or calibration,
saves no touch data, and starts no network. Choose Default or 180° and reopen it
to compare alignment. Physical alignment still requires observation on the
device; simulated diagnostic input cannot establish it.

## Build and flash

Run the one-time setup below to install **ESP-IDF 5.5.4** and fetch the pinned
factory dependencies. Builds use **LVGL 9.5.0**, **Smooth UI Toolkit 2.12.1**,
**Mooncake 2.3.3**, and **M5GFX 0.2.19**. The reviewed USB discovery/preflight/upload
wrapper still needs Arduino CLI and ESP32 core **3.3.10**; the application itself
contains no Arduino or M5Unified runtime. Host tests also require CMake, Node.js,
Clang, and the legacy ArduinoJson 7.4.3 headers for retained regression fixtures.
See [the factory stack guide](docs/factory-stack.md) for source organization.

```sh
./scripts/setup-factory.sh  # once per workstation
./scripts/test.sh
./scripts/build.sh
./scripts/flash.sh --no-build /dev/cu.usbmodemYOUR_PORT
```

The same compiled artifact can serve a batch. Each upload sets fresh UTC time
from the flashing computer, reads back the RX8130 RTC, verifies that it ticks,
and checks storage/radio/hardware readiness. The default display offset is the
computer's current local offset. See the [clock and batch runbook](docs/conference-clock.md)
for first-install storage preparation and failure handling. Ordinary flashes
preserve user storage. Before uploading, the script reads and verifies the current
partition map and follows the same USB identity across resets. A factory or
different map is rejected; factory conversion requires a separately authorized
migration, not the filesystem-initialization option. No full-device dump,
developer profile, or photo is shipped.

## Development and evidence

- [Conference behavior and verification](docs/conference-badge.md)
- [Clock and batch flashing](docs/conference-clock.md)
- [Stock UI framework/source/license findings](docs/stock-ui-reference.md)
- [Hardware geometry and calibrated orientation](docs/hardware.md)
- [Project instructions](AGENTS.md)

Active source is `firmware/factory_badge/main/`. Each native LVGL page has its own
file under `ui/`, with shared widgets, styling and navigation. Board, profile/
portal, and clock services have separate modules. Host tests include failure injection;
physical device observations are separately labeled. Build outputs, uploaded test
photos, diagnostic captures and private logs stay in ignored `.build/`.

The former Arduino conference application remains under `firmware/devices_badge/`
for reference and regression tests. `build-arduino-legacy.sh` builds that historical
application; normal build/flash commands select the native factory stack.
The prior connected AuthKit/voice badge remains in `legacy_connected_app.h`,
[historical documentation](docs/connected-badge-history.md), and Git history.
Its cached identities are not used by the manual conference badge. Shared services
remain in the private `chan-services` monorepo; this firmware change deploys none.

Original project code has no selected license. Dependencies and brand assets have
separate terms; see [third-party notices](THIRD_PARTY_NOTICES.md).
