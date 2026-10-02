#!/usr/bin/env python3
"""Set and verify a StopWatch clock over USB using only Python's POSIX stdlib.

Reads only clock replies and never echoes unrelated serial diagnostics. No
profile, photo, credentials, or compile-time date is added to the artifact.
"""

import argparse
import datetime as dt
import json
from pathlib import Path
import os
import secrets
import select
import sys
import time

MIN_EPOCH = 1704067200
MAX_EPOCH = 4102444800
RELEASE_VERSION = (Path(__file__).resolve().parents[1] / "firmware/factory_badge/version.txt").read_text().strip()
EXPECTED_BUILD = f"v{RELEASE_VERSION}"


class ClockError(Exception):
    pass


class SerialPort:
    """Small bounded POSIX serial transport; does not toggle DTR/RTS to reset."""

    def __init__(self, path):
        import termios
        import tty

        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            tty.setraw(self.fd)
            attrs = termios.tcgetattr(self.fd)
            attrs[2] |= termios.CLOCAL | termios.CREAD
            attrs[2] &= ~getattr(termios, "HUPCL", 0)
            attrs[4] = attrs[5] = termios.B115200
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            termios.tcflush(self.fd, termios.TCIFLUSH)
        except Exception:
            os.close(self.fd)
            raise
        self.pending = bytearray()
        self.discard = False

    def close(self):
        os.close(self.fd)

    def write(self, value):
        deadline = time.monotonic() + 2
        while value:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise ClockError("USB write timed out")
            try:
                count = os.write(self.fd, value)
            except BlockingIOError:
                continue
            value = value[count:]

    def readline(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b"\n" in self.pending:
                line, _, rest = self.pending.partition(b"\n")
                self.pending = bytearray(rest)
                if self.discard or len(line) > 2048:
                    self.discard = False
                    continue
                return bytes(line)
            if len(self.pending) > 2048:
                self.pending.clear()
                self.discard = True
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                return b""
            try:
                value = os.read(self.fd, 1024)
            except BlockingIOError:
                continue
            if not value:
                raise ClockError("USB disconnected before clock verification")
            self.pending.extend(value)
        return b""


def current_offset(epoch):
    return int(dt.datetime.fromtimestamp(epoch).astimezone().utcoffset().total_seconds() / 60)


def read_reply(port, prefix, nonce, timeout, monotonic=time.monotonic):
    deadline = monotonic() + timeout
    while monotonic() < deadline:
        line = port.readline(max(0, deadline - monotonic()))
        if not line.startswith(prefix):
            continue
        try:
            reply = json.loads(line[len(prefix):])
        except (ValueError, UnicodeDecodeError):
            continue
        if isinstance(reply, dict) and reply.get("nonce") == nonce:
            return reply
    return None


def validate_reply(reply, now, offset):
    if type(reply.get("protocol")) is not int or reply["protocol"] != 1:
        raise ClockError("Unsupported clock protocol")
    if reply.get("ok") is not True or reply.get("valid") is not True:
        # Whitelist the error so a malformed serial response cannot leak data.
        reason = reply.get("error")
        known = {"invalid_nonce", "invalid_value", "rtc_unavailable", "rtc_read_failed",
                 "rtc_init_failed", "rtc_write_failed", "rtc_flag_failed", "rtc_verify_failed",
                 "offset_store_failed", "system_clock_failed", "clock_unset"}
        raise ClockError("Board rejected clock sync: " + (reason if isinstance(reason, str) and reason in known else "invalid acknowledgment"))
    if reply.get("source") not in ("computer", "rtc"):
        raise ClockError("Clock has no verified RTC source")
    if type(reply.get("offset_minutes")) is not int or reply["offset_minutes"] != offset:
        raise ClockError("Clock display offset differs from the requested offset")
    for name in ("epoch", "rtc_epoch"):
        value = reply.get(name)
        if type(value) is not int or not MIN_EPOCH <= value < MAX_EPOCH or abs(value - now) > 3:
            raise ClockError("Clock readback differs from current computer time")
    if abs(reply["epoch"] - reply["rtc_epoch"]) > 1:
        raise ClockError("System and hardware RTC readback disagree")


def validate_readiness(reply, require_store=True):
    if reply.get("build") != EXPECTED_BUILD:
        raise ClockError("Unexpected firmware build; expected the reviewed conference scaffold")
    for field, expected in (("board", 30), ("flash_bytes", 16777216), ("psram_bytes", 8388608)):
        if type(reply.get(field)) is not int or reply[field] != expected:
            raise ClockError("Wrong board or memory configuration; expected StopWatch, 16 MiB flash, 8 MiB PSRAM")
    if type(reply.get("store_ready")) is not bool:
        raise ClockError("Firmware did not report profile storage readiness")
    if require_store and reply["store_ready"] is not True:
        raise ClockError("Profile storage is unavailable; blank/factory units require explicit storage provisioning. No formatting or erasure was attempted")
    if reply.get("clock_valid") is not True or reply.get("rtc") is not True:
        raise ClockError("Firmware does not report a valid hardware clock")
    if (reply.get("setup") is not False or type(reply.get("wifi_mode")) is not int or
            reply["wifi_mode"] != 0 or type(reply.get("bluetooth")) is not int or reply["bluetooth"] != 0):
        raise ClockError("Unit is not in offline operating mode; close setup and verify Wi-Fi and Bluetooth are off")


def provision(port, timeout=30, offset=None, now=time.time, monotonic=time.monotonic,
              sleep=time.sleep, nonce_factory=lambda: secrets.token_hex(12),
              initialize_profile_storage=False):
    deadline = monotonic() + timeout
    reply = None
    while monotonic() < deadline:
        epoch = int(now())  # Fresh for every device and every retry after boot.
        if not MIN_EPOCH <= epoch < MAX_EPOCH:
            raise ClockError("Computer time is outside supported years 2024–2099")
        selected_offset = current_offset(epoch) if offset is None else offset
        if type(selected_offset) is not int or not -840 <= selected_offset <= 840:
            raise ClockError("Offset must be an integer from -840 to 840 minutes")
        nonce = nonce_factory()
        payload = {"op": "clock_set", "epoch": epoch, "offset_minutes": selected_offset, "nonce": nonce}
        port.write(json.dumps(payload, separators=(",", ":")).encode() + b"\n")
        reply = read_reply(port, b"CLOCK_ACK ", nonce, min(2, max(0, deadline - monotonic())), monotonic)
        if reply is not None:
            validate_reply(reply, int(now()), selected_offset)
            break
    if reply is None:
        raise ClockError("No fresh CLOCK_ACK received; clock was NOT verified. Check firmware, USB port, and other serial monitors")
    # A write/read equality alone can pass even if the oscillator is stopped.
    sleep(1.2)
    nonce = nonce_factory()
    port.write(json.dumps({"op": "clock_status", "nonce": nonce}).encode() + b"\n")
    status = read_reply(port, b"CLOCK_STATUS ", nonce, 3, monotonic)
    if status is None:
        raise ClockError("No fresh CLOCK_STATUS readback after sync")
    validate_reply(status, int(now()), selected_offset)
    if status["rtc_epoch"] <= reply["rtc_epoch"]:
        raise ClockError("Hardware RTC did not advance after sync")
    nonce = nonce_factory()
    port.write(json.dumps({"op": "status", "nonce": nonce}).encode() + b"\n")
    ready = read_reply(port, b"CONFERENCE_STATUS ", nonce, 3, monotonic)
    if ready is None:
        raise ClockError("No fresh CONFERENCE_STATUS received; clock synced but unit readiness was NOT verified")
    storage_initialized = False
    if initialize_profile_storage and ready.get("store_ready") is False:
        # The destructive opt-in is limited to a verified target with an
        # unavailable store; it is never sent to a ready/personalized filesystem.
        validate_readiness(ready, require_store=False)
        nonce = nonce_factory()
        port.write(json.dumps({"op": "initialize_conference_storage", "nonce": nonce,
                               "confirm": "ERASE_FFAT_FOR_CONFERENCE"}).encode() + b"\n")
        initialized = read_reply(port, b"STORAGE_ACK ", nonce, 30, monotonic)
        if initialized is None or initialized.get("ok") is not True or initialized.get("store_ready") is not True:
            raise ClockError("Explicit profile storage initialization was not verified; do not mark this unit ready")
        nonce = nonce_factory()
        port.write(json.dumps({"op": "status", "nonce": nonce}).encode() + b"\n")
        ready = read_reply(port, b"CONFERENCE_STATUS ", nonce, 3, monotonic)
        if ready is None:
            raise ClockError("Storage acknowledged, but fresh unit status was not received")
        storage_initialized = True
    validate_readiness(ready)
    return {"verified": True, "unit_ready": True, "build": ready["build"],
            "storage_initialized": storage_initialized,
            "epoch": status["epoch"], "rtc_epoch": status["rtc_epoch"],
            "offset_minutes": selected_offset, "source": status["source"]}


def main():
    global EXPECTED_BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Live /dev/cu.usbmodem… or /dev/ttyACM… port")
    parser.add_argument("--offset-minutes", type=int, help="Event UTC offset; defaults to computer's current local offset")
    parser.add_argument("--timeout", type=float, default=30, help="Maximum wait for clock_set acknowledgment (default 30 seconds)")
    parser.add_argument("--initialize-profile-storage", action="store_true",
                        help="DESTRUCTIVE opt-in for blank/factory units: when profile storage is unavailable, erase and format ONLY ffat. All existing filesystem data is lost; NVS is retained")
    parser.add_argument("--expected-build", default=EXPECTED_BUILD,
                        help=f"Build ID the unit must report (default {EXPECTED_BUILD}, from version.txt; a release's post_flash.expected_build)")
    args = parser.parse_args()
    EXPECTED_BUILD = args.expected_build
    if args.timeout <= 0 or args.timeout > 120:
        parser.error("--timeout must be greater than zero and at most 120")
    if args.offset_minutes is not None and not -840 <= args.offset_minutes <= 840:
        parser.error("--offset-minutes must be from -840 to 840")
    if os.name != "posix":
        parser.error("This provisioning script supports macOS and Linux")
    port = None
    try:
        # USB can briefly disappear when the upload resets into the application.
        deadline = time.monotonic() + args.timeout
        while port is None:
            try:
                port = SerialPort(args.port)
            except OSError:
                if time.monotonic() >= deadline:
                    raise ClockError("USB port did not become available; rediscover the live device port")
                time.sleep(0.25)
        result = provision(port, timeout=max(1, deadline - time.monotonic()), offset=args.offset_minutes,
                           initialize_profile_storage=args.initialize_profile_storage)
        print("UNIT_READY " + json.dumps(result, sort_keys=True))
        return 0
    except (ClockError, OSError) as error:
        print("Clock provisioning failed: " + str(error), file=sys.stderr)
        return 1
    finally:
        if port is not None:
            port.close()


if __name__ == "__main__":
    raise SystemExit(main())
