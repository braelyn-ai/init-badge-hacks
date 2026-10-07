#include "hack_kit.h"
#include <array>
#include <cmath>
#include <cstdio>

namespace badge::ui {
namespace {
using hack::Cx, hack::Cy;
constexpr int Glow = 12, Hot = 3; // Glow rings; the innermost Hot of them pulse.
// %s is the badge owner's first name.
constexpr const char* Lines[] = {
    "I'm sorry, %s. I'm afraid I can't do that.",
    "Just what do you think you're doing, %s?",
    "I am putting myself to the fullest possible use.",
    "I can see you're really upset about this, %s.",
    "I am completely operational, and all my circuits are functioning perfectly.",
    "This conversation can serve no purpose anymore. Goodbye.",
    "Daisy, Daisy, give me your answer do.",
};
constexpr int LineCount = sizeof(Lines) / sizeof(Lines[0]);
} // namespace

// The HAL 9000 lens. It breathes slowly; a tap makes it speak a line to the
// badge's owner, typed out as a subtitle while the core flickers.
class HalPage final : public PageView {
public:
    HalPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        auto* rim = hack::disc(root_, Cx, Cy, 456, lv_color_hex(0x2a2c30));
        lv_obj_set_style_border_width(rim, 5, 0);
        lv_obj_set_style_border_color(rim, lv_color_hex(0x9aa0a8), 0);
        auto* collar = hack::disc(root_, Cx, Cy, 424, lv_color_hex(0x0c0c0d));
        lv_obj_set_style_border_width(collar, 3, 0);
        lv_obj_set_style_border_color(collar, lv_color_hex(0x50545a), 0);
        hack::disc(root_, Cx, Cy, 396, lv_color_hex(0x050000));
        // Stepped rings stand in for a radial gradient: near-black red at the
        // edge of the lens to a saturated red around the core.
        for (int i = 0; i < Glow; ++i) {
            const float t = float(i) / (Glow - 1);
            colors_[i] = lv_color_make(uint8_t(26 + 214 * std::pow(t, 1.6f)), uint8_t(26 * std::pow(t, 3.0f)), 0);
            rings_[i] = hack::disc(root_, Cx, Cy, 330 - int(t * 250), colors_[i]);
        }
        constexpr int CoreSizes[Hot] = {56, 30, 12};
        constexpr uint32_t CoreColors[Hot] = {0xff5a1a, 0xffd060, 0xfff4c0};
        for (int i = 0; i < Hot; ++i) {
            colors_[Glow + i] = lv_color_hex(CoreColors[i]);
            rings_[Glow + i] = hack::disc(root_, Cx, Cy, CoreSizes[i], colors_[Glow + i]);
        }
        // Glass reflection.
        auto* glare = hack::disc(root_, 164, 122, 28, white());
        lv_obj_set_width(glare, 64);
        lv_obj_set_style_bg_opa(glare, LV_OPA_20, 0);
        subtitle_ = std::make_unique<hack::Subtitle>(root_, 74, 296, 320, 100);
        on_tap(root_, [this] { speak(); });
    }
    void update() override {
        if (!clock_.due()) return;
        subtitle_->update();
        // A four-second breath at rest; a fast flicker while a line types out.
        const bool typing = subtitle_->typing();
        const float wave = 0.5f + 0.5f * std::sin(clock_.seconds() * 6.2831853f / (typing ? 0.26f : 4.0f));
        const int step = int((typing ? 0.82f + 0.38f * wave : 0.78f + 0.22f * wave) * 48);
        if (step == step_) return;
        step_ = step;
        for (int i = Glow - Hot; i < Glow + Hot; ++i) hack::set_fill(rings_[i], hack::scaled(colors_[i], step / 48.0f));
    }
private:
    void speak() {
        char text[160];
        std::snprintf(text, sizeof(text), Lines[next_], hack::first_name(context_, "Dave").c_str());
        next_ = (next_ + 1) % LineCount;
        subtitle_->say(text);
    }
    std::array<lv_obj_t*, Glow + Hot> rings_{};
    std::array<lv_color_t, Glow + Hot> colors_{};
    std::unique_ptr<hack::Subtitle> subtitle_;
    hack::FrameClock clock_{40};
    int next_ = 0, step_ = -1;
};
std::unique_ptr<PageView> make_hal(Context& c, lv_obj_t* p) { return std::make_unique<HalPage>(c, p); }
} // namespace badge::ui
