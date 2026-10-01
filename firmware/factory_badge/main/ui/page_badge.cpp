#include "widgets.h"
#include "design_assets.h"
#include <algorithm>
#include <array>

namespace badge::ui {
class BadgePage final : public PageView {
public:
    BadgePage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        page_heading(root_, "Badge");
        list_ = container(root_, 72, ContentTop, 324, CardHeight);
        lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(list_, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_snap_y(list_, LV_SCROLL_SNAP_CENTER);
        lv_obj_remove_flag(list_, LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list_, 0, 0);
        for (int i = 0; i < 3; ++i) {
            cards_[i] = container(list_, 0, 0, 324, CardHeight);
            lv_obj_add_flag(cards_[i], LV_OBJ_FLAG_SNAPPABLE);
        }
        lv_obj_add_event_cb(list_, [](lv_event_t* event) {
            auto& self = *static_cast<BadgePage*>(lv_event_get_user_data(event));
            const int selected = std::clamp((int(lv_obj_get_scroll_y(self.list_)) + CardHeight / 2) / CardHeight, 0, 2);
            if (selected != self.context_.model.selected_network) {
                self.context_.model.selected_network = selected;
                if (self.context_.callbacks.network) self.context_.callbacks.network(selected);
            }
            self.selected_ = selected;
        }, LV_EVENT_SCROLL_END, this);
        rebuild_cards();
        lv_obj_update_layout(list_);
        scroll_to_model();
    }
    ~BadgePage() override {
        lv_obj_clean(root_);
#if LV_CACHE_DEF_SIZE > 0
        lv_image_cache_drop(&image_);
#endif
    }
    void update() override {
        const auto& model = context_.model;
        if (revision_ != model.profile_revision || name_ != model.name || company_ != model.company ||
            urls_ != model.socials || avatar_ != model.avatar) rebuild_cards();
        if (selected_ != model.selected_network) scroll_to_model();
    }
private:
    static constexpr int CardHeight = ContentBottom - ContentTop;
    void scroll_to_model() {
        selected_ = std::clamp(context_.model.selected_network, 0, 2);
        lv_obj_scroll_to_y(list_, selected_ * CardHeight, LV_ANIM_OFF);
    }
    void avatar(lv_obj_t* card) {
        auto* frame = container(card, 82, 0, 160, 160);
        lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
        if (avatar_ && image_.header.w && image_.header.h) {
            auto* photo = lv_image_create(frame);
            lv_image_set_src(photo, &image_);
            lv_image_set_scale(photo, 256 * 160 / image_.header.w);
            lv_obj_center(photo);
        } else {
            auto* placeholder = lv_image_create(frame);
            lv_image_set_src(placeholder, &supplied_empty_portrait);
            lv_obj_set_style_image_recolor(placeholder, white(), 0);
            lv_obj_set_style_image_recolor_opa(placeholder, LV_OPA_COVER, 0);
            lv_obj_center(placeholder);
        }
    }
    void rebuild_cards() {
        const auto& model = context_.model;
        // Remove every image user before changing its descriptor or backing data.
        if (expanded_) { lv_obj_delete(expanded_); expanded_ = nullptr; }
        set_hidden(list_, false);
        for (auto* card : cards_) lv_obj_clean(card);
#if LV_CACHE_DEF_SIZE > 0
        lv_image_cache_drop(&image_);
#endif
        revision_ = model.profile_revision; name_ = model.name; company_ = model.company;
        urls_ = model.socials; avatar_ = model.avatar;
        image_ = {};
        image_.header.magic = LV_IMAGE_HEADER_MAGIC;
        image_.header.cf = LV_COLOR_FORMAT_RGB565;
        image_.header.w = model.avatar_width; image_.header.h = model.avatar_height;
        image_.header.stride = model.avatar_width * 2;
        image_.data_size = model.avatar_width * model.avatar_height * 2;
        image_.data = reinterpret_cast<const uint8_t*>(avatar_);
        const bool filled = !name_.empty() || !company_.empty() || avatar_ ||
            std::any_of(urls_.begin(), urls_.end(), [](const auto& url) { return !url.empty(); });
        // One social account per badge: show only cards with an account (the
        // first card stands in when none is set).
        const bool any = std::any_of(urls_.begin(), urls_.end(), [](const auto& url) { return !url.empty(); });
        for (int i = 0; i < 3; ++i) {
            auto* card = cards_[i];
            set_hidden(card, any ? urls_[i].empty() : i != 0);
            avatar(card);
            label(card, name_.empty() ? "Your name" : name_.c_str(), 6, 172, 312, &font_mono_24);
            label(card, company_.empty() ? (filled ? "" : "Company") : company_.c_str(),
                  6, 208, 312, &font_mono_20, muted());
            if (!filled) {
                button(card, "Tap to configure", 62, 255, 200, 44, [this] { request_setup(context_); });
            } else {
                // No repeated event bindings on rebuild: the tap plane belongs
                // to this card's freshly created children.
                auto* tap_plane = container(card, 0, 0, 324, CardHeight);
                on_tap(tap_plane, [this, i] { expand(i); });
            }
        }
    }
    void expand(int network) {
        if (expanded_) return;
        set_hidden(list_, true);
        expanded_ = container(root_, 72, ContentTop, 324, CardHeight);
        lv_obj_set_style_bg_color(expanded_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(expanded_, LV_OPA_COVER, 0);
        label(expanded_, NetworkNames[network], 22, 6, 280, &font_sans_20);
        if (urls_[network].empty()) {
            label(expanded_, "No account yet", 22, 145, 280, &font_mono_18, muted());
            button(expanded_, "Tap to configure", 62, 216, 200, 44, [this] { request_setup(context_); });
            button(expanded_, "Go back", 104, 258, 116, 40, [this] { collapse(); });
        } else {
            qr(expanded_, urls_[network], 40, 35, 244);
            label(expanded_, "Tap to close", 22, 284, 280, &font_mono_12, muted());
            on_tap(expanded_, [this] { collapse(); });
        }
    }
    void collapse() { lv_obj_delete(expanded_); expanded_ = nullptr; set_hidden(list_, false); }
    lv_obj_t* list_ = nullptr;
    lv_obj_t* expanded_ = nullptr;
    std::array<lv_obj_t*, 3> cards_{};
    uint32_t revision_ = 0;
    int selected_ = -1;
    const uint16_t* avatar_ = nullptr;
    lv_image_dsc_t image_{};
    std::string name_, company_;
    std::array<std::string, 3> urls_;
};
std::unique_ptr<PageView> make_badge(Context& c, lv_obj_t* p) { return std::make_unique<BadgePage>(c, p); }
} // namespace badge::ui
