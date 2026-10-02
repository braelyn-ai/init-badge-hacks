# Browser releases

The browser installer is built from `web-flasher/`. Firmware binaries are release
assets, never Git source files or copies of a device's flash. The first alpha is
`conference-factory-3`, the same application verified in
[schedule verification](schedule-verification-2026-09-17.md).

## Versioned releases (from v1.0.0)

Releases use semantic versions. `firmware/factory_badge/version.txt` (`1.0.0`)
is the single source: ESP-IDF stamps it into the app descriptor, the firmware
reports `v1.0.0` as its readiness build ID, `provision-clock.py`,
`verify-factory.py` and the packager read it, and the web installer pins the
same value (checked by `web-flasher/test/release-version.test.js`). Bump it for
every release (`1.0.1` fixes, `1.1.0` features).

1. Change `version.txt`, run the checks, commit, and tag `vX.Y.Z` on
   `chantastic/stopwatch`.
2. Build from that clean commit (`scripts/build.sh`), then package:
   `python3 scripts/package-web-release.py` (build ID defaults to `v` + version).
   A stable `vX.Y.Z` release refuses a dirty worktree or an app whose embedded
   version differs. Verify with `--verify .build/web-release/releases/vX.Y.Z`.
3. Qualify the exact folder on hardware: the update path on a configured badge
   and the station's first-install path on a few factory units.
4. Publish the same four files (`release.json`, `bootloader.bin`,
   `partition-table.bin`, `firmware.bin`) as GitHub Release `vX.Y.Z` on
   `chantastic/stopwatch`, then hand the folder to workos.com/init/badge/install
   (`/init/badge/install/releases/vX.Y.Z/`), and run the batch station with
   `scripts/flash-station.py --release .build/web-release/releases/vX.Y.Z`.

Earlier `conference-factory-*` alphas remain in `chantastic/m5stack-stopwatch-authkit`.

## Hosting

- Requested entry: `https://drops.workos.cloud/stopwatch` redirects to the installer.
- Installer: `https://stopwatch.workos.cloud/stopwatch/install/`.
- Existing guide Drop: `https://drop.workos.cloud/stopwatch` (singular).
- Issue reports: `https://github.com/chantastic/stopwatch/issues`.

