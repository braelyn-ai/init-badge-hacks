#include "hack_kit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace badge::ui {
namespace {
constexpr int Count = 9, PlatformWidth = 58, PlatformHeight = 10, Body = 30;
constexpr int SpriteWidth = 46, SpriteHeight = 40; // The drawn jumper; Body is its collision size.
constexpr int Left = 92, Right = 376 - PlatformWidth; // Platform x range; the panel is round.
constexpr float Gravity = 1500, Jump = 660, Boost = 1050; // Pixels, seconds.
constexpr float Steer = 620;       // Sideways pixels per second per g of tilt.
constexpr float CameraLine = 215;  // The jumper never rises above this screen y; the world scrolls instead.

enum class Kind : uint8_t { Solid, Moving, Fragile, Spring };
constexpr uint32_t Colors[] = {0x5fd35f, 0x4aa8ff, 0xe8e6df, 0xffb020};
} // namespace

// Doodle Jump: the jumper bounces by itself; tilt the badge to steer onto the
// next platform. A black world with a few small platforms keeps redraws tiny.
class JumpPage final : public PageView {
public:
    JumpPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        for (auto& platform : platforms_) {
            platform.bar = container(root_, 0, 0, PlatformWidth, PlatformHeight);
            lv_obj_remove_flag(platform.bar, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(platform.bar, PlatformHeight / 2, 0);
            lv_obj_set_style_bg_opa(platform.bar, LV_OPA_COVER, 0);
        }
        // The Doodler: a lime body in striped green trousers, four stubby legs,
        // and a snout that points the way it is heading.
        jumper_ = container(root_, 0, 0, SpriteWidth, SpriteHeight);
        lv_obj_remove_flag(jumper_, LV_OBJ_FLAG_CLICKABLE);
        const auto part = [this](int w, int h, int radius, uint32_t color) {
            auto* object = container(jumper_, 0, 0, w, h);
            lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(object, radius, 0);
            lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
            lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
            return object;
        };
        for (auto& leg : legs_) leg = part(4, 7, 0, 0x1c1c1c);
        snout_ = part(15, 10, 4, 0xd3e03c);
        nostril_ = part(4, 10, 2, 0x1c1c1c);
        lv_obj_set_pos(part(28, 34, 11, 0xd3e03c), 9, 0);   // Body.
        lv_obj_set_pos(part(28, 12, 0, 0x4f9a3c), 9, 22);   // Trousers.
        lv_obj_set_pos(part(28, 2, 0, 0x2f6b28), 9, 25);
        lv_obj_set_pos(part(28, 2, 0, 0x2f6b28), 9, 30);
        for (auto& eye : eyes_) eye = part(4, 5, 2, 0x1c1c1c);
        face(1);
        score_ = label(root_, "0", 164, 14, 140, &font_mono_24, cream());
        over_ = hack::disc(root_, hack::Cx, hack::Cy, 250, lv_color_black());
        hack::ring(over_, 125, 125, 250, 3, lv_color_hex(Colors[0]));
        label(over_, "GAME OVER", 25, 52, 200, &font_mono_18, muted());
        final_ = label(over_, "", 25, 82, 200, &font_sans_32, cream());
        best_ = label(over_, "", 25, 132, 200, &font_mono_18, lv_color_hex(Colors[3]));
        label(over_, "TAP TO RETRY", 25, 172, 200, &font_mono_18, muted());
        on_tap(root_, [this] { if (dead_) start(); });
        start();
    }
    void update() override {
        if (!clock_.due() || dead_) return;
        const float dt = std::min(clock_.dt(), 0.033f);
        // Steering: ease toward the tilt so sensor noise does not jitter the jumper.
        vx_ += (hack::gravity(context_).x * Steer - vx_) * std::min(1.0f, dt * 10);
        x_ += vx_ * dt;
        if (x_ < -Body / 2) x_ += Width + Body; else if (x_ > Width + Body / 2) x_ -= Width + Body;
        const float before = y_;
        vy_ += Gravity * dt;
        y_ += vy_ * dt;
        for (auto& platform : platforms_) {
            if (platform.kind == Kind::Moving) {
                platform.x += platform.drift * dt;
                if (platform.x < Left || platform.x > Right) { platform.drift = -platform.drift; platform.x = std::clamp(platform.x, float(Left), float(Right)); }
            }
            // Land only while falling, when the feet cross the top of the platform.
            const float feet_before = before + Body / 2, feet = y_ + Body / 2;
            if (!platform.gone && vy_ > 0 && feet_before <= platform.y && feet >= platform.y &&
                x_ > platform.x - Body / 3 && x_ < platform.x + PlatformWidth + Body / 3) {
                y_ = platform.y - Body / 2;
                vy_ = platform.kind == Kind::Spring ? -Boost : -Jump;
                if (platform.kind == Kind::Fragile) platform.gone = true;
            }
        }
        // Scroll the world down instead of letting the jumper rise past the line.
        if (y_ < CameraLine) {
            const float shift = CameraLine - y_;
            y_ = CameraLine;
            climbed_ += shift;
            for (auto& platform : platforms_) {
                platform.y += shift;
                if (platform.y > Height + PlatformHeight) respawn(platform);
            }
            top_ += shift;
        }
        if (y_ > Height + Body) { finish(); return; }
        place();
        const int score = int(climbed_ / 10);
        if (score != shown_) { shown_ = score; char text[12]; std::snprintf(text, sizeof(text), "%d", score); set_text(score_, text); }
    }
private:
    struct Platform {
        lv_obj_t* bar = nullptr;
        float x = 0, y = 0, drift = 0;
        Kind kind = Kind::Solid;
        bool gone = false;
        int px = -999, py = -999, shown = -1;
    };
    void start() {
        dead_ = false;
        climbed_ = 0; shown_ = -1;
        set_hidden(over_, true);
        set_hidden(jumper_, false);
        // A safe floor under the jumper, then a ladder of platforms above it.
        top_ = Height - 60;
        for (int i = 0; i < Count; ++i) {
            auto& platform = platforms_[i];
            platform = Platform{platform.bar};
            platform.y = top_;
            platform.x = i ? hack::random_between(Left, Right) : hack::Cx - PlatformWidth / 2;
            top_ -= 52;
        }
        top_ += 52;
        x_ = hack::Cx; y_ = Height - 60 - Body / 2; vx_ = 0; vy_ = -Jump;
        place();
    }
    // Reuse a platform that scrolled off the bottom as the next one at the top.
    // Gaps widen and the special kinds appear as the score grows.
    void respawn(Platform& platform) {
        const int level = std::min(60, int(climbed_ / 220));
        top_ -= float(hack::random_between(48 + level / 2, 72 + level));
        platform.y = top_;
        platform.x = float(hack::random_between(Left, Right));
        platform.gone = false;
        const int roll = hack::random_between(0, 99);
        platform.kind = roll < 6 ? Kind::Spring : roll < 6 + level / 2 ? Kind::Fragile
                      : roll < 12 + level ? Kind::Moving : Kind::Solid;
        platform.drift = float(hack::random_between(0, 1) ? 70 + level : -70 - level);
    }
    void place() {
        for (auto& platform : platforms_) {
            const int kind = platform.gone ? -1 : int(platform.kind);
            if (kind != platform.shown) {
                platform.shown = kind;
                set_hidden(platform.bar, kind < 0);
                if (kind >= 0) lv_obj_set_style_bg_color(platform.bar, lv_color_hex(Colors[kind]), 0);
            }
            const int x = int(std::lround(platform.x)), y = int(std::lround(platform.y));
            if (x != platform.px || y != platform.py) { platform.px = x; platform.py = y; lv_obj_set_pos(platform.bar, x, y); }
        }
        if (std::fabs(vx_) > 40) face(vx_ > 0 ? 1 : -1);
        // The sprite's feet sit at the bottom of the collision circle.
        const int x = int(std::lround(x_)) - SpriteWidth / 2, y = int(std::lround(y_)) + Body / 2 - SpriteHeight;
        if (x != jx_ || y != jy_) { jx_ = x; jy_ = y; lv_obj_set_pos(jumper_, x, y); }
    }
    void face(int direction) {
        if (direction == facing_) return;
        facing_ = direction;
        const bool right = direction > 0;
        lv_obj_set_pos(snout_, right ? 31 : 0, 8);
        lv_obj_set_pos(nostril_, right ? 42 : 0, 8);
        lv_obj_set_pos(eyes_[0], right ? 23 : 19, 6);
        lv_obj_set_pos(eyes_[1], right ? 30 : 12, 6);
        for (int i = 0; i < 4; ++i) lv_obj_set_pos(legs_[i], 11 + i * 7 + (right ? 1 : -1), 33);
    }
    void finish() {
        dead_ = true;
        const int score = int(climbed_ / 10);
        if (score > context_.model.jump_best) {
            context_.model.jump_best = score; // Shown at once; main saves it.
            if (context_.callbacks.jump_best) context_.callbacks.jump_best(score);
        }
        char text[24];
        std::snprintf(text, sizeof(text), "%d", score);
        set_text(final_, text);
        std::snprintf(text, sizeof(text), "BEST %d", context_.model.jump_best);
        set_text(best_, text);
        set_hidden(jumper_, true);
        set_hidden(over_, false);
    }
    std::array<Platform, Count> platforms_{};
    lv_obj_t *snout_ = nullptr, *nostril_ = nullptr, *eyes_[2] = {}, *legs_[4] = {};
    int facing_ = 0;
    lv_obj_t *jumper_ = nullptr, *score_ = nullptr, *over_ = nullptr, *final_ = nullptr, *best_ = nullptr;
    hack::FrameClock clock_{16};
    float x_ = 0, y_ = 0, vx_ = 0, vy_ = 0, climbed_ = 0, top_ = 0;
    int shown_ = -1, jx_ = -999, jy_ = -999;
    bool dead_ = false;
};
std::unique_ptr<PageView> make_jump(Context& c, lv_obj_t* p) { return std::make_unique<JumpPage>(c, p); }
} // namespace badge::ui
