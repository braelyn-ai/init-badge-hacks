#!/usr/bin/env python3
"""Read the partition sector and refuse incompatible application uploads."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time


SECTOR_BYTES = 0x1000
PARTITION_RECORD = struct.Struct("<HBBII16sI")
PINNED_CORE = "3.3.10"
PINNED_ESPTOOL = "5.3.0"
EXPECTED_PARTITIONS = [
    (1, 2, 0x9000, 0x5000, "nvs", 0),
    (1, 0, 0xE000, 0x2000, "otadata", 0),
    (0, 0x10, 0x10000, 0x300000, "app0", 0),
    (0, 0x11, 0x310000, 0x300000, "app1", 0),
    (1, 0x81, 0x610000, 0x9E0000, "ffat", 0),
    (1, 3, 0xFF0000, 0x10000, "coredump", 0),
]


def parse_table(data):
    if not data or len(data) > SECTOR_BYTES or len(data) % 32:
        raise RuntimeError("Invalid partition table size")
    entries = []
    for offset in range(0, len(data), 32):
        record = data[offset:offset + 32]
        if record[:2] == b"\xeb\xeb":
            if record[:16] != b"\xeb\xeb" + b"\xff" * 14:
                raise RuntimeError("Invalid partition checksum record")
            if record[16:] != hashlib.md5(data[:offset]).digest():
                raise RuntimeError("Partition checksum mismatch")
            if not entries or any(byte != 255 for byte in data[offset + 32:]):
                raise RuntimeError("Invalid partition table padding")
            return entries
        if record[:2] != b"\xaa\x50":
            raise RuntimeError("Blank or unrecognized partition table")
        _, kind, subtype, start, size, raw_name, flags = PARTITION_RECORD.unpack(record)
        try:
            name = raw_name.split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as exc:
            raise RuntimeError("Invalid partition label") from exc
        if size == 0 or start < 0x9000 or start + size > 0x1000000:
            raise RuntimeError("Partition outside the established 16 MiB layout")
        entries.append((kind, subtype, start, size, name, flags))
    raise RuntimeError("Partition table has no checksum record")


def validate_layout(compiled, current):
    if parse_table(compiled) != EXPECTED_PARTITIONS:
        raise RuntimeError("Compiled firmware does not use the established partition layout")
    if len(current) != SECTOR_BYTES:
        raise RuntimeError("Incomplete partition sector read; upload blocked")
    parse_table(current)
    expected = compiled.ljust(SECTOR_BYTES, b"\xff")
    if current != expected:
        raise RuntimeError(
            "Device partition layout differs from this firmware; upload blocked. "
            "Factory or other layouts need a separate backup and authorized migration."
        )
    return hashlib.sha256(expected).hexdigest()


def discover_esptool(arduino_cli, fqbn, sketch):
    result = subprocess.run(
        [arduino_cli, "compile", "--fqbn", fqbn, "--show-properties=expanded", str(sketch)],
        capture_output=True, text=True, timeout=30,
    )
    if result.returncode:
        raise RuntimeError("Could not resolve the pinned Arduino toolchain; upload blocked")
    properties = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    platform = Path(properties.get("runtime.platform.path", ""))
    if platform.name != PINNED_CORE or properties.get("build.partitions") != "app3M_fat9M_16MB":
        raise RuntimeError("Install the pinned ESP32 core 3.3.10; upload blocked")
    tool_path = properties.get("tools.esptool_py.path")
    pinned_path = properties.get(f"runtime.tools.esptool_py-{PINNED_ESPTOOL}.path")
    if not tool_path or tool_path != pinned_path:
        raise RuntimeError("Pinned esptool 5.3.0 was not resolved by Arduino CLI")
    executable = Path(tool_path) / properties.get("tools.esptool_py.cmd", "esptool")
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise RuntimeError("Pinned esptool executable is unavailable")
    return executable


def discovered_ports(arduino_cli):
    result = subprocess.run([arduino_cli, "board", "list", "--format", "json"],
                            # Discovery can exceed 10s while the host is under build load.
                            capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError("USB discovery failed; upload blocked. Rediscover the device port")
    try:
        ports = json.loads(result.stdout)["detected_ports"]
    except (ValueError, KeyError, TypeError) as exc:
        raise RuntimeError("USB discovery response is invalid; upload blocked") from exc
    return [entry.get("port", {}) for entry in ports if isinstance(entry, dict)]


def port_identity(port):
    properties = port.get("properties", {})
    if port.get("protocol") != "serial" or not isinstance(properties, dict):
        return None
    values = [properties.get(key) for key in ("serialNumber", "vid", "pid")]
    if any(not isinstance(value, str) or not value.strip() or len(value) > 128 for value in values):
        return None
    return {"serialNumber": values[0], "vid": values[1].lower(), "pid": values[2].lower()}


def selected_identity(ports, address):
    selected = [port for port in ports if port.get("address") == address]
    if len(selected) != 1 or port_identity(selected[0]) is None:
        raise RuntimeError("Selected USB port has no unique serial number/VID/PID; upload blocked. Rediscover the device port")
    identity = port_identity(selected[0])
    if sum(port_identity(port) == identity for port in ports) != 1:
        raise RuntimeError("USB identity is ambiguous; upload blocked. Connect only the intended device")
    return identity


def resolve_identity(arduino_cli, identity, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        matches = [port for port in discovered_ports(arduino_cli) if port_identity(port) == identity]
        if len(matches) > 1:
            raise RuntimeError("USB identity became ambiguous; stopped before proceeding")
        if len(matches) == 1:
            address = matches[0].get("address")
            if isinstance(address, str) and address.startswith("/dev/") and "\n" not in address:
                return address
            raise RuntimeError("Resolved USB port is invalid; stopped before proceeding")
        time.sleep(0.3)
    raise RuntimeError("Verified USB device did not return after reset; stopped. Rediscover its port and rerun the full command")


def run_preflight(args):
    # Validate the build before touching a device, including --skip-build runs.
    compiled = args.compiled.read_bytes()
    if parse_table(compiled) != EXPECTED_PARTITIONS:
        raise RuntimeError("Compiled firmware does not use the established partition layout")
    esptool = discover_esptool(args.arduino_cli, args.fqbn, args.sketch)
    identity = selected_identity(discovered_ports(args.arduino_cli), args.port)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    private_dir = Path(tempfile.mkdtemp(prefix="partition-", dir=args.output_dir))
    current_path = private_dir / "partition-sector.bin"
    # Read only. A normal reset returns the existing app even if validation fails.
    command = [str(esptool), "--chip", "esp32s3", "--port", args.port,
               "--baud", "460800", "--before", "default-reset", "--after", "hard-reset",
               "read-flash", "0x8000", "0x1000", str(current_path)]
    try:
        result = subprocess.run(command, capture_output=True, timeout=60)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError("Partition read timed out; upload blocked") from exc
    # Hardware identifiers stay in ignored local output, not normal task output.
    (private_dir / "read.log").write_bytes(result.stdout + result.stderr)
    if result.returncode or not current_path.is_file():
        raise RuntimeError("Partition read failed; upload blocked (see .build/partition-preflight)")
    digest = validate_layout(compiled, current_path.read_bytes())
    (private_dir / "verified.json").write_text(json.dumps({
        "partition_sha256": digest, "partition_match": True,
        "core": PINNED_CORE, "esptool": PINNED_ESPTOOL,
    }, sort_keys=True) + "\n", encoding="utf-8")
    # Keep the USB identity private. The shell reuses it after upload as well,
    # never guessing that a newly appearing device is the one just verified.
    args.state_file.write_text(json.dumps({"identity": identity}) + "\n", encoding="utf-8")
    os.chmod(args.state_file, 0o600)
    address = resolve_identity(args.arduino_cli, identity)
    print("Partition layout verified; existing data boundaries match the compiled firmware.", file=sys.stderr)
    print(address)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--fqbn")
    parser.add_argument("--arduino-cli", required=True)
    parser.add_argument("--compiled", type=Path)
    parser.add_argument("--sketch", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--state-file", type=Path, required=True)
    parser.add_argument("--resolve-port", action="store_true",
                        help="After upload, locate only the USB identity verified by preflight")
    args = parser.parse_args()
    if args.resolve_port:
        try:
            identity = json.loads(args.state_file.read_text())["identity"]
        except (ValueError, KeyError, TypeError) as exc:
            raise RuntimeError("Verified USB identity record is invalid; stopped") from exc
        if not isinstance(identity, dict) or set(identity) != {"serialNumber", "vid", "pid"}:
            raise RuntimeError("Verified USB identity record is invalid; stopped")
        print(resolve_identity(args.arduino_cli, identity))
    else:
        if not all((args.port, args.fqbn, args.compiled, args.sketch, args.output_dir)):
            parser.error("preflight requires --port, --fqbn, --compiled, --sketch and --output-dir")
        run_preflight(args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
