"""Convert hack-preview PPM frames to PNG and build a half-size contact sheet."""
import glob, os, struct, sys, zlib

def read_ppm(path):
    with open(path, "rb") as file:
        assert file.readline().strip() == b"P6"
        width, height = map(int, file.readline().split())
        file.readline()
        return width, height, file.read()

def write_png(path, width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    with open(path, "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                   + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))

directory = sys.argv[1]
frames = []
for path in sorted(glob.glob(os.path.join(directory, "frame-*.ppm"))):
    width, height, rgb = read_ppm(path)
    write_png(path[:-4] + ".png", width, height, rgb)
    os.remove(path)
    half = bytearray()
    for y in range(0, height - 1, 2):
        row = rgb[y * width * 3:(y + 1) * width * 3]
        for x in range(0, width - 1, 2):
            half += row[x * 3:x * 3 + 3]
    frames.append((width // 2, height // 2, bytes(half)))
if frames and len(frames) <= 40:
    cell_w, cell_h, _ = frames[0]
    columns = min(4, len(frames))
    rows = (len(frames) + columns - 1) // columns
    sheet = bytearray(columns * cell_w * rows * cell_h * 3)
    for index, (_, _, pixels) in enumerate(frames):
        left, top = index % columns * cell_w, index // columns * cell_h
        for y in range(cell_h):
            start = ((top + y) * columns * cell_w + left) * 3
            sheet[start:start + cell_w * 3] = pixels[y * cell_w * 3:(y + 1) * cell_w * 3]
    write_png(os.path.join(directory, "sheet.png"), columns * cell_w, rows * cell_h, bytes(sheet))
print(f"{len(frames)} PNG frames + sheet.png")
