#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-clang++}"
export ARDUINOJSON_INCLUDE="${ARDUINOJSON_INCLUDE:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}"
if [[ ! -f "$ARDUINOJSON_INCLUDE/ArduinoJson.h" ]]; then
  echo 'Set ARDUINOJSON_INCLUDE to the ArduinoJson 7.4.3 src directory.' >&2
  exit 2
fi
if [[ "$(uname -s)" != Darwin ]]; then
  echo 'The storage fixture currently uses macOS CommonCrypto.' >&2
  exit 2
fi
mkdir -p "$REPO_ROOT/.build/tests"
FLAGS=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
for name in account_paging badge_styles button_gesture orientation_filter profile_urls voice_reply_state conference_navigation conference_settings daily_schedule schedule_bookmarks morse_unlock after_dark_unlock touch_observation; do
  "$CXX" "${FLAGS[@]}" "$REPO_ROOT/tests/check_$name.cpp" -o "$REPO_ROOT/.build/tests/$name"
  "$REPO_ROOT/.build/tests/$name"
done
"$CXX" "${FLAGS[@]}" -I"$REPO_ROOT/tests/background-http-host" \
  "$REPO_ROOT/tests/check_voice_reply_ui.cpp" -o "$REPO_ROOT/.build/tests/voice-reply-ui"
"$REPO_ROOT/.build/tests/voice-reply-ui"
"$CXX" "${FLAGS[@]}" -pthread -I"$REPO_ROOT/tests/voice-recorder-host" \
  "$REPO_ROOT/tests/voice-recorder-host/check.cpp" -o "$REPO_ROOT/.build/tests/voice-recorder"
"$REPO_ROOT/.build/tests/voice-recorder"
"$CXX" "${FLAGS[@]}" -Wno-deprecated-declarations \
  -I"$REPO_ROOT/tests/profile-store-test/include" \
  "$REPO_ROOT/tests/profile-store-test/test.cpp" -o "$REPO_ROOT/.build/tests/profile-store"
"$REPO_ROOT/.build/tests/profile-store"
"$CXX" "${FLAGS[@]}" -pthread \
  -I"$REPO_ROOT/tests/background-http-host" -I"$ARDUINOJSON_INCLUDE" \
  "$REPO_ROOT/tests/background-http-host/check.cpp" -o "$REPO_ROOT/.build/tests/background-http"
"$REPO_ROOT/.build/tests/background-http"
export CXX
python3 "$REPO_ROOT/tests/profile-scheduler-check/run.py"
python3 "$REPO_ROOT/tests/voice-controller-host/run.py"

bash "$REPO_ROOT/tests/conference-profile-host/run.sh"

"$CXX" "${FLAGS[@]}" -I"$REPO_ROOT/tests/conference-clock-host" -I"$ARDUINOJSON_INCLUDE" \
  "$REPO_ROOT/tests/conference-clock-host/check.cpp" -o "$REPO_ROOT/.build/tests/conference-clock"
"$REPO_ROOT/.build/tests/conference-clock"
python3 "$REPO_ROOT/tests/test_provision_clock.py"
python3 "$REPO_ROOT/tests/check_flash_layout.py"
python3 "$REPO_ROOT/tests/test_flash_clock.py"
python3 "$REPO_ROOT/tests/test_verify_factory.py"

# Current native factory-stack application. These compile the production code
# against real LVGL or bounded host hardware/filesystem adapters.
bash "$REPO_ROOT/tests/factory-board/run.sh"
python3 "$REPO_ROOT/tests/factory-services/run.py"
python3 "$REPO_ROOT/tests/factory-reset/run.py"
python3 "$REPO_ROOT/tests/factory-clock/run.py"
bash "$REPO_ROOT/tests/factory-ui/run.sh"

bash "$REPO_ROOT/tests/factory-intro/run.sh"

bash "$REPO_ROOT/tests/touch-mapping/run.sh"
