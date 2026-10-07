#!/usr/bin/env bash
# Render one hack page off-device and convert its frames to PNG.
#   scripts/hack-preview.sh PAGE [--seconds N] [--every MS] [--tap MS[:X:Y]]...
# PAGE is the file suffix: eyes -> firmware/factory_badge/main/ui/page_eyes.cpp.
# Output: .build/hack-preview/PAGE/frame-NN.png and sheet.png (all frames, half size).
# Each page has its own build directory, so pages can be previewed in parallel.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAGE="${1:?usage: $0 PAGE [preview options]}"; shift
BUILD="$REPO_ROOT/.build/tests/hack-$PAGE"
OUT="$REPO_ROOT/.build/hack-preview/$PAGE"
mkdir -p "$BUILD"; rm -rf "$OUT"; mkdir -p "$OUT"
cmake -S "$REPO_ROOT/tests/hack-pages" -B "$BUILD" -G Ninja -DHACK_PAGE="$PAGE" -DCMAKE_BUILD_TYPE=Debug > "$BUILD/configure.log" 2>&1 || { cat "$BUILD/configure.log" >&2; exit 1; }
cmake --build "$BUILD" > "$BUILD/build.log" 2>&1 || { grep -E "error|Error|undefined" -A6 "$BUILD/build.log" | head -80 >&2; exit 1; }
"$BUILD/hack_page_preview" "$OUT" "$@"
python3 -I "$REPO_ROOT/scripts/hack-frames.py" "$OUT"
echo "$OUT"
