"""Exercise native capture navigation without opening a serial port."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("verify_factory", ROOT / "scripts/verify-factory.py")
factory = importlib.util.module_from_spec(spec)
# Hardware/image dependencies are optional on CI. Only the capture/navigation
# contract is exercised here; these fakes do not claim framebuffer/QR coverage.
with patch.dict("sys.modules", {name: MagicMock() for name in ("serial", "PIL", "zxingcpp")}):
    spec.loader.exec_module(factory)


class FakeDevice:
    def __init__(self, status):
        self.state = status
        self.calls = []

    def status(self):
        self.calls.append(("status",))
        return self.state

    def page(self, target):
        self.calls.append(("page", target))

    def capture(self, path):
        self.calls.append(("capture", path.name))
        return SimpleNamespace(size=(468, 466), name=path.name)

    def touch(self, phase, x, y):
        self.calls.append(("touch", phase, x, y))


class CapturePagesTests(unittest.TestCase):
    def capture(self, state):
        device = FakeDevice(state)
        scans = [[], [SimpleNamespace(text="https://workos.com/init/badge")]]
        if state.get("after_dark_unlocked"):
            scans.insert(1, [SimpleNamespace(text="https://luma.com/developers-after-dark")])
        with patch.object(factory.time, "sleep"), patch.object(
                factory.zxingcpp, "read_barcodes", side_effect=scans):
            captured, skipped = factory.capture_pages(device, Path("unused-private-output"))
        pages = [call[1] for call in device.calls if call[0] == "page"]
        self.assertEqual(pages, [0, 1, 2, 3, 4, 0, 4])
        self.assertEqual(captured, [0, 1, 2, 3, 4])
        self.assertEqual(skipped, [])
        self.assertIn(("capture", "settings-hack.png"), device.calls)
        self.assertNotIn(("page", 5), device.calls)
        self.assertEqual(device.calls[0], ("status",))
        return captured, skipped

    def test_locked_party_and_settings_hack_menu_are_captured(self):
        self.capture({"after_dark_unlocked": False, "page_count": 5})

    def test_unlocked_party_and_live_qr_are_captured(self):
        self.capture({"after_dark_unlocked": True, "page_count": 5})

    def test_personal_fields_refuse_capture_before_navigation(self):
        # A company-only badge contains attendee data even without a name,
        # photo or configured account. Reject all personal profile variants.
        for field in ("company_present", "name_present", "avatar", "configured_mask"):
            with self.subTest(field=field):
                state = {"company_present": False, "name_present": False,
                         "avatar": False, "configured_mask": 0}
                state[field] = 1
                device = FakeDevice(state)
                with self.assertRaisesRegex(AssertionError, "personal profile"):
                    factory.capture_pages(device, Path("unused-private-output"))
                self.assertEqual(device.calls, [("status",)])



if __name__ == "__main__":
    unittest.main()
