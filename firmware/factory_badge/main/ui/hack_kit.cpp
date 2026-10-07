#include "hack_kit.h"
#include <algorithm>
#include <cmath>

namespace badge::ui::hack {
namespace {
constexpr uint32_t CharMs = 34;
lv_obj_t* circle(lv_obj_t* parent, int cx, int cy, int diameter) {
    auto* object = container(parent, cx - diameter / 2, cy - diameter / 2, diameter, diameter);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
    return object;
}
} // namespace

lv_obj_t* disc(lv_obj_t* parent, int cx, int cy, int diameter, lv_color_t color) {
    auto* object = circle(parent, cx, cy, diameter);
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}
lv_obj_t* ring(lv_obj_t* parent, int cx, int cy, int diameter, int thickness, lv_color_t color) {
    auto* object = circle(parent, cx, cy, diameter);
    lv_obj_set_style_border_width(object, thickness, 0);
    lv_obj_set_style_border_color(object, color, 0);
    return object;
}
void set_fill(lv_obj_t* object, lv_color_t color) {
    // Skip unchanged colours: a style write always invalidates the object.
    if (lv_obj_get_style_border_width(object, LV_PART_MAIN) > 0) {
        if (!lv_color_eq(lv_obj_get_style_border_color(object, LV_PART_MAIN), color)) lv_obj_set_style_border_color(object, color, 0);
    } else if (!lv_color_eq(lv_obj_get_style_bg_color(object, LV_PART_MAIN), color)) lv_obj_set_style_bg_color(object, color, 0);
}

lv_color_t hsv(int hue, int saturation, int value) {
    hue = ((hue % 360) + 360) % 360;
    return lv_color_hsv_to_rgb(uint16_t(hue), uint8_t(std::clamp(saturation, 0, 100)), uint8_t(std::clamp(value, 0, 100)));
}
lv_color_t scaled(lv_color_t color, float level) {
    const auto channel = [level](uint8_t value) { return uint8_t(std::clamp(value * level, 0.0f, 255.0f)); };
    return lv_color_make(channel(color.red), channel(color.green), channel(color.blue));
}
lv_color_t mix(lv_color_t from, lv_color_t to, float amount) {
    return lv_color_mix(to, from, uint8_t(std::clamp(amount, 0.0f, 1.0f) * 255));
}

FrameClock::FrameClock(uint32_t interval_ms)
    : interval_(interval_ms), start_(lv_tick_get()), last_(start_), now_(start_) {}
bool FrameClock::due() {
    const uint32_t now = lv_tick_get();
    const uint32_t elapsed = now - last_;
    if (elapsed < interval_) return false;
    last_ = now_ = now;
    dt_ = std::min<uint32_t>(elapsed, 100) / 1000.0f;
    return true;
}

UiMotion gravity(const Context& context) {
    return context.motion.valid ? context.motion : UiMotion{true, 0.0f, 1.0f};
}
std::string first_name(const Context& context, const char* fallback) {
    const auto& name = context.model.name;
    const auto begin = name.find_first_not_of(' ');
    if (begin == std::string::npos) return fallback;
    return name.substr(begin, name.find(' ', begin) - begin);
}
float random_unit() { return lv_rand(0, 0xffff) / 65536.0f; }
int random_between(int low, int high) { return low + int(lv_rand(0, uint32_t(high - low))); }

void on_paint(lv_obj_t* object, Painter painter, bool after_children) {
    struct Paint { lv_obj_t* object; Painter painter; };
    auto* paint = new Paint{object, std::move(painter)};
    lv_obj_add_event_cb(object, [](lv_event_t* event) {
        auto* paint = static_cast<Paint*>(lv_event_get_user_data(event));
        lv_area_t coords;
        lv_obj_get_coords(paint->object, &coords);
        paint->painter(lv_event_get_layer(event), coords);
    }, after_children ? LV_EVENT_DRAW_POST : LV_EVENT_DRAW_MAIN, paint);
    lv_obj_add_event_cb(object, [](lv_event_t* event) {
        auto* paint = static_cast<Paint*>(lv_event_get_user_data(event));
        if (lv_event_get_target(event) == paint->object) delete paint;
    }, LV_EVENT_DELETE, paint);
}
lv_obj_t* surface(lv_obj_t* parent, int x, int y, int width, int height, Painter painter) {
    auto* object = container(parent, x, y, width, height);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    on_paint(object, std::move(painter));
    return object;
}
bool needs_paint(const lv_layer_t* layer, int x, int y, int width, int height) {
    const auto& clip = layer->_clip_area;
    return x <= clip.x2 && x + width > clip.x1 && y <= clip.y2 && y + height > clip.y1;
}
void fill_triangle(lv_layer_t* layer, float x0, float y0, float x1, float y1, float x2, float y2,
                   lv_color_t color, lv_opa_t opa) {
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.p[0] = {lv_value_precise_t(x0), lv_value_precise_t(y0)};
    dsc.p[1] = {lv_value_precise_t(x1), lv_value_precise_t(y1)};
    dsc.p[2] = {lv_value_precise_t(x2), lv_value_precise_t(y2)};
    dsc.color = color;
    dsc.opa = opa;
    lv_draw_triangle(layer, &dsc);
}
void fill_rect(lv_layer_t* layer, int x, int y, int width, int height, lv_color_t color, lv_opa_t opa) {
    if (width <= 0 || height <= 0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    const lv_area_t area = {x, y, x + width - 1, y + height - 1};
    lv_draw_rect(layer, &dsc, &area);
}
void fill_circle(lv_layer_t* layer, int cx, int cy, int radius, lv_color_t color, lv_opa_t opa) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_area_t area = {cx - radius, cy - radius, cx + radius, cy + radius};
    lv_draw_rect(layer, &dsc, &area);
}
void draw_line(lv_layer_t* layer, float x0, float y0, float x1, float y1, int width,
               lv_color_t color, lv_opa_t opa) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1 = {lv_value_precise_t(x0), lv_value_precise_t(y0)};
    dsc.p2 = {lv_value_precise_t(x1), lv_value_precise_t(y1)};
    dsc.width = width;
    dsc.color = color;
    dsc.opa = opa;
    dsc.round_start = dsc.round_end = 1;
    lv_draw_line(layer, &dsc);
}
void draw_glyph(lv_layer_t* layer, const lv_font_t* font, uint32_t codepoint, int x, int y,
                lv_color_t color, lv_opa_t opa) {
    char text[3] = {};
    if (codepoint < 0x80) text[0] = char(codepoint);
    else if (codepoint < 0x800) { text[0] = char(0xc0 | codepoint >> 6); text[1] = char(0x80 | (codepoint & 0x3f)); }
    else return;
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.color = color;
    dsc.opa = opa;
    dsc.text = text;
    dsc.text_local = 1; // The renderer keeps its own copy of this stack buffer.
    const int height = lv_font_get_line_height(font);
    const lv_area_t area = {x, y, x + height * 2, y + height};
    lv_draw_label(layer, &dsc, &area);
}