Alto Drop documents run in a sandboxed iframe without USB permissions. The Drop
links to the standalone application; do not embed the flasher in a Drop.
The application uses its existing public `/stopwatch` surface. The Drop retains
its existing organization audience. Canonical source is
[chantastic/stopwatch](https://github.com/chantastic/stopwatch), with the website
under `site/`. Tested changes go directly to `main`; they do not deploy the site.
Alto's managed `https://git.workos.cloud/stopwatch.git` remains the publication
target. The old WorkOS promotion script cannot publish this repository, and a
replacement publisher is pending; see [publication status](site-promotion.md).
The small plural-host redirect remains a separate Alto application, `drops`.

The existing public release files remain in `chantastic/m5stack-stopwatch-authkit`
so anonymous installers can still download the exact pinned bytes. That repo is
the current artifact host, not the source of truth. Do not replace these URLs
with the new source repository until a new release has been qualified, published,
and verified there. Moving source alone does not move firmware assets.

## Browser installation paths

**Update badge** updates already prepared conference badges using desktop Chrome
or Edge and Web Serial. Updates never format storage. Factory layouts, unavailable
storage, unusual OTA selectors, protected chips, and wrong hardware fail closed.

The separate **First install** path requires a verified local recovery copy and
explicit replacement confirmation for the recognized factory layout. **Finish
first install** can prepare unavailable storage after an interrupted conversion.
See [browser first install](browser-factory-install.md) for the exact safeguards
and pending hardware qualification. These paths reuse the same published release;
website publication remains a separate action. A failed update never erases
storage automatically.

The browser downloads and verifies hashes, then checks ESP32-S3 security state,
16 MiB flash, the complete existing partition sector and a recognized app0 boot
selector before writing. The user identifies the physical board as StopWatch;
the USB vendor/product and chip alone cannot identify the entire board.
Final readiness checks verify the firmware's StopWatch/memory configuration.

Writes are separate bootloader, partition-table and app0 components, with chip
erase disabled and existing image headers preserved. There is no merged image,
NVS, filesystem, OTA-selector or device-dump asset. On compatible updates, saved
profiles and settings remain in their existing partitions.

After reset, the same selected browser SerialPort receives a fresh clock, current
UTC offset, and nonce-bound commands. Success requires matching build/memory,
mounted profile storage, an advancing RTC, and radios off. Failure after flashing
is reported as incomplete verification, not a completed installation. The
separate clock/check button can finish provisioning without another flash.

## Prepare a release

```sh
./scripts/build.sh
./scripts/test.sh
python3 -m unittest discover -s tests -p 'test_package_web_release.py'
python3 scripts/package-web-release.py --help
```

Use the packager's build-ID and source/output options to create an immutable
release directory under `.build/web-release/releases/`. Verify it independently:

```sh
python3 scripts/package-web-release.py --verify .build/web-release/releases/conference-factory-3
cd web-flasher
npm ci --ignore-scripts
npm test
npm run build
```

The manifest identifies embedded firmware metadata separately from the
packaging worktree. A dirty worktree is recorded honestly; its commit does not
attest to all bytes in an existing binary. The initial application hash is
`78e3c38b36cd4a1a75fd49c707f4796b522d03bfbc28fa4ca83c7c8b966cce73`.

Publish the four verified files as GitHub Release assets: `release.json`,
`bootloader.bin`, `partition-table.bin`, and `firmware.bin`. Do not overwrite an
existing released tag's assets. Update the site's exact size/SHA-256 allowlist
when adding a release. The site build downloads and verifies each pinned asset,
then packages those bytes as ignored generated Worker content. The same-origin
download routes need no runtime GitHub request or browser credentials. Binaries
and generated content remain outside Git.

Copy `.build/web-installer/` into `site/public/stopwatch/install/` and run the
site's checks. Alto publication needs the replacement publisher described in
[publication status](site-promotion.md); the retained WorkOS promotion script
cannot publish this repository. Do not use a direct Cloudflare deployment for
that application. Stage and publish the updated Drop separately, preserving its
audience. Verify the anonymous installer and all
four deployed asset hashes after publication. Alto's managed Worker egress policy
can reject runtime GitHub fetches; the first proxy deployment returned 502 even
though Node and local workerd checks passed. Keep release acquisition in the
build rather than widening the organization's outbound policy.

## Pinned browser library adaptations

`esptool-js` is pinned at 0.6.1 and `js-md5` at 0.8.3. The installer keeps the
library's chip detection, ROM/stub loading, serial framing, compression, writes
and verification. Small adapters cover observed gaps in that exact release:

- Consume and verify the 16-byte stub digest left unread after `readFlash()`.
- Release writer locks after rejected writes and bound disconnect waits.
- Assert RTS before the library's hard reset deassertion.
- Do not rewrite image headers in the browser; packaged headers already encode
  DIO / 16 MiB / 80 MHz. Supply MD5 calculation so writes are verified.
- Close the loader transport before opening the clock protocol; one reader owns
  the port at a time. Never select another previously granted port automatically.

Tests cover preservation guards, download integrity, serial framing and cleanup,
strict clock/readiness checks, fresh nonces and diagnostic redaction. Host tests
do not establish a physical USB flash by themselves. Record browser/device
observations separately when qualifying a release.

## Feedback

The issue button opens a GitHub draft for the tester to review and submit. The
installer does not automatically post issues or upload diagnostics. Copied
details contain only release, build, hardware and readiness fields. They omit
profiles, photos, credentials, device identifiers and unrelated serial logs.
