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
    // Each arrow owns the full outer strip between the heading and the page
    // dots. Page content stays within x=72..396, so the targets never overlap
    // it; the icons keep their original positions.
    constexpr int TargetWidth = 72, TargetTop = ContentTop, TargetHeight = ContentBottom - ContentTop;
    constexpr int IconTop = 214 - TargetTop;
    auto* left = button(root_, "", 0, TargetTop, TargetWidth, TargetHeight, [] { ui_page(-1); });
    auto* right = button(root_, "", Width - TargetWidth, TargetTop, TargetWidth, TargetHeight, [] { ui_page(1); });
    for (auto* arrow : {left, right}) {
        lv_obj_set_style_bg_opa(arrow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(arrow, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_opa(arrow, LV_OPA_50, LV_STATE_PRESSED); // Dim the icon, not a large patch.
        auto* image = lv_image_create(arrow);
        lv_image_set_src(image, arrow == left ? &supplied_left : &supplied_right);
        lv_obj_set_style_image_recolor(image, white(), 0);
        lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
        lv_obj_set_pos(image, arrow == left ? 30 : 413 - (Width - TargetWidth), IconTop);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    }
    left_ = left; right_ = right;
    status_root_ = container(parent, 0, 0, Width, Height);
    lv_obj_center(status_root_);
    lv_obj_remove_flag(status_root_, LV_OBJ_FLAG_CLICKABLE);
    footer_ = label(status_root_, "", 94, 411, 280, &font_mono_12, muted());
    lv_obj_set_style_text_align(footer_, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < PageCount; ++i) {
        dots_[i] = container(root_, 0, 431, 8, 8);
        lv_obj_remove_flag(dots_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(dots_[i], LV_OPA_COVER, 0);
    }
    update();
}
Chrome::~Chrome() {
    if (status_root_) lv_obj_delete(status_root_);
    if (root_) lv_obj_delete(root_);
}
void Chrome::reflow() { lv_obj_center(root_); lv_obj_center(status_root_); }
void Chrome::update() {
    const auto& model = context_.model;
    const bool profile_filled = context_.page == 3 &&
        (!model.name.empty() || !model.company.empty() || model.avatar ||
         std::any_of(model.socials.begin(), model.socials.end(), [](const auto& url) { return !url.empty(); }));
    // A configured Badge keeps the brand mark and arrows but drops the dots.
    set_hidden(root_, context_.setup || context_.touch_test || context_.reset);
    set_hidden(brand_, context_.page == 0);
    set_hidden(status_root_, context_.setup || context_.touch_test || context_.reset);
    set_text(footer_, !model.photo_status.empty() ? model.photo_status
        : context_.page == SettingsPageIndex && model.settings_pending ? "Saving settings..." : "");
    const bool submenu = context_.page == SettingsPageIndex && context_.settings_submenu;
    set_hidden(left_, submenu);
    set_hidden(right_, submenu);
    const int count = visible_page_count(model);
    const bool dotless = submenu || profile_filled;
    if (page_ != context_.page || visible_count_ != count || dotless_ != dotless) {
        dotless_ = dotless;
        page_ = context_.page;
        visible_count_ = count;
        // Eight pixels between each square, including the wider active one.
        int x = (Width - (count * 8 + 4 + (count - 1) * 8)) / 2;
        for (int i = 0; i < PageCount; ++i) {
            const bool visible = page_visible(i, model);
            set_hidden(dots_[i], !visible || dotless);
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
