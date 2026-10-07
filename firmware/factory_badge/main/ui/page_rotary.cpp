#include "hack_kit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace badge::ui {
namespace {
constexpr int Ox = hack::Cx, Oy = 200;       // Output shaft centre.
constexpr float R = 104, E = 15;             // Rotor centre-to-apex, and shaft eccentricity.
constexpr float Tau = 6.2831853f;
constexpr int Arc = 7, Flank = 6;            // Segments per chamber wall and per rotor flank.
constexpr float Overlap = 0.012f;            // Radians; hides antialiased seams between fan slices.
constexpr float Idle = 0.22f, Redline = 1.6f; // Rotor turns per second.
constexpr uint32_t Bore = 0x121316;          // Unlit chamber.

// The housing bore (an epitrochoid): narrow waist left and right, where the
// ports and spark plugs sit; long axis vertical.
void bore(float t, float& x, float& y) {
    x = Ox + R * std::cos(t) - E * std::cos(3 * t);
    y = Oy + R * std::sin(t) - E * std::sin(3 * t);
}
// What you would see through a glass side plate. Each chamber makes the four
// strokes once per rotor turn (clockwise): it lights at the plugs on the right,
// burns down the right side, goes out through the lower left, and draws a cool
// fresh charge in at the upper left. More throttle, more fire.
lv_color_t chamber_color(float angle, float throttle) {
    const float turn = angle / Tau - std::floor(angle / Tau); // 0 at the plugs, clockwise.
    const float t = turn * 4 - std::floor(turn * 4);
    const lv_color_t dark = lv_color_hex(Bore);
    const float heat = 0.55f + 0.45f * throttle;
    switch (int(turn * 4)) {
        case 0: { // Power: white-hot flash, then orange flame cooling as it expands.
            const lv_color_t flame = t < 0.25f ? hack::mix(lv_color_hex(0xfff6d0), lv_color_hex(0xff8a1c), t * 4)
                                               : hack::mix(lv_color_hex(0xff8a1c), lv_color_hex(0x7a1c08), (t - 0.25f) / 0.75f);
            return hack::mix(dark, flame, heat);
        }
        case 1: return hack::mix(hack::mix(dark, lv_color_hex(0x7a1c08), heat), lv_color_hex(0x221a18), t); // Exhaust.
        case 2: return hack::mix(dark, lv_color_hex(0x1c2733), t);                                         // Intake.
        default: return hack::mix(lv_color_hex(0x1c2733), lv_color_hex(0x2b3142), t);                      // Compression.
    }
}
} // namespace

