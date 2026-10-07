#include "hack_kit.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace badge::ui {
namespace {
using hack::Cx, hack::Cy;
// Rainbow rings from the socket out to the rim, thin to thick like a tunnel.
constexpr int Rings = 6, SocketRadius = 138;
constexpr int RingWidth[Rings] = {12, 14, 15, 17, 18, 20};
// The eye is the overlap of two circles (a vesica). Apex offsets are pixels
// below Cy; the radii put both arcs through the corners (Cx +- HalfWidth, Cy).
// The lower lid never moves: the upper lid's circle slides down until the
// overlap is gone, which leaves the lower arc as the closed lash line.
constexpr int HalfWidth = 128, OpenTop = -80, OpenBottom = 56;
constexpr int TopRadius = 142, BottomRadius = 174;
constexpr int TopCover = 180, BottomCover = 130; // Lid thickness: enough to fill the box.
constexpr int EyeX = HalfWidth + 8, EyeY = 10 - OpenTop; // The corner line, inside the box.
constexpr int BoxWidth = 2 * EyeX, BoxHeight = EyeY + OpenBottom + 24;
constexpr int IrisSize = 120, Bands = 2;
constexpr int BandSize[Bands] = {92, 64};
constexpr float OpenSeconds = 1.0f, CloseSeconds = 0.45f, WakeAfter = 1.5f;
constexpr uint32_t RingMs = 120, BlinkMs = 300;
// cos of 0, 18 .. 90 degrees: each ring is invalidated as 20 arc-hugging boxes.
constexpr int Sectors = 5;
constexpr float Cos[Sectors + 1] = {1.0f, 0.95106f, 0.80902f, 0.58779f, 0.30902f, 0.0f};

lv_color_t socket_color() { return lv_color_hex(0x120a2a); }

// A circle outline painted directly: inner radius plus thickness outward.
void paint_band(lv_layer_t* layer, int cx, int cy, int inner, int thickness, lv_color_t color) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_color = color;
    dsc.border_width = thickness;
    const int outer = inner + thickness;
    const lv_area_t area = {cx - outer, cy - outer, cx + outer, cy + outer};
    lv_draw_rect(layer, &dsc, &area);
}
float ease(float t) { return t * t * (3.0f - 2.0f * t); }
// How far a circle's arc has dropped below its apex, x pixels off centre.
float drop(int radius, float x) { return radius - std::sqrt(std::max(0.0f, float(radius * radius) - x * x)); }
} // namespace

