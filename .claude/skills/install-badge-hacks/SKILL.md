---
name: install-badge-hacks
description: Install the hack-pages firmware (Googly Eyes, Radar, HAL 9000, Third Eye, Matrix, Bit, Labyrinth, Umbrella ID) on a WorkOS init() conference badge (M5Stack StopWatch) over USB, keeping the owner's profile, photo and settings. Use when someone asks to flash, install, update, or restore this badge firmware.
---

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
| Labyrinth | Tilt the badge flat to roll a marble to the centre of the maze | Restarts the level |
| Umbrella | An Umbrella Corporation ID with their name, photo and a job title rolled from their name | |

These pages have no arrows or dots: swipe sideways or use the side buttons.
The Radar page turns the Wi-Fi radio on in receive-only mode for about two
seconds every few seconds while it is showing; it never joins or transmits.

## Going back

```sh
.venv/bin/python scripts/hack-flash.py --restore backups/app0-<time>.bin
```

or reinstall the stock firmware from https://workos.com/init/badge/install.
