#!/usr/bin/env python3
"""Unattended batch station: watch USB, convert factory StopWatches, provision them.

Plug units in; each one is probed, classified by its exact partition sector and
handled without prompts. Factory units get the reviewed first-install plan
(no per-unit backup), then clock/storage provisioning and the UNIT_READY check.
Conference badges on an older build with a recognized app0 boot selector get a
preservation update (bootloader + app only; settings, profile and storage kept),
then the same readiness check without storage initialization. Conference badges
running this exact build are only re-provisioned. Anything else (unknown layout
or boot selector, not a StopWatch) is left untouched.

The ledger and per-unit logs stay private in .build/station/; they contain
hardware identifiers. Run one station process per computer.
"""

import argparse
import datetime as dt
import glob
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
from check_flash_layout import EXPECTED_PARTITIONS, SECTOR_BYTES, parse_table  # noqa: E402

DEFAULT_ESPTOOL = Path.home() / "Library/Arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool"
# Same exact sectors as web-flasher/src/factory.js. A new factory revision needs
# its own reviewed entry; matching some offsets is not enough.
FACTORY_SECTOR_SHA256 = "551de813efa412911f6d15afef5c0cdb9b3c2d9d8dece864280fa39963117e8f"
TARGET_SECTOR_SHA256 = "0bcf1787e46f4bf1ce9ad28bd22c2257e715987e913add5008c0265d3feb2fcd"
SETTINGS_RANGE = (0x9000, 0x7000)     # factory NVS/OTA data; erased OTA data selects app0
UNUSED_RANGE = (0x310000, 0xCF0000)   # app1, ffat and coredump
OTADATA_RANGE = (0xE000, 0x2000)


def app0_selectors():
    """SHA-256s of otadata states that boot app0, matching web-flasher guards.js:
    erased, the pinned IDF first-boot selector, and Arduino-ESP32 3.3.10
    boot_app0.bin. Any other state (e.g. app1 selected) is never updated."""
    erased = bytearray(b"\xff" * OTADATA_RANGE[1])
    idf = bytearray(erased)
    idf[0:4] = (1).to_bytes(4, "little")
    idf[24:28] = (2).to_bytes(4, "little")
    idf[28:32] = (0x4743989A).to_bytes(4, "little")
    arduino = bytearray(idf)
    arduino[24:28] = (0xFFFFFFFF).to_bytes(4, "little")
    arduino[4096:4100] = (0).to_bytes(4, "little")
    return {hashlib.sha256(bytes(state)).hexdigest() for state in (erased, idf, arduino)}


APP0_SELECTOR_SHA256 = app0_selectors()
MAC_PATTERN = re.compile(rb"MAC:\s*((?:[0-9a-f]{2}:){5}[0-9a-f]{2})", re.I)


def now():
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


