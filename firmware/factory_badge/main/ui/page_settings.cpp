#include "widgets.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <cstdio>

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
        }, LV_EVENT_VALUE_CHANGED, nullptr);
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
        auto* back = lv_menu_get_main_header_back_button(menu_);
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
        auto* home = page("Settings");
        lv_obj_set_style_pad_row(home, 4, 0);
        auto* light = page("Brightness");
        brightness_ = text(light, "", &font_sans_24);
        row(light, "Decrease", [this] { brightness(-10); });
        row(light, "Increase", [this] { brightness(10); });
        battery_ = text(light, "", &font_mono_12);
        auto* rotation = page("Orientation");
        constexpr const char* modes[] = {"Free", "Default", "180°"};
        for (int i = 0; i < 3; ++i) orientation_[i] = row(rotation, modes[i], [this, i] {
            context_.model.orientation = static_cast<Orientation>(i);
            if (context_.callbacks.orientation) context_.callbacks.orientation(context_.model.orientation);
            update();
        });
        row(rotation, "Calibrate touch", [this] { if (context_.callbacks.calibrate_touch) context_.callbacks.calibrate_touch(); });
        row(rotation, "Touch test", [] { ui_show_touch_test(); });
        clock_page_ = page("Date / time");
        date_ = text(clock_page_, "", &font_sans_16);
        auto* dates = page("Choose date");
        calendar_ = lv_calendar_create(dates);
        lv_obj_set_size(calendar_, 272, 250);
        lv_obj_set_style_text_font(calendar_, &font_sans_14, 0);
        auto* calendar_header = lv_calendar_add_header_dropdown(calendar_);
        for (int year = 2024; year <= 2099; ++year) years_ += (years_.empty() ? "" : "\n") + std::to_string(year);
        lv_calendar_header_dropdown_set_year_list(calendar_, years_.c_str());
        for (uint32_t i = 0; i < lv_obj_get_child_count(calendar_header); ++i)
            lv_obj_set_style_text_font(lv_obj_get_child(calendar_header, i), LV_FONT_DEFAULT, 0);
        lv_obj_add_event_cb(calendar_, [](lv_event_t* e) {
            auto& self = *static_cast<SettingsPage*>(lv_event_get_user_data(e));
            lv_calendar_date_t date;
            if (lv_calendar_get_pressed_date(self.calendar_, &date) == LV_RESULT_OK) {
                self.year_ = date.year; self.month_ = date.month; self.day_ = date.day;
                lv_calendar_set_today_date(self.calendar_, date.year, date.month, date.day);
                self.update_draft();
            }
        }, LV_EVENT_VALUE_CHANGED, this);
        auto* times = page("Choose time");
        auto* wheels = container(times, 0, 0, 272, 150);
        hour_ = roller(wheels, 0, 82, numbers(1, 12));
        minute_ = roller(wheels, 92, 82, numbers(0, 59));
        ampm_ = roller(wheels, 184, 80, "AM\nPM");
        for (auto* wheel : {hour_, minute_, ampm_}) lv_obj_add_event_cb(wheel, [](lv_event_t* e) {
            static_cast<SettingsPage*>(lv_event_get_user_data(e))->update_draft();
        }, LV_EVENT_VALUE_CHANGED, this);
        text(times, "Scroll to choose", &font_mono_12);
        auto* zones = page("UTC offset");
        zone_value_ = text(zones, "", &font_sans_24);
        row(zones, "Earlier (-15 min)", [this] { zone_ = std::max(-840, zone_ - 15); update_draft(); });
        row(zones, "Later (+15 min)", [this] { zone_ = std::min(840, zone_ + 15); update_draft(); });
        link(clock_page_, "Date", dates);
        link(clock_page_, "Time", times);
        link(clock_page_, "UTC offset", zones);
        row(clock_page_, "Save date / time", [this] { save_clock(); });
        row(clock_page_, "Sync from phone", [this] { request_setup(context_); });
        clock_result_ = text(clock_page_, "", &font_sans_14);
        auto* hack = page("Hack this device");
        auto* code = qr(hack, HackUrl, 0, 0, 232, 24);
        lv_obj_set_style_align(code, LV_ALIGN_CENTER, 0);
        text(hack, "workos.com/init/badge", &font_mono_12);
        auto* phone = page("Connect phone");
        auto* help = text(phone, "Connect to edit your badge and sync its clock.", &font_sans_16);
        lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
        row(phone, "Connect phone", [this] { request_setup(context_); });
        link(home, "Brightness", light);
        link(home, "Orientation", rotation);
        row(home, "Date / time", [this] { begin_clock(); lv_menu_set_page(menu_, clock_page_); });
        link(home, "Hack this device", hack);
        link(home, "Connect phone", phone);
        row(home, "Reset", [] { ui_show_reset(); });
        lv_menu_set_page(menu_, home);
        begin_clock();
        update();
    }
    void update() override {
        set_text(brightness_, std::to_string(context_.model.brightness_percent) + "%");
        set_text(battery_, battery_text(context_.model.battery_percent));
        for (int i = 0; i < 3; ++i) {
            lv_obj_set_style_border_color(orientation_[i], white(), 0);
            lv_obj_set_style_border_width(orientation_[i], i == int(context_.model.orientation) ? 1 : 0, 0);
        }
    }
