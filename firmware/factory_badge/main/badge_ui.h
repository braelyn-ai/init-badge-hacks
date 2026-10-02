#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <lvgl.h>

namespace badge {

enum class Orientation : uint8_t { Free, Default, UpsideDown };
enum class ResetState : uint8_t { Ready, Working, Failed, SettingsFailed, Complete };

// The main task owns this model and all UI calls. Avatar memory must remain valid
// until the next ui_update(), including while LVGL renders the current frame.
struct UiModel {
    bool touch_calibrated = false;
    bool calibration_target_visible = false;
    int calibration_x = 234, calibration_y = 233;
    std::string calibration_title, calibration_message;
    std::string name;
    std::string company;
    // The one social account: badge_social::Networks index (-1 none) and its
    // profile URL, which the Badge QR opens.
    int social_network = -1;
    std::string social_url;
    std::string social_label; // Gray name above the QR: the network, or an Other link's host.
    std::string clock_text = "--:--";
    std::string date_text = "Date / time not set";
    const uint16_t* avatar = nullptr;
    uint16_t avatar_width = 0;
    uint16_t avatar_height = 0;
    uint32_t profile_revision = 0;
    int64_t clock_epoch = 0;
    int utc_offset_minutes = 0;
    int battery_percent = -1;
    int brightness_percent = 70;
    Orientation orientation = Orientation::Default;
    bool settings_pending = false;
    std::string photo_status; // Footer text while a photo downloads and briefly after.
    bool clock_valid = false;
    bool after_dark_unlocked = false;
    int schedule_minute = -1; // Local minute of day; -1 when the clock is unset.
    int schedule_current = -1;
    uint16_t schedule_bookmarks = 0;
    ResetState reset_state = ResetState::Ready;
};

struct UiCallbacks {
    std::function<void()> calibrate_touch;
    std::function<void()> cancel_calibration;
    std::function<void()> request_setup;
    std::function<void()> close_setup;
    std::function<void(int)> brightness; // Absolute percentage, 10 through 100.
    std::function<void(Orientation)> orientation;
    std::function<void(int)> bookmark; // Toggle this agenda item.
    std::function<void()> reset_badge; // Only after a fresh confirmation tap.
    std::function<void()> unlock_after_dark; // Complete touch-entered Morse word.
    std::function<void(bool)> morse_pressed; // Input haptic; false on release/cancellation.
};

struct UiTouchSample {
    bool sample = false;
    bool pressed = false;
    bool sensor = true;
    int raw_x = 0;
    int raw_y = 0;
    int x = 0;
    int y = 0;
    uint8_t rotation = 0;
};

void ui_init(lv_display_t* display, UiCallbacks callbacks);
void ui_update(const UiModel& model);
void ui_tick(uint32_t now_ms);
void ui_rotation_changed();
void ui_page(int delta);
void ui_button(bool both, int delta);
void ui_open_after_dark(); // Successful code entry; never interrupts a modal.
void ui_show_setup(const std::string& ssid, const std::string& password,
                   const std::string& ip = "192.168.4.1", const std::string& status = "");
void ui_close_setup(bool saved);
void ui_show_touch_test();
void ui_show_calibration();
bool ui_calibration_active();
void ui_close_touch_test();
void ui_show_reset();
void ui_close_reset();
bool ui_reset_active();
void ui_touch_sample(int raw_x, int raw_y, int x, int y, bool pressed,
                     uint8_t rotation, bool sensor = true);
UiTouchSample ui_touch_state();
bool ui_touch_test_active();
bool ui_setup_active();
int ui_page_index();
int ui_page_count(); // All five pages remain visible, including the locked invite.

} // namespace badge