def load_release_verifier():
    import importlib.util
    spec = importlib.util.spec_from_file_location("package_web_release", REPO_ROOT / "scripts/package-web-release.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.verify_release


class Artifacts:
    """Either a packaged release (preferred: verified release.json, its exact
    files and expected build) or a raw build output directory."""

    def __init__(self, directory, workdir, release=None):
        self.expected_build = None
        if release:
            release = Path(release)
            try:
                manifest = load_release_verifier()(release)
            except RuntimeError as error:
                raise SystemExit(f"Release {release} failed verification: {error}")
            files = {entry["name"]: release / entry["path"] for entry in manifest["artifacts"]}
            self.boot, self.app = files["bootloader"], files["firmware"]
            table = files["partition-table"].read_bytes()
            self.expected_build = manifest["flash"]["post_flash"]["expected_build"]
            self.build_id = manifest["build_id"]
        else:
            directory = Path(directory)
            self.boot = directory / "devices_badge.ino.bootloader.bin"
            self.app = directory / "devices_badge.ino.bin"
            table = (directory / "devices_badge.ino.partitions.bin").read_bytes()
            self.build_id = None
        if parse_table(table) != EXPECTED_PARTITIONS:
            raise SystemExit("Compiled partition table is not the established conference layout")
        sector = table.ljust(SECTOR_BYTES, b"\xff")
        if hashlib.sha256(sector).hexdigest() != TARGET_SECTOR_SHA256:
            raise SystemExit("Compiled partition sector is not the approved target sector")
        self.table = workdir / "partition-sector.bin"
        self.table.write_bytes(sector)
        # Written (not just erased) so esptool hash-verifies the cleared settings.
        self.blank_settings = workdir / "blank-settings.bin"
        self.blank_settings.write_bytes(b"\xff" * SETTINGS_RANGE[1])
        self.app_sha256 = hashlib.sha256(self.app.read_bytes()).hexdigest()
        self.boot_sha256 = hashlib.sha256(self.boot.read_bytes()).hexdigest()


class Ledger:
    """Append-only JSONL record keyed by chip MAC; survives station restarts."""

    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        self.state = {}
        if path.exists():
            for line in path.read_text().splitlines():
                try:
                    entry = json.loads(line)
                    self.state[entry["mac"]] = entry["event"]
                except (ValueError, KeyError, TypeError):
                    continue

    def get(self, mac):
        with self.lock:
            return self.state.get(mac)

    def record(self, mac, event, **fields):
        with self.lock:
            self.state[mac] = event
            with self.path.open("a") as handle:
                handle.write(json.dumps({"time": now(), "mac": mac, "event": event, **fields}) + "\n")


class Station:
    def __init__(self, args):
        self.args = args
        self.root = REPO_ROOT / ".build" / "station"
        (self.root / "logs").mkdir(parents=True, exist_ok=True)
        self.workdir = Path(tempfile.mkdtemp(prefix="artifacts-", dir=self.root))
        self.artifacts = Artifacts(args.artifact_dir, self.workdir, args.release)
        self.ledger = Ledger(self.root / "ledger.jsonl")
        self.slots = threading.Semaphore(args.max_parallel)
        self.print_lock = threading.Lock()
        self.counts = {"ready": 0, "skipped": 0, "failed": 0}

    # -- output -----------------------------------------------------------
    def say(self, port, message):
        slot = port.replace("/dev/cu.usbmodem", "").replace("/dev/ttyACM", "acm")
        with self.print_lock:
            print(f"{time.strftime('%H:%M:%S')}  [{slot:>6}]  {message}", flush=True)

    def chime(self, outcome):
        if self.args.quiet or sys.platform != "darwin":
            return
        sound = {"ready": "Glass", "skipped": "Tink", "failed": "Basso"}[outcome]
        subprocess.Popen(["afplay", f"/System/Library/Sounds/{sound}.aiff"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # -- tools ------------------------------------------------------------
    def esptool(self, log, port, *args, after="hard-reset", timeout=120):
        command = [str(self.args.esptool), "--chip", "esp32s3", "--port", port, "--baud", "460800",
                   "--before", "default-reset", "--after", after, *args]
        log.write(f"\n$ {' '.join(command)}\n".encode())
        try:
            result = subprocess.run(command, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            log.write(b"TIMEOUT\n")
            return 124, b""
        output = result.stdout + result.stderr
        log.write(output)
        return result.returncode, output

    def provision(self, log, port, initialize_storage=True):
        command = [sys.executable, str(REPO_ROOT / "scripts/provision-clock.py"), port, "--timeout", "60"]
        # Updates keep existing storage: unavailable storage fails rather than formats.
        if initialize_storage:
            command.append("--initialize-profile-storage")
        if self.artifacts.expected_build:
            command += ["--expected-build", self.artifacts.expected_build]
        if self.args.offset_minutes is not None:
            command += ["--offset-minutes", str(self.args.offset_minutes)]
        log.write(f"\n$ {' '.join(command)}\n".encode())
        try:
            result = subprocess.run(command, capture_output=True, timeout=180)
        except subprocess.TimeoutExpired:
            return False, "clock/readiness check timed out"
        log.write(result.stdout + result.stderr)
        if result.returncode == 0 and result.stdout.startswith(b"UNIT_READY "):
            return True, ""
        reason = result.stderr.decode(errors="replace").strip().splitlines()
        return False, reason[-1] if reason else "readiness check failed"

    # -- per-unit flow ----------------------------------------------------
    def classify(self, sector, mac, app_matches, selector=None):
        digest = hashlib.sha256(sector).hexdigest()
        if digest == FACTORY_SECTOR_SHA256:
            return "install", "factory layout"
        if digest != TARGET_SECTOR_SHA256:
            return "skip", "unrecognized partition layout - not touched"
        if app_matches:
            return "provision", "already running this build"
        if self.ledger.get(mac) in ("install_started", "install_failed"):
            return "install", "resuming interrupted install"
        if selector is not None and hashlib.sha256(selector).hexdigest() in APP0_SELECTOR_SHA256:
            return "update", "conference badge on an older build"
        return "skip", "conference badge with an unrecognized boot selection - not touched"

    def handle(self, port):
        with self.slots:
            stamp = time.strftime("%Y%m%d-%H%M%S")
            log_path = self.root / "logs" / f"{stamp}-{Path(port).name}.log"
            started = time.monotonic()
            with log_path.open("wb") as log:
                outcome, detail = self.run_unit(log, port)
            elapsed = time.monotonic() - started
            with self.print_lock:
                self.counts[outcome] += 1
                c = dict(self.counts)
            label = {"ready": "READY  ", "skipped": "SKIPPED", "failed": "FAILED "}[outcome]
            self.say(port, f"{label} {detail} ({elapsed:.0f}s)   "
                           f"[ready {c['ready']} / skipped {c['skipped']} / failed {c['failed']}]")
            self.chime(outcome)

    def run_unit(self, log, port):
        self.say(port, "probing")
        sector_path = self.workdir / f"sector-{Path(port).name}.bin"
        sector_path.unlink(missing_ok=True)
        code, output = self.esptool(log, port, "read-flash", "0x8000", "0x1000", str(sector_path),
                                    after="no-reset", timeout=60)
        match = MAC_PATTERN.search(output)
        if code or not match or not sector_path.is_file():
            return "failed", "no ESP32-S3 bootloader response (check cable/hub; unplug and replug)"
        mac = match.group(1).decode().lower()
        sector = sector_path.read_bytes()
        app_matches, selector = False, None
        if hashlib.sha256(sector).hexdigest() == TARGET_SECTOR_SHA256:
            code, _ = self.esptool(log, port, "verify-flash", "0x10000", str(self.artifacts.app),
                                   after="no-reset", timeout=120)
            app_matches = code == 0
            if not app_matches:
                selector_path = self.workdir / f"otadata-{Path(port).name}.bin"
                selector_path.unlink(missing_ok=True)
                code, _ = self.esptool(log, port, "read-flash", hex(OTADATA_RANGE[0]), hex(OTADATA_RANGE[1]),
                                       str(selector_path), after="no-reset", timeout=60)
                if code == 0 and selector_path.is_file():
                    selector = selector_path.read_bytes()
        action, reason = self.classify(sector, mac, app_matches, selector)
        if self.args.dry_run:
            self.esptool(log, port, "chip-id", timeout=30)  # hard reset back to its own app
            return "skipped", f"dry run: would {action} ({reason})"
        if action == "skip":
            self.esptool(log, port, "chip-id", timeout=30)
            return "skipped", reason
        if action == "install":
            self.say(port, f"installing ({reason})")
            self.ledger.record(mac, "install_started", app_sha256=self.artifacts.app_sha256)
            # Erase first: an interruption here leaves the factory sector, so a
            # replug simply restarts the full install.
            code, _ = self.esptool(log, port, "erase-region", hex(UNUSED_RANGE[0]), hex(UNUSED_RANGE[1]),
                                   after="no-reset", timeout=600)
            if code == 0:
                code, _ = self.esptool(
                    log, port, "write-flash", "--flash-mode", "keep", "--flash-freq", "keep",
                    "--flash-size", "keep",
                    "0x0", str(self.artifacts.boot),
                    "0x8000", str(self.artifacts.table),
                    hex(SETTINGS_RANGE[0]), str(self.artifacts.blank_settings),
                    "0x10000", str(self.artifacts.app),
                    timeout=600)
            if code:
                self.ledger.record(mac, "install_failed", stage="flash")
                return "failed", "flash write failed - replug to retry"
            self.say(port, "flashed; setting clock and preparing storage")
        elif action == "update":
            self.say(port, f"updating ({reason}); saved settings and storage are kept")
            self.ledger.record(mac, "update_started", app_sha256=self.artifacts.app_sha256)
            # Same write set as the browser update: the partition sector already
            # matches, and NVS, otadata, app1, ffat and coredump are not written.
            code, _ = self.esptool(
                log, port, "write-flash", "--flash-mode", "keep", "--flash-freq", "keep",
                "--flash-size", "keep",
                "0x0", str(self.artifacts.boot),
                "0x10000", str(self.artifacts.app),
                timeout=600)
            if code:
                self.ledger.record(mac, "update_failed", stage="flash")
                return "failed", "update write failed - replug to retry"
            self.say(port, "updated; setting clock and checking storage")
        else:
            self.say(port, "provisioning clock (no flash write)")
            self.esptool(log, port, "chip-id", timeout=30)  # boot the installed app
        ok, why = self.provision(log, port, initialize_storage=action != "update")
        if not ok:
            failed = {"install": "install_failed", "update": "update_failed"}.get(action, "provision_failed")
            self.ledger.record(mac, failed, stage="provision")
            return "failed", f"{why} - replug to retry"
        self.ledger.record(mac, "ready", app_sha256=self.artifacts.app_sha256)
        return "ready", "badge ready - unplug"

    # -- watcher ----------------------------------------------------------
    def watch(self):
        workers = {}
        missing = {}
        ignored = set(self.args.ignore_port)
        print(f"Station watching {self.args.port_glob}  (max {self.args.max_parallel} at once)"
              f"{'  DRY RUN - no writes' if self.args.dry_run else ''}")
        if self.artifacts.build_id:
            print(f"  release {self.artifacts.build_id} (verified release.json)")
        print(f"  build app sha256 {self.artifacts.app_sha256}")
        print(f"  bootloader sha256 {self.artifacts.boot_sha256}")
        offset = self.args.offset_minutes
        print(f"  clock offset: {'computer local' if offset is None else f'{offset} min east of UTC'}")
        print("  ledger/logs: .build/station/   Ctrl-C to stop.\n", flush=True)
        while True:
            present = {p for p in glob.glob(self.args.port_glob) if p not in ignored}
            for port in present:
                missing.pop(port, None)
                if port not in workers:
                    thread = threading.Thread(target=self.handle, args=(port,), daemon=True)
                    workers[port] = thread
                    thread.start()
            # A port is free for a new unit only after its worker finished and the
            # port stayed absent for a few polls (a reset re-enumerates briefly).
            for port, thread in list(workers.items()):
                if port in present or thread.is_alive():
                    continue
                missing[port] = missing.get(port, 0) + 1
                if missing[port] >= 3:
                    del workers[port], missing[port]
            time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--release", type=Path, metavar="DIR",
                        help="Packaged release (release.json + bins), verified before any write; use this for the batch")
    parser.add_argument("--artifact-dir", type=Path, default=REPO_ROOT / ".build/firmware",
                        help="Raw build output when --release is not given (default .build/firmware)")
    parser.add_argument("--esptool", type=Path, default=DEFAULT_ESPTOOL)
    parser.add_argument("--port-glob", default="/dev/cu.usbmodem*" if sys.platform == "darwin" else "/dev/ttyACM*")
    parser.add_argument("--ignore-port", action="append", default=[], help="Never probe this port (repeatable)")
    parser.add_argument("--max-parallel", type=int, default=8)
    parser.add_argument("--offset-minutes", type=int, help="Event UTC offset, minutes east of UTC")
    parser.add_argument("--dry-run", action="store_true", help="Probe and classify only; never write")
    parser.add_argument("--quiet", action="store_true", help="No completion sounds")
    args = parser.parse_args()
    if not args.esptool.is_file():
        parser.error(f"pinned esptool 5.3.0 not found at {args.esptool}")
    if args.offset_minutes is not None and not -840 <= args.offset_minutes <= 840:
        parser.error("--offset-minutes must be from -840 to 840")
    station = Station(args)
    signal.signal(signal.SIGTERM, signal.default_int_handler)
    if sys.platform == "darwin":  # keep the Mac awake while the station runs
        subprocess.Popen(["caffeinate", "-ims", "-w", str(os.getpid())])
    try:
        station.watch()
    except KeyboardInterrupt:
        c = station.counts
        print(f"\nStopped. ready {c['ready']} / skipped {c['skipped']} / failed {c['failed']}")


if __name__ == "__main__":
    main()