private:
    lv_obj_t* page(const char* title) {
        auto* p = lv_menu_page_create(menu_, title);
        lv_obj_set_style_bg_color(p, lv_color_black(), 0);
        lv_obj_set_style_pad_hor(p, 8, 0);
        lv_obj_set_style_pad_ver(p, 0, 0);
        lv_obj_set_style_pad_row(p, 6, 0);
        lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
        return p;
    }
    lv_obj_t* text(lv_obj_t* parent, const std::string& value, const lv_font_t* font) {
        return label(parent, value.c_str(), 0, 0, 272, font, white());
    }
    lv_obj_t* row(lv_obj_t* parent, const char* title, std::function<void()> action) {
        auto* b = button(parent, title, 0, 0, 272, 44, std::move(action));
        lv_obj_set_style_bg_color(b, lv_color_hex(0x181818), 0);
        lv_obj_set_style_radius(b, 8, 0);
        return b;
    }
    void link(lv_obj_t* parent, const char* title, lv_obj_t* target) {
        row(parent, title, [this, target] { lv_menu_set_page(menu_, target); });
    }
    std::string numbers(int first, int last) {
        std::string out;
        for (int n = first; n <= last; ++n) { char value[8]; std::snprintf(value, sizeof(value), "%02d", n); if (!out.empty()) out += '\n'; out += value; }
        return out;
    }
    lv_obj_t* roller(lv_obj_t* parent, int x, int width, const std::string& values) {
        auto* r = lv_roller_create(parent);
        lv_roller_set_options(r, values.c_str(), LV_ROLLER_MODE_NORMAL);
        lv_obj_set_width(r, width); lv_obj_set_pos(r, x, 0);
        lv_obj_set_style_text_font(r, &font_mono_20, 0);
        lv_roller_set_visible_row_count(r, 3);
        return r;
    }
    void begin_clock() {
        zone_ = context_.model.utc_offset_minutes;
        time_t local = context_.model.clock_epoch + int64_t(zone_) * 60;
        tm fields{};
        if (context_.model.clock_valid && gmtime_r(&local, &fields)) {
            year_ = fields.tm_year + 1900; month_ = fields.tm_mon + 1; day_ = fields.tm_mday;
        } else { year_ = 2026; month_ = 10; day_ = 7; fields.tm_hour = 9; }
        lv_calendar_set_today_date(calendar_, year_, month_, day_);
        lv_calendar_set_month_shown(calendar_, year_, month_);
        lv_roller_set_selected(hour_, (fields.tm_hour + 11) % 12, LV_ANIM_OFF);
        lv_roller_set_selected(minute_, fields.tm_min, LV_ANIM_OFF);
        lv_roller_set_selected(ampm_, fields.tm_hour >= 12, LV_ANIM_OFF);
        set_text(clock_result_, ""); update_draft();
    }
    void update_draft() {
        int hour = (lv_roller_get_selected(hour_) + 1) % 12 + 12 * lv_roller_get_selected(ampm_);
        char value[64];
        std::snprintf(value, sizeof(value), "%04d-%02d-%02d  %d:%02d %s", year_, month_, day_, hour % 12 ? hour % 12 : 12, int(lv_roller_get_selected(minute_)), hour >= 12 ? "PM" : "AM");
        set_text(date_, value);
        std::snprintf(value, sizeof(value), "UTC%c%02d:%02d", zone_ < 0 ? '-' : '+', std::abs(zone_) / 60, std::abs(zone_) % 60);
        set_text(zone_value_, value);
        if (clock_result_) set_text(clock_result_, "Changes apply on Save");
    }
    void save_clock() {
        // Gregorian civil date to Unix days, independent of the host timezone.
        int y = year_ - (month_ <= 2);
        int era = y / 400;
        unsigned yo = unsigned(y - era * 400);
        unsigned doy = (153 * unsigned(month_ + (month_ > 2 ? -3 : 9)) + 2) / 5 + day_ - 1;
        unsigned doe = yo * 365 + yo / 4 - yo / 100 + doy;
        int64_t days = int64_t(era) * 146097 + doe - 719468;
        int hour = (lv_roller_get_selected(hour_) + 1) % 12 + 12 * lv_roller_get_selected(ampm_);
        int64_t epoch = days * 86400 + hour * 3600 + lv_roller_get_selected(minute_) * 60 - zone_ * 60;
        if (epoch < 1704067200LL || epoch > 4102444800LL) { set_text(clock_result_, "Choose a date in 2024-2099"); return; }
        bool ok = context_.callbacks.set_clock && context_.callbacks.set_clock(epoch, zone_);
        set_text(clock_result_, ok ? "Date / time saved" : "Could not save. Try again.");
        lv_obj_scroll_to_view(clock_result_, LV_ANIM_OFF);
    }
    void brightness(int delta) {
        context_.model.brightness_percent = std::clamp(context_.model.brightness_percent + delta, 10, 100);
        if (context_.callbacks.brightness) context_.callbacks.brightness(context_.model.brightness_percent);
        update();
    }
    lv_obj_t *menu_{}, *brightness_{}, *battery_{}, *clock_page_{}, *date_{}, *calendar_{},
             *hour_{}, *minute_{}, *ampm_{}, *zone_value_{}, *clock_result_{};
    std::array<lv_obj_t*, 3> orientation_{};
    std::string years_;
    int year_ = 2026, month_ = 10, day_ = 7, zone_ = 0;
};
std::unique_ptr<PageView> make_settings(Context& c, lv_obj_t* p) { return std::make_unique<SettingsPage>(c, p); }
} // namespace badge::ui
