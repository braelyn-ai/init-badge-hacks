#include "widgets.h"
#include "design_assets.h"
#include <algorithm>
#include <array>

namespace badge::ui {
class BadgePage final : public PageView {
public:
    BadgePage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        heading_ = page_heading(root_, "Badge");
        // A configured badge hides its title but keeps that space; the QR's
        // network name appears there in gray.
        network_ = label(root_, "", HeadingX, HeadingY, HeadingWidth, &font_sans_24, muted());
        lv_obj_set_style_text_align(network_, LV_TEXT_ALIGN_CENTER, 0);
        set_hidden(network_, true);
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
    // A configured badge shows a larger photo with the Schedule cards' square
    // corner steps; its QR replaces the photo in exactly the same shape.
    static constexpr int PhotoSide = 208, PhotoX = (324 - PhotoSide) / 2, PhotoY = 0;
    void scroll_to_model() {
        selected_ = std::clamp(context_.model.selected_network, 0, 2);
        lv_obj_scroll_to_y(list_, selected_ * CardHeight, LV_ANIM_OFF);
    }
    lv_obj_t* avatar(lv_obj_t* card, int x, int y, int side) {
        auto* frame = container(card, x, y, side, side);
        lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
        auto* image = lv_image_create(frame);
        if (avatar_ && image_.header.w && image_.header.h) {
            lv_image_set_src(image, &image_);
            lv_image_set_scale(image, 256 * side / image_.header.w);
        } else {
            lv_image_set_src(image, &supplied_empty_portrait);
            lv_image_set_scale(image, 256 * side / 160);
            lv_obj_set_style_image_recolor(image, white(), 0);
            lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
        }
        lv_image_set_antialias(image, true);
        lv_obj_center(image);
        return frame;
    }
    void rebuild_cards() {
        const auto& model = context_.model;
        // Remove every image user before changing its descriptor or backing data.
        for (auto* card : cards_) lv_obj_clean(card);
        photos_ = {}; codes_ = {}; showing_code_ = {};
        set_hidden(network_, true);
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
        const bool any = std::any_of(urls_.begin(), urls_.end(), [](const auto& url) { return !url.empty(); });
        const bool filled = !name_.empty() || !company_.empty() || avatar_ || any;
        set_hidden(heading_, filled);
        // One social account per badge: show only cards with an account (the
        // first card stands in when none is set).
        for (int i = 0; i < 3; ++i) {
            auto* card = cards_[i];
            set_hidden(card, any ? urls_[i].empty() : i != 0);
            if (!filled) {
                avatar(card, 82, 0, 160);
                label(card, "Your name", 6, 172, 312, &font_mono_24);
                label(card, "Company", 6, 208, 312, &font_mono_20, muted());
                button(card, "Tap to configure", 62, 255, 200, 44, [this] { request_setup(context_); });
                continue;
            }
            photos_[i] = avatar(card, PhotoX, PhotoY, PhotoSide);
            square_notches(photos_[i]);
            if (!urls_[i].empty()) {
                codes_[i] = qr(card, urls_[i], PhotoX, PhotoY, PhotoSide, 16);
                square_notches(codes_[i]);
                set_hidden(codes_[i], true);
            }
            auto* name = label(card, name_.c_str(), 6, PhotoY + PhotoSide + 8, 312, &font_mono_32);
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            auto* company = label(card, company_.c_str(), 6, PhotoY + PhotoSide + 50, 312, &font_mono_regular_24, muted());
            lv_label_set_long_mode(company, LV_LABEL_LONG_DOT);
            // No repeated event bindings on rebuild: the tap plane belongs
            // to this card's freshly created children.
            auto* tap_plane = container(card, 0, 0, 324, CardHeight);
            on_tap(tap_plane, [this, i] { toggle_code(i); });
        }
    }
    void toggle_code(int network) {
        if (!codes_[network]) return;
        showing_code_[network] = !showing_code_[network];
        set_hidden(photos_[network], showing_code_[network]);
        set_hidden(codes_[network], !showing_code_[network]);
        set_text(network_, NetworkNames[network]);
        set_hidden(network_, !showing_code_[network]);
    }
    lv_obj_t* heading_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* network_ = nullptr;
    std::array<lv_obj_t*, 3> cards_{}, photos_{}, codes_{};
    std::array<bool, 3> showing_code_{};
    uint32_t revision_ = 0;
    int selected_ = -1;
    const uint16_t* avatar_ = nullptr;
    lv_image_dsc_t image_{};
    std::string name_, company_;
    std::array<std::string, 3> urls_;
};
std::unique_ptr<PageView> make_badge(Context& c, lv_obj_t* p) { return std::make_unique<BadgePage>(c, p); }
} // namespace badge::ui