Subtitle::Subtitle(lv_obj_t* parent, int x, int y, int width, int height, const lv_font_t* font, lv_color_t color) {
    panel_ = container(parent, x, y, width, height);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(panel_, 12, 0);
    lv_obj_set_style_bg_color(panel_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_80, 0);
    text_ = label(panel_, "", 0, 0, width - 24, font, color);
    lv_label_set_long_mode(text_, LV_LABEL_LONG_WRAP);
    lv_obj_center(text_);
    set_hidden(panel_, true);
}
void Subtitle::say(const std::string& text, uint32_t linger_ms) {
    line_ = text;
    shown_ = 0;
    linger_ = linger_ms;
    started_ = lv_tick_get();
    typing_ = visible_ = true;
    lv_label_set_text(text_, "");
    set_hidden(panel_, false);
}
void Subtitle::hide() {
    typing_ = visible_ = false;
    set_hidden(panel_, true);
}
void Subtitle::update() {
    if (!visible_) return;
    const uint32_t elapsed = lv_tick_get() - started_;
    // Never split a two-byte character: the font covers Latin-1 names.
    size_t shown = std::min<size_t>(line_.size(), elapsed / CharMs + 1);
    while (shown < line_.size() && (uint8_t(line_[shown]) & 0xc0) == 0x80) ++shown;
    typing_ = shown < line_.size();
    if (shown != shown_) {
        shown_ = shown;
        lv_label_set_text(text_, line_.substr(0, shown).c_str());
        lv_obj_center(text_);
    }
    if (elapsed > line_.size() * CharMs + linger_) hide();
}
} // namespace badge::ui::hack
