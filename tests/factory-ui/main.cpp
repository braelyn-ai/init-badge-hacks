#include "badge_ui.h"
#include "schedule.h"
#include "ui/design_fonts.h"
#include "ui/design_assets.h"
#include "ui/invitation_art.h"
#include <mooncake.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr int Width = 468, Height = 466;
std::vector<uint16_t> pixels(Width * Height);
uint16_t buffer[Width * 40];
uint32_t clock_ms = 0;
int touch_x = 0, touch_y = 0;
bool pressed = false;
unsigned flush_count = 0;
std::string output_directory;

void spin(int count = 6) {
    for (int i = 0; i < count; ++i) {
        clock_ms += 20;
        lv_tick_inc(20);
        badge::ui_tick(clock_ms);
        lv_timer_handler();
    }
}
void touch(int x, int y, bool down, int ticks = 3) {
    touch_x = x;
    touch_y = y;
    pressed = down;
    spin(ticks);
}
void tap(int x, int y) {
    touch(x, y, true, 5);
    touch(x, y, false, 6);
}
void swipe(int x0, int y0, int x1, int y1) {
    touch(x0, y0, true);
    for (int i = 1; i <= 12; ++i) touch(x0 + (x1 - x0) * i / 12, y0 + (y1 - y0) * i / 12, true, 1);
    touch(x1, y1, false);
    spin(60); // Allow LVGL's native scroll throw and snap to settle.
}
int lit_pixels(int x0, int y0, int x1, int y1) {
    int count = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        const auto p = pixels[y * Width + x];
        if (((p >> 11) & 31) > 7) ++count;
    }
    return count;
}
void assert_chrome(bool intro = false, bool filled_profile = false) {
    if (filled_profile) {
        // A configured Badge keeps the brand mark and arrows but no dots.
        assert(lit_pixels(203, 24, 265, 38) > 30);
        assert(lit_pixels(10, 210, 64, 253) > 40);
        assert(lit_pixels(404, 210, 458, 253) > 40);
        assert(lit_pixels(184, 430, 284, 442) == 0);
        return;
    }
    if (!intro) assert(lit_pixels(203, 24, 265, 38) > 30); // Supplied small mark.
    assert(lit_pixels(10, 210, 64, 253) > 40);
    assert(lit_pixels(404, 210, 458, 253) > 40);
    assert(lit_pixels(184, 430, 284, 442) > 25); // Square page indicators.
}

void assert_idle() {
    spin(30); // Finish button style transitions.
    const auto before = flush_count;
    spin(30);
    assert(flush_count == before); // Static pages do no periodic redraws.
}
void assert_complete_schedule_frame(lv_display_t* display) {
    assert_chrome();
    assert(lit_pixels(175, 52, 293, 80) > 100); // Schedule heading survives scrolling.
    assert(lit_pixels(160, 392, 308, 412) > 30); // Swipe instruction survives scrolling.
    // A settled partial render must match a full invalidation byte-for-byte:
    // missing chrome or a clipped/stale section of a card cannot pass through
    // geometry-only assertions or be hidden by our review-image conversion.
    const auto partial_frame = pixels;
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_obj_invalidate(lv_display_get_layer_top(display));
    spin();
    assert(pixels == partial_frame);
}
void snapshot(const std::string& name) {
    spin();
    if (output_directory.empty()) return;
    auto path = output_directory + "/" + name + ".ppm";
    auto* file = std::fopen(path.c_str(), "wb");
    assert(file);
    std::fprintf(file, "P6\n%d %d\n255\n", Width, Height);
    for (auto p : pixels) {
        const unsigned char rgb[3] = {uint8_t(((p >> 11) & 31) * 255 / 31),
            uint8_t(((p >> 5) & 63) * 255 / 63), uint8_t((p & 31) * 255 / 31)};
        std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file);
}
lv_obj_t* find_label(lv_obj_t* object, const char* text) {
    if (lv_obj_check_type(object, &lv_label_class) && !std::strcmp(lv_label_get_text(object), text)) return object;
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) {
        if (auto* found = find_label(lv_obj_get_child(object, i), text)) return found;
    }
    return nullptr;
}
bool hidden_ancestor(lv_obj_t* obj) {
    for (; obj; obj = lv_obj_get_parent(obj)) if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return true;
    return false;
}
lv_obj_t* active_label(lv_obj_t* obj, const char* text) {
    if (lv_obj_check_type(obj, &lv_label_class) && !std::strcmp(lv_label_get_text(obj), text) && !hidden_ancestor(obj)) return obj;
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto* result = active_label(lv_obj_get_child(obj, i), text)) return result;
    return nullptr;
}
lv_obj_t* action_label(lv_obj_t* obj, const char* text) {
    if (lv_obj_check_type(obj, &lv_label_class) && !std::strcmp(lv_label_get_text(obj), text) &&
        !hidden_ancestor(obj) && lv_obj_check_type(lv_obj_get_parent(obj), &lv_button_class)) return obj;
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto* result = action_label(lv_obj_get_child(obj, i), text)) return result;
    return nullptr;
}
void tap_text(const char* text) {
    auto* obj = action_label(lv_screen_active(), text);
    if (!obj) std::fprintf(stderr, "Missing action: %s on page %d\n", text, badge::ui_page_index());
    assert(obj);
    lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF); spin();
    lv_area_t box; lv_obj_get_coords(obj, &box);
    tap((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2);
}
lv_obj_t* widget(lv_obj_t* obj, const lv_obj_class_t* type) {
    if (lv_obj_check_type(obj, type)) return obj;
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto* result = widget(lv_obj_get_child(obj, i), type)) return result;
    return nullptr;
}
// Submenus return through the large white bottom action, never the chevron.
void menu_back() {
    auto* menu = widget(lv_screen_active(), &lv_menu_class); assert(menu);
    assert(lv_obj_has_flag(lv_menu_get_main_header_back_button(menu), LV_OBJ_FLAG_HIDDEN));
    auto* done = action_label(lv_screen_active(), "Done"); assert(done);
    lv_area_t box; lv_obj_get_coords(lv_obj_get_parent(done), &box);
    // Same left edge and width as the menu rows (asserted on Settings home).
    assert(box.x1 == 98 && box.x2 == 369 && box.y1 == 354 && box.y2 == 399);
    tap((box.x1 + box.x2) / 2, (box.y1 + box.y2) / 2);
}
lv_obj_t* find_image(lv_obj_t* object, const lv_image_dsc_t* source) {
    if (lv_obj_check_type(object, &lv_image_class) && lv_image_get_src(object) == source) return object;
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) {
        if (auto* found = find_image(lv_obj_get_child(object, i), source)) return found;
    }
    return nullptr;
}
lv_obj_t* find_qr(lv_obj_t* object) {
    if (lv_obj_check_type(object, &lv_qrcode_class)) return object;
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) {
        if (auto* found = find_qr(lv_obj_get_child(object, i))) return found;
    }
    return nullptr;
}
void assert_invitation_qr(bool visible) {
    auto* code = find_qr(lv_display_get_screen_active(lv_display_get_default()));
    assert(code);
    assert(lv_obj_is_visible(code) == visible);
    auto* frame = lv_obj_get_parent(code);
    assert(lv_obj_get_width(frame) == 300 && lv_obj_get_height(frame) == 300);
    assert(lv_obj_get_x(frame) == 10 && lv_obj_get_y(frame) == 0); // Same frame as the poster.
}
lv_obj_t* find_gif(lv_obj_t* object) {
    if (lv_obj_check_type(object, &lv_gif_class)) return object;
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) {
        if (auto* found = find_gif(lv_obj_get_child(object, i))) return found;
    }
    return nullptr;
}
lv_obj_t* find_morse_feedback(lv_obj_t* object) {
    if (lv_obj_get_width(object) == 336 && lv_obj_get_height(object) == 86) return object;
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) {
        if (auto* found = find_morse_feedback(lv_obj_get_child(object, i))) return found;
    }
    return nullptr;
}
std::vector<lv_obj_t*> morse_symbols(lv_obj_t* object) {
    std::vector<lv_obj_t*> symbols;
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return symbols;
    // Registered input is drawn as shapes, never translated letter labels.
    assert(!lv_obj_check_type(object, &lv_label_class));
    if (lv_obj_get_child_count(object) == 0 &&
        lv_obj_get_style_bg_opa(object, LV_PART_MAIN) == LV_OPA_COVER &&
        lv_obj_get_height(object) == 8 &&
        (lv_obj_get_width(object) == 8 || lv_obj_get_width(object) == 24)) {
        symbols.push_back(object);
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) {
        auto children = morse_symbols(lv_obj_get_child(object, i));
        symbols.insert(symbols.end(), children.begin(), children.end());
    }
    return symbols;
}
int horizontal_gap(lv_obj_t* left, lv_obj_t* right) {
    lv_area_t left_area, right_area;
    lv_obj_get_coords(left, &left_area);
    lv_obj_get_coords(right, &right_area);
    assert(left_area.y1 == right_area.y1);
    return right_area.x1 - left_area.x2 - 1;
}
void assert_inside(lv_obj_t* child, lv_obj_t* parent) {
    lv_area_t child_area, parent_area;
    lv_obj_get_coords(child, &child_area);
    lv_obj_get_coords(parent, &parent_area);
    assert(child_area.x1 >= parent_area.x1 && child_area.x2 <= parent_area.x2);
    assert(child_area.y1 >= parent_area.y1 && child_area.y2 <= parent_area.y2);
}
void assert_schedule_center(lv_obj_t* row) {
    lv_area_t area;
    lv_obj_get_coords(row, &area);
    // The page arrows meet at display y=233, independent of row height.
    const int center_y = area.y1 + lv_area_get_height(&area) / 2;
    assert(center_y == 233);
}
unsigned centered_schedule_row(lv_obj_t* list) {
    unsigned nearest = 0;
    int nearest_distance = Height * 2;
    for (unsigned i = 0; i < lv_obj_get_child_count(list); ++i) {
        lv_area_t area;
        lv_obj_get_coords(lv_obj_get_child(list, i), &area);
        const int delta = area.y1 + area.y2 - 466;
        const int distance = delta < 0 ? -delta : delta;
        if (distance < nearest_distance) {
            nearest = i;
            nearest_distance = distance;
        }
    }
    assert_schedule_center(lv_obj_get_child(list, nearest));
    return nearest;
}
int visible_dots(lv_obj_t* object) {
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return 0;
    if ((lv_obj_get_width(object) == 8 && lv_obj_get_height(object) == 8) ||
        (lv_obj_get_width(object) == 12 && lv_obj_get_height(object) == 12)) return 1;
    int count = 0;
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i)
        count += visible_dots(lv_obj_get_child(object, i));
    return count;
}

}

