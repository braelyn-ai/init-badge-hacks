// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstddef>
#include <lvgl.h>

// StopWatch hardware boundary. All calls, including poll(), belong to the main
// task. No networking, filesystem initialization, or NVS erasure occurs here.
namespace board {
constexpr int NativeWidth = 468;
constexpr int NativeHeight = 466;

struct TouchSample {
    bool valid = false;       // Latest controller read succeeded.
    bool pressed = false;
    bool sensor = true;      // False only for explicit USB-injected input.
    int16_t rawX = 0, rawY = 0;
    int16_t x = 0, y = 0;    // Calibrated display coordinates, with LVGL rotation applied once.
    uint32_t sequence = 0;
    uint32_t sampledAtMs = 0;
};
struct Buttons { bool yellow = false, blue = false; };
struct Acceleration {
    bool valid = false;
    float x = 0, y = 0, z = 0;  // Native sensor axes, in g.
    uint32_t sampledAtMs = 0;
};
struct Battery {
    bool valid = false, charging = false, usb = false;
    uint16_t millivolts = 0;
    uint8_t percent = 0;        // Factory voltage-based estimate.
};

bool init();                   // Initializes hardware and LVGL display/input.
void poll();                   // Hardware only; caller runs lv_timer_handler().
uint32_t millis();
const TouchSample& touch();
// Main-task Morse feedback: 40% PWM while a valid contact is held, at most
// 1400 ms. Release/cancellation stops it. These are checked driver states;
// active also includes an uncertain output awaiting a successful OFF retry.
void setInputVibration(bool pressed);
bool inputVibrationAvailable();
bool inputVibrationActive();
// Begin/move with pressed=true, end with false. The completed release survives
// until LVGL consumes it. Rejects starting while a real finger is down.
bool injectTouch(int displayedX, int displayedY, bool pressed);
Buttons buttons();             // Debounced active-high physical button levels.
Acceleration acceleration();
Battery battery();

// Rotation numbering matches M5GFX/current badge: 0=stock, 2=180 degrees.
// LVGL quarter-turn numbering is translated internally. Returns false while
// touching, so the caller can defer orientation changes until full release.
bool setRotation(uint8_t rotation);
uint8_t rotation();
int width();
int height();
void setBrightness(uint8_t percent);
uint8_t brightness();
lv_display_t* display();
lv_indev_t* pointer();
// Read one current-orientation framebuffer row as RGB888 for private USB
// diagnostics. bytes must be at least width()*3; no state or storage changes.
bool readFrameRow(int y, uint8_t* rgb, size_t bytes);

// RTC always stores UTC. Reads never modify its calendar or system timezone.
// Both functions report I2C/invalid-calendar failures; caller owns system time.
bool rtcAvailable();
bool readRtcUtc(int64_t& epoch);
bool setRtcUtc(int64_t epoch);
}  // namespace board

Validation for the September 28 calibration implementation: 1,052 ESPtember
contract assertions, NVS loader/save-failure fixtures, physical-only session
fixtures, native UI sanitizers, and all five board tests passed. The guarded
ESP32-S3 flash passed UNIT_READY. The connected device reported no saved record
(version 0), and the calibration wizard was opened at target 1. Physical target
collection, resulting accuracy and on-device record persistence remain pending;
these are not established by host tests or opening the screen. Private evidence:
`.build/touch-mapping/`.
