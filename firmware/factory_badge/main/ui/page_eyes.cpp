#include "hack_kit.h"
#include <algorithm>
#include <cmath>

namespace badge::ui {
namespace {
constexpr int EyeSize = 196, PupilSize = 92, GlintSize = 24;
constexpr int EyeOffset = 104; // Each eye's centre, left/right of the screen centre.
constexpr float Travel = (EyeSize - PupilSize) / 2.0f - 5.0f;
constexpr float Pull = 2600.0f; // Pixels per second squared, per g.
constexpr uint32_t BlinkMs = 220;
} // namespace

// Two googly eyes. Each pupil is a loose disc inside its eye: gravity from the
// accelerometer pulls it, the rim bounces it. A tap pokes both.
class EyesPage final : public PageView {
public:
    EyesPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        int side = -1;
        for (auto& eye : eyes_) {
            eye.centre = Width / 2 + side * EyeOffset;
            eye.white = hack::disc(root_, eye.centre, Height / 2, EyeSize, white());
            eye.pupil = hack::disc(eye.white, EyeSize / 2, EyeSize / 2, PupilSize, lv_color_black());
            hack::disc(eye.pupil, 30, 26, GlintSize, white());
            eye.y = Travel; // Resting at the bottom until the first sample.
            side = 1;
        }
        // Slightly different pupils so the pair never moves in lockstep.
        eyes_[0].drag = 1.1f; eyes_[0].bounce = 0.55f;
        eyes_[1].drag = 1.5f; eyes_[1].bounce = 0.42f;
        on_tap(root_, [this] { poke(); });
        next_blink_ = lv_tick_get() + 2500;
        layout(1.0f);
    }
    void update() override {
        if (!clock_.due()) return;
        const uint32_t now = clock_.now();
        const float dt = std::min(clock_.dt(), 0.05f);
        const auto pull = hack::gravity(context_);
        const float gx = pull.x, gy = pull.y;
        for (auto& eye : eyes_) {
            eye.vx += gx * Pull * dt;
            eye.vy += gy * Pull * dt;
            const float keep = std::exp(-eye.drag * dt);
            eye.vx *= keep; eye.vy *= keep;
            eye.x += eye.vx * dt; eye.y += eye.vy * dt;
            const float distance = std::sqrt(eye.x * eye.x + eye.y * eye.y);
            if (distance > Travel) {
                const float nx = eye.x / distance, ny = eye.y / distance;
                eye.x = nx * Travel; eye.y = ny * Travel;
                const float outward = eye.vx * nx + eye.vy * ny;
                if (outward > 0) {
                    eye.vx -= (1.0f + eye.bounce) * outward * nx;
                    eye.vy -= (1.0f + eye.bounce) * outward * ny;
                }
            }
        }
        if (int32_t(now - next_blink_) >= 0) {
            blink_started_ = now; blinking_ = true;
            next_blink_ = now + hack::random_between(2500, 7000);
        }
        float open = 1.0f;
        if (blinking_) {
            const uint32_t t = now - blink_started_;
            if (t >= BlinkMs) blinking_ = false;
            else open = std::fabs(float(t) / (BlinkMs / 2) - 1.0f);
        }
        layout(open);
    }
private:
    struct Eye {
        lv_obj_t *white = nullptr, *pupil = nullptr;
        int centre = 0;
        float x = 0, y = 0, vx = 0, vy = 0;
        float drag = 1, bounce = 0.5f;
        int height = -1, left = -1, top = -1;
    };
    void poke() {
        for (auto& eye : eyes_) {
            eye.vx += float(hack::random_between(-1200, 1200));
            eye.vy -= float(hack::random_between(600, 1500));
        }
        blink_started_ = lv_tick_get(); blinking_ = true;
    }
    void layout(float open) {
        for (auto& eye : eyes_) {
            const int height = std::max(12, int(EyeSize * open));
            if (height != eye.height) {
                eye.height = height;
                lv_obj_set_size(eye.white, EyeSize, height);
                lv_obj_set_pos(eye.white, eye.centre - EyeSize / 2, Height / 2 - height / 2);
                eye.left = -1; // The pupil's parent moved; place it again.
            }
            const int left = EyeSize / 2 + int(std::lround(eye.x)) - PupilSize / 2;
            const int top = height / 2 + int(std::lround(eye.y)) - PupilSize / 2;
            if (left != eye.left || top != eye.top) {
                eye.left = left; eye.top = top;
                lv_obj_set_pos(eye.pupil, left, top);
            }
        }
    }
    Eye eyes_[2];
    hack::FrameClock clock_{16};
    uint32_t next_blink_ = 0, blink_started_ = 0;
    bool blinking_ = false;
};
std::unique_ptr<PageView> make_eyes(Context& c, lv_obj_t* p) { return std::make_unique<EyesPage>(c, p); }
} // namespace badge::ui
