#!/usr/bin/env python3
"""Check native/Arduino record compatibility without touching a device."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / ".build/tests/factory-services"
OUT.mkdir(parents=True, exist_ok=True)

source = (ROOT / "firmware/factory_badge/main/services.cpp").read_text()
# Compile the actual production validators/record read/write helpers. Platform
# mount/Wi-Fi code stays out; host adapters supply crypto and temporary files.
production = source[source.index("bool alnum("):source.index("std::string random_hex(")]
production += source[source.index("std::string escape("):source.index("const char* status_text(")]
production += source[source.index("bool json_fields("):source.index("void finish_session(")]
prefix = (HERE / "fixture-prefix.cpp").read_text().replace("@@ROOT@@", str(ROOT))
initialize = source[source.index("bool profile_initialize_for_conference()"):source.rindex("} // namespace badge")]
reset_api = source[source.index("bool profile_reset_request("):source.index("bool services_init(")]
tests = (HERE / "fixture-tests.cpp").read_text().replace("// NATIVE_INITIALIZER_HERE", initialize)
tests = tests.replace("// NATIVE_RESET_API_HERE", reset_api)
fixture = prefix + production + tests
(OUT / "compat.cpp").write_text(fixture)
(OUT / "synthetic.ppm").write_bytes(b"P6\n16 16\n255\n" + bytes([255, 20, 0]) * 256)

def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)

run("sips", "-s", "format", "jpeg", OUT / "synthetic.ppm", "--out", OUT / "synthetic.jpg")
run("clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
    "-fno-omit-frame-pointer", "-Wno-deprecated-declarations",
    "-I" + str(ROOT / "tests/conference-profile-host/include"),
    "-I" + str(ROOT / "tests/profile-store-test/include"),
    OUT / "compat.cpp", "-o", OUT / "compat")
run(OUT / "compat", OUT / "synthetic.jpg")

asset = (ROOT / "firmware/factory_badge/main/portal_page.h").read_text()
script = re.search(r"<script>(.*?)</script>", asset, re.S)
assert script, "Native portal asset must contain its local browser script"
(OUT / "portal.js").write_text(script[1].replace("{{NONCE}}", "0123456789abcdef0123456789abcdef"))
run("node", "--check", OUT / "portal.js")
# The setup page's network select and labels must match the firmware table.
import re
table = re.findall(r'\{"([a-z]+)", "([^"]+)", "(?:https://[^"]*)?", Rule::', (ROOT / "firmware/factory_badge/main/social_networks.h").read_text())
assert [key for key, _ in table] == ["linkedin", "x", "github", "huggingface", "youtube", "url"], table
options = re.findall(r'<option value="([a-z]+)">([^<]+)</option>', asset[asset.index('id="network"'):asset.index('id="handle"')])
labels = dict(re.findall(r"([a-z]+):'([^']+)'", re.search(r"const networkNames=\{([^}]*)\}", asset).group(1)))
assert options == table and labels == dict(table), (options, labels, table)
run("node", ROOT / "tests/conference-profile-host/browser-clock.cjs", OUT / "portal.js")
run("node", HERE / "browser-profile.cjs", OUT / "portal.js")
run("node", HERE / "browser-wifi.cjs", OUT / "portal.js")
