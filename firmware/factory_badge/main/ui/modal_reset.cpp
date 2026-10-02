#include "widgets.h"

namespace badge::ui {
class ResetView final : public PageView {
public:
    ResetView(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        brand(root_, 24);
        heading_ = label(root_, "Reset badge?", 64, 80, 340, &font_sans_24);
        message_ = label(root_, "", 84, 135, 300, &font_sans_16);
        lv_label_set_long_mode(message_, LV_LABEL_LONG_WRAP);
        confirm_ = button(root_, "Reset badge", 124, 294, 220, 44, [this] {
            if (context_.reset_confirmed && context_.model.reset_state == ResetState::Working) return;
            context_.reset_confirmed = true;
            context_.model.reset_state = ResetState::Working;
            if (context_.callbacks.reset_badge) context_.callbacks.reset_badge();
            update();
        });
        back_ = button(root_, "Cancel", 164, 352, 140, 44, [] { ui_close_reset(); });
        update();
    }
    void update() override {
        const auto state = context_.reset_confirmed ? context_.model.reset_state : ResetState::Ready;
        const bool working = state == ResetState::Working;
        const bool complete = state == ResetState::Complete;
        set_text(heading_, complete ? "Badge reset" : working ? "Resetting badge" : "Reset badge?");
        const char* message = "Clears your profile, saved sessions and invitation unlock.\n\nRestores 70% brightness and Default orientation. The clock stays set.";
        if (working) message = "Clearing your badge and saving its default settings...";
        else if (state == ResetState::Failed) message = "Your profile could not be cleared. It has been kept.\n\nTry again when setup is closed.";
        else if (state == ResetState::SettingsFailed) message = "Your profile was cleared. Some settings may have changed.\n\nTap Retry to finish the reset.";
        else if (complete) message = "Your profile, saved sessions and invitation unlock are cleared.\n\nBrightness is 70%. You can configure a new badge.";
        set_text(message_, message);
        set_hidden(confirm_, working || complete);
        set_text(lv_obj_get_child(confirm_, 0), state == ResetState::Ready ? "Reset badge" : "Retry");
        set_hidden(back_, working);
        set_text(lv_obj_get_child(back_, 0), complete ? "Done" : state == ResetState::Ready ? "Cancel" : "Go back");
    }
private:
    lv_obj_t *heading_, *message_, *confirm_, *back_;
};
std::unique_ptr<PageView> make_reset(Context& c, lv_obj_t* p) { return std::make_unique<ResetView>(c, p); }
} // namespace badge::ui
