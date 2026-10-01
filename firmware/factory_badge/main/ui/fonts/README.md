# Design fonts

The `font_mono_*.c` and `font_sans_*.c` files in the parent directory are generated
LVGL bitmap subsets. Both upstream families are licensed under the SIL Open Font
License 1.1; the complete copyright notices and license texts are retained here.
The converted font data remains under that license.

The user specified IBM Plex Mono Medium and SemiBold on September 17, 2026.
Inspecting the supplied Figma source also confirmed Regular for the company line.
Inter Medium is the approved approximation for Suisse Intl Medium; it is not
an exact typeface match. The separate logo/arrow marks use the exact supplied
pixels and do not depend on these fonts.

| Assets | Upstream | Pinned source |
| --- | --- | --- |
| `font_mono_12`, `18`, `24`, `32` | [IBM Plex Mono Medium](https://github.com/IBM/plex) | Revision `78cd4223d8de9fcb78cba84eadecb269c56093c5`, `packages/plex-mono/fonts/complete/ttf/IBMPlexMono-Medium.ttf` |
| `font_mono_20`, `font_mono_regular_24` | [IBM Plex Mono Regular](https://github.com/IBM/plex) | Same revision, `packages/plex-mono/fonts/complete/ttf/IBMPlexMono-Regular.ttf` |
| `font_mono_semibold_12` | [IBM Plex Mono SemiBold](https://github.com/IBM/plex) | Same revision, `packages/plex-mono/fonts/complete/ttf/IBMPlexMono-SemiBold.ttf` |
| `font_sans_12`, `14`, `16`, `20`, `24`, `32` | [Inter Medium](https://github.com/rsms/inter) | [Inter 4.1 release](https://github.com/rsms/inter/releases/tag/v4.1), `extras/ttf/Inter-Medium.ttf` |

Regular is weight 400, Medium is 500 and SemiBold is 600; these are upstream
font outlines, not simulated emboldening. The generated subsets retain neutral
`font_mono_*` / `font_sans_*` names; upstream family names above identify their
provenance.

Confirmed [Figma text styles](https://www.figma.com/design/H0uyTnXh5IkKwiZRTL7QRo/init---conf-2026?node-id=7307-612)
(September 17, 2026):

| Role / source node | Source typeface and size | Source line height | Source tracking |
| --- | --- | --- | --- |
| Profile name `7294-81` | IBM Plex Mono Medium 24px | Auto (31px) | 0 |
| Company `7294-82` | IBM Plex Mono Regular 20px | Auto (26px) | 0 |
| Invitation footer `7294-11` | IBM Plex Mono SemiBold 12px | 20.8px | -2% |
| Schedule subtitle `7316-44` | IBM Plex Mono SemiBold 12px | 20.8px | -2% |
| Schedule swipe hint `7316-45` | IBM Plex Mono SemiBold 12px | 20.8px | -2% |
| Battery `7307-578` | IBM Plex Mono SemiBold 12px | 20.8px | -2% |
| Settings edit hint `7307-628` | IBM Plex Mono SemiBold 12px | 20.8px | -2% |
| Phone subtitle `7312-669` and SSID `7312-673` | IBM Plex Mono SemiBold 12px | 20.8px | -2% |
| Hack subtitle `7294-101` and URL `7294-93` | IBM Plex Mono SemiBold 12px | 20.8px | -2% |
| Schedule time `7316-470` | IBM Plex Mono Medium 12px | Auto (16px) | 0 |
| Room / speaker detail `7316-350` | IBM Plex Mono Medium 12px | Auto (16px) | 0 |
| Orientation option `7307-606` | Suisse Intl Medium 12px | Auto (16px) | 0 |
| Schedule heading `7316-43` / Settings `7307-577` | Suisse Intl Medium 24px | 33px | 0 |
| Session title `7316-471` | Suisse Intl Medium 20px | Auto (26px) | 0 |
| Profile button `7312-750` and Go back `7312-688` | Suisse Intl Medium 16px | Auto (21px) | 0.3px |
| Invitation heading `7294-10` | Suisse Intl Medium 32px | Auto (42px) | 0 |

These are values read from Figma's visible inspector fields, rather than inferred
from raster images. The source places the schedule swipe hint at Y=394. Phone
SSID text is white at 100%; hints use white at 50%.

Inter approximates the Suisse outlines, while native LVGL label padding and
line spacing match the recorded line boxes at whole pixels (20.8px becomes
21px). LVGL's integer `text_letter_space` rounds the -0.24px tracking (-2% at
12px) and +0.3px tracking to zero. The later [page audit](../../../../../docs/pixel-audit-2026-09-17.md) places
caption leading below the glyphs and adjusts measured visible baselines; native
line boxes are retained. No custom renderer is used. This is a font-role
and line-box match with the stated approximations, not a claim of pixel parity
between the Suisse/Figma renderer and Inter/LVGL.

Each number is the converter's requested pixel size, not the complete line
height: ascenders, descenders and accented Latin letters can make the line taller.
All fonts contain printable ASCII and Latin-1 (`U+0020–007E`, `U+00A0–00FF`),
with four-bit antialiasing and uncompressed LVGL bitmap data. Kerning is retained
where supplied upstream. This does not add support for other writing systems.

## Verification

September 17, 2026, application SHA-256
`3f05fec0476ced94338af87de3ddc379dbff54bb1df451c5b61a30b6b940530f`:
the sanitized native LVGL UI suite and ESP32-S3 build passed. Host renders were
reviewed for names/company, wrapped agenda rows, Settings, setup and modals;
setup, social and customization QR codes decoded. Connected-device captures
confirmed the public schedule, Settings and customization screens, its decoded
QR, advancing intro with black background, valid clock and offline radios.
Saved profile/preferences were preserved and the original page was restored.
No personal profile capture or badge reset was performed. Private evidence is
under `.build/brand-typography-verification/figma-host/` and `figma-device/`.

## Regeneration

From the repository root, with Python 3 and Node.js/npm installed:

```sh
python3 scripts/generate-design-fonts.py
```

The script pins [LVGL's `lv_font_conv`](https://github.com/lvgl/lv_font_conv)
to version **1.5.3**, which bundles its runtime dependencies. Its npm tarball
integrity is
`sha512-0xJQThBOw2iptFccSXrKDIUTQAwr/2zhKjCI1lATIRgZo8uvYRTmenKafW9yTw6G0y5AyW00tqGpUtYuTuBIbQ==`.
The converter is an MIT-licensed development tool; it is not linked into firmware.
To use an existing installation, including when local package policy prevents
automatic installation, pass `--converter /path/to/lv_font_conv.js`. The script
checks its version before use and does not change package-manager policy.

Fonts and the upstream release archive are downloaded into `.build/design-fonts/`
and verified against fixed SHA-256 checksums before conversion. Only the generated
C files, declarations, provenance and licenses belong in Git. Source and output
paths in converter headers are relative to the repository, so regeneration does
not embed a workstation path. Normal firmware and host builds use the checked-in
C files and do not download fonts or need Node.js.
Generated files and license copies omit trailing whitespace; the checksums below
identify the unmodified upstream inputs.

| Source file | SHA-256 |
| --- | --- |
| `IBMPlexMono-Regular.ttf` | `7c6fbddca4b700be918f5f6183d9bd4464fa427fe435f0b480d77fe2bb8c5a43` |
| `IBMPlexMono-Medium.ttf` | `98fbd727aae340b236955879dabed4d991aac9e8e90b3b2a67ce4a59221cc97c` |
| `IBMPlexMono-SemiBold.ttf` | `f04d7c488ddf7d1fa99f2574efc3406ea4cbe17bb1af3a1ab960f84d0c96a172` |
| `IBM Plex` upstream `LICENSE.txt` | `7e6b2818edbd8f6a01ae80641cc8f16a51080d08fb4e532be3a0b6f74adb07da` |
| `Inter-4.1.zip` | `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e` |
| `Inter-Medium.ttf` | `97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872` |
| `Inter` upstream `LICENSE.txt` | `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a` |
