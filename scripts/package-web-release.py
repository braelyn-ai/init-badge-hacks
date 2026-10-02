#!/usr/bin/env python3
"""Package a guarded StopWatch preservation update; never access a device.

Example, after building the intended firmware from a clean, tagged commit:
  python3 scripts/package-web-release.py --build-id v1.0.0
  python3 scripts/package-web-release.py --verify .build/web-release/releases/v1.0.0

Only the application, bootloader and exact partition table are exported. No
filesystem, NVS, OTA-selection image, backup, merged image or device dump is read.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("badge_partition_guard", ROOT / "scripts/check_flash_layout.py")
layout = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(layout)

SCHEMA_VERSION = 1
FLASH_BYTES = 16 * 1024 * 1024
PSRAM_BYTES = 8 * 1024 * 1024
PARTITION_OFFSET = 0x8000
APP_OFFSET = 0x10000
APP_LIMIT = 0x300000
CHIP_ID = 9  # Espressif image-format chip ID for ESP32-S3.
ARTIFACTS = (
    ("bootloader", "bootloader/bootloader.bin", "bootloader.bin", 0, PARTITION_OFFSET),
    ("partition-table", "partition_table/partition-table.bin", "partition-table.bin", PARTITION_OFFSET, 0x1000),
    ("firmware", "conference_badge.bin", "firmware.bin", APP_OFFSET, APP_LIMIT),
)
LIMITATIONS = [
    "This preservation update requires an exact existing partition-sector match; factory layouts use the separate reviewed first-install path.",
    "Only app0 is updated. OTA boot selection is preserved, so the installed build must be verified after restart.",
    "A fresh computer-clock sync and verified advancing RTC are required after flashing; the binary contains no provisioning timestamp.",
    "Captive-portal discovery has known iOS redirect and DNS compatibility gaps; 192.168.4.1 is the manual fallback.",
    "Profile photos download from avatar.chan.dev only when an attendee saves a recognized account and Wi-Fi is reachable.",
    "Battery runtime and clock retention after complete battery exhaustion remain unqualified.",
]
RELEASE_VERSION = (ROOT / "firmware/factory_badge/version.txt").read_text().strip()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def valid_build_id(value: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,63}", value) or ".." in value:
        raise RuntimeError("Build ID must be a safe lowercase identifier of 1–64 characters")
    return value


def parse_image(data: bytes, label: str, limit: int) -> dict:
    """Validate unsigned ESP-IDF images without invoking a device tool.

    Image header: 24 bytes; each segment has an 8-byte header. XOR checksum is
    the final byte of the next 16-byte block, followed by an optional SHA-256.
    Signed/encrypted/merged/padded images are intentionally outside this writer.
    """
    if len(data) < 24 or len(data) > limit:
        raise RuntimeError(f"{label}: image size is outside its flash bounds")
    if data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise RuntimeError(f"{label}: invalid ESP image header")
    if struct.unpack_from("<H", data, 12)[0] != CHIP_ID:
        raise RuntimeError(f"{label}: expected an ESP32-S3 image")
    if data[2] != 2 or data[3] != 0x4F:
        raise RuntimeError(f"{label}: expected DIO / 16 MiB / 80 MHz flash settings")
    if data[23] not in (0, 1):
        raise RuntimeError(f"{label}: invalid image hash flag")
    cursor, checksum = 24, 0xEF
    first_segment = b""
    for index in range(data[1]):
        if cursor + 8 > len(data):
            raise RuntimeError(f"{label}: truncated segment header")
        _, length = struct.unpack_from("<II", data, cursor)
        cursor += 8
        if length > len(data) - cursor:
            raise RuntimeError(f"{label}: truncated image segment")
        segment = data[cursor:cursor + length]
        if index == 0:
            first_segment = segment
        for byte in segment:
            checksum ^= byte
        cursor += length
    checksum_at = (cursor // 16 + 1) * 16 - 1
    end = checksum_at + 1
    if checksum_at >= len(data) or data[checksum_at] != checksum:
        raise RuntimeError(f"{label}: image checksum mismatch")
    if data[23]:
        if len(data) != end + 32 or hashlib.sha256(data[:end]).digest() != data[end:]:
            raise RuntimeError(f"{label}: image SHA-256 mismatch")
    elif len(data) != end:
        raise RuntimeError(f"{label}: unexpected trailing image bytes")
    result = {"chip_id": CHIP_ID, "segment_count": data[1], "embedded_sha256": bool(data[23])}
    if label == "firmware":
        if len(first_segment) < 256 or struct.unpack_from("<I", first_segment)[0] != 0xABCD5432:
            raise RuntimeError("firmware: missing native ESP-IDF application descriptor")

        def text_field(offset: int, size: int) -> str:
            raw = first_segment[offset:offset + size]
            if b"\0" not in raw:
                raise RuntimeError("firmware: unterminated application descriptor field")
            try:
                text = raw.split(b"\0", 1)[0].decode("ascii")
            except UnicodeDecodeError as exc:
                raise RuntimeError("firmware: invalid application descriptor field") from exc
            if any(ord(char) < 32 or ord(char) == 127 for char in text):
                raise RuntimeError("firmware: invalid application descriptor characters")
            return text

        description = {
            "project_version": text_field(16, 32), "project_name": text_field(48, 32),
            "compile_time": text_field(80, 16), "compile_date": text_field(96, 16),
            "idf_version": text_field(112, 32), "elf_sha256": first_segment[144:176].hex(),
        }
        if description["project_name"] != "conference_badge" or description["idf_version"] != "v5.5.4":
            raise RuntimeError("firmware: expected conference_badge built with ESP-IDF v5.5.4")
        result["application"] = description
    return result


def validate_parts(parts: dict[str, bytes], build_id: str) -> dict:
    valid_build_id(build_id)
    if set(parts) != {item[0] for item in ARTIFACTS}:
        raise RuntimeError("Release must contain exactly the three approved artifacts")
    table = parts["partition-table"]
    if layout.parse_table(table) != layout.EXPECTED_PARTITIONS:
        raise RuntimeError("Partition table differs from the exact preservation layout")
    sector = table.ljust(layout.SECTOR_BYTES, b"\xff")
    sector_hash = layout.validate_layout(table, sector)
    boot = parse_image(parts["bootloader"], "bootloader", PARTITION_OFFSET)
    app = parse_image(parts["firmware"], "firmware", APP_LIMIT)
    if build_id.encode("ascii") + b"\0" not in parts["firmware"]:
        raise RuntimeError("Requested build ID was not found in the firmware image; rebuild or correct --build-id")
    version = app["application"]["project_version"]
    if re.fullmatch(r"v\d+\.\d+\.\d+", build_id) and build_id != f"v{version}":
        raise RuntimeError(f"Build ID {build_id} does not match the app version {version} from version.txt")
    return {"sector_sha256": sector_hash, "bootloader": boot, "firmware": app}


def source_metadata(repo: Path) -> dict:
    def git(*args: str) -> bytes:
        result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, check=True, timeout=20)
        return result.stdout

    commit = git("rev-parse", "HEAD").decode().strip()
    branch = git("branch", "--show-current").decode().strip() or None
    status = git("status", "--porcelain=v1", "-z", "--untracked-files=all")
    return {
        "commit": commit, "branch": branch, "dirty": bool(status),
        "status_sha256": sha256(status),
        "capture_scope": "packaging_worktree",
        "artifact_source_verified": False,
        "note": "Commit, branch and dirty state describe the worktree at packaging time. They do not attest which uncommitted source produced the image; embedded build metadata is reported separately.",
    }


def check_expected(parts: dict[str, bytes], expected: dict[str, str]) -> None:
    for name, expected_hash in expected.items():
        if name not in parts or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            raise RuntimeError("Expected hashes must name an approved artifact and contain 64 lowercase hex characters")
        if sha256(parts[name]) != expected_hash:
            raise RuntimeError(f"{name}: expected SHA-256 does not match the supplied artifact")


def partition_metadata(sector_hash: str) -> dict:
    return {
        "offset": PARTITION_OFFSET, "sector_size": layout.SECTOR_BYTES,
        "padding_byte": 255, "sector_sha256": sector_hash,
        "layout": [{"name": name, "type": kind, "subtype": subtype, "offset": offset, "size_bytes": size, "flags": flags}
                   for kind, subtype, offset, size, name, flags in layout.EXPECTED_PARTITIONS],
    }


def release_manifest(parts: dict[str, bytes], build_id: str, source: dict, validated: dict) -> dict:
    return {
        "schema_version": SCHEMA_VERSION, "build_id": build_id,
        "channel": "stable" if re.fullmatch(r"v\d+\.\d+\.\d+", build_id) else "alpha",
        "created_at": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
        "hardware": {"model": "M5Stack StopWatch", "chip": "esp32s3", "chip_id": CHIP_ID,
                     "flash_bytes": FLASH_BYTES, "psram_bytes": PSRAM_BYTES, "psram_mode": "opi"},
        "flash": {"mode": "dio", "frequency": "80m", "erase_all": False, "requires_partition_match": True,
                  "write_set": [item[0] for item in ARTIFACTS],
                  "preserve_partitions": ["nvs", "otadata", "app1", "ffat", "coredump"],
                  "post_flash": {"clock_protocol": 1, "fresh_clock_sync_required": True,
                                 "rtc_advancement_required": True, "expected_build": build_id,
                                 "storage_ready_required": True, "radios_off_required": True}},
        "partition_table": partition_metadata(validated["sector_sha256"]),
        "artifacts": [{"name": name, "path": filename, "offset": offset,
                       "size_bytes": len(parts[name]), "sha256": sha256(parts[name])}
                      for name, _, filename, offset, _ in ARTIFACTS],
        "source": source,
        "embedded_build": validated["firmware"]["application"],
        "limitations": list(LIMITATIONS),
    }


def verify_release(directory: Path) -> dict:
    directory = directory.resolve()
    manifest_path = directory / "release.json"
    if manifest_path.is_symlink() or not manifest_path.is_file() or manifest_path.stat().st_size > 128 * 1024:
        raise RuntimeError("Release manifest is missing or invalid")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (ValueError, UnicodeError) as exc:
        raise RuntimeError("Release manifest is not valid JSON") from exc
    if not isinstance(manifest, dict) or type(manifest.get("schema_version")) is not int or manifest.get("schema_version") != SCHEMA_VERSION:
        raise RuntimeError("Unsupported release manifest")
    build_id = valid_build_id(manifest.get("build_id", ""))
    entries = manifest.get("artifacts")
    if not isinstance(entries, list) or len(entries) != len(ARTIFACTS):
        raise RuntimeError("Invalid release artifact list")
    allowed_files = {"release.json", *(item[2] for item in ARTIFACTS)}
    if {p.name for p in directory.iterdir()} != allowed_files:
        raise RuntimeError("Release contains missing or unexpected files")
    parts = {}
    for entry, (name, _, filename, offset, limit) in zip(entries, ARTIFACTS):
        if not isinstance(entry, dict) or entry.get("name") != name or entry.get("path") != filename or entry.get("offset") != offset:
            raise RuntimeError("Release artifact name, path or flash offset is not approved")
        path = directory / filename
        if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
            raise RuntimeError(f"{name}: invalid artifact file or bounds")
        data = path.read_bytes()
        if entry.get("size_bytes") != len(data) or entry.get("sha256") != sha256(data):
            raise RuntimeError(f"{name}: release size or SHA-256 mismatch")
        parts[name] = data
    validated = validate_parts(parts, build_id)
    expected_manifest = release_manifest(parts, build_id, {}, validated)
    for key in ("hardware", "flash", "partition_table", "embedded_build"):
        # JSON distinguishes booleans from numbers; Python's equality does not.
        if json.dumps(manifest.get(key), sort_keys=True) != json.dumps(expected_manifest[key], sort_keys=True):
            raise RuntimeError(f"Release {key} does not match validated artifact requirements")
    if manifest.get("channel") != expected_manifest["channel"] or manifest.get("limitations") != LIMITATIONS:
        raise RuntimeError("Release must retain its channel and limitations")
    source = manifest.get("source")
    if not isinstance(source, dict) or type(source.get("dirty")) is not bool or source.get("artifact_source_verified") is not False or source.get("capture_scope") != "packaging_worktree":
        raise RuntimeError("Release must retain honest packaging-worktree source metadata")
    if not isinstance(source.get("commit"), str) or not re.fullmatch(r"[0-9a-f]{40,64}", source["commit"]) or not isinstance(source.get("status_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", source["status_sha256"]):
        raise RuntimeError("Release source metadata hashes are invalid")
    return manifest


def package_release(artifact_dir: Path, output_root: Path, repo: Path, build_id: str,
                    expected_hashes: dict[str, str] | None = None) -> Path:
    valid_build_id(build_id)
    artifact_dir, output_root, repo = artifact_dir.resolve(), output_root.resolve(), repo.resolve()
    if not output_root.is_relative_to(repo / ".build"):
        raise RuntimeError("Release output must stay inside this repository's ignored .build directory")
    target = output_root / build_id
    if target.exists() or target.is_symlink():
        raise RuntimeError("Release directory already exists; immutable releases are never overwritten")
    parts = {}
    for name, relative, _, _, limit in ARTIFACTS:
        path = artifact_dir / relative
        if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(artifact_dir):
            raise RuntimeError(f"{name}: missing, symlinked or unexpected native build artifact")
        if path.stat().st_size > limit:
            raise RuntimeError(f"{name}: artifact exceeds its flash bounds")
        parts[name] = path.read_bytes()
    check_expected(parts, expected_hashes or {})
    validated = validate_parts(parts, build_id)
    manifest = release_manifest(parts, build_id, source_metadata(repo), validated)
    if manifest["channel"] == "stable" and manifest["source"]["dirty"]:
        raise RuntimeError("Stable releases must be packaged from a clean, committed worktree")
    output_root.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".package-", dir=output_root))
    try:
        for name, _, filename, _, _ in ARTIFACTS:
            with (temporary / filename).open("xb") as output:
                output.write(parts[name]); output.flush(); os.fsync(output.fileno())
        with (temporary / "release.json").open("x", encoding="utf-8") as output:
            json.dump(manifest, output, indent=2, sort_keys=True)
            output.write("\n"); output.flush(); os.fsync(output.fileno())
        verify_release(temporary)
        # No publisher or device writer is invoked. Directory publication is
        # local and atomic, only after every exported byte has been reverified.
        if target.exists() or target.is_symlink():
            raise RuntimeError("Release directory appeared during packaging; refusing overwrite")
        temporary.rename(target)
    except BaseException:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    return target


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--artifact-dir", type=Path, default=ROOT / ".build/factory")
    parser.add_argument("--output-root", type=Path, default=ROOT / ".build/web-release/releases")
    parser.add_argument("--build-id", help=f"Exact runtime build ID embedded in the app image (default v{RELEASE_VERSION} from version.txt)")
    parser.add_argument("--expected-sha256", action="append", default=[], metavar="NAME=SHA256",
                        help="Optional previously reviewed digest for firmware, bootloader or partition-table")
    parser.add_argument("--verify", type=Path, metavar="RELEASE_DIRECTORY", help="Read-only verification of an existing release")
    args = parser.parse_args(argv)
    if args.verify:
        if args.build_id or args.expected_sha256:
            parser.error("--verify cannot be combined with packaging arguments")
        manifest = verify_release(args.verify)
        print(f"Verified {manifest['build_id']}: three preservation-update artifacts and exact partition sector.")
        return 0
    args.build_id = args.build_id or f"v{RELEASE_VERSION}"
    expected = {}
    for pair in args.expected_sha256:
        name, sep, value = pair.partition("=")
        if not sep or name in expected:
            parser.error("Expected hashes must use unique NAME=SHA256 pairs")
        expected[name] = value
    directory = package_release(args.artifact_dir, args.output_root, ROOT, args.build_id, expected)
    print(f"Packaged {args.build_id}: {directory}")
    print("Local artifacts only. No device was accessed and nothing was published.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"Release packaging failed: {error}", file=sys.stderr)
        raise SystemExit(1)
