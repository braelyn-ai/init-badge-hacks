"""Batch station classification and write plans with a fake esptool.

No serial port is opened and nothing is flashed. Covers the v1.2.0 update path
for conference badges on an older build.
"""
import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("flash_station", ROOT / "scripts/flash-station.py")
station = importlib.util.module_from_spec(spec)
spec.loader.exec_module(station)

ARDUINO_BOOT_APP0 = "f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0"


def sector_with(digest):
    """A stand-in sector whose SHA-256 the station will compare."""
    return digest.encode()


class FakeStation(station.Station):
    def __init__(self, workdir, sector, selector, app_matches=False, ledger_state=None):
        self.args = types.SimpleNamespace(dry_run=False, offset_minutes=None)
        self.workdir = workdir
        self.artifacts = types.SimpleNamespace(boot=workdir / "boot.bin", app=workdir / "app.bin",
                                               table=workdir / "table.bin", blank_settings=workdir / "blank.bin",
                                               app_sha256="a" * 64, expected_build="v1.2.0")
        self.ledger = types.SimpleNamespace(state=dict(ledger_state or {}), events=[])
        self.ledger.get = self.ledger.state.get
        self.ledger.record = lambda mac, event, **fields: self.ledger.events.append(event)
        self.sector, self.selector, self.app_matches = sector, selector, app_matches
        self.calls, self.provisioned = [], []

    def say(self, port, message):
        pass

    def esptool(self, log, port, *args, after="hard-reset", timeout=120):
        self.calls.append(args)
        if args[0] == "read-flash":
            data = self.sector if args[1] == "0x8000" else self.selector
            if data is None:
                return 2, b""
            Path(args[3]).write_bytes(data)
            return 0, b"MAC: 30:ed:a0:00:00:01\n"
        if args[0] == "verify-flash":
            return (0 if self.app_matches else 1), b""
        return 0, b""

    def provision(self, log, port, initialize_storage=True):
        self.provisioned.append(initialize_storage)
        return True, ""


class StationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="station-")
        self.workdir = Path(self.temp.name)
        self.target = station.TARGET_SECTOR_SHA256
        self.factory = station.FACTORY_SECTOR_SHA256
        # Make the sector/selector digests the station sees equal the real ones.
        self.real_sha256 = station.hashlib.sha256
        mapping = {b"TARGET": self.target, b"FACTORY": self.factory}

        def fake_sha256(data=b""):
            if data in mapping:
                return types.SimpleNamespace(hexdigest=lambda: mapping[data])
            return self.real_sha256(data)
        station.hashlib.sha256 = fake_sha256

    def tearDown(self):
        station.hashlib.sha256 = self.real_sha256
        self.temp.cleanup()

    def run_unit(self, sector, selector, **options):
        unit = FakeStation(self.workdir, sector, selector, **options)
        with open(self.workdir / "log", "wb") as log:
            outcome, detail = unit.run_unit(log, "/dev/cu.usbmodem101")
        writes = [call for call in unit.calls if call[0] in ("write-flash", "erase-region")]
        return unit, outcome, detail, writes

    def test_selectors_match_browser_guard(self):
        self.assertIn(ARDUINO_BOOT_APP0, station.APP0_SELECTOR_SHA256)
        self.assertIn(self.real_sha256(b"\xff" * 0x2000).hexdigest(), station.APP0_SELECTOR_SHA256)
        self.assertEqual(len(station.APP0_SELECTOR_SHA256), 3)

    def test_older_build_gets_preservation_update(self):
        unit, outcome, _, writes = self.run_unit(b"TARGET", b"\xff" * 0x2000)
        self.assertEqual(outcome, "ready")
        self.assertEqual(len(writes), 1)
        write = writes[0]
        self.assertEqual(write[0], "write-flash")
        offsets = [write[i] for i in range(len(write)) if write[i].startswith("0x")]
        self.assertEqual(offsets, ["0x0", "0x10000"])  # no table, NVS, otadata, ffat
        self.assertEqual(unit.provisioned, [False])  # never initializes storage
        self.assertEqual(unit.ledger.events, ["update_started", "ready"])

    def test_unrecognized_selector_is_not_touched(self):
        selector = bytearray(b"\xff" * 0x2000)
        selector[0] = 2  # e.g. an OTA state selecting app1
        unit, outcome, detail, writes = self.run_unit(b"TARGET", bytes(selector))
        self.assertEqual((outcome, writes, unit.provisioned), ("skipped", [], []))
        self.assertIn("boot selection", detail)

    def test_unreadable_selector_is_not_touched(self):
        unit, outcome, _, writes = self.run_unit(b"TARGET", None)
        self.assertEqual((outcome, writes, unit.provisioned), ("skipped", [], []))

    def test_same_build_only_provisions(self):
        unit, outcome, _, writes = self.run_unit(b"TARGET", None, app_matches=True)
        self.assertEqual((outcome, writes), ("ready", []))
        self.assertEqual(unit.provisioned, [True])
        self.assertFalse(any(call[0] == "read-flash" and call[1] == hex(0xE000) for call in unit.calls))

    def test_factory_install_unchanged(self):
        unit, outcome, _, writes = self.run_unit(b"FACTORY", None)
        self.assertEqual(outcome, "ready")
        self.assertEqual([w[0] for w in writes], ["erase-region", "write-flash"])
        self.assertEqual(unit.provisioned, [True])

    def test_interrupted_install_resumes_before_update(self):
        unit, outcome, _, writes = self.run_unit(b"TARGET", b"\xff" * 0x2000,
                                                 ledger_state={"30:ed:a0:00:00:01": "install_failed"})
        self.assertEqual([w[0] for w in writes], ["erase-region", "write-flash"])
        self.assertEqual(unit.provisioned, [True])

    def test_unknown_layout_is_not_touched(self):
        unit, outcome, _, writes = self.run_unit(b"OTHER", b"\xff" * 0x2000)
        self.assertEqual((outcome, writes), ("skipped", []))

    def test_dry_run_never_writes(self):
        unit = FakeStation(self.workdir, b"TARGET", b"\xff" * 0x2000)
        unit.args.dry_run = True
        with open(self.workdir / "log", "wb") as log:
            outcome, detail = unit.run_unit(log, "/dev/cu.usbmodem101")
        self.assertEqual(outcome, "skipped")
        self.assertIn("would update", detail)
        self.assertFalse(any(call[0] in ("write-flash", "erase-region") for call in unit.calls))


if __name__ == "__main__":
    unittest.main()