int main(int argc, char** argv) {
    if (argc == 2) {
        output_directory = argv[1];
        std::filesystem::create_directories(output_directory);
    }
    lv_init();
    auto* display = lv_display_create(Width, Height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t* area, uint8_t* map) {
        ++flush_count;
        auto* source = reinterpret_cast<uint16_t*>(map);
        const int stride = lv_display_get_buf_active(d)->header.stride / sizeof(uint16_t);
        assert(area->x1 >= 0 && area->x2 < Width && area->y1 >= 0 && area->y2 < Height);
        for (int y = area->y1; y <= area->y2; ++y) for (int x = area->x1; x <= area->x2; ++x) {
            pixels[y * Width + x] = source[(y - area->y1) * stride + x - area->x1];
        }
        lv_display_flush_ready(d);
    });
    auto* input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_read_cb(input, [](lv_indev_t*, lv_indev_data_t* data) {
        data->point = {touch_x, touch_y};
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    });

    int brightness = 0, orientation = -1, network = -1, setup = 0;
    int bookmarked = -1, resets = 0, after_dark_unlocks = 0;
    int calibration_requests = 0;
    bool vibrating = false;
    unsigned vibration_starts = 0, vibration_stops = 0;
    badge::UiModel model;
    badge::UiCallbacks callbacks;
    callbacks.calibrate_touch = [&] { ++calibration_requests; };
    callbacks.brightness = [&](int value) { brightness = value; };
    callbacks.orientation = [&](badge::Orientation value) { orientation = int(value); };
    callbacks.bookmark = [&](int index) { bookmarked = index; };
    callbacks.reset_badge = [&] { ++resets; };
    callbacks.network = [&](int value) { network = value; };
    callbacks.request_setup = [&] { ++setup; badge::ui_show_setup("init-test-badge", "example1234"); };
    callbacks.close_setup = [] { badge::ui_close_setup(false); };
    callbacks.unlock_after_dark = [&] {
        ++after_dark_unlocks;
        model.after_dark_unlocked = true;
        badge::ui_update(model);
    };
    callbacks.morse_pressed = [&](bool active) {
        assert(active != vibrating); // Hardware writes are contact edges only.
        vibrating = active;
        if (active) ++vibration_starts;
        else ++vibration_stops;
    };
    badge::ui_init(display, callbacks);
    model.clock_text = "12:34 PM";
    model.date_text = "2026-09-17 12:34 PM";
    model.clock_epoch = 1789648440;
    model.utc_offset_minutes = 0;
    model.clock_valid = true;
    model.battery_percent = 74;
    badge::ui_update(model);
    spin();

    // The locked invitation is discoverable through every navigation route.
    assert(badge::ui_page_count() == 5);
    assert(visible_dots(lv_display_get_layer_top(display)) == 5);
    badge::ui_open_after_dark();
    assert(badge::ui_page_index() == 0);
    for (int id : {1, 2, 3, 4, 0}) {
        tap(434, 233);
        assert(badge::ui_page_index() == id);
    }
    for (int id : {4, 3, 2, 1, 0}) {
        badge::ui_button(false, -1); spin();
        assert(badge::ui_page_index() == id);
    }
    badge::ui_page(1); spin();
    swipe(320, 390, 150, 390);
    assert(badge::ui_page_index() == 2);
    badge::ui_page(-1); spin();
    assert(badge::ui_page_index() == 1);

    // Native pointer holds enter Morse only on the locked invitation. A dash
    // deliberately exceeds LVGL's ordinary long-press time.
    badge::ui_page(1); spin();
    auto* invitation_prompt = find_label(lv_display_get_screen_active(display), "tap the code to reveal a secret invitation");
    assert(invitation_prompt && lv_obj_get_height(invitation_prompt) <= 100);
    assert_invitation_qr(false);
    assert_inside(invitation_prompt, lv_obj_get_parent(invitation_prompt));
    assert(!lv_obj_is_visible(find_image(lv_display_get_screen_active(display), &badge::ui::invitation_art)));
    assert(!find_label(lv_display_get_screen_active(display), "After Dark"));
    assert(lit_pixels(54, 88, 414, 166) == 0); // The secret event name is not painted while locked.
    assert(!find_gif(lv_display_get_screen_active(display)));
    snapshot("after-dark-locked");
    assert_idle();
    const auto code_pulse = [](bool dash, int gap_ticks = 10, int x = 234, int y = 270) {
        touch(x, y, true, dash ? 30 : 10); // Nominal 200 ms dot / 600 ms dash.
        touch(x, y, false, gap_ticks);
    };
    const auto code_prefix = [&] {
        code_pulse(false); code_pulse(false, 40); // i
        code_pulse(true); code_pulse(false, 40);  // n
        code_pulse(false); code_pulse(false, 40); // i
    };
    const auto code_suffix = [&] {
        code_pulse(true); code_pulse(false, 40); // n
        code_pulse(false); code_pulse(false, 40); // i
        code_pulse(true, 40);                    // t
    };
    const auto restart_code_page = [] {
        badge::ui_page(1); spin();
        badge::ui_page(-1); spin();
    };

    // Morse entry keeps the background black and never invokes haptics.
    auto starts = vibration_starts;
    touch(234, 270, true, 10);
    assert(!vibrating && vibration_starts == starts);
    auto* surface = lv_obj_get_parent(invitation_prompt);
    assert(lv_color_to_u32(lv_obj_get_style_bg_color(surface, LV_PART_MAIN)) == lv_color_to_u32(lv_color_black()));
    touch(234, 270, false, 10);
    restart_code_page();
    touch(234, 270, true, 100);
    assert(!vibrating && vibration_starts == starts);
    touch(234, 270, false);
    restart_code_page();

    // Each completed contact draws the registered symbol immediately. A bad
    // prefix stays visible and keeps accepting input until the attendee pauses.
    auto feedback = [&] {
        auto* object = find_morse_feedback(lv_display_get_screen_active(display));
        assert(object);
        return object;
    };
    assert(lv_obj_has_flag(feedback(), LV_OBJ_FLAG_HIDDEN));
    code_pulse(false);
    auto symbols = morse_symbols(feedback());
    assert(symbols.size() == 1 && lv_obj_get_width(symbols[0]) == 8);
    assert(lv_obj_get_style_radius(symbols[0], LV_PART_MAIN) == LV_RADIUS_CIRCLE);
    assert(lit_pixels(66, 100, 402, 186) > 10);
    lv_area_t first_dot_area;
    lv_obj_get_coords(symbols[0], &first_dot_area);
    // The visualization itself remains part of the same touch target.
    code_pulse(false, 7, first_dot_area.x1 + 3, first_dot_area.y1 + 3);
    symbols = morse_symbols(feedback());
    assert(symbols.size() == 2 && horizontal_gap(symbols[0], symbols[1]) == 8);
    code_pulse(false); // "..." is wrong, but all three taps must be shown.
    assert(morse_symbols(feedback()).size() == 3 && after_dark_unlocks == 0);
    spin(30); // A letter pause introduces a visibly larger next-group gap.
    code_pulse(true);
    symbols = morse_symbols(feedback());
    assert(symbols.size() == 4 && lv_obj_get_width(symbols[3]) == 24);
    assert(horizontal_gap(symbols[2], symbols[3]) == 24);
    code_pulse(false);
    assert(morse_symbols(feedback()).size() == 5);
    snapshot("after-dark-registered-symbols");
    // snapshot advances 120 ms; leave another 2.08 seconds after the pulse's
    // release gap, still safely below the 2.5-second idle deadline.
    spin(104);
    assert(!lv_obj_has_flag(feedback(), LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_style_translate_x(feedback(), LV_PART_MAIN) == 0);
    assert(lv_obj_get_style_opa(feedback(), LV_PART_MAIN) == LV_OPA_COVER);
    assert(morse_symbols(feedback()).size() == 5 && after_dark_unlocks == 0);
    bool saw_shake = false, saw_fade = false;
    for (int i = 0; i < 40; ++i) {
        spin(1);
        saw_shake |= lv_obj_get_style_translate_x(feedback(), LV_PART_MAIN) != 0;
        const auto opacity = lv_obj_get_style_opa(feedback(), LV_PART_MAIN);
        saw_fade |= opacity > LV_OPA_TRANSP && opacity < LV_OPA_COVER;
    }
    assert(saw_shake && saw_fade);
    assert(lv_obj_has_flag(feedback(), LV_OBJ_FLAG_HIDDEN));
    assert(morse_symbols(feedback()).empty());
    assert_idle();

    // A fresh press during either the shake or fade starts its next attempt
    // immediately. The old animation cannot later erase the newly entered dot.
    for (const int phase_ticks : {120, 132}) {
        code_pulse(true);
        spin(phase_ticks);
        assert(!lv_obj_has_flag(feedback(), LV_OBJ_FLAG_HIDDEN));
        if (phase_ticks == 132)
            assert(lv_obj_get_style_opa(feedback(), LV_PART_MAIN) < LV_OPA_COVER);
        touch(234, 270, true, 8);
        assert(lv_obj_get_style_translate_x(feedback(), LV_PART_MAIN) == 0);
        assert(lv_obj_get_style_opa(feedback(), LV_PART_MAIN) == LV_OPA_COVER);
        touch(234, 270, false, 20);
        symbols = morse_symbols(feedback());
        assert(symbols.size() == 1 && lv_obj_get_width(symbols[0]) == 8);
        assert(after_dark_unlocks == 0);
        restart_code_page();
        assert(lv_obj_has_flag(feedback(), LV_OBJ_FLAG_HIDDEN));
    }

    // A long incorrect letter wraps inside the reserved input area rather
    // than disappearing past the round screen or covering the prompt.
    for (int i = 0; i < 25; ++i) code_pulse(false);
    symbols = morse_symbols(feedback());
    assert(symbols.size() == 25);
    bool wrapped = false;
    lv_area_t first_symbol;
    lv_obj_get_coords(symbols.front(), &first_symbol);
    for (auto* symbol : symbols) {
        lv_area_t area, container_area;
        lv_obj_get_coords(symbol, &area);
        lv_obj_get_coords(feedback(), &container_area);
        if (area.x1 < container_area.x1 || area.x2 > container_area.x2 ||
            area.y1 < container_area.y1 || area.y2 > container_area.y2)
            std::fprintf(stderr, "Morse mark outside input area: mark %d,%d..%d,%d container %d,%d..%d,%d\n",
                area.x1, area.y1, area.x2, area.y2, container_area.x1, container_area.y1, container_area.x2, container_area.y2);
        assert_inside(symbol, feedback());
        wrapped |= area.y1 != first_symbol.y1;
    }
    assert(wrapped && after_dark_unlocks == 0);
    snapshot("after-dark-wrapped-input");
    restart_code_page();

    // Feedback callbacks must not outlive the view when navigation or setup
    // destroys it. Returning always starts with an empty registered sequence.
    code_pulse(true); spin(120);
    badge::ui_page(1); spin(30);
    badge::ui_page(-1); spin();
    assert(lv_obj_has_flag(feedback(), LV_OBJ_FLAG_HIDDEN));
    code_pulse(true); spin(120);
    badge::ui_show_setup("init-test-badge", "example1234"); spin(30);
    badge::ui_close_setup(false); spin();
    assert(badge::ui_page_index() == 2);
    assert(lv_obj_has_flag(feedback(), LV_OBJ_FLAG_HIDDEN));

    // Movement cancels the whole attempt even if the finger returns to its
    // start point before release, so a completed suffix cannot unlock it.
    code_pulse(false); code_pulse(false, 40);
    touch(234, 270, true);
    touch(246, 270, true);
    assert(!vibrating); // Moving out of the gesture stops held feedback.
    touch(234, 270, true, 25);
    touch(234, 270, false, 40);
    code_suffix();
    assert(after_dark_unlocks == 0);
    restart_code_page();

    // The separate chrome arrows still navigate and discard partial words.
    code_pulse(false); code_pulse(false, 40);
    tap(434, 233);
    assert(badge::ui_page_index() == 3);
    tap(34, 233);
    assert(badge::ui_page_index() == 2);
    code_suffix();
    assert(after_dark_unlocks == 0);
    restart_code_page();

    // Full-page code input retains completed horizontal swipe navigation.
    code_pulse(false); code_pulse(false, 40);
    swipe(320, 390, 150, 390);
    assert(badge::ui_page_index() == 3);
    swipe(150, 390, 320, 390);
    assert(badge::ui_page_index() == 2);
    code_suffix();
    assert(after_dark_unlocks == 0);
    restart_code_page();

    // Returning through a pusher while a finger stays down requires release:
    // the new page cannot count the tail of that contact as the first dot.
    touch(234, 270, true);
    assert(!vibrating);
    badge::ui_button(false, 1); spin();
    assert(!vibrating); // The old page's destructor shuts down feedback.
    badge::ui_button(false, -1); spin();
    touch(234, 270, true, 8);
    touch(234, 270, false, 7);
    code_pulse(false, 40);
    code_suffix();
    assert(after_dark_unlocks == 0);
    restart_code_page();

    // Lost contact cannot leave a held symbol or partial word behind.
    code_pulse(false); code_pulse(false, 40);
    touch(234, 270, true);
    lv_indev_wait_release(input);
    touch(234, 270, false, 40);
    assert(!vibrating);
    code_suffix();
    assert(after_dark_unlocks == 0);
    restart_code_page();

    // Leaving the page or entering a modal discards an unfinished word.
    code_pulse(false); code_pulse(false, 40);
    badge::ui_button(false, 1); spin();
    badge::ui_button(false, -1); spin();
    code_suffix();
    assert(after_dark_unlocks == 0);
    restart_code_page();
    code_pulse(false); code_pulse(false, 40);
    badge::ui_show_setup("init-test-badge", "example1234"); spin();
    code_suffix();
    assert(after_dark_unlocks == 0 && badge::ui_setup_active());
    badge::ui_close_setup(false); spin();
    code_suffix();
    assert(after_dark_unlocks == 0 && badge::ui_page_index() == 2);
    restart_code_page();

    // Rotation is a new coordinate space and cancels the current attempt.
    code_pulse(false); code_pulse(false, 40);
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180); spin();
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_0); spin();
    code_suffix();
    assert(after_dark_unlocks == 0);
    restart_code_page();

    // Every part of the page accepts code, including heading, empty background,
    // prompt and footer. Children and chrome decoration cannot swallow taps.
    // Deliberately wrong grouping, including a long pause, across the page.
    code_pulse(false, 180, 234, 100); code_pulse(false, 3, 234, 140); // i
    code_pulse(true, 40, 100, 300); code_pulse(false, 3, 234, 270);  // n
    code_pulse(false, 10, 234, 370); code_pulse(false, 30, 234, 432); // i
    // Decode once on the final dash release, without making a correct code
    // wait for either a letter pause or the failed-attempt idle deadline.
    touch(234, 270, true, 30);
    assert(after_dark_unlocks == 0);
    assert(!vibrating);
    touch(234, 270, false, 3);
    assert(!vibrating);
    assert(after_dark_unlocks == 1 && model.after_dark_unlocked);
    assert(badge::ui_page_index() == 2 && badge::ui_page_count() == 5);
    assert(!find_label(lv_display_get_screen_active(display), "tap the code to reveal a secret invitation"));
    auto* celebration = find_gif(lv_display_get_screen_active(display));
    assert(celebration && lv_gif_is_loaded(celebration));
    assert(lv_gif_get_loop_count(celebration) == 1);
    assert(find_label(lv_display_get_screen_active(display), "You're"));
    assert(!lv_obj_is_visible(find_image(lv_display_get_screen_active(display), &badge::ui::invitation_art)));
    assert_invitation_qr(false);
    snapshot("after-dark-youre");
    spin(30);
    assert(find_label(lv_display_get_screen_active(display), "Invited"));
    snapshot("after-dark-invited");
    spin(30);
    assert(find_label(lv_display_get_screen_active(display), "To"));
    snapshot("after-dark-to");
    spin(30);
    assert(!find_label(lv_display_get_screen_active(display), "You're invited"));
    spin(30); // Finish the 450ms upward slide after the words.
    assert(lv_obj_is_visible(find_image(lv_display_get_screen_active(display), &badge::ui::invitation_art)));
    assert(!find_gif(lv_display_get_screen_active(display)));
    assert(!find_label(lv_display_get_screen_active(display), "Scan to RSVP"));
    assert(!find_label(lv_display_get_screen_active(display), "Invite details coming soon"));
    auto* poster = find_image(lv_display_get_screen_active(display), &badge::ui::invitation_art);
    lv_area_t poster_area;
    lv_obj_get_coords(poster, &poster_area);
    assert(poster_area.x1 == 84 && poster_area.y1 == 100);
    assert_invitation_qr(false);
    assert(find_label(lv_display_get_screen_active(display), "Tap for QR code"));
    snapshot("after-dark-revealed");
    swipe(234, 360, 234, 160);
    assert(badge::ui_page_index() == 2);
    assert_invitation_qr(false); // Drags cannot toggle the QR.
    tap(234, 240);
    assert_invitation_qr(true);
    assert(!lv_obj_is_visible(poster));
    assert(find_label(lv_display_get_screen_active(display), "Tap to return"));
    snapshot("after-dark-qr-expanded");
    badge::ui_update(model); spin();
    assert_invitation_qr(true);
    tap(234, 240);
    assert_invitation_qr(false);
    assert(lv_obj_is_visible(poster));
    tap(234, 417); // The caption is also a tap target.
    assert_invitation_qr(true);
    tap(234, 417);
    assert_invitation_qr(false);

    starts = vibration_starts;
    code_prefix(); code_pulse(true, 40);
    assert(after_dark_unlocks == 1);
    assert(!vibrating && vibration_starts == vibration_stops);
    assert(vibration_starts == starts); // An unlocked invitation has no input buzz.
    spin(550); // One source loop finishes and releases its native decoder/timer.
    assert(!find_gif(lv_display_get_screen_active(display)));
    assert_idle();
    snapshot("after-dark-design");
    // An explicit USB relock refreshes the same page, including its completed
    // gesture latch, so the attendee can immediately try the code again.
    model.after_dark_unlocked = false;
    badge::ui_update(model); spin();
    assert(badge::ui_page_index() == 2);
    assert(find_label(lv_display_get_screen_active(display), "tap the code to reveal a secret invitation"));
    assert(lit_pixels(54, 88, 414, 166) == 0); // Relocking hides the event name again.
    code_prefix(); code_pulse(true, 40);
    assert(after_dark_unlocks == 2 && model.after_dark_unlocked);
    assert(find_gif(lv_display_get_screen_active(display)));
    // Leaving during the word sequence destroys both LVGL animations/timers.
    badge::ui_page(1); spin(150);
    assert(badge::ui_page_index() == 3);
    assert(!find_gif(lv_display_get_screen_active(display)));
    badge::ui_page(-1); spin();
    assert(!find_label(lv_display_get_screen_active(display), "You're invited"));
    assert(!find_gif(lv_display_get_screen_active(display)));
    assert_idle();
    model.after_dark_unlocked = false;
    badge::ui_update(model);
    badge::ui_page(-1); spin();

    // A normal press-locked page contact may end over the separate chrome
    // layer: it pages once on release and cannot affect the next plain tap.
    badge::ui_page(-1); spin();
    touch(100, 390, true);
    touch(250, 390, true);
    touch(434, 200, true);
    touch(434, 270, true);
    touch(434, 270, false);
    assert(badge::ui_page_index() == 4);
    tap(234, 80);
    assert(badge::ui_page_index() == 4);
    tap(434, 233); // A fresh arrow contact still completes normally.
    assert(badge::ui_page_index() == 0);

    // Framework cancellation sends PRESS_LOST instead of RELEASED. An already
    // recognized gesture must be discarded even if release lands on chrome.
    touch(100, 390, true);
    touch(250, 390, true);
    lv_indev_wait_release(input);
    touch(434, 233, true);
    touch(434, 233, false);
    assert(badge::ui_page_index() == 0);
    tap(234, 80);
    assert(badge::ui_page_index() == 0);
    tap(434, 233);
    assert(badge::ui_page_index() == 1);

    // A clock reveal preserves the six dots and the reader's agenda scroll.
    auto* locked_title = find_label(lv_display_get_screen_active(display), badge_schedule::Items[0].title);
    auto* locked_schedule = lv_obj_get_parent(lv_obj_get_parent(locked_title));
    swipe(234, 335, 234, 170);
    const int locked_scroll = lv_obj_get_scroll_y(locked_schedule);
    assert(locked_scroll > 0);
    model.after_dark_unlocked = true;
    badge::ui_update(model); spin();
    assert(badge::ui_page_index() == 1 && badge::ui_page_count() == 5);
    assert(visible_dots(lv_display_get_layer_top(display)) == 5);
    assert(locked_title == find_label(lv_display_get_screen_active(display), badge_schedule::Items[0].title));
    assert(lv_obj_get_scroll_y(locked_schedule) == locked_scroll);
    badge::ui_open_after_dark(); spin();
    assert(badge::ui_page_index() == 2);
    assert(find_image(lv_display_get_screen_active(display), &badge::ui::invitation_art));
    assert(lv_obj_is_visible(find_image(lv_display_get_screen_active(display), &badge::ui::invitation_art)));
    assert(!find_label(lv_display_get_screen_active(display), "After Dark"));
    assert(!find_gif(lv_display_get_screen_active(display))); // Timed reveals do not fake a code celebration.
    snapshot("after-dark-unlocked");
    badge::ui_page(-2); spin();

    // A reveal during setup or Touch test cannot replace the modal. Stable IDs
    // preserve Settings returns when the invitation changes state.
    model.after_dark_unlocked = false;
    badge::ui_update(model);
    badge::ui_page(-1); spin();
    badge::ui_show_setup("init-test-badge", "example1234"); spin();
    model.after_dark_unlocked = true;
    badge::ui_update(model);
    badge::ui_open_after_dark(); spin();
    assert(badge::ui_setup_active() && badge::ui_page_index() == 4);
    badge::ui_close_setup(true); spin();
    assert(badge::ui_page_index() == 4);
    badge::ui_show_touch_test();
    badge::ui_open_after_dark(); spin();
    assert(badge::ui_touch_test_active());
    badge::ui_close_touch_test();
    badge::ui_page(1); spin();

    for (int page = 0; page < 5; ++page) {
        assert(badge::ui_page_index() == page);
        if (page) {
            const char* titles[] = {"", "Schedule", "Party", "Badge", "Settings"};
            auto* heading = find_label(lv_screen_active(), titles[page]);
            assert(heading && lv_obj_is_visible(heading));
            lv_area_t bounds; lv_obj_get_coords(heading, &bounds);
            if (bounds.x1 != 84 || bounds.y1 != 52 || bounds.x2 != 383)
                std::fprintf(stderr, "Heading %s bounds %d,%d..%d,%d\n", titles[page],bounds.x1,bounds.y1,bounds.x2,bounds.y2);
            assert(bounds.x1 == 84 && bounds.y1 == 52 && bounds.x2 == 383);
            assert(lv_obj_get_style_text_align(heading,LV_PART_MAIN)==LV_TEXT_ALIGN_CENTER);
        }
        assert(!find_label(lv_screen_active(), "Learn how"));
        snapshot("page" + std::to_string(page));
        assert_chrome(page == 0);
        if (page != 0) assert_idle();
        if (page < 4) {
            tap(434, 233); // Native arrow hit test, release and page lifecycle.
            assert(badge::ui_page_index() == page + 1);
        }
    }

    // Enlarged arrow strips reach the content edges (x=72/396, y=100..403) but
    // stop before page content, the heading and the dots.
    tap(70, 104); assert(badge::ui_page_index() == 3);
    tap(398, 400); assert(badge::ui_page_index() == 4);
    tap(70, 96); assert(badge::ui_page_index() == 4);   // Above the strip.
    tap(70, 410); assert(badge::ui_page_index() == 4);  // Below the strip.

    auto* first_setting = action_label(lv_screen_active(), "Brightness");
    assert(first_setting);
    lv_area_t first_setting_bounds;
    lv_obj_get_coords(lv_obj_get_parent(first_setting), &first_setting_bounds);
    auto* battery = find_label(lv_screen_active(), "Battery 74%");
    assert(battery);
    lv_area_t battery_bounds; lv_obj_get_coords(battery, &battery_bounds);
    assert(battery_bounds.y1 == 100 && first_setting_bounds.y1 > battery_bounds.y2);
    assert(first_setting_bounds.x1 == 98 && first_setting_bounds.x2 == 369);
    tap_text("Brightness");
    snapshot("settings-brightness");
    auto* increase = active_label(lv_screen_active(), "Increase"); assert(increase);
    lv_area_t inc; lv_obj_get_coords(increase, &inc);
    int ix = (inc.x1 + inc.x2) / 2, iy = (inc.y1 + inc.y2) / 2;
    touch(ix, iy, true); touch(ix + 30, iy, true); touch(ix, iy, false);
    assert(brightness == 0);
    touch(ix, iy, true, 40); touch(ix, iy, false);
    assert(brightness == 0);
    tap_text("Increase"); assert(brightness == 70);
    menu_back(); tap_text("Orientation"); tap_text("180°"); assert(orientation == 2);
    tap(434, 233); assert(badge::ui_page_index() == 4); // Page arrows are hidden in submenus.
    snapshot("settings-orientation");
    menu_back();
    // The clock is set only through phone setup; Settings has no manual editor.
    assert(!action_label(lv_screen_active(), "Date / time"));
    assert(!widget(lv_screen_active(), &lv_calendar_class) && !widget(lv_screen_active(), &lv_roller_class));
    tap_text("Calibrate touch"); // A top-level row that opens the wizard directly.
    assert(calibration_requests == 1 && badge::ui_page_index() == 4);
    tap_text("Hack this device");
    spin();
    assert(lit_pixels(120, 132, 348, 360) > 10000);
    const auto menu_frame = pixels;
    lv_obj_invalidate(lv_screen_active()); spin();
    assert(pixels == menu_frame);
    snapshot("settings-hack"); menu_back();
    tap_text("Reset");
    assert(badge::ui_reset_active() && resets == 0);
    auto* reset_message = find_label(lv_display_get_screen_active(display), "Clears your profile, saved sessions and invitation unlock.\n\nRestores 60% brightness and Default orientation. The clock stays set.");
    assert(reset_message);
    lv_area_t reset_message_area;
    lv_obj_get_coords(reset_message, &reset_message_area);
    assert(reset_message_area.y2 < 294); // Explanation stays above confirmation.
    snapshot("reset-confirmation");
    badge::ui_page(1); badge::ui_open_after_dark(); spin();
    swipe(340, 245, 120, 245);
    assert(badge::ui_reset_active() && badge::ui_page_index() == 4);
    touch(234, 316, true); touch(234, 240, true); touch(234, 316, true); touch(234, 316, false);
    touch(234, 316, true, 40); touch(234, 316, false);
    assert(resets == 0);
    tap(234, 374);
    assert(!badge::ui_reset_active() && resets == 0);
    tap_text("Reset");
    touch(234, 316, true);
    assert(resets == 0); // Press alone cannot erase anything.
    touch(234, 316, false);
    assert(resets == 1 && badge::ui_reset_active());
    badge::ui_button(true, 1); badge::ui_button(false, 1); spin();
    tap(234, 316);
    assert(badge::ui_reset_active() && resets == 1 && setup == 0);
    model.reset_state = badge::ResetState::Failed;
    badge::ui_update(model); spin();
    assert(find_label(lv_display_get_screen_active(display), "Retry"));
    snapshot("reset-failed");
    tap(234, 316);
    assert(resets == 2);
    model.reset_state = badge::ResetState::SettingsFailed;
    badge::ui_update(model); spin();
    assert(find_label(lv_display_get_screen_active(display), "Your profile was cleared. Some settings may have changed.\n\nTap Retry to finish the reset."));
    snapshot("reset-settings-failed");
    tap(234, 316);
    assert(resets == 3);
    model.reset_state = badge::ResetState::Complete;
    model.after_dark_unlocked = false;
    badge::ui_update(model); spin();
    reset_message = find_label(lv_display_get_screen_active(display), "Your profile, saved sessions and invitation unlock are cleared.\n\nBrightness is 60%. You can configure a new badge.");
    assert(reset_message);
    lv_obj_get_coords(reset_message, &reset_message_area);
    assert(reset_message_area.y2 < 352); // Completion stays above Done.
    snapshot("reset-complete");
    tap(234, 374);
    assert(!badge::ui_reset_active() && badge::ui_page_index() == 3);
    badge::ui_page(-1); spin();
    assert(badge::ui_page_index() == 2);
    assert(find_label(lv_display_get_screen_active(display), "tap the code to reveal a secret invitation"));
    assert(lit_pixels(96, 88, 372, 165) == 0); // Reset cannot leave the revealed title behind.
    badge::ui_page(2); spin();
    // Reopening is always a new confirmation even after a successful reset.
    tap_text("Reset");
    assert(find_label(lv_display_get_screen_active(display), "Reset badge?"));
    badge::ui_button(false, -1); spin();
    assert(!badge::ui_reset_active() && badge::ui_page_index() == 4 && resets == 3);

    tap_text("Orientation"); tap_text("Touch test");
    assert(badge::ui_touch_test_active());
    badge::ui_touch_sample(234, 234, 234, 234, true, 0);
    snapshot("touch");
    assert(badge::ui_touch_state().x == 234);
    tap(234, 417);
    assert(!badge::ui_touch_test_active() && badge::ui_page_index() == 4);
    tap_text("Connect phone"); tap_text("Connect phone");
    assert(badge::ui_setup_active() && setup == 1);
    snapshot("setup");
    tap(234, 414);
    assert(!badge::ui_setup_active() && badge::ui_page_index() == 4);
    assert_chrome();

    // Opening the real agenda focuses the active session once. Past sessions
    // dim, upcoming sessions stay readable, and full titles wrap.
    model.clock_text = "9:45 AM";
    model.schedule_minute = 9 * 60 + 45;
    model.schedule_current = badge_schedule::current(model.schedule_minute);
    badge::ui_update(model);
    badge::ui_page(-3);
    spin();
    auto* first_title = find_label(lv_display_get_screen_active(display), badge_schedule::Items[0].title);
    assert(first_title);
    auto* schedule = lv_obj_get_parent(lv_obj_get_parent(first_title));
    assert(lv_obj_get_child_count(schedule) == badge_schedule::Items.size());
    std::array<int, badge_schedule::Items.size()> heights{};
    for (size_t i = 0; i < badge_schedule::Items.size(); ++i) {
        auto* row = lv_obj_get_child(schedule, i);
        const auto& item = badge_schedule::Items[i];
        auto* title = find_label(row, item.title);
        assert(title && find_label(row, item.time));
        assert(lv_obj_get_style_text_font(title, LV_PART_MAIN) == &font_sans_24);
        assert(lv_obj_get_style_text_font(find_label(row, item.time), LV_PART_MAIN) == &font_mono_18);
        assert(lv_label_get_long_mode(title) == LV_LABEL_LONG_WRAP);
        assert_inside(title, row);
        if (item.detail[0]) assert(!find_label(row, item.detail));
        heights[i] = lv_obj_get_height(row);
    }
    auto* long_title = find_label(schedule, "Networking break and sponsors");
    assert(lv_obj_get_height(long_title) > font_sans_20.line_height);
    auto* past = lv_obj_get_child(schedule, 0);
    auto* current = lv_obj_get_child(schedule, 1);
    auto* upcoming = lv_obj_get_child(schedule, 2);
    assert(lv_obj_get_style_opa(past, LV_PART_MAIN) == LV_OPA_50);
    assert(lv_obj_get_style_opa(current, LV_PART_MAIN) == LV_OPA_COVER);
    assert(lv_obj_get_style_border_opa(current, LV_PART_MAIN) == LV_OPA_COVER);
    assert(find_label(current, "On now"));
    assert(lv_obj_get_style_border_opa(upcoming, LV_PART_MAIN) == LV_OPA_TRANSP);
    assert(lv_obj_get_style_opa(upcoming, LV_PART_MAIN) == LV_OPA_COVER);
    assert_inside(current, schedule);
    assert_schedule_center(current);
    snapshot("schedule-current");
    lv_area_t current_area;
    lv_obj_get_coords(current, &current_area);
    // Neutral RGB565 levels: R=3/31, G=6/63, B=3/31 render as 24/24/24.
    assert(pixels[(current_area.y1 + 20) * Width + current_area.x1 + 4] == 0x18c3);
    // The selected outline follows the stepped silhouette. A conventional
    // inset rectangular border used to paint into all four black cutouts.
    for (int y = 0; y < 10; ++y) for (int x = 0; x < 10; ++x) {
        const uint16_t expected = x < 8 && y < 8 ? 0 : 0xffff;
        for (int px : {current_area.x1 + x, current_area.x2 - x})
            for (int py : {current_area.y1 + y, current_area.y2 - y})
                assert(pixels[py * Width + px] == expected);
    }
    assert(!find_image(schedule, &badge::ui::supplied_bookmark_outline));
    assert(!find_image(schedule, &badge::ui::supplied_bookmark));
    assert(!find_label(lv_display_get_screen_active(display), "All times local"));
    assert(!find_label(lv_display_get_screen_active(display), "Preview / event details pending"));
    bookmarked = -1;
    tap(340, current_area.y1 + 26);
    assert(bookmarked == -1); // Schedule cards have no bookmark action.
    lv_obj_scroll_to_view(current, LV_ANIM_OFF); spin();
    assert_idle();
    const int entry_scroll = lv_obj_get_scroll_y(schedule);
    swipe(234, 335, 234, 170);
    assert(lv_obj_get_scroll_y(schedule) > entry_scroll && badge::ui_page_index() == 1);
    assert(centered_schedule_row(schedule) > 1);
    assert(bookmarked == -1);
    const int user_scroll = lv_obj_get_scroll_y(schedule);
    snapshot("schedule-scrolled");

    // A new session updates existing widgets without moving the attendee's
    // chosen scroll position or changing row heights.
    model.clock_text = "11:02 AM";
    model.schedule_minute = 11 * 60 + 2;
    model.schedule_current = badge_schedule::current(model.schedule_minute);
    badge::ui_update(model);
    spin();
    assert(lv_obj_get_scroll_y(schedule) == user_scroll);
    centered_schedule_row(schedule);
    assert(lv_obj_get_style_opa(current, LV_PART_MAIN) == LV_OPA_50);
    assert(find_label(upcoming, "On now"));
    for (size_t i = 0; i < heights.size(); ++i) assert(lv_obj_get_height(lv_obj_get_child(schedule, i)) == heights[i]);
    snapshot("schedule-passed");
    assert_idle();
    const auto settled_flush_count = flush_count;
    ++model.schedule_minute; // Same session: no schedule properties need redraw.
    badge::ui_update(model);
    spin();
    assert(flush_count == settled_flush_count);

    model.clock_text = "--:--";
    model.clock_valid = false;
    model.schedule_minute = -1;
    model.schedule_current = -1;
    badge::ui_update(model);
    spin();
    assert(!find_label(lv_display_get_screen_active(display), "Set time in Settings"));
    assert(!find_label(schedule, "On now"));
    for (size_t i = 0; i < heights.size(); ++i) {
        auto* row = lv_obj_get_child(schedule, i);
        assert(lv_obj_get_style_opa(row, LV_PART_MAIN) == LV_OPA_COVER);
        assert(lv_obj_get_style_border_opa(row, LV_PART_MAIN) == LV_OPA_TRANSP);
    }
    assert(lv_obj_get_scroll_y(schedule) == user_scroll);
    snapshot("schedule-invalid-time");
    model.clock_valid = true;
    model.clock_text = "11:59 PM";
    model.schedule_minute = 23 * 60 + 59;
    model.schedule_current = badge_schedule::current(model.schedule_minute);
    badge::ui_update(model);
    spin();
    assert(find_label(lv_obj_get_child(schedule, 8), "On now"));
    assert(!find_label(lv_obj_get_child(schedule, 8), "End time not listed"));
    badge::ui_page(1); spin();
    badge::ui_page(-1); spin(); // Re-entry must make the late-day current row visible.
    auto* last_title = find_label(lv_display_get_screen_active(display), badge_schedule::Items[8].title);
    schedule = lv_obj_get_parent(lv_obj_get_parent(last_title));
    assert_inside(lv_obj_get_child(schedule, 8), schedule);
    assert_schedule_center(lv_obj_get_child(schedule, 8));
    const int last_entry_scroll = lv_obj_get_scroll_y(schedule);
    assert_chrome();
    assert_complete_schedule_frame(display);
    snapshot("schedule-last-session");
    model.clock_text = "12:00 AM";
    model.schedule_minute = 0; // The published agenda repeats on the next local day.
    model.schedule_current = -1;
    badge::ui_update(model);
    spin();
    assert(!find_label(schedule, "On now"));
    for (size_t i = 0; i < heights.size(); ++i) assert(lv_obj_get_style_opa(lv_obj_get_child(schedule, i), LV_PART_MAIN) == LV_OPA_COVER);
    assert(lv_obj_get_scroll_y(schedule) == last_entry_scroll);
    snapshot("schedule-midnight");
    assert_chrome();
    assert_idle();

    // Entry before the first session or without a clock centers the first
    // card, rather than leaving it at the clipping edge. Reopening while a
    // session is current centers that row for every distinct wrapped height.
    const auto reopen_schedule = [&]() {
        badge::ui_page(1); spin();
        badge::ui_page(-1); spin();
        auto* title = find_label(lv_display_get_screen_active(display), badge_schedule::Items[0].title);
        assert(title && badge::ui_page_index() == 1);
        return lv_obj_get_parent(lv_obj_get_parent(title));
    };
    schedule = reopen_schedule();
    assert_schedule_center(lv_obj_get_child(schedule, 0));
    snapshot("schedule-before-first-session");
    model.clock_valid = false;
    model.schedule_minute = -1;
    model.schedule_current = -1;
    badge::ui_update(model); spin();
    schedule = reopen_schedule();
    assert_schedule_center(lv_obj_get_child(schedule, 0));
    assert(!find_label(lv_display_get_screen_active(display), "Set time in Settings"));
    snapshot("schedule-invalid-entry");
    for (size_t i = 0; i < badge_schedule::Items.size(); ++i) {
        model.clock_valid = true;
        model.schedule_minute = badge_schedule::Items[i].minute;
        model.schedule_current = int(i);
        badge::ui_update(model); spin();
        schedule = reopen_schedule();
        auto* row = lv_obj_get_child(schedule, i);
        assert(find_label(row, "On now"));
        assert_schedule_center(row);
        assert_complete_schedule_frame(display);
    }
    assert_idle();

    // Real native pointer drags settle on a centered row in both directions,
    // including the first/last boundary. Browsing does not toggle bookmarks
    // or accidentally become horizontal page navigation.
    bookmarked = -1;
    unsigned focused = centered_schedule_row(schedule);
    assert(focused == badge_schedule::Items.size() - 1);
    for (size_t step = 0; focused && step < badge_schedule::Items.size(); ++step) {
        swipe(234, 170, 234, 335);
        const unsigned next = centered_schedule_row(schedule);
        assert(next < focused && badge::ui_page_index() == 1 && bookmarked == -1);
        focused = next;
    }
    assert(focused == 0);
    swipe(234, 170, 234, 335);
    assert(centered_schedule_row(schedule) == 0);
    assert(badge::ui_page_index() == 1 && bookmarked == -1);
    assert_complete_schedule_frame(display);
    snapshot("schedule-first-focused");
    assert_idle();
    for (size_t step = 0; focused + 1 < badge_schedule::Items.size() && step < badge_schedule::Items.size(); ++step) {
        swipe(234, 335, 234, 170);
        const unsigned next = centered_schedule_row(schedule);
        assert(next > focused && badge::ui_page_index() == 1 && bookmarked == -1);
        focused = next;
    }
    assert(focused == badge_schedule::Items.size() - 1);
    swipe(234, 335, 234, 170);
    assert(centered_schedule_row(schedule) == badge_schedule::Items.size() - 1);
    assert(badge::ui_page_index() == 1 && bookmarked == -1);
    assert_complete_schedule_frame(display);
    snapshot("schedule-last-focused");
    assert_idle();

    badge::ui_page(2);
    spin();
    const int network_before = network;
    swipe(234, 338, 234, 160);
    // A badge without an account shows one card, so a swipe cannot switch networks.
    assert(network == network_before && badge::ui_page_index() == 3);
    // Populate each social independently; the current card must remain selected.
    model.name = "Conference attendee";
    model.company = "WorkOS";
    model.socials[0] = "https://github.com/octocat";
    model.socials[1] = "https://x.com/example";
    model.selected_network = 0;
    model.profile_revision = 1;
    std::vector<uint16_t> avatar(160 * 160, 0xf800);
    model.avatar = avatar.data();
    model.avatar_width = model.avatar_height = 160;
    badge::ui_update(model);
    spin();
    snapshot("profile");
    assert_chrome(false, true);
    auto visible_label = [&](const char* text) {
        auto* found = find_label(lv_display_get_screen_active(display), text);
        return found && lv_obj_is_visible(found);
    };
    // Configured: the title is hidden but keeps its space; larger rounded photo.
    assert(!visible_label("Badge") && visible_label("Conference attendee") && visible_label("WorkOS"));
    assert(lit_pixels(130, 100, 338, 308) > 40000);     // 208px photo square at the content top.
    assert(lit_pixels(84, 52, 384, 96) == 0);           // Title space stays empty until a QR shows.
    assert(lit_pixels(130, 100, 138, 108) == 0);        // Square eight-pixel corner step,
    assert(lit_pixels(138, 100, 146, 108) == 64);       // like the Schedule cards.
    assert(lv_obj_get_style_text_font(find_label(lv_screen_active(), "Conference attendee"), LV_PART_MAIN) == &font_mono_32);
    assert_idle();
    tap(234, 180); // The QR replaces the photo in the same square.
    assert(visible_label("GitHub") && !visible_label("Badge"));
    assert(visible_label("Conference attendee"));
    assert(lit_pixels(130, 100, 338, 308) > 20000 && lit_pixels(130, 100, 338, 308) < 40000);
    snapshot("profile-qr");
    assert_chrome(false, true);
    tap(234, 180);
    assert(!visible_label("GitHub") && lit_pixels(130, 100, 338, 308) > 40000);
    snapshot("profile-restored");
    assert_chrome(false, true);
    assert_idle();

    // Unconfigured networks have no card, even when selected programmatically.
    model.selected_network = 2;
    badge::ui_update(model);
    spin();
    tap(234, 180);
    assert(!find_label(lv_display_get_screen_active(display), "No account yet"));
    assert(visible_label("X / Twitter"));
    tap(234, 180);
    assert(badge::ui_page_index() == 3 && !badge::ui_setup_active());
    assert_chrome(false, true);

    // A profile with only a social URL must still expose that QR, and a
    // profile change must not leave a code showing for stale profile data.
    model.name.clear(); model.company.clear(); model.avatar = nullptr;
    model.avatar_width = model.avatar_height = 0;
    model.selected_network = 0; ++model.profile_revision;
    badge::ui_update(model); spin();
    tap(234, 180); spin();
    assert(visible_label("GitHub") && lit_pixels(130, 100, 338, 308) > 20000);
    model.company = "Example company"; ++model.profile_revision;
    badge::ui_update(model); spin();
    assert(!visible_label("GitHub") && visible_label("Example company"));

    // Calibration is modal, freezes paging, uses raw physical samples elsewhere,
    // and physical exit cancels without changing preferences.
    model.calibration_target_visible = true;
    model.calibration_x = 234; model.calibration_y = 63;
    model.calibration_title = "Calibrate 1 / 9";
    model.calibration_message = "Hold the target, then lift";
    badge::ui_update(model); badge::ui_show_calibration(); spin();
    assert(badge::ui_calibration_active() && badge::ui_touch_test_active());
    assert(find_label(lv_screen_active(), "Calibrate 1 / 9"));
    assert(lit_pixels(217,46,251,80)>30);
    snapshot("touch-calibration");
    badge::ui_page(1); assert(badge::ui_page_index()==4);
    badge::ui_button(false,1); spin();
    assert(!badge::ui_calibration_active() && !badge::ui_touch_test_active());
    mooncake::GetMooncake().uninstallAllApps();
    lv_indev_delete(input);
    lv_display_delete(display);
    lv_deinit();
    std::puts("PASS: native UI pages, partial rendering, idle efficiency, tap cancellation, touch Morse, daily schedule, scrolling, profiles and modal lifecycle");
}
