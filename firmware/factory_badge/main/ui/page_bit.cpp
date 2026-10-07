#include "hack_kit.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace badge::ui {
namespace {
using hack::Cx, hack::Cy;
struct Vec { float x, y, z; };
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec unit(Vec a) { return a * (1.0f / std::sqrt(dot(a, a))); }
Vec lerp(Vec a, Vec b, float t) { return a + (b - a) * t; }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

constexpr int Corners = 12, Faces = 20, Points = Corners + Faces;
// core: icosahedron circumradius in pixels. spike: apex radius of the pyramid
// on each face, in cores (0.795 would be flat). octa: 1 folds the same mesh
// into an exact octahedron.
struct Form { float core, spike, octa; uint32_t lo, hi, text; };
constexpr Form Idle{46, 1.25f, 0, 0x06187a, 0x58d8ff, 0};
constexpr Form Yes{46, 1.25f, 1, 0x7a3a00, 0xffe030, 0xffd91a};
constexpr Form No{30, 2.6f, 0, 0x520008, 0xff3a22, 0xff2a1a};
constexpr float OctaRadius = 76, Wobble = 0.30f; // Idle pulses its two spike sets in antiphase.
constexpr float Eye = 520;                        // Viewer distance; mild perspective.
constexpr uint32_t HoldMs = 6000, ReturnMs = 450; // An answer holds until tapped, or this long.
constexpr float IdleMid = Cy, IdleAmp = 125, AnswerMid = 150, AnswerAmp = 22, BobSeconds = 2.6f;
constexpr int Unit = 11, Stroke = 9, LetterGap = 20; // Letters are drawn on a 4x6 grid of Units.
constexpr int TextWidth = 200, TextHeight = 84, TextTop = 300;

// Pen paths on the letter grid; -1 lifts the pen.
constexpr int8_t LetterY[] = {0, 0, 2, 3, 4, 0, -1, 2, 3, 2, 6, -2};
constexpr int8_t LetterE[] = {4, 0, 0, 0, 0, 6, 4, 6, -1, 0, 3, 3, 3, -2};
constexpr int8_t LetterS[] = {4, 0, 1, 0, 0, 1, 0, 2, 1, 3, 3, 3, 4, 4, 4, 5, 3, 6, 0, 6, -2};
constexpr int8_t LetterN[] = {0, 6, 0, 0, 4, 6, 4, 0, -2};
constexpr int8_t LetterO[] = {1, 0, 3, 0, 4, 1, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, -2};
const int8_t* letter(char c) {
    switch (c) {
    case 'Y': return LetterY;
    case 'E': return LetterE;
    case 'S': return LetterS;
    case 'N': return LetterN;
    default: return LetterO;
    }
}
} // namespace

// The Bit from Tron. One mesh, an icosahedron with a pyramid on every face,
// does all three forms: a pulsing blue star at rest, a red spike ball for NO,
// and, with its points folded onto an octahedron, a yellow YES.
class BitPage final : public PageView {
public:
    BitPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        build_mesh();
        shape_ = hack::surface(root_, 0, 0, Width, Height, [this](lv_layer_t* layer, const lv_area_t& coords) {
            if (!hack::needs_paint(layer, coords.x1 + box_.x1, coords.y1 + box_.y1,
                                   box_.x2 - box_.x1 + 1, box_.y2 - box_.y1 + 1)) return;
            const float ox = coords.x1, oy = coords.y1;
            for (int i = 0; i < prim_count_; ++i) {
                const auto& p = prims_[i];
                hack::fill_triangle(layer, ox + p.x0, oy + p.y0, ox + p.x1, oy + p.y1, ox + p.x2, oy + p.y2, p.color);
            }
        });
        text_ = hack::surface(root_, Cx - TextWidth / 2, TextTop, TextWidth, TextHeight,
                              [this](lv_layer_t* layer, const lv_area_t& coords) { paint_text(layer, coords); });
        on_tap(root_, [this] {
            const uint32_t now = lv_tick_get();
            // A tap on a held answer dismisses it: back to the undecided state.
            if (answer_ && uint32_t(now - tapped_) < HoldMs) { tapped_ = now - HoldMs; return; }
            answer_ = hack::random_between(0, 1) ? &Yes : &No;
            tapped_ = now;
            kick_ = 7.0f;
        });
        update_scene(0);
    }
    void update() override {
        if (clock_.due()) update_scene(clock_.dt());
    }
