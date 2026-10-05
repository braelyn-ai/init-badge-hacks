#!/usr/bin/env python3
"""Check daily agenda on the attached badge, then restore fresh real time.

Temporarily provisions representative local times. Captures only the public
schedule, retains the current timezone offset, and leaves the schedule open.
Requires the same Python dependencies as verify-factory.py.
"""
import argparse
import importlib.util
import json
import secrets
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("factory_usb", ROOT / "scripts/verify-factory.py")
usb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(usb)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    out = Path(args.output).resolve()
    assert ROOT / ".build" in out.parents, "Keep captures in the private .build directory"
    out.mkdir(parents=True, exist_ok=True)
    device = usb.Device(args.port)
    report = {"cases": []}
    offset = None

    def clock(op, **fields):
        nonce = secrets.token_hex(8)
        device.send({"op": op, "nonce": nonce, **fields})
        reply = json.loads(device.response(b"CLOCK_ACK " if op == "clock_set" else b"CLOCK_STATUS "))
        assert reply["nonce"] == nonce and reply["ok"] and reply["valid"], reply
        assert abs(reply["epoch"] - reply["rtc_epoch"]) <= 1, reply
        return reply

    def wait_minute(minute):
        for _ in range(30):
            state = device.status()
            if state["schedule_minute"] == minute:
                return state
            time.sleep(.1)
        raise AssertionError("Schedule did not adopt local time: " + json.dumps(state))

    try:
        report["before"] = device.status()
        assert report["before"]["build"] == "v" + (ROOT / "firmware/factory_badge/version.txt").read_text().strip()
        assert not report["before"]["setup"] and report["before"]["clock_valid"]
        offset = clock("clock_status")["offset_minutes"]
        day = (int(time.time()) + offset * 60) // 86400 * 86400
        cases = [
            ("before-start", 479, -1, 0), ("check-in", 480, 0, 0),
            ("keynote", 570, 1, 0), ("break", 630, 2, 0),
            ("lunch", 750, 4, 0), ("afternoon", 825, 5, 0),
            ("happy-hour", 990, 8, 0), ("late", 1439, 8, 0),
            ("midnight", 0, -1, 1), ("next-day-keynote", 570, 1, 1),
        ]
        for name, minute, expected, days in cases:
            epoch = day + days * 86400 + minute * 60 - offset * 60
            reply = clock("clock_set", epoch=epoch, offset_minutes=offset)
            assert abs(reply["epoch"] - epoch) <= 1
            state = wait_minute(minute)
            assert state["schedule_current"] == expected, (name, state)
            device.page(0)
            device.page(1)  # Entry should bring the current block into view.
            time.sleep(.15)
            if name in ("keynote", "break", "happy-hour", "midnight"):
                device.capture(out / (name + ".png"))
            report["cases"].append({"name": name, "minute": minute, "current": expected})
        report["passed"] = True
    finally:
        try:
            if offset is not None:
                # Never leave a scenario time on the attendee's badge, including
                # when a comparison or capture above fails.
                fresh = int(time.time())
                reply = clock("clock_set", epoch=fresh, offset_minutes=offset)
                assert abs(reply["epoch"] - int(time.time())) <= 2
                minute = ((fresh + offset * 60) % 86400) // 60
                wait_minute(minute)
                report["clock_restored"] = True
                device.page(0)
                device.page(1)
                time.sleep(.2)
                device.capture(out / "current-real-time.png")
                report["final"] = device.status()
        finally:
            (out / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
            device.close()
    print(json.dumps({"passed": True, "cases": len(report["cases"]), "clock_restored": report["clock_restored"]}))


if __name__ == "__main__":
    main()
