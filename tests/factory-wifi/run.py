#!/usr/bin/env python3
"""Compile production Wi-Fi storage against an in-memory NVS double."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / ".build/tests/factory-wifi"
OUT.mkdir(parents=True, exist_ok=True)
subprocess.run([
    "clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    "-I" + str(HERE), "-I" + str(ROOT / "firmware/factory_badge/main"),
    HERE / "check.cpp", ROOT / "firmware/factory_badge/main/wifi_config.cpp", "-o", OUT / "check",
], cwd=ROOT, check=True)
subprocess.run([OUT / "check"], cwd=ROOT, check=True)