// One mystical eye. It sleeps behind a closed lid, then opens onto a trip:
// colour ripples out through the rainbow rings, the iris cycles and the pupil
// throbs while the eye follows gravity. A tap puts it to sleep or wakes it.
class ThirdEyePage final : public PageView {
public:
    ThirdEyePage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        // A square is enough for the socket: the rings above hide its corners.
        auto* socket = container(root_, Cx - SocketRadius - 2, Cy - SocketRadius - 2, 2 * SocketRadius + 4, 2 * SocketRadius + 4);
        lv_obj_remove_flag(socket, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(socket, socket_color(), 0);
        lv_obj_set_style_bg_opa(socket, LV_OPA_COVER, 0);

        eye_ = container(root_, Cx - EyeX, Cy - EyeY, BoxWidth, BoxHeight);
        lv_obj_remove_flag(eye_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(eye_, lv_color_hex(0xf6f2ff), 0);
        lv_obj_set_style_bg_opa(eye_, LV_OPA_COVER, 0);
        iris_ = hack::disc(eye_, 0, 0, IrisSize, lv_color_black());
        for (int i = 0; i < Bands; ++i) bands_[i] = hack::disc(iris_, IrisSize / 2, IrisSize / 2, BandSize[i], lv_color_black());
        pupil_ = hack::disc(iris_, IrisSize / 2, IrisSize / 2, pupil_size_, lv_color_black());
        hack::disc(iris_, IrisSize / 2 - 22, IrisSize / 2 - 24, 16, white());
        hack::on_paint(eye_, [this](lv_layer_t* layer, const lv_area_t& coords) { paint_lids(layer, coords); }, true);

        // Last, so the rings also trim the eye box where it leaves the socket.
        rings_ = hack::surface(root_, 0, 0, Width, Height, [this](lv_layer_t* layer, const lv_area_t& coords) { paint_rings(layer, coords); });
        for (int i = 0; i < Rings; ++i) ring_color_[i] = ring_color(i, 0);
        on_tap(root_, [this] { touched_ = true; wake(!awake_); });
        place_iris();
    }
    void update() override {
        if (!clock_.due()) return;
        const uint32_t now = clock_.now();
        const float dt = clock_.dt(), t = clock_.seconds();
        if (!touched_ && !awake_ && t >= WakeAfter) wake(true);
        open_ = std::clamp(open_ + (awake_ ? dt / OpenSeconds : -dt / CloseSeconds), 0.0f, 1.0f);

        float lid = ease(open_);
        if (open_ < 1.0f) next_blink_ = now + 3000;
        else if (int32_t(now - next_blink_) >= 0) {
            const uint32_t elapsed = now - next_blink_;
            if (elapsed >= BlinkMs) next_blink_ = now + hack::random_between(3000, 7000);
            else lid = std::fabs(float(elapsed) / (BlinkMs / 2) - 1.0f);
        }
        move_lid(lid);
        if (open_ > 0) {
            // The iris recolours first: its box then absorbs the smaller
            // invalidations of everything inside it.
            hack::set_fill(iris_, hack::hsv(int(t * 200), 100, 100));
            for (int i = 0; i < Bands; ++i) hack::set_fill(bands_[i], hack::hsv(int(t * 200) + (i + 1) * 100, 100, 100));
            // Follow gravity, with a slow wander so a badge at rest still looks alive.
            const auto pull = hack::gravity(context_);
            const float tx = std::clamp(pull.x * 110.0f, -42.0f, 42.0f) + 6.0f * std::sin(t * 1.3f);
            const float ty = RestY + std::clamp(pull.y * 8.0f, -10.0f, 10.0f) + 3.0f * std::sin(t * 1.7f);
            const float follow = 1.0f - std::exp(-7.0f * dt);
            look_x_ += (tx - look_x_) * follow;
            look_y_ += (ty - look_y_) * follow;
            place_iris();
            // A slow dilation with a quick heartbeat on top.
            const int size = int(36 + 10 * std::sin(t * 2.1f) + 5 * std::sin(t * 9.0f)) & ~1;
            if (size != pupil_size_) {
                pupil_size_ = size;
                lv_obj_set_size(pupil_, size, size);
                lv_obj_set_pos(pupil_, (IrisSize - size) / 2, (IrisSize - size) / 2);
            }
        }
        step_rings(now);
    }
private:
    static constexpr float RestY = (OpenTop + OpenBottom) / 2 - 2.0f;

    void wake(bool awake) {
        awake_ = awake;
        step_ = (step_ / Rings + 1) * Rings; // The change ripples out from the innermost ring.
    }
    void place_iris() {
        const int left = EyeX + int(std::lround(look_x_)) - IrisSize / 2;
        const int top = EyeY + int(std::lround(look_y_)) - IrisSize / 2;
        if (left == iris_left_ && top == iris_top_) return;
        iris_left_ = left; iris_top_ = top;
        lv_obj_set_pos(iris_, left, top);
    }
    int lid_top() const { return int(std::lround(OpenBottom + (OpenTop - OpenBottom) * lid_)); }
    void move_lid(float lid) {
        if (lid == lid_) return;
        const int before = lid_top();
        const bool lashes = (lid == 0) != (lid_ == 0); // They show only when shut.
        lid_ = lid;
        const int after = lid_top();
        if (lashes) { lv_obj_invalidate(eye_); return; }
        if (after == before) return;
        // Only the sliver the upper arc swept through, in four columns.
        lv_area_t box;
        lv_obj_get_coords(eye_, &box);
        const int cx = int(box.x1) + EyeX, cy = int(box.y1) + EyeY;
        constexpr int Column = EyeX / 2;
        for (int i = -2; i < 2; ++i) {
            const int x0 = i * Column, x1 = x0 + Column;
            const float inside = (i == -1 || i == 0) ? 0.0f : float(Column), outside = inside + Column;
            const int y0 = std::min(before, after) + int(drop(TopRadius, inside)) - 4;
            const int y1 = std::min(std::max(before, after) + int(drop(TopRadius, outside)), OpenBottom - int(drop(BottomRadius, inside))) + 5;
            if (y1 < y0) continue;
            const lv_area_t area = {cx + x0, std::max(cy + y0, int(box.y1)), cx + x1, std::min(cy + y1, int(box.y2))};
            lv_obj_invalidate_area(eye_, &area);
        }
    }
    lv_color_t ring_color(int ring, int pass) const {
        // Each pass hands every ring the hue of the one inside it, so the
        // rainbow marches outward; 47 degrees keeps it drifting round the wheel.
        return awake_ ? hack::hsv(47 * (pass - ring), 100, 100) : hack::hsv(258 + ring * 6, 75, 10);
    }
    // One ring changes colour per step, and only once the last change has
    // reached the screen: a slow panel then slows the ripple, not the buttons.
    void step_rings(uint32_t now) {
        if (ring_pending_ || now - ring_at_ < RingMs) return;
        const int ring = step_ % Rings;
        const auto color = ring_color(ring, step_ / Rings);
        ++step_;
        if (lv_color_eq(color, ring_color_[ring])) return;
        ring_at_ = now;
        ring_color_[ring] = color;
        ring_pending_ = true;
        // Invalidate boxes that hug the ring rather than its whole bounding square.
        lv_area_t origin;
        lv_obj_get_coords(rings_, &origin);
        const int cx = int(origin.x1) + Cx, cy = int(origin.y1) + Cy;
        int inner = SocketRadius;
        for (int i = 0; i < ring; ++i) inner += RingWidth[i];
        const float low = inner - 2, high = inner + RingWidth[ring] + 2;
        for (int i = 0; i < Sectors; ++i) {
            const int x0 = int(low * Cos[i + 1]), x1 = int(high * Cos[i]) + 1;
            const int y0 = int(low * Cos[Sectors - i]), y1 = int(high * Cos[Sectors - i - 1]) + 1;
            const lv_area_t areas[4] = {{cx + x0, cy + y0, cx + x1, cy + y1}, {cx - x1, cy + y0, cx - x0, cy + y1},
                                        {cx + x0, cy - y1, cx + x1, cy - y0}, {cx - x1, cy - y1, cx - x0, cy - y0}};
            for (const auto& area : areas) lv_obj_invalidate_area(rings_, &area);
        }
    }
    void paint_rings(lv_layer_t* layer, const lv_area_t& coords) {
        ring_pending_ = false; // Any repaint means the refresh holding our boxes ran.
        const int cx = int(coords.x1) + Cx, cy = int(coords.y1) + Cy;
        // Distance range of the region being rendered: rings outside it are skipped.
        const int x1 = int(layer->_clip_area.x1) - cx, x2 = int(layer->_clip_area.x2) - cx;
        const int y1 = int(layer->_clip_area.y1) - cy, y2 = int(layer->_clip_area.y2) - cy;
        const int min_x = std::max<int>({x1, -x2, 0}), min_y = std::max<int>({y1, -y2, 0});
        const int max_x = std::max<int>(std::abs(x1), std::abs(x2)), max_y = std::max<int>(std::abs(y1), std::abs(y2));
        const int nearest2 = min_x * min_x + min_y * min_y, farthest2 = max_x * max_x + max_y * max_y;
        int outer = hack::ScreenRadius + 1;
        for (int i = Rings - 1; i >= 0; --i) {
            // Each ring reaches 2 px under the next one in, hiding the antialiased seam.
            const int inner = outer - RingWidth[i], reach = i ? inner - 2 : inner;
            if (nearest2 < (outer + 1) * (outer + 1) && farthest2 > (reach - 1) * (reach - 1))
                paint_band(layer, cx, cy, reach, outer - reach, ring_color_[i]);
            outer = inner;
        }
    }
    // Painted over the eyeball and iris: everything outside the almond.
    void paint_lids(lv_layer_t* layer, const lv_area_t& coords) const {
        const int cx = int(coords.x1) + EyeX, cy = int(coords.y1) + EyeY;
        const int top_cy = cy + lid_top() + TopRadius, bottom_cy = cy + OpenBottom - BottomRadius;
        const auto skin = socket_color();
        // Two fixed colours: only the swept sliver repaints while the lid moves.
        const auto liner = lv_color_hex(lid_ == 0 ? 0x7466c8 : 0xffd24a);
        if (lid_ == 0) {
            hack::fill_rect(layer, int(coords.x1), int(coords.y1), BoxWidth, BoxHeight, skin);
            for (int i = -3; i <= 3; ++i) {
                const float x = i * 30.0f, y = cy + OpenBottom - drop(BottomRadius, x);
                hack::draw_line(layer, cx + x, y, cx + x * 1.12f, y + 16.0f, 3, liner);
            }
        } else {
            // The upper lid is everything outside its circle; where that
            // circle's outline strays past the lower one, the lower lid hides it.
            paint_band(layer, cx, top_cy, TopRadius, TopCover, skin);
            paint_band(layer, cx, top_cy, TopRadius - 2, 4, liner);
            paint_band(layer, cx, bottom_cy, BottomRadius, BottomCover, skin);
        }
        paint_band(layer, cx, bottom_cy, BottomRadius - 2, 4, liner);
    }

    lv_obj_t *eye_ = nullptr, *iris_ = nullptr, *pupil_ = nullptr, *rings_ = nullptr;
    std::array<lv_obj_t*, Bands> bands_{};
    std::array<lv_color_t, Rings> ring_color_{};
    hack::FrameClock clock_{50};
    float open_ = 0, lid_ = 0, look_x_ = 0, look_y_ = RestY;
    int iris_left_ = -1, iris_top_ = -1, pupil_size_ = 36, step_ = 0;
    uint32_t ring_at_ = 0, next_blink_ = 0;
    bool awake_ = false, touched_ = false, ring_pending_ = false;
};
std::unique_ptr<PageView> make_third_eye(Context& c, lv_obj_t* p) { return std::make_unique<ThirdEyePage>(c, p); }
} // namespace badge::ui