// A Wankel rotary engine seen through its side plate: cast housing, steel
// rotor, fire in the chambers. The rotor turns once for every three turns of
// the eccentric shaft that carries it. Twist the badge like a motorcycle grip
// to open the throttle; a tap blips it.
class RotaryPage final : public PageView {
public:
    RotaryPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        const int half_w = int(R - E) + 34, half_h = int(R + E) + 26;
        // Intake (upper) and exhaust (lower) runners leave through the left wall.
        runner(Ox - half_w - 30, Oy - 52, 0x3a3d43);
        exhaust_ = runner(Ox - half_w - 30, Oy + 24, 0x3a3d43);
        // The cast housing is a native rounded rectangle with a vertical sheen:
        // cheap to repaint under the bore every frame.
        auto* block = container(root_, Ox - half_w, Oy - half_h, 2 * half_w, 2 * half_h);
        lv_obj_remove_flag(block, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(block, 92, 0);
        lv_obj_set_style_bg_color(block, lv_color_hex(0x8d929a), 0);
        lv_obj_set_style_bg_grad_color(block, lv_color_hex(0x4a4e55), 0);
        lv_obj_set_style_bg_grad_dir(block, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(block, 3, 0);
        lv_obj_set_style_border_color(block, lv_color_hex(0xb4bac2), 0);
        // Tension bolts around the housing.
        for (int i = 0; i < 12; ++i) {
            const float a = (i + 0.5f) * Tau / 12;
            const int x = Ox + int(std::cos(a) * (half_w - 13) * (1 + 0.10f * std::fabs(std::sin(2 * a))));
            const int y = Oy + int(std::sin(a) * (half_h - 13) * (1 + 0.10f * std::fabs(std::sin(2 * a))));
            hack::disc(root_, x, y, 13, lv_color_hex(0x2a2c31));
            hack::disc(root_, x, y, 7, lv_color_hex(0x9da3ab));
        }
        // Two spark plugs: steel shell, ceramic, terminal.
        for (int i = 0; i < 2; ++i) {
            const int y = Oy - 24 + i * 36;
            rect(Ox + int(R - E) + 2, y, 16, 12, 2, 0x70757d);
            rect(Ox + int(R - E) + 18, y + 2, 20, 8, 2, 0xe9e6dc);
            rect(Ox + int(R - E) + 38, y + 4, 8, 4, 1, 0x9da3ab);
        }
        const int box_w = int(R - E) + 3, box_h = int(R + E) + 3;
        engine_ = hack::surface(root_, Ox - box_w, Oy - box_h, 2 * box_w, 2 * box_h,
                                [this](lv_layer_t* layer, const lv_area_t&) { paint(layer); });
        rpm_ = label(root_, "", 134, 350, 200, &font_mono_24, cream());
        throttle_text_ = label(root_, "", 134, 380, 200, &font_mono_18, muted());
        on_tap(root_, [this] { blip_until_ = lv_tick_get() + 900; });
    }
    void update() override {
        if (!clock_.due()) return;
        // Throttle is how far the badge is twisted from upright: 0 hanging
        // straight, 100% at a quarter turn either way. Lying flat reads as closed.
        const auto pull = hack::gravity(context_);
        float throttle = 0;
        if (pull.x * pull.x + pull.y * pull.y > 0.2f)
            throttle = std::clamp((std::fabs(std::atan2(pull.x, pull.y)) - 0.10f) / (Tau / 4 - 0.10f), 0.0f, 1.0f);
        if (int32_t(blip_until_ - clock_.now()) > 0) throttle = 1;
        throttle_ += (throttle - throttle_) * std::min(1.0f, clock_.dt() * 8);
        // The engine follows the throttle with some inertia, quicker up than down.
        const float target = Idle + (Redline - Idle) * throttle_;
        speed_ += (target - speed_) * std::min(1.0f, clock_.dt() * (target > speed_ ? 3.0f : 1.4f));
        rotor_ = std::fmod(rotor_ + speed_ * Tau * clock_.dt(), Tau);
        lv_obj_invalidate(engine_);
        const int rpm = int(850 + (speed_ - Idle) / (Redline - Idle) * 8150) / 50 * 50;
        char text[24];
        if (rpm != rpm_shown_) { rpm_shown_ = rpm; std::snprintf(text, sizeof(text), "%d RPM", rpm); set_text(rpm_, text); }
        const int percent = int(throttle_ * 100 + 0.5f) / 5 * 5;
        if (percent != percent_shown_) { percent_shown_ = percent; std::snprintf(text, sizeof(text), "THROTTLE %d%%", percent); set_text(throttle_text_, text); }
        // The exhaust runner glows with load.
        const int glow = int(throttle_ * 8);
        if (glow != glow_) { glow_ = glow; lv_obj_set_style_bg_color(exhaust_, hack::mix(lv_color_hex(0x3a3d43), lv_color_hex(0xd0481a), glow / 8.0f), 0); }
    }
private:
    lv_obj_t* rect(int x, int y, int w, int h, int radius, uint32_t color) {
        auto* object = container(root_, x, y, w, h);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(object, radius, 0);
        lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        return object;
    }
    lv_obj_t* runner(int x, int y, uint32_t color) {
        auto* pipe = rect(x, y, 60, 28, 6, color);
        lv_obj_set_style_border_width(pipe, 2, 0);
        lv_obj_set_style_border_color(pipe, lv_color_hex(0x8d929a), 0);
        return pipe;
    }
    void paint(lv_layer_t* layer) {
        // The rotor's centre rides the eccentric: three shaft turns per rotor turn.
        const float cx = Ox - E * std::cos(3 * rotor_), cy = Oy - E * std::sin(3 * rotor_);
        const int icx = int(std::lround(cx)), icy = int(std::lround(cy));
        float x0, y0, x1, y1;
        // Chambers: fans from the rotor centre out to the bore. The rotor is
        // painted over their inner part afterwards.
        for (int k = 0; k < 3; ++k) {
            const float from = rotor_ + k * Tau / 3;
            const lv_color_t color = chamber_color(from + Tau / 6, throttle_);
            for (int i = 0; i < Arc; ++i) {
                bore(from + Tau / 3 * i / Arc - Overlap, x0, y0);
                bore(from + Tau / 3 * (i + 1) / Arc + Overlap, x1, y1);
                hack::fill_triangle(layer, cx, cy, x0, y0, x1, y1, color);
            }
        }
        // Rotor: a triangle with bulged flanks, apexes on the bore. Each slice is
        // lit by where it faces on screen, so the steel face keeps a fixed sheen
        // while the rotor turns under it.
        const auto rim = [&](float turn, float scale, float& x, float& y) {
            const float within = turn * 3 - std::floor(turn * 3);          // 0..1 along one flank.
            const float flat = 0.5f / std::cos((within - 0.5f) * Tau / 3); // A straight-sided triangle.
            const float radius = (R * (flat + (1 - flat) * 0.4f) - 1.5f) * scale;
            const float angle = rotor_ + turn * Tau;
            x = cx + std::cos(angle) * radius; y = cy + std::sin(angle) * radius;
        };
        const int slices = 3 * Flank;
        for (int i = 0; i < slices; ++i) {
            rim(float(i) / slices - 0.002f, 1, x0, y0);
            rim(float(i + 1) / slices + 0.002f, 1, x1, y1);
            const float facing = rotor_ + (i + 0.5f) / slices * Tau;
            const float light = 0.72f + 0.28f * std::cos(facing + 2.2f); // Lit from the upper left.
            hack::fill_triangle(layer, cx, cy, x0, y0, x1, y1, hack::scaled(lv_color_hex(0xc4c8cf), light));
        }
        for (int k = 0; k < 3; ++k) {
            // Apex seal at each tip, and the combustion recess cut into each flank.
            rim(k / 3.0f, 0.95f, x0, y0);
            hack::fill_circle(layer, int(std::lround(x0)), int(std::lround(y0)), 4, lv_color_hex(0x2a2c31));
            rim((k + 0.5f) / 3.0f, 0.86f, x0, y0);
            hack::fill_circle(layer, int(std::lround(x0)), int(std::lround(y0)), 8, lv_color_hex(0x8a8e96));
        }
        // Rotor bearing and internal gear, turning with the rotor.
        hack::fill_circle(layer, icx, icy, int(3 * E) + 5, lv_color_hex(0x9ea3aa));
        hack::fill_circle(layer, icx, icy, int(3 * E), lv_color_hex(0x1b1c20));
        for (int i = 0; i < 12; ++i) {
            const float a = rotor_ + i * Tau / 12;
            hack::fill_circle(layer, icx + int(std::lround(std::cos(a) * (3 * E - 3))),
                              icy + int(std::lround(std::sin(a) * (3 * E - 3))), 3, lv_color_hex(0x9ea3aa));
        }
        // The fixed gear bolted to the side plate, and the eccentric shaft
        // journal that swings around inside it three times as fast.
        hack::fill_circle(layer, Ox, Oy, int(2 * E), lv_color_hex(0x6d7178));
        hack::fill_circle(layer, Ox, Oy, int(2 * E) - 5, lv_color_hex(0xb9bec6));
        hack::fill_circle(layer, int(std::lround(Ox - 0.8f * E * std::cos(3 * rotor_))),
                          int(std::lround(Oy - 0.8f * E * std::sin(3 * rotor_))), 9, lv_color_hex(0x3a3d43));
        hack::fill_circle(layer, Ox, Oy, 4, lv_color_hex(0x121316));
    }
    lv_obj_t *engine_ = nullptr, *rpm_ = nullptr, *throttle_text_ = nullptr, *exhaust_ = nullptr;
    hack::FrameClock clock_{66};
    float rotor_ = 0, speed_ = Idle, throttle_ = 0;
    uint32_t blip_until_ = 0;
    int rpm_shown_ = -1, percent_shown_ = -1, glow_ = -1;
};
std::unique_ptr<PageView> make_rotary(Context& c, lv_obj_t* p) { return std::make_unique<RotaryPage>(c, p); }
} // namespace badge::ui
