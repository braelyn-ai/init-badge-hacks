#include "widgets.h"
#include "brand_assets.h"
#include "design_assets.h"
#include <algorithm>
#include <cstring>

namespace badge::ui {
PageView::PageView(Context& context, lv_obj_t* parent) : context_(context), root_(container(parent, 0, 0, Width, Height)) {}
PageView::~PageView() { if (root_) lv_obj_delete(root_); }

lv_obj_t* container(lv_obj_t* parent, int x, int y, int width, int height) {
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return object;
}

void set_font(lv_obj_t* object, const lv_font_t* font) {
    lv_obj_set_style_text_font(object, font, 0);
    // Figma line boxes, rounded to native display pixels. Keep native LVGL
    // layout/rendering; padding supplies the first/last leading and the
    // line-space style supplies it between wrapped lines.
    int line_height = font->line_height;
    if (font == &font_mono_semibold_12) line_height = 21; // Figma: 20.8 px.
    else if (font == &font_mono_12 || font == &font_sans_12) line_height = 16;
    else if (font == &font_mono_24) line_height = 31;
    else if (font == &font_sans_16) line_height = 21;
    else if (font == &font_sans_24) line_height = 33;
    else if (font == &font_sans_32) line_height = 42;
    const int leading = std::max(0, line_height - int(font->line_height));
    // Plex captions and the Suisse substitute share Figma's measured top
    // baseline; symmetric leading put their visible glyphs too low.
    const int top = (font == &font_mono_semibold_12 || font == &font_sans_24) ? 0 : leading / 2;
    lv_obj_set_style_pad_top(object, top, 0);
    lv_obj_set_style_pad_bottom(object, leading - top, 0);
    lv_obj_set_style_text_line_space(object, leading, 0);
}

lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int width, const lv_font_t* font, lv_color_t color) {
    auto* object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    set_font(object, font);
    lv_obj_set_style_text_color(object, color, 0);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_label_set_text(object, text);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int width, int height,
                 std::function<void()> action) {
    auto* object = lv_button_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, panel(), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
    auto* title = label(object, text, 0, 0, width - 8, &font_sans_16, cream());
    lv_obj_center(title);
    on_tap(object, std::move(action));
    return object;
}

void on_tap(lv_obj_t* object, std::function<void()> action) {
    struct TapAction {
        lv_obj_t* object;
        std::function<void()> action;
        lv_point_t start{};
        bool armed = false;
    };
    auto* tap = new TapAction{object, std::move(action)};
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(object, [](lv_event_t* event) {
        auto* tap = static_cast<TapAction*>(lv_event_get_user_data(event));
        auto code = lv_event_get_code(event);
        if (code == LV_EVENT_DELETE) {
            if (lv_event_get_target(event) == tap->object) delete tap;
            return;
        }
        auto* input = lv_indev_active();
        if (code == LV_EVENT_PRESSED) {
            tap->armed = true;
            if (input) lv_indev_get_point(input, &tap->start);
        } else if (code == LV_EVENT_PRESSING && input && tap->armed) {
            lv_point_t point;
            lv_indev_get_point(input, &point);
            // Match LVGL's default scrolling threshold. A single drag remains
            // cancelled even when it returns to its starting widget/position.
            if (std::abs(point.x - tap->start.x) > 10 ||
                std::abs(point.y - tap->start.y) > 10) tap->armed = false;
        } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_LONG_PRESSED ||
                   code == LV_EVENT_SCROLL_BEGIN || code == LV_EVENT_GESTURE) {
            tap->armed = false;
        } else if (code == LV_EVENT_SHORT_CLICKED && tap->armed) {
            tap->armed = false;
            // Copy before invoking: an action may delete the widget/modal.
            auto action = tap->action;
            if (action) action();
        }
    }, LV_EVENT_ALL, tap);
}

void set_text(lv_obj_t* object, const std::string& text) {
    if (object && std::strcmp(lv_label_get_text(object), text.c_str()) != 0) lv_label_set_text(object, text.c_str());
}
void set_hidden(lv_obj_t* object, bool hidden) {
    if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t* qr(lv_obj_t* parent, const std::string& value, int x, int y, int size, int quiet_zone) {
    auto* frame = container(parent, x, y, size, size);
    lv_obj_set_style_bg_color(frame, white(), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    auto* code = lv_qrcode_create(frame);
    lv_qrcode_set_size(code, size - 2 * quiet_zone);
    lv_qrcode_set_dark_color(code, lv_color_black());
    lv_qrcode_set_light_color(code, white());
    lv_obj_center(code);
    if (lv_qrcode_update(code, value.data(), value.size()) != LV_RESULT_OK) {
        lv_obj_delete(code);
        label(frame, "QR unavailable", 4, size / 2 - 8, size - 8, &font_sans_14, lv_color_black());
    }
    return frame;
}
void brand(lv_obj_t* parent, int y, bool large) {
    auto* image = lv_image_create(parent);
    lv_image_set_src(image, large ? &supplied_logo_large : &supplied_logo_small);
    lv_obj_set_style_image_recolor(image, white(), 0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    lv_obj_align(image, LV_ALIGN_TOP_MID, 0, y);
}

void request_setup(Context& context) {
    if (!context.setup && !context.touch_test && context.callbacks.request_setup) context.callbacks.request_setup();
}
std::string battery_text(int percentage) {
    return percentage < 0 ? "Battery unavailable" : "Battery " + std::to_string(std::min(100, percentage)) + "%";
}
} // namespace badge::ui
