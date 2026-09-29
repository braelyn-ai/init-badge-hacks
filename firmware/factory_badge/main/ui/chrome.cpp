#include "chrome.h"
#include "design_assets.h"
#include <algorithm>

namespace badge::ui {
Chrome::Chrome(Context& context, lv_obj_t* parent) : context_(context) {
    root_ = container(parent, 0, 0, Width, Height);
    lv_obj_center(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    brand(root_, 24);
    brand_ = lv_obj_get_child(root_, -1);
    auto* left = button(root_, "", 15, 170, 54, 126, [] { ui_page(-1); });
    auto* right = button(root_, "", 398, 170, 54, 126, [] { ui_page(1); });
    for (auto* arrow : {left, right}) {
        lv_obj_set_style_bg_opa(arrow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(arrow, LV_OPA_30, LV_STATE_PRESSED);
        auto* image = lv_image_create(arrow);
        lv_image_set_src(image, arrow == left ? &supplied_left : &supplied_right);
        lv_obj_set_style_image_recolor(image, white(), 0);
        lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
        lv_obj_center(image);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    }
    footer_ = label(root_, "", 114, 411, 240, &font_mono_12, muted());
    for (int i = 0; i < PageCount; ++i) {
        dots_[i] = container(root_, 0, 431, 8, 8);
        lv_obj_remove_flag(dots_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(dots_[i], LV_OPA_COVER, 0);
    }
    update();
}
Chrome::~Chrome() { if (root_) lv_obj_delete(root_); }
void Chrome::reflow() { lv_obj_center(root_); }
void Chrome::update() {
    const auto& model = context_.model;
    const bool profile_filled = context_.page == 3 &&
        (!model.name.empty() || !model.company.empty() || model.avatar ||
         std::any_of(model.socials.begin(), model.socials.end(), [](const auto& url) { return !url.empty(); }));
    set_hidden(root_, context_.setup || context_.touch_test || context_.reset || profile_filled);
    set_hidden(brand_, context_.page == 0);
    set_text(footer_, context_.page == SettingsPageIndex && model.settings_pending ? "Saving settings..." : "");
    const int count = visible_page_count(model);
    if (page_ != context_.page || visible_count_ != count) {
        page_ = context_.page;
        visible_count_ = count;
        // Eight pixels between each square, including the wider active one.
        int x = (Width - (count * 8 + 4 + (count - 1) * 8)) / 2;
        for (int i = 0; i < PageCount; ++i) {
            const bool visible = page_visible(i, model);
            set_hidden(dots_[i], !visible);
            if (!visible) continue;
            const bool active = i == page_;
            lv_obj_set_pos(dots_[i], x, active ? 429 : 431);
            x += (active ? 12 : 8) + 8;
            lv_obj_set_size(dots_[i], active ? 12 : 8, active ? 12 : 8);
            lv_obj_set_style_bg_color(dots_[i], active ? white() : muted(), 0);
        }
    }
}
} // namespace badge::ui
