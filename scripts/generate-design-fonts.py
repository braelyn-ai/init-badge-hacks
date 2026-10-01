#!/usr/bin/env python3
"""Regenerate the badge's LVGL fonts from checksum-verified upstream sources."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".build/design-fonts"
UI = ROOT / "firmware/factory_badge/main/ui"
VERSION = "1.5.3"
PLEX_REV = "78cd4223d8de9fcb78cba84eadecb269c56093c5"
PLEX_URL = f"https://raw.githubusercontent.com/IBM/plex/{PLEX_REV}"
INPUTS = {
    "IBMPlexMono-Regular.ttf": (
        f"{PLEX_URL}/packages/plex-mono/fonts/complete/ttf/IBMPlexMono-Regular.ttf",
        "7c6fbddca4b700be918f5f6183d9bd4464fa427fe435f0b480d77fe2bb8c5a43",
    ),
    "IBMPlexMono-Medium.ttf": (
        f"{PLEX_URL}/packages/plex-mono/fonts/complete/ttf/IBMPlexMono-Medium.ttf",
        "98fbd727aae340b236955879dabed4d991aac9e8e90b3b2a67ce4a59221cc97c",
    ),
    "IBMPlexMono-SemiBold.ttf": (
        f"{PLEX_URL}/packages/plex-mono/fonts/complete/ttf/IBMPlexMono-SemiBold.ttf",
        "f04d7c488ddf7d1fa99f2574efc3406ea4cbe17bb1af3a1ab960f84d0c96a172",
    ),
    "IBMPlexMono-OFL.txt": (
        f"{PLEX_URL}/LICENSE.txt",
        "7e6b2818edbd8f6a01ae80641cc8f16a51080d08fb4e532be3a0b6f74adb07da",
    ),
    "Inter-4.1.zip": (
        "https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip",
        "9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e",
    ),
}
INTER_FILES = {
    "extras/ttf/Inter-Medium.ttf": (
        "Inter-Medium.ttf",
        "97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872",
    ),
    "LICENSE.txt": (
        "Inter-OFL.txt",
        "262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a",
    ),
}


def verify(data, expected, label):
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise SystemExit(f"SHA-256 mismatch for {label}: {actual}")


def download(name, url, digest):
    target = CACHE / name
    if target.exists():
        data = target.read_bytes()
    else:
        print(f"Downloading {name}", flush=True)
        with urllib.request.urlopen(url, timeout=120) as response:
            data = response.read()
    verify(data, digest, name)
    target.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--converter", type=Path,
        help=f"Use an installed lv_font_conv.js (must be version {VERSION}).",
    )
    args = parser.parse_args()
    if args.converter:
        command = ["node", str(args.converter.resolve())]
    else:
        command = ["npx", "--yes", f"--package=lv_font_conv@{VERSION}", "lv_font_conv"]
    version = subprocess.check_output(command + ["--version"], cwd=ROOT, text=True).strip()
    if version != VERSION:
        raise SystemExit(f"Expected lv_font_conv {VERSION}, got {version}")

    CACHE.mkdir(parents=True, exist_ok=True)
    for name, (url, digest) in INPUTS.items():
        download(name, url, digest)
    with zipfile.ZipFile(CACHE / "Inter-4.1.zip") as archive:
        for entry, (name, digest) in INTER_FILES.items():
            data = archive.read(entry)
            verify(data, digest, entry)
            (CACHE / name).write_bytes(data)

    licenses = UI / "fonts"
    licenses.mkdir(parents=True, exist_ok=True)
    for name in ("IBMPlexMono-OFL.txt", "Inter-OFL.txt"):
        # Preserve the license text while removing upstream trailing spaces.
        (licenses / name).write_text("\n".join(
            line.rstrip() for line in (CACHE / name).read_text().splitlines()
        ).rstrip() + "\n")

    for family, source, license_family, sizes in (
        ("mono", "IBMPlexMono-Medium.ttf", "IBMPlexMono", (12, 18, 24, 32)),
        ("mono", "IBMPlexMono-Regular.ttf", "IBMPlexMono", (20,)),
        ("mono_regular", "IBMPlexMono-Regular.ttf", "IBMPlexMono", (24,)),
        ("mono_semibold", "IBMPlexMono-SemiBold.ttf", "IBMPlexMono", (12,)),
        ("sans", "Inter-Medium.ttf", "Inter", (12, 14, 16, 20, 24, 32)),
    ):
        for size in sizes:
            name = f"font_{family}_{size}"
            output = UI / f"{name}.c"
            subprocess.run(command + [
                "--font", str((CACHE / source).relative_to(ROOT)),
                "--range", "0x20-0x7e,0xa0-0xff",
                "--size", str(size), "--bpp", "4", "--format", "lvgl",
                "--no-compress", "--lv-include", "lvgl.h",
                "--lv-font-name", name,
                "--output", str(output.relative_to(ROOT)),
            ], cwd=ROOT, check=True)
            notice = (
                "/* SPDX-License-Identifier: OFL-1.1\n"
                " * Derived font data; upstream copyright and license: "
                f"fonts/{license_family}-OFL.txt.\n"
                " * Regenerate with scripts/generate-design-fonts.py.\n */\n\n"
            )
            output.write_text(notice + output.read_text().rstrip() + "\n")
            print(f"Generated {name}", flush=True)


if __name__ == "__main__":
    main()
