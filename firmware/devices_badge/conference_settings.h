#pragma once
#include <stdint.h>

enum class ConferenceOrientationMode : uint8_t { Free, Default, Opposite };
enum class ConferenceSettingsAction : uint8_t { None, Dimmer, Brighter, Connect, TouchTest, Free, Default, Opposite };

inline ConferenceSettingsAction conferenceSettingsHit(int x, int y) {
  if (y >= 146 && y <= 188) {
    if (x >= 94 && x <= 158) return ConferenceSettingsAction::Dimmer;
    if (x >= 310 && x <= 374) return ConferenceSettingsAction::Brighter;
  }
  if (y >= 250 && y <= 294) {
    if (x >= 78 && x < 230) return ConferenceSettingsAction::Connect;
    if (x >= 238 && x < 390) return ConferenceSettingsAction::TouchTest;
  }
  if (y >= 342 && y <= 382) {
    if (x >= 91 && x <= 181) return ConferenceSettingsAction::Free;
    if (x >= 189 && x <= 279) return ConferenceSettingsAction::Default;
    if (x >= 287 && x <= 377) return ConferenceSettingsAction::Opposite;
  }
  return ConferenceSettingsAction::None;
}

// A single versioned preference value makes brightness/mode persistence atomic.
// Debouncing uses elapsed monotonic milliseconds, never the adjustable RTC.
class ConferenceSettings {
 public:
  static constexpr uint8_t MIN_BRIGHTNESS = 10, MAX_BRIGHTNESS = 100, DEFAULT_BRIGHTNESS = 70;
  static constexpr ConferenceOrientationMode DEFAULT_ORIENTATION = ConferenceOrientationMode::Default;
  static constexpr uint32_t SAVE_DELAY_MS = 1200;
  uint8_t brightness = DEFAULT_BRIGHTNESS;
  ConferenceOrientationMode orientation = DEFAULT_ORIENTATION;
  bool pending() const { return dirty_; }
  uint8_t displayBrightness() const { return (uint16_t(brightness) * 255 + 50) / 100; }
  uint32_t encoded() const { return 0xc7010000u | (uint32_t(orientation) << 8) | brightness; }
  bool restore(uint32_t value) {
    brightness = DEFAULT_BRIGHTNESS; orientation = DEFAULT_ORIENTATION; dirty_ = false;
    uint8_t level = value & 255, mode = (value >> 8) & 255;
    if ((value & 0xffff0000u) != 0xc7010000u || level < MIN_BRIGHTNESS || level > MAX_BRIGHTNESS || mode > 2) return false;
    brightness = level; orientation = ConferenceOrientationMode(mode); return true;
  }
  bool adjustBrightness(int direction, uint32_t now) {
    int next = int(brightness) + (direction > 0 ? 10 : -10);
    return setBrightness(next, now);
  }
  bool setBrightness(int next, uint32_t now) {
    if (next < MIN_BRIGHTNESS) next = MIN_BRIGHTNESS;
    if (next > MAX_BRIGHTNESS) next = MAX_BRIGHTNESS;
    if (next == brightness) return false;
    brightness = uint8_t(next); changed(now); return true;
  }
  bool setOrientation(ConferenceOrientationMode mode, uint32_t now) {
    if (uint8_t(mode) > 2 || mode == orientation) return false;
    orientation = mode; changed(now); return true;
  }
  bool saveDue(uint32_t now) const { return dirty_ && uint32_t(now - changedAt_) >= waitMs_; }
  void saved() { dirty_ = false; }
  void saveFailed(uint32_t now) { changedAt_ = now; waitMs_ = 5000; }
  bool automatic() const { return orientation == ConferenceOrientationMode::Free; }
  uint8_t fixedRotation() const { return orientation == ConferenceOrientationMode::Default ? 0 : 2; }
  const char *orientationName() const { return automatic() ? "Free" : orientation == ConferenceOrientationMode::Default ? "Default" : "180°"; }
 private:
  bool dirty_ = false;
  uint32_t changedAt_ = 0, waitMs_ = SAVE_DELAY_MS;
  void changed(uint32_t now) { dirty_ = true; changedAt_ = now; waitMs_ = SAVE_DELAY_MS; }
};
