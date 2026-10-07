"""Render every built hack page to an animated gallery you can open in a browser.

  python3 scripts/hack-gallery.py [PAGE ...]     (default: every page with a preview build)

Uses the binaries built by scripts/hack-preview.sh (run that once per page first).
Output: .build/hack-gallery/index.html
"""
import json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, ".build", "hack-gallery")
SECONDS, EVERY = 12, 40
# name, title, what a tap does, tap times in ms
PAGES = [
    ("eyes", "Googly Eyes", "Tap pokes the pupils", [3000, 8000]),
    ("radar", "Radar", "Blips are nearby Wi-Fi access points", []),
    ("hal", "HAL 9000", "Tap makes it speak to you", [2000]),
    ("third_eye", "Third Eye", "Wakes by itself; tap toggles", [6000, 9000]),
    ("matrix", "Matrix", "Tap types a message", [4000, 8500]),
    ("bit", "Bit", "Tap answers yes or no; tap again to clear", [2000, 5000, 7000, 10000]),
    ("labyrinth", "Labyrinth", "Tilt the badge to roll the marble", []),
    ("umbrella", "Umbrella Corp ID", "Your job title is rolled from a hash of your name", []),
    ("jump", "Jump", "Tilt to steer; tap to retry", [9000]),
    ("rotary", "Rotary Engine", "Twist the badge for throttle; tap to blip", [5000]),
]
FLAT = {"labyrinth"}  # Pages previewed lying flat and tilted, not swinging on a lanyard.
wanted = set(sys.argv[1:])
shown = []
for name, title, hint, taps in PAGES:
    binary = os.path.join(ROOT, ".build", "tests", f"hack-{name}", "hack_page_preview")
    target = os.path.join(OUT, name)
    if wanted and name not in wanted:
        if os.path.isdir(target): shown.append((name, title, hint, taps))
        continue
    if not os.path.exists(binary):
        print(f"skip {name}: run scripts/hack-preview.sh {name} first")
        continue
    subprocess.run(["rm", "-rf", target]); os.makedirs(target)
    command = [binary, target, "--seconds", str(SECONDS), "--every", str(EVERY)]
    for tap in taps: command += ["--tap", str(tap)]
    if name in FLAT: command += ["--tilt", "flat"]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        print(f"FAILED {name}:\n{result.stderr[-2000:]}")
        continue
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "scripts", "hack-frames.py"), target], check=True, capture_output=True)
    print(f"{name}: {result.stdout.strip()}")
    shown.append((name, title, hint, taps))

frames = SECONDS * 1000 // EVERY + 1
manifest = [{"name": n, "title": t, "hint": h, "taps": taps} for n, t, h, taps in shown]
html = """<!doctype html><meta charset="utf-8"><title>Badge pages</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; background: #0b0b0d; color: #e8e6df; font: 15px/1.4 ui-sans-serif, system-ui, sans-serif; }
  header { display: flex; gap: 16px; align-items: center; padding: 16px 24px; position: sticky; top: 0; background: #0b0b0dee; z-index: 2; }
  h1 { font-size: 17px; margin: 0 auto 0 0; font-weight: 600; }
  button, select { background: #1c1c20; color: inherit; border: 1px solid #333; border-radius: 6px; padding: 6px 12px; font: inherit; }
  input[type=range] { width: 260px; }
  main { display: grid; grid-template-columns: repeat(auto-fill, minmax(380px, 1fr)); gap: 28px; padding: 12px 24px 40px; }
  figure { margin: 0; text-align: center; }
  .bezel { width: 360px; aspect-ratio: 468/466; margin: 0 auto; border-radius: 50%; padding: 10px; background: #1a1a1d;
           box-shadow: 0 0 0 2px #2c2c31, 0 12px 40px #000; transition: box-shadow .12s; cursor: zoom-in; }
  .bezel.tap { box-shadow: 0 0 0 5px #a596ff, 0 12px 40px #000; }
  canvas { width: 100%; height: 100%; border-radius: 50%; display: block; background: #000; }
  figcaption b { display: block; margin-top: 12px; font-size: 16px; }
  figcaption span { color: #8a8a90; }
  figure.big { grid-column: 1 / -1; } figure.big .bezel { width: min(86vh, 900px); cursor: zoom-out; }
</style>
<header><h1>Badge pages &middot; off-device render</h1>
  <button id="play">Pause</button>
  <select id="speed"><option value="0.25">0.25x<option value="0.5">0.5x<option value="1" selected>1x<option value="2">2x</select>
  <input id="scrub" type="range" min="0" max="__LAST__" value="0"><span id="time"></span>
</header><main id="grid"></main>
<script>
const pages = __MANIFEST__, frameCount = __FRAMES__, every = __EVERY__;
const grid = document.getElementById('grid'), scrub = document.getElementById('scrub');
const views = pages.map(page => {
  const figure = document.createElement('figure');
  figure.innerHTML = `<div class="bezel"><canvas width="468" height="466"></canvas></div>
    <figcaption><b>${page.title}</b><span>${page.hint}</span></figcaption>`;
  grid.append(figure);
  figure.querySelector('.bezel').onclick = () => figure.classList.toggle('big');
  const images = Array.from({length: frameCount}, (_, i) => {
    const image = new Image(); image.src = `${page.name}/frame-${String(i).padStart(2, '0')}.png?${Date.now()}`; return image;
  });
  return {page, images, context: figure.querySelector('canvas').getContext('2d'), bezel: figure.querySelector('.bezel')};
});
let frame = 0, playing = true, last = performance.now(), carry = 0;
function draw() {
  for (const view of views) {
    const image = view.images[frame];
    if (image.complete && image.naturalWidth) view.context.drawImage(image, 0, 0);
    const now = frame * every;
    view.bezel.classList.toggle('tap', view.page.taps.some(t => now >= t && now < t + 350));
  }
  scrub.value = frame;
  document.getElementById('time').textContent = (frame * every / 1000).toFixed(1) + ' s';
}
function loop(now) {
  if (playing) {
    carry += (now - last) * +document.getElementById('speed').value;
    while (carry >= every) { carry -= every; frame = (frame + 1) % frameCount; }
    draw();
  }
  last = now; requestAnimationFrame(loop);
}
document.getElementById('play').onclick = event => { playing = !playing; event.target.textContent = playing ? 'Pause' : 'Play'; };
scrub.oninput = () => { frame = +scrub.value; draw(); };
requestAnimationFrame(loop);
</script>
"""
html = (html.replace("__MANIFEST__", json.dumps(manifest)).replace("__FRAMES__", str(frames))
            .replace("__EVERY__", str(EVERY)).replace("__LAST__", str(frames - 1)))
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, "index.html"), "w") as file: file.write(html)
print(os.path.join(OUT, "index.html"))
