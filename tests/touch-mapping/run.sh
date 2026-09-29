#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/.build/tests/touch-mapping"
mkdir -p "$OUT"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$ROOT/tests/touch-mapping" -I"$ROOT/firmware/factory_badge/main" "$ROOT/tests/touch-mapping/main.cpp" "$ROOT/firmware/factory_badge/main/touch_mapping_session.cpp" "$ROOT/firmware/factory_badge/main/touch_mapping.cpp" "$ROOT/firmware/factory_badge/main/touchcal/touch_calibration_core.cpp" -o "$OUT/test"
"$OUT/test"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$ROOT/firmware/factory_badge/main/touchcal" "$ROOT/tests/touch-mapping/contract.cpp" "$ROOT/firmware/factory_badge/main/touchcal/touch_calibration_core.cpp" -o "$OUT/contract"
"$OUT/contract"
