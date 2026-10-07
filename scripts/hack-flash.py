#!/usr/bin/env python3
"""Install the hack-pages firmware on an init() badge without touching saved data.

  python3 scripts/hack-flash.py                      # flash .build/factory/conference_badge.bin
  python3 scripts/hack-flash.py --image FILE.bin     # flash a downloaded release image
  python3 scripts/hack-flash.py --restore backups/app0-....bin   # put a backup back

Only the app slot (0x10000, 3 MiB) is written. The bootloader, partition table,
NVS settings and the profile/photo filesystem are never touched. Before writing,
the script checks the badge has the expected conference partition layout and
boots from that slot, and saves the current app to backups/.
Needs: pip install esptool
"""
import argparse, glob, json, os, re, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP_OFFSET, APP_SIZE = 0x10000, 0x300000
# name, type, subtype, offset, size: the conference badge layout (partitions.csv).
EXPECTED = [("nvs", 1, 0x02, 0x9000, 0x5000), ("otadata", 1, 0x00, 0xE000, 0x2000),
            ("app0", 0, 0x10, 0x10000, 0x300000), ("app1", 0, 0x11, 0x310000, 0x300000),
            ("ffat", 1, 0x81, 0x610000, 0x9E0000), ("coredump", 1, 0x03, 0xFF0000, 0x10000)]

def fail(message):
    sys.exit(f"STOPPED, nothing {'more ' if fail.wrote else ''}was written: {message}")
fail.wrote = False

def esptool(port, *arguments, after="no_reset"):
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "-b", "460800",
               "--after", after, *arguments]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        if "No module named esptool" in result.stderr: fail("esptool is missing; run: pip install esptool")
        fail(f"esptool {arguments[0]} failed:\n{(result.stdout + result.stderr)[-800:]}")
    return result.stdout

def read_flash(port, offset, size):
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "read.bin")
        esptool(port, "read_flash", hex(offset), hex(size), path)
        with open(path, "rb") as file: return file.read()

def partitions(table):
    found = []
    for index in range(0, len(table), 32):
        entry = table[index:index + 32]
        if entry[:2] != b"\xaa\x50": break
        kind, subtype, offset, size = struct.unpack("<BBII", entry[2:12])
        found.append((entry[12:28].rstrip(b"\0").decode(), kind, subtype, offset, size))
    return found

def status(port_name, wait=10):
    import serial  # Installed with esptool.
    end = time.time() + wait
    while time.time() < end:
        try:
            with serial.Serial(port_name, 115200, timeout=0.3) as port:
                port.write(b'{"op":"status"}\n')
                buffer, until = b"", time.time() + 2
                while time.time() < until:
                    buffer += port.read(4096)
                    found = re.findall(rb"CONFERENCE_STATUS (\{.*\})", buffer)
                    if found: return json.loads(found[-1])
        except (OSError, ValueError):
            time.sleep(0.5)
    return None

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--port", help="serial port (default: the only /dev/cu.usbmodem* or /dev/ttyACM*)")
parser.add_argument("--image", default=os.path.join(ROOT, ".build", "factory", "conference_badge.bin"))
parser.add_argument("--restore", metavar="BACKUP", help="write a backups/app0-*.bin file back instead")
options = parser.parse_args()

image_path = options.restore or options.image
if not os.path.exists(image_path): fail(f"{image_path} not found (build with scripts/build.sh or pass --image)")
with open(image_path, "rb") as file: image = file.read()
if not image or image[0] != 0xE9 or len(image) > APP_SIZE:
    fail(f"{image_path} is not an ESP32 app image that fits the 3 MiB slot")

ports = [options.port] if options.port else sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
if len(ports) != 1:
    fail(f"expected exactly one badge, found {ports or 'none'}; plug it in with a data cable, hold PWR ~2 s until the green LED lights, or pass --port")
port = ports[0]

before = status(port, wait=4)
print(f"Badge on {port}: " + (f"build {before['build']}, {before['page_count']} pages" if before else "not answering (continuing with flash-level checks)"))

layout = partitions(read_flash(port, 0x8000, 0x1000))
if layout != EXPECTED:
    fail("this badge does not have the conference partition layout. If it still runs M5Stack's factory demo, "
         f"install the stock conference firmware first at https://workos.com/init/badge/install\nfound: {layout}")
otadata = read_flash(port, 0xE000, 0x2000)
sequences = [value for value in (struct.unpack("<I", otadata[i:i + 4])[0] for i in (0, 0x1000)) if value != 0xFFFFFFFF]
if sequences and (max(sequences) - 1) % 2 != 0:
    fail("this badge boots from the second app slot, which this script does not write")
print("Partition layout and boot slot check out.")

if not options.restore:
    os.makedirs(os.path.join(ROOT, "backups"), exist_ok=True)
    backup = os.path.join(ROOT, "backups", time.strftime("app0-%Y%m%d-%H%M%S.bin"))
    print("Backing up the current app (about a minute)...")
    esptool(port, "read_flash", hex(APP_OFFSET), hex(APP_SIZE), backup)
    print(f"Backup: {backup}")

print(f"Writing {os.path.basename(image_path)} ({len(image)} bytes) to the app slot...")
fail.wrote = True
output = esptool(port, "write_flash", "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
                 hex(APP_OFFSET), image_path, after="hard_reset")
if "Hash of data verified" not in output: fail("the write did not verify:\n" + output[-600:])

after = status(port, wait=12)
if not after:
    # Some resets leave the chip waiting in its bootloader; one more gets it going.
    esptool(port, "chip_id", after="hard_reset")
    after = status(port, wait=12)
if not after: sys.exit("Written and verified, but the badge is not answering. Press its reset/PWR button; if it stays dark, run with --restore.")
print(f"Done: build {after['build']}, {after['page_count']} pages, profile {'kept' if after['store_ready'] else 'store not ready'}.")
