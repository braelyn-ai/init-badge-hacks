#include "widgets.h"
namespace badge::ui {
class CalibrationView final : public PageView {
public:
    CalibrationView(Context& c, lv_obj_t* p) : PageView(c,p) {
        title_ = label(root_, "", 64, 162, 340, &font_sans_20);
        message_ = label(root_, "", 54, 274, 360, &font_sans_14, muted());
        label(root_, "Either side button exits", 64, 306, 340, &font_sans_14, muted());
        target_ = container(root_,0,0,30,30);
        lv_obj_set_style_border_width(target_,3,0);
        lv_obj_set_style_border_color(target_,lv_color_hex(0xFF7A00),0);
        lv_obj_set_style_radius(target_,LV_RADIUS_CIRCLE,0);
        auto* pip=container(target_,11,11,2,2);
        lv_obj_set_style_bg_color(pip,white(),0); lv_obj_set_style_bg_opa(pip,LV_OPA_COVER,0);
        update();
    }
    void update() override {
        const auto& m=context_.model;
        set_text(title_,m.calibration_title); set_text(message_,m.calibration_message);
        set_hidden(target_,!m.calibration_target_visible);
        lv_obj_set_pos(target_,m.calibration_x-15,m.calibration_y-15);
    }
private: lv_obj_t *title_{}, *message_{}, *target_{};
};
std::unique_ptr<PageView> make_calibration(Context& c, lv_obj_t* p){return std::make_unique<CalibrationView>(c,p);}
}
