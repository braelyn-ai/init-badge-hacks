#include "widgets.h"

namespace badge::ui {
static void line(lv_obj_t* parent, int x, int y, int width, int height, lv_color_t color) {
    auto* object = container(parent, x, y, width, height);
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
}
class TouchView final : public PageView {
public:
    TouchView(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        label(root_, "Touch test", 64, 42, 340, &font_sans_24);
        label(root_, context.model.touch_calibrated ? "Calibrated / LVGL coordinates" : "Factory sensor / LVGL coordinates", 54, 76, 360, &font_sans_14, muted());
        constexpr int points[][2] = {{234, 120}, {234, 234}, {234, 362}, {112, 234}, {356, 234}};
        for (const auto& point : points) {
            auto* ring = container(root_, point[0] - 14, point[1] - 14, 28, 28);
            lv_obj_set_style_border_color(ring, white(), 0);
            lv_obj_set_style_border_width(ring, 1, 0);
            lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
            line(root_, point[0] - 22, point[1], 45, 1, white());
            line(root_, point[0], point[1] - 22, 1, 45, white());
        }
        label(root_, "White target / purple touch", 54, 167, 360, &font_sans_14, muted());
        pose_ = label(root_, "", 64, 190, 340, &font_sans_14, muted());
        coordinates_ = label(root_, "Your touch appears in purple", 54, 280, 360, &font_mono_18, accent());
        raw_ = label(root_, "", 54, 308, 360, &font_sans_14, muted());
        button(root_, "Back", 164, 399, 140, 37, [] { ui_close_touch_test(); });
        marker_ = container(root_, 0, 0, 18, 18);
        lv_obj_set_style_border_color(marker_, accent(), 0);
        lv_obj_set_style_border_width(marker_, 3, 0);
        lv_obj_set_style_radius(marker_, LV_RADIUS_CIRCLE, 0);
        auto* center = container(marker_, 6, 6, 6, 6);
        lv_obj_set_style_bg_color(center, accent(), 0);
        lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
        update();
    }
    void update() override {
        const auto& touch = context_.touch;
        set_hidden(marker_, !touch.sample);
        set_text(pose_, (context_.rotation == 0 ? "Default" : context_.rotation == 2 ? "180 degrees" : "Sideways") + std::string(" / held during test"));
        if (!touch.sample) return;
        set_text(coordinates_, std::string(touch.pressed ? "Touch " : "Last touch ") + std::to_string(touch.x) + ", " + std::to_string(touch.y));
        set_text(raw_, touch.sensor ? "Sensor " + std::to_string(touch.raw_x) + ", " + std::to_string(touch.raw_y) : "Simulated input");
        lv_obj_set_pos(marker_, touch.x - 9, touch.y - 9);
    }
private:
    lv_obj_t *coordinates_ = nullptr, *raw_ = nullptr, *pose_ = nullptr, *marker_ = nullptr;
};
std::unique_ptr<PageView> make_touch_test(Context& c, lv_obj_t* p) { return std::make_unique<TouchView>(c, p); }
} // namespace badge::ui
