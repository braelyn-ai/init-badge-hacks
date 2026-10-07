#pragma once
#include "../badge_ui.h"
#include <memory>
#include <utility>

namespace badge::ui {

constexpr int Width = 468;
constexpr int Height = 466;
constexpr int HeadingX = 84, HeadingY = 52, HeadingWidth = 300;
constexpr int ContentTop = 100, ContentBottom = 404;
constexpr int PageCount = 13;
constexpr int AfterDarkPage = 2;
constexpr int SettingsPageIndex = 4;
// Hack pages follow Settings so the stock page IDs stay stable. They are
// full-screen: no brand mark, arrows or dots; swipes and pushers still page.
constexpr int FirstHackPage = 5;
inline bool immersive_page(int page) { return page >= FirstHackPage; }
inline bool page_visible(int page, const UiModel&) {
    return page >= 0 && page < PageCount;
}
inline int visible_page_count(const UiModel&) {
    return PageCount;
}
inline constexpr const char* PageNames[] = {"init()", "Schedule", "Party", "Badge", "Settings", "Eyes", "Radar", "HAL", "Third Eye", "Matrix", "Bit", "Labyrinth", "Umbrella"};
inline constexpr const char* HackUrl = "https://workos.com/init/badge";

struct Context {
    UiModel model;
    UiCallbacks callbacks;
    int page = 0;
    int setup_origin = 0;
    bool setup = false;
    bool touch_test = false;
    bool calibration = false;
    bool reset = false;
    bool reset_confirmed = false;
    bool settings_submenu = false; // Hides page arrows/dots behind a Settings submenu.
    bool rebuild = false;
    uint8_t rotation = 2;
    UiTouchSample touch;
    UiMotion motion;
    std::string ssid, password, ip, setup_status;
};

class PageView {
public:
    PageView(Context& context, lv_obj_t* parent);
    virtual ~PageView();
    virtual void update() {}
    virtual void cancel_input() {}
    PageView(const PageView&) = delete;
    PageView& operator=(const PageView&) = delete;
protected:
    Context& context_;
    lv_obj_t* root_;
};

std::unique_ptr<PageView> make_init(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_schedule(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_after_dark(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_badge(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_settings(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_eyes(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_radar(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_hal(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_third_eye(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_matrix(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_bit(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_labyrinth(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_umbrella(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_setup(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_calibration(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_touch_test(Context&, lv_obj_t*);
std::unique_ptr<PageView> make_reset(Context&, lv_obj_t*);

} // namespace badge::ui
