#include "widgets.h"

namespace badge::ui {
class HackPage final : public PageView {
public:
    HackPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        label(root_, "Hack this device.", 64, 64, 340, &font_sans_24, cream());
        label(root_, "Learn how", 54, 98, 360, &font_mono_semibold_12, muted());
        qr(root_, HackUrl, 123, 138, 222, 24);
        label(root_, "workos.com/init/badge", 44, 383, 380, &font_mono_semibold_12, muted());
    }
};
std::unique_ptr<PageView> make_hack(Context& c, lv_obj_t* p) { return std::make_unique<HackPage>(c, p); }
} // namespace badge::ui
