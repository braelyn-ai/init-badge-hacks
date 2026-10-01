#include "widgets.h"
#include "design_assets.h"

namespace badge::ui {
// One card for the badge's single social account. A blank badge shows the
// title, a placeholder and Tap to configure; a configured badge hides the title
// (keeping its space), enlarges the photo and swaps it for a same-size QR.
class BadgePage final : public PageView {
public:
    BadgePage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        heading_ = page_heading(root_, "Badge");
        // The QR's network name appears in gray in the hidden title's place.
        network_ = label(root_, "", HeadingX, HeadingY, HeadingWidth, &font_sans_24, muted());
        lv_obj_set_style_text_align(network_, LV_TEXT_ALIGN_CENTER, 0);
        card_ = container(root_, 72, ContentTop, 324, ContentBottom - ContentTop);
        rebuild();
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
            url_ != model.social_url || label_ != model.social_label || avatar_ != model.avatar) rebuild();
    }
private:
    static constexpr int PhotoSide = 208, PhotoX = (324 - PhotoSide) / 2, PhotoY = 0, PhotoRadius = 16;
    lv_obj_t* avatar(int x, int y, int side) {
        auto* frame = container(card_, x, y, side, side);
        lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(frame, PhotoRadius, 0);
        lv_obj_set_style_clip_corner(frame, true, 0);
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
    void rebuild() {
        const auto& model = context_.model;
        // Remove every image user before changing its descriptor or backing data.
        lv_obj_clean(card_);
        photo_ = code_ = nullptr; showing_code_ = false;
        set_hidden(network_, true);
#if LV_CACHE_DEF_SIZE > 0
        lv_image_cache_drop(&image_);
#endif
        revision_ = model.profile_revision; name_ = model.name; company_ = model.company;
        url_ = model.social_url; label_ = model.social_label; avatar_ = model.avatar;
        image_ = {};
        image_.header.magic = LV_IMAGE_HEADER_MAGIC;
        image_.header.cf = LV_COLOR_FORMAT_RGB565;
        image_.header.w = model.avatar_width; image_.header.h = model.avatar_height;
        image_.header.stride = model.avatar_width * 2;
        image_.data_size = model.avatar_width * model.avatar_height * 2;
        image_.data = reinterpret_cast<const uint8_t*>(avatar_);
        const bool filled = !name_.empty() || !company_.empty() || avatar_ || !url_.empty();
        set_hidden(heading_, filled);
        if (!filled) {
            avatar(82, 0, 160);
            label(card_, "Your name", 6, 172, 312, &font_mono_24);
            label(card_, "Company", 6, 208, 312, &font_mono_20, muted());
            button(card_, "Tap to configure", 62, 255, 200, 44, [this] { request_setup(context_); });
            return;
        }
        photo_ = avatar(PhotoX, PhotoY, PhotoSide);
        if (!url_.empty()) {
            code_ = qr(card_, url_, PhotoX, PhotoY, PhotoSide, 16);
            lv_obj_set_style_radius(code_, PhotoRadius, 0);
            set_hidden(code_, true);
        }
        auto* name = label(card_, name_.c_str(), 6, PhotoY + PhotoSide + 8, 312, &font_mono_32);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        auto* company = label(card_, company_.c_str(), 6, PhotoY + PhotoSide + 50, 312, &font_mono_regular_24, muted());
        lv_label_set_long_mode(company, LV_LABEL_LONG_DOT);
        // No repeated event bindings on rebuild: the tap plane belongs to this
        // card's freshly created children.
        auto* tap_plane = container(card_, 0, 0, 324, ContentBottom - ContentTop);
        on_tap(tap_plane, [this] { toggle_code(); });
    }
    void toggle_code() {
        if (!code_) return;
        showing_code_ = !showing_code_;
        set_hidden(photo_, showing_code_);
        set_hidden(code_, !showing_code_);
        set_text(network_, label_);
        set_hidden(network_, !showing_code_);
    }
    lv_obj_t *heading_ = nullptr, *network_ = nullptr, *card_ = nullptr, *photo_ = nullptr, *code_ = nullptr;
    bool showing_code_ = false;
    uint32_t revision_ = 0;
    const uint16_t* avatar_ = nullptr;
    lv_image_dsc_t image_{};
    std::string name_, company_, url_, label_;
};
std::unique_ptr<PageView> make_badge(Context& c, lv_obj_t* p) { return std::make_unique<BadgePage>(c, p); }
} // namespace badge::ui
