#pragma once
#include "ui_internal.h"
#include "design_fonts.h"
#include <functional>

namespace badge::ui {
inline lv_color_t white() { return lv_color_hex(0xffffff); }
inline lv_color_t cream() { return lv_color_hex(0xf5f2e8); }
inline lv_color_t muted() { return lv_color_hex(0x808080); }
inline lv_color_t panel() { return lv_color_hex(0x161616); }
inline lv_color_t accent() { return lv_color_hex(0xa596ff); }

lv_obj_t* container(lv_obj_t* parent, int x, int y, int width, int height);
void set_font(lv_obj_t* object, const lv_font_t* font);
lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int width,
                const lv_font_t* font = &font_sans_16, lv_color_t color = white());
lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int width, int height,
                 std::function<void()> action);
// Release-only action with drag/hold cancellation, shared by buttons and QR cards.
void on_tap(lv_obj_t* object, std::function<void()> action);
void set_text(lv_obj_t* object, const std::string& text);
void set_hidden(lv_obj_t* object, bool hidden);
lv_obj_t* qr(lv_obj_t* parent, const std::string& value, int x, int y, int size, int quiet_zone = 10);
void brand(lv_obj_t* parent, int y, bool large = false);
void request_setup(Context& context);
std::string battery_text(int percentage);

} // namespace badge::ui
