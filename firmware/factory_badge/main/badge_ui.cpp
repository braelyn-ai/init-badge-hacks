#include "badge_ui.h"
#include "ui/chrome.h"
#include "ui/ui_internal.h"
#include <algorithm>
#include <memory>
#include <mooncake.h>
#include <lvgl_cpp/obj.hpp>

namespace badge {
namespace {
ui::Context context;
lv_display_t* display = nullptr;
int pending_gesture = 0;

// Mooncake owns application lifecycle. Smooth's RAII container owns the LVGL
// scene; each page owns only its children and updates live fields in place.
class BadgeApp final : public mooncake::AppAbility {
public:
    BadgeApp() { setAppInfo().name = "Conference Badge"; }
    void onOpen() override {
        auto* screen = lv_display_get_screen_active(display);
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        frame_ = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(screen);
        lv_obj_remove_style_all(frame_->get());
        frame_->setSize(ui::Width, ui::Height);
        frame_->center();
        frame_->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        // LVGL delivers gestures to the first ancestor without this flag.
        // This scene owns horizontal paging; do not bubble past its handler.
        frame_->removeFlag(LV_OBJ_FLAG_GESTURE_BUBBLE);
        frame_->setBgColor(lv_color_black());
        frame_->setBgOpa(LV_OPA_COVER);
        // A gesture belongs to one uninterrupted page contact. LVGL can cancel
        // it with PRESS_LOST instead of sending this frame a RELEASED event.
        // Discard on loss and begin every new contact cleanly.
        auto cancel_gesture = [](lv_event_t*) { pending_gesture = 0; };
        lv_obj_add_event_cb(frame_->get(), cancel_gesture, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(frame_->get(), cancel_gesture, LV_EVENT_PRESS_LOST, nullptr);
        lv_obj_add_event_cb(frame_->get(), [](lv_event_t*) {
            if (context.setup || context.touch_test || context.reset) return;
            auto* input = lv_indev_active();
            if (!input) return;
            auto direction = lv_indev_get_gesture_dir(input);
            if (direction == LV_DIR_LEFT) pending_gesture = 1;
            else if (direction == LV_DIR_RIGHT) pending_gesture = -1;
        }, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(frame_->get(), [](lv_event_t*) {
            const int delta = pending_gesture;
            pending_gesture = 0;
            if (delta && !context.setup && !context.touch_test && !context.reset) ui_page(delta);
        }, LV_EVENT_RELEASED, nullptr);
        page_host_ = ui::container(frame_->get(), 0, 0, ui::Width, ui::Height);
        chrome_ = std::make_unique<ui::Chrome>(context, lv_display_get_layer_top(display));
        context.rebuild = true;
        refresh();
    }
    void onRunning() override { refresh(); }
    void onClose() override {
        page_.reset();
        chrome_.reset();
        frame_.reset();
        page_host_ = nullptr;
    }
    void reflow() {
        if (frame_) frame_->center();
        if (chrome_) chrome_->reflow();
        if (display) context.rotation = uint8_t(lv_display_get_rotation(display));
        if (page_) page_->cancel_input();
    }
    void refresh() {
        if (!frame_) return;
        if (context.rebuild) {
            page_.reset();
            if (context.setup) page_ = ui::make_setup(context, page_host_);
            else if (context.calibration) page_ = ui::make_calibration(context, page_host_);
            else if (context.touch_test) page_ = ui::make_touch_test(context, page_host_);
            else if (context.reset) page_ = ui::make_reset(context, page_host_);
            else {
                using Factory = std::unique_ptr<ui::PageView>(*)(ui::Context&, lv_obj_t*);
                static constexpr Factory factories[] = {
                    ui::make_init, ui::make_schedule, ui::make_after_dark,
                    ui::make_badge, ui::make_hack, ui::make_settings
                };
                page_ = factories[context.page](context, page_host_);
            }
            context.rebuild = false;
        }
        if (page_) page_->update();
        chrome_->update();
    }
private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> frame_;
    std::unique_ptr<ui::PageView> page_;
    std::unique_ptr<ui::Chrome> chrome_;
    lv_obj_t* page_host_ = nullptr;
};
BadgeApp* app = nullptr;
} // namespace

void ui_init(lv_display_t* target, UiCallbacks callbacks) {
    display = target;
    context.callbacks = std::move(callbacks);
    context.rotation = uint8_t(lv_display_get_rotation(display));
    auto instance = std::make_unique<BadgeApp>();
    app = instance.get();
    const int id = mooncake::GetMooncake().installApp(std::move(instance));
    mooncake::GetMooncake().openApp(id);
    mooncake::GetMooncake().update();
    lv_display_add_event_cb(display, [](lv_event_t*) { ui_rotation_changed(); }, LV_EVENT_RESOLUTION_CHANGED, nullptr);
}
void ui_update(const UiModel& model) {
    if (context.model.after_dark_unlocked && !model.after_dark_unlocked &&
        context.page == ui::AfterDarkPage && !context.setup && !context.touch_test && !context.reset) {
        // A deliberate USB relock must discard the completed Morse gesture.
        context.rebuild = true;
        pending_gesture = 0;
    }
    context.model = model;
    if (!context.setup && !context.touch_test && !context.reset && !ui::page_visible(context.page, model)) {
        context.page = 3;
        context.rebuild = true;
    }
}
void ui_tick(uint32_t now_ms) {
    (void)now_ms;
    mooncake::GetMooncake().update();
}
void ui_rotation_changed() { if (app) app->reflow(); }
void ui_page(int delta) {
    if (context.setup || context.touch_test || context.reset || delta == 0) return;
    // IDs stay stable for setup returns and diagnostics. The locked invitation
    // is a normal page; its code surface owns the reveal interaction.
    const int steps = delta % ui::visible_page_count(context.model);
    const int direction = steps > 0 ? 1 : -1;
    for (int i = 0; i < std::abs(steps); ++i) {
        do {
            context.page = (context.page + direction + ui::PageCount) % ui::PageCount;
        } while (!ui::page_visible(context.page, context.model));
    }
    context.rebuild = true;
    pending_gesture = 0;
}
void ui_open_after_dark() {
    if (!context.model.after_dark_unlocked || context.setup || context.touch_test || context.reset) return;
    context.page = ui::AfterDarkPage;
    context.rebuild = true;
    pending_gesture = 0;
}
void ui_button(bool both, int delta) {
    if (context.reset) { ui_close_reset(); return; }
    if (context.touch_test) { ui_close_touch_test(); return; }
    if (context.setup) {
        if (context.callbacks.close_setup) context.callbacks.close_setup();
        return;
    }
    if (both) ui::request_setup(context);
    else ui_page(delta);
}
void ui_show_setup(const std::string& ssid, const std::string& password, const std::string& ip, const std::string& status) {
    if (context.reset) return;
    if (!context.setup) {
        pending_gesture = 0;
        context.setup_origin = context.page;
        context.setup = true;
        context.touch_test = false;
        context.rebuild = true;
    }
    context.ssid = ssid;
    context.password = password;
    context.ip = ip;
    context.setup_status = status;
}
void ui_close_setup(bool saved) {
    if (!context.setup) return;
    context.setup = false;
    context.page = saved && context.setup_origin != 5 ? 3 : context.setup_origin;
    context.ssid.clear();
    context.password.clear();
    context.setup_status.clear();
    context.rebuild = true;
}
void ui_show_touch_test() {
    if (context.setup || context.reset) return;
    context.page = 5;
    pending_gesture = 0;
    context.touch_test = true;
    context.touch = {};
    context.rotation = uint8_t(lv_display_get_rotation(display));
    context.touch.rotation = context.rotation;
    context.rebuild = true;
}
void ui_show_calibration() {
    ui_show_touch_test();
    if (context.touch_test) { context.calibration = true; context.rebuild = true; }
}
bool ui_calibration_active() { return context.calibration; }
void ui_close_touch_test() {
    if (!context.touch_test) return;
    context.touch_test = false;
    if (context.calibration && context.callbacks.cancel_calibration) context.callbacks.cancel_calibration();
    context.calibration = false;
    context.page = 5;
    context.rebuild = true;
}
void ui_show_reset() {
    if (context.setup || context.touch_test || context.reset) return;
    context.page = 5;
    pending_gesture = 0;
    context.reset = true;
    context.reset_confirmed = false;
    context.rebuild = true;
}
void ui_close_reset() {
    if (!context.reset || (context.reset_confirmed && context.model.reset_state == ResetState::Working)) return;
    context.reset = false;
    context.page = context.reset_confirmed && context.model.reset_state == ResetState::Complete ? 3 : 5;
    context.reset_confirmed = false;
    pending_gesture = 0;
    context.rebuild = true;
}
bool ui_reset_active() { return context.reset; }
void ui_touch_sample(int raw_x, int raw_y, int x, int y, bool pressed, uint8_t rotation, bool sensor) {
    if (!context.touch_test) return;
    // Sensor coordinates are supplied by the native LVGL input path. No UI
    // calibration, second rotation, scaling or gesture interpretation occurs.
    context.touch = {true, pressed, sensor, raw_x, raw_y, x, y, rotation};
}
UiTouchSample ui_touch_state() { return context.touch; }
bool ui_touch_test_active() { return context.touch_test; }
bool ui_setup_active() { return context.setup; }
int ui_page_index() { return context.page; }
int ui_page_count() { return ui::visible_page_count(context.model); }

} // namespace badge
