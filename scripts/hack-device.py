"""Drive the badge over USB.

  python3 scripts/hack-device.py goto PAGE
  python3 scripts/hack-device.py measure PAGE [SECONDS]
  python3 scripts/hack-device.py shot PAGE OUT.png     (the badge's own framebuffer)
"""
import glob, json, re, struct, sys, time, zlib
import serial

def ask(port, request, wait=1.5):
    port.reset_input_buffer()
    port.write((json.dumps(request) + "\n").encode())
    buffer, end = b"", time.time() + wait
    while time.time() < end:
        buffer += port.read(4096)
        found = re.findall(rb"CONFERENCE_STATUS (\{.*\})", buffer)
        if found: return json.loads(found[-1])
    raise SystemExit("badge did not answer")

def goto(port, page):
    state = ask(port, {"op": "status"})
    while state["page"] != page: state = ask(port, {"op": "page", "step": 1})
    return state

port = serial.Serial(glob.glob("/dev/cu.usbmodem*")[0], 115200, timeout=0.2)
command, page = sys.argv[1], int(sys.argv[2])
goto(port, page)
if command == "measure":
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 4
    time.sleep(1)
    before = ask(port, {"op": "status", "reset_metrics": True})
    time.sleep(seconds)
    after = ask(port, {"op": "status"})
    loops = (after["ui_ticks"] - before["ui_ticks"]) / seconds
    print(f"page {page}: {loops:.0f} main loops/s, worst gap {after['max_loop_gap_ms']} ms, wifi_mode {after['wifi_mode']}")
elif command == "shot":
    time.sleep(1.5)
    port.reset_input_buffer()
    port.write(b'{"op":"capture"}\n')
    buffer, end = b"", time.time() + 15
    header = None
    while time.time() < end:
        buffer += port.read(65536)
        header = header or re.search(rb"BADGE_CAPTURE (\d+) (\d+)\n", buffer)
        if header and len(buffer) >= header.end() + int(header[1]) * int(header[2]) * 3: break
    if not header: raise SystemExit("no capture (the badge refuses during setup)")
    width, height = int(header[1]), int(header[2])
    rgb = buffer[header.end():header.end() + width * height * 3]
    if len(rgb) != width * height * 3: raise SystemExit("capture was cut short")
    def chunk(kind, data): return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    with open(sys.argv[3], "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                   + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    print(sys.argv[3])
else:
    print(f"on page {page}")