private:
    struct Prim { float x0, y0, x1, y1, x2, y2; lv_color_t color; };

    void build_mesh() {
        const float phi = 1.6180340f;
        int n = 0;
        for (float a : {-1.0f, 1.0f}) for (float b : {-phi, phi}) {
            dir_[n++] = unit({0, a, b}); dir_[n++] = unit({a, b, 0}); dir_[n++] = unit({b, 0, a});
        }
        int f = 0;
        for (int i = 0; i < Corners; ++i) for (int j = i + 1; j < Corners; ++j) for (int k = j + 1; k < Corners; ++k) {
            const auto near = [this](int a, int b) { const Vec d = dir_[a] - dir_[b]; return dot(d, d) < 1.3f; };
            if (!near(i, j) || !near(j, k) || !near(i, k)) continue;
            const Vec centre = dir_[i] + dir_[j] + dir_[k];
            const bool outward = dot(cross(dir_[j] - dir_[i], dir_[k] - dir_[i]), centre) > 0;
            face_[f][0] = uint8_t(i); face_[f][1] = uint8_t(outward ? j : k); face_[f][2] = uint8_t(outward ? k : j);
            const Vec c = unit(centre);
            dir_[Corners + f] = c;
            // Eight faces look down the octants and flatten into the octahedron's
            // faces; the other twelve pair up to make its six corners.
            const Vec m{std::fabs(c.x), std::fabs(c.y), std::fabs(c.z)};
            octant_[f] = std::min({m.x, m.y, m.z}) > 0.3f;
            if (octant_[f]) octa_[Corners + f] = Vec{std::copysign(1.0f, c.x), std::copysign(1.0f, c.y), std::copysign(1.0f, c.z)} * (OctaRadius / 3);
            else octa_[Corners + f] = Vec{m.x > m.y && m.x > m.z ? c.x : 0, m.y > m.x && m.y > m.z ? c.y : 0, m.z > m.x && m.z > m.y ? c.z : 0};
            if (!octant_[f]) octa_[Corners + f] = unit(octa_[Corners + f]) * OctaRadius;
            order_[f] = uint8_t(f);
            ++f;
        }
        for (int i = 0; i < Corners; ++i)
            octa_[i] = dir_[i] * (OctaRadius / (std::fabs(dir_[i].x) + std::fabs(dir_[i].y) + std::fabs(dir_[i].z)));
    }

    void update_scene(float dt) {
        // idle: 0 while an answer holds, easing back to 1 afterwards.
        float idle = 1;
        bool holding = false;
        if (answer_) {
            const uint32_t t = lv_tick_get() - tapped_;
            if (t >= HoldMs + ReturnMs) answer_ = nullptr;
            else if (t > HoldMs) { const float u = float(t - HoldMs) / ReturnMs; idle = u * u * (3 - 2 * u); }
            else { idle = 0; holding = true; }
        }
        const Form& from = answer_ ? *answer_ : Idle;
        const float core = lerp(from.core, Idle.core, idle), octa = lerp(from.octa, Idle.octa, idle);
        const float spike = lerp(from.spike, Idle.spike, idle);
        morph_ += dt * 2.4f;
        const float pulse = Wobble * idle * std::sin(morph_);
        const lv_color_t lo = hack::mix(lv_color_hex(from.lo), lv_color_hex(Idle.lo), idle);
        const lv_color_t hi = hack::mix(lv_color_hex(from.hi), lv_color_hex(Idle.hi), idle);

        // Float: quick through the bottom of the bob, lingering at the top.
        // An answer lifts it clear of the word and holds it nearly still.
        const float ease = 1 - std::exp(-7 * dt);
        mid_ += ((holding ? AnswerMid : IdleMid) - mid_) * ease;
        amp_ += ((holding ? AnswerAmp : IdleAmp) - amp_) * ease;
        bob_ += dt * 3.1415927f / BobSeconds;
        const float centre_y = mid_ + amp_ * (1 - 2 * std::pow(std::fabs(std::sin(bob_)), 1.35f));

        kick_ *= std::exp(-4 * dt);
        yaw_ += dt * (0.8f + kick_);
        pitch_ += dt * 0.47f;
        const float cy = std::cos(yaw_), sy = std::sin(yaw_), cp = std::cos(pitch_), sp = std::sin(pitch_);
        const auto turn = [=](Vec v) {
            const float z = v.z * cy - v.x * sy;
            return Vec{v.x * cy + v.z * sy, v.y * cp - z * sp, v.y * sp + z * cp};
        };

        Vec at[Points];
        float sx[Points], sy2[Points];
        lv_area_t box{Width, Height, 0, 0};
        const auto project = [=](Vec v, float& x, float& y) {
            const float scale = Eye / (Eye - v.z);
            x = Cx + v.x * scale; y = centre_y + v.y * scale;
        };
        for (int i = 0; i < Points; ++i) {
            float radius = core;
            if (i >= Corners) radius *= spike + (octant_[i - Corners] ? pulse : -pulse);
            at[i] = turn(lerp(dir_[i] * radius, octa_[i], octa));
            project(at[i], sx[i], sy2[i]);
            box.x1 = std::min<int32_t>(box.x1, int32_t(sx[i]) - 1); box.x2 = std::max<int32_t>(box.x2, int32_t(sx[i]) + 2);
            box.y1 = std::min<int32_t>(box.y1, int32_t(sy2[i]) - 1); box.y2 = std::max<int32_t>(box.y2, int32_t(sy2[i]) + 2);
        }

        // Each pyramid stays inside the cone from the centre through its face,
        // so drawing whole pyramids far-to-near by face direction is an exact
        // painter's order; within a pyramid, back-face culling is enough.
        float depth[Faces];
        for (int f = 0; f < Faces; ++f) depth[f] = turn(dir_[Corners + f]).z;
        for (int i = 1; i < Faces; ++i)
            for (int j = i; j > 0 && depth[order_[j]] < depth[order_[j - 1]]; --j) std::swap(order_[j], order_[j - 1]);

        static const Vec Key = unit({-0.40f, -0.65f, 0.65f}), Fill = unit({0.65f, 0.45f, 0.35f});
        const Vec eye{0, 0, Eye};
        const auto lit = [&](Vec normal) {
            const Vec nu = unit(normal);
            const float key = std::max(0.0f, dot(nu, Key));
            const lv_color_t color = hack::mix(lo, hi, 0.10f + 0.90f * key + 0.25f * std::max(0.0f, dot(nu, Fill)));
            return key > 0.8f ? hack::mix(color, white(), (key - 0.8f) * 2.5f) : color;
        };
        prim_count_ = 0;
        // The folded mesh splits each octahedron face into six triangles whose
        // antialiased joins show, so the settled YES is drawn as eight.
        for (int n = 0; octa >= 1 && n < 8; ++n) {
            const Vec p[3] = {turn({n & 1 ? OctaRadius : -OctaRadius, 0, 0}), turn({0, n & 2 ? OctaRadius : -OctaRadius, 0}),
                              turn({0, 0, n & 4 ? OctaRadius : -OctaRadius})};
            Vec normal = cross(p[1] - p[0], p[2] - p[0]);
            if (dot(normal, p[0] + p[1] + p[2]) < 0) normal = normal * -1;
            if (dot(normal, eye - p[0]) <= 0) continue;
            float x[3], y[3];
            for (int k = 0; k < 3; ++k) project(p[k], x[k], y[k]);
            prims_[prim_count_++] = {x[0], y[0], x[1], y[1], x[2], y[2], lit(normal)};
        }
        for (int n = 0; octa < 1 && n < Faces; ++n) {
            const int f = order_[n], apex = Corners + f;
            for (int k = 0; k < 3; ++k) {
                const int a = face_[f][k], b = face_[f][(k + 1) % 3];
                const Vec normal = cross(at[b] - at[a], at[apex] - at[a]);
                if (dot(normal, eye - at[apex]) <= 0) continue;
                prims_[prim_count_++] = {sx[a], sy2[a], sx[b], sy2[b], sx[apex], sy2[apex], lit(normal)};
            }
        }

        lv_area_t dirty = box, coords;
        if (box_.x2 >= box_.x1)
            dirty = {std::min(box.x1, box_.x1), std::min(box.y1, box_.y1), std::max(box.x2, box_.x2), std::max(box.y2, box_.y2)};
        box_ = box;
        lv_obj_get_coords(shape_, &coords);
        lv_area_move(&dirty, coords.x1, coords.y1);
        lv_obj_invalidate_area(shape_, &dirty);

        const char* word = answer_ == &Yes ? "YES" : answer_ == &No ? "NO" : "";
        const int level = answer_ ? int(std::ceil((1 - idle) * 4)) : 0; // Dims in steps; opaque strokes are cheaper.
        if (word != word_ || level != text_level_) {
            if (*word) { word_ = word; text_color_ = hack::scaled(lv_color_hex(answer_->text), level / 4.0f); }
            text_level_ = level;
            lv_obj_invalidate(text_);
        }
    }

    void paint_text(lv_layer_t* layer, const lv_area_t& coords) const {
        if (!text_level_) return;
        const int count = int(std::strlen(word_));
        float x = coords.x1 + (TextWidth - count * 4 * Unit - (count - 1) * LetterGap) / 2.0f;
        const float y = coords.y1 + (TextHeight - 6 * Unit) / 2.0f;
        for (const char* c = word_; *c; ++c, x += 4 * Unit + LetterGap) {
            bool down = false;
            float px = 0, py = 0;
            for (const int8_t* p = letter(*c); *p != -2;) {
                if (*p == -1) { down = false; ++p; continue; }
                const float nx = x + p[0] * Unit, ny = y + p[1] * Unit;
                if (down) hack::draw_line(layer, px, py, nx, ny, Stroke, text_color_);
                px = nx; py = ny; down = true; p += 2;
            }
        }
    }

    Vec dir_[Points];    // Unit directions: 12 corners, then 20 face centres.
    Vec octa_[Points];   // Where each point sits on the octahedron.
    uint8_t face_[Faces][3], order_[Faces];
    bool octant_[Faces];
    Prim prims_[Faces * 3];
    int prim_count_ = 0;
    lv_area_t box_{0, 0, -1, -1}; // Screen box of the shape as last drawn.
    lv_obj_t *shape_ = nullptr, *text_ = nullptr;
    hack::FrameClock clock_{55}; // 18 frames/s: the panel cannot push this shape faster.
    const Form* answer_ = nullptr;
    uint32_t tapped_ = 0;
    float mid_ = IdleMid, amp_ = IdleAmp, bob_ = 0, morph_ = 0, yaw_ = 0.4f, pitch_ = 0.3f, kick_ = 0;
    const char* word_ = "";
    lv_color_t text_color_{};
    int text_level_ = 0;
};
std::unique_ptr<PageView> make_bit(Context& c, lv_obj_t* p) { return std::make_unique<BitPage>(c, p); }
} // namespace badge::ui
