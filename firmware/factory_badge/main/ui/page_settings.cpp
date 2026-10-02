#include "widgets.h"
#include <algorithm>
#include <array>

namespace badge::ui {
class SettingsPage final : public PageView {
public:
    SettingsPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        menu_ = lv_menu_create(root_);
        lv_obj_set_pos(menu_, HeadingX, HeadingY);
        lv_obj_set_size(menu_, HeadingWidth, ContentBottom - HeadingY);
        lv_obj_set_style_bg_color(menu_, lv_color_black(), 0);
        lv_obj_set_style_text_color(menu_, white(), 0);
        lv_obj_set_style_text_font(menu_, &font_sans_20, 0);
        lv_obj_remove_flag(menu_, LV_OBJ_FLAG_GESTURE_BUBBLE);
        // Menu history reparents hidden pages. Invalidate its complete viewport
        // so partial display flushes repaint newly exposed QR/header content.
        lv_obj_add_event_cb(menu_, [](lv_event_t* e) {
            lv_obj_invalidate(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
            auto& self = *static_cast<SettingsPage*>(lv_event_get_user_data(e));
            if (lv_event_get_target(e) == self.menu_ && self.home_) self.page_changed();
        }, LV_EVENT_VALUE_CHANGED, this);
        auto* header = lv_menu_get_main_header(menu_);
        lv_obj_set_style_bg_color(header, lv_color_black(), 0);
        lv_obj_set_style_text_font(header, &font_sans_24, 0);
        lv_obj_set_layout(header, LV_LAYOUT_NONE);
        lv_obj_set_style_pad_all(header, 0, 0);
        lv_obj_set_height(header, ContentTop - HeadingY);
        auto* heading = lv_obj_get_child(header, 1);
        set_font(heading, &font_sans_24);
        lv_obj_set_pos(heading, 0, 0);
        lv_obj_set_width(heading, HeadingWidth);
        lv_obj_set_style_text_color(heading, cream(), 0);
        lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
        // Submenus return through the large bottom action instead of this
        // chevron; it stays as the native history target but is never shown.
        auto* back = back_ = lv_menu_get_main_header_back_button(menu_);
        lv_obj_set_size(back, 44, 36);
        lv_obj_set_pos(back, 0, 0);
        lv_obj_move_foreground(back);
        lv_obj_set_style_text_font(back, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(back, white(), 0);
        // A label follows font/style changes reliably across native menu history.
        lv_obj_clean(back);
        lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto* back_icon = lv_label_create(back);
        lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(back_icon, LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(back_icon, white(), 0);
        auto* home = home_ = page("Settings");
        battery_ = text(home, "", &font_mono_18); // Status directly under the title.
        lv_obj_set_style_text_color(battery_, muted(), 0);
        auto* light = page("Brightness");
        brightness_ = text(light, "", &font_sans_24);
        row(light, "Decrease", [this] { brightness(-10); });
        row(light, "Increase", [this] { brightness(10); });
        auto* rotation = page("Orientation");
        constexpr const char* modes[] = {"Free", "Default", "180°"};
        for (int i = 0; i < 3; ++i) orientation_[i] = row(rotation, modes[i], [this, i] {
            context_.model.orientation = static_cast<Orientation>(i);
            if (context_.callbacks.orientation) context_.callbacks.orientation(context_.model.orientation);
            update();
        });
        // Less common tools live one level down.
        auto* advanced = page("Advanced");
        row(advanced, "Calibrate touch", [this] { if (context_.callbacks.calibrate_touch) context_.callbacks.calibrate_touch(); });
        row(advanced, "Touch test", [] { ui_show_touch_test(); });
        auto* hack = page("Hack this device");
        auto* code = qr(hack, HackUrl, 0, 0, 216, 22);
        lv_obj_set_style_align(code, LV_ALIGN_CENTER, 0);
        text(hack, "workos.com/init/badge", &font_mono_12);
        auto* phone = page("Connect phone");
        auto* help = text(phone, "Connect to edit your badge and sync its clock.", &font_sans_16);
        lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
        row(phone, "Connect phone", [this] { request_setup(context_); });
        auto* first_row = row(home, "Brightness", [this, light] { lv_menu_set_page(menu_, light); });
        link(home, "Orientation", rotation);
        link(home, "Hack this device", hack);
        link(home, "Connect phone", phone);
        link(home, "Advanced", advanced);
        row(home, "Reset", [] { ui_show_reset(); });
        action_ = button(root_, "Done", HeadingX + 8, ActionTop, 272, ActionHeight, [this] { act(); });
        lv_obj_set_style_bg_color(action_, white(), 0);
        lv_obj_set_style_bg_color(action_, lv_color_hex(0xc8c8c8), LV_STATE_PRESSED);
        action_label_ = lv_obj_get_child(action_, 0);
        lv_obj_set_style_text_color(action_label_, lv_color_black(), 0);
        set_font(action_label_, &font_sans_20);
        lv_menu_set_page(menu_, home);
        // Align the action with the rendered rows, whose inset includes the
        // menu's own content padding.
        lv_obj_update_layout(menu_);
        lv_area_t row_bounds;
        lv_obj_get_coords(first_row, &row_bounds);
        lv_obj_set_x(action_, row_bounds.x1);
        page_changed();
        update();
    }
    ~SettingsPage() override { context_.settings_submenu = false; }
    void update() override {
        set_text(brightness_, std::to_string(context_.model.brightness_percent) + "%");
        set_text(battery_, battery_text(context_.model.battery_percent));
        for (int i = 0; i < 3; ++i) {
            lv_obj_set_style_border_color(orientation_[i], white(), 0);
            lv_obj_set_style_border_width(orientation_[i], i == int(context_.model.orientation) ? 1 : 0, 0);
        }
    }
private:
    static constexpr int ActionTop = 354, ActionHeight = 46, RowHeight = 52, RowGap = 9;
    void page_changed() {
        auto* current = lv_menu_get_cur_main_page(menu_);
        const bool submenu = current && current != home_;
        context_.settings_submenu = submenu;
        set_hidden(back_, true);
        set_hidden(action_, !submenu);
        // Submenu content ends above the fixed action so nothing scrolls under it.
        lv_obj_set_height(menu_, (submenu ? ActionTop - 8 : ContentBottom) - HeadingY);
    }
    void act() {
        lv_obj_send_event(back_, LV_EVENT_CLICKED, nullptr); // Native menu history.
    }
    lv_obj_t* page(const char* title) {
        auto* p = lv_menu_page_create(menu_, title);
        lv_obj_set_style_bg_color(p, lv_color_black(), 0);
        lv_obj_set_style_pad_hor(p, 8, 0);
        lv_obj_set_style_pad_ver(p, 0, 0);
        lv_obj_set_style_pad_row(p, RowGap, 0);
        lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
        return p;
    }
    lv_obj_t* text(lv_obj_t* parent, const std::string& value, const lv_font_t* font) {
        return label(parent, value.c_str(), 0, 0, 272, font, white());
    }
    lv_obj_t* row(lv_obj_t* parent, const char* title, std::function<void()> action) {
        // Square rows, larger than the 46px action, with clear separation;
        // longer pages scroll instead of shrinking targets.
        auto* b = button(parent, title, 0, 0, 272, RowHeight, std::move(action));
        set_font(lv_obj_get_child(b, 0), &font_sans_20);
        return b;
    }
    void link(lv_obj_t* parent, const char* title, lv_obj_t* target) {
        row(parent, title, [this, target] { lv_menu_set_page(menu_, target); });
    }
    void brightness(int delta) {
        context_.model.brightness_percent = std::clamp(context_.model.brightness_percent + delta, 10, 100);
        if (context_.callbacks.brightness) context_.callbacks.brightness(context_.model.brightness_percent);
        update();
    }
    lv_obj_t *home_{}, *back_{}, *action_{}, *action_label_{};
    lv_obj_t *menu_{}, *brightness_{}, *battery_{};
    std::array<lv_obj_t*, 3> orientation_{};
};
std::unique_ptr<PageView> make_settings(Context& c, lv_obj_t* p) { return std::make_unique<SettingsPage>(c, p); }
} // namespace badge::ui
