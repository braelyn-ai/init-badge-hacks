#include "hack_kit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace badge::ui {
namespace {
using hack::Cx, hack::Cy;
constexpr int OuterR = 203;  // Outer wall centreline; the rim beyond it holds the two labels.
constexpr int ChamberR = 42; // Innermost wall, around the goal.
constexpr int Wall = 4, GoalR = 18;
constexpr int MaxRings = 5, MaxDoors = 2, MaxBars = 3, MaxTraps = 2;
constexpr int StartDeg = 90; // Bottom of the outer corridor: where a hanging badge's marble rests anyway.
constexpr int BallBox = 26;
constexpr int Candidates = 12;
constexpr float Degrees = 180.0f / 3.14159265f;
constexpr float Unreachable = 1e9f;

// Feel. Distances in pixels, tilt in g.
constexpr float Pull = 1500.0f;    // Acceleration per g of tilt.
constexpr float DeadZone = 0.03f;  // Tilt ignored, so a badge lying flat keeps the marble still.
constexpr float MaxTilt = 0.6f;    // Steeper tilts pull no harder.
constexpr float Drag = 0.9f;       // Per second.
constexpr float Friction = 45.0f;  // Rolling resistance, pixels per second squared.
constexpr float MaxSpeed = 520.0f;
constexpr float Bounce = 0.3f, BounceAbove = 70.0f; // Slower hits just stop, so resting contact is still.
constexpr uint32_t FallMs = 380, WinMs = 2200;

lv_color_t board() { return lv_color_hex(0x0e151f); }
lv_color_t wall() { return lv_color_hex(0xf5f2e8); }
lv_color_t goal() { return lv_color_hex(0x35ff6a); }
lv_color_t danger() { return lv_color_hex(0xff4a3d); }

// Private generator: LVGL's starts from the same seed every boot, which would
// make the first maze always the same. Stirred with the tick at each new maze.
uint32_t seed = 0x9e3779b9u;
int pick(int low, int high) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return low + int(seed % uint32_t(high - low + 1));
}

int wrap(int deg) { deg %= 360; return deg < 0 ? deg + 360 : deg; }
int apart(int a, int b) { const int d = wrap(a - b); return d > 180 ? 360 - d : d; }

// Corridor c lies between wall c (outside) and wall c + 1; wall 0 is the solid
// rim and the last wall encloses the goal. doors[c] are the gaps in wall c + 1.
// Angles are whole degrees, clockwise from screen right, as LVGL draws arcs.
struct Maze {
    int rings = 3, ball = 12;
    int doors[MaxRings][MaxDoors] = {}, door_count[MaxRings] = {}, half[MaxRings] = {};
    int bars[MaxRings][MaxBars] = {}, bar_count[MaxRings] = {};
    struct Trap { int ring, deg; float radius, capture; } traps[MaxTraps] = {};
    int trap_count = 0;
    int radius(int k) const { return OuterR - int(std::lround(float(k) * (OuterR - ChamberR) / rings)); }
    float mid(int c) const { return (radius(c) + radius(c + 1)) / 2.0f; }
    // Shortest way round corridor c between two angles that crosses no barrier, in degrees.
    float travel(int c, int a, int b) const {
        const int forward = wrap(b - a), back = 360 - forward;
        bool clear_forward = true, clear_back = forward != 0;
        for (int i = 0; i < bar_count[c]; ++i) {
            if (wrap(bars[c][i] - a) < forward) clear_forward = false;
            if (wrap(a - bars[c][i]) < back) clear_back = false;
        }
        float best = Unreachable;
        if (clear_forward) best = float(forward);
        if (clear_back && back < best) best = float(back);
        return best;
    }
};

constexpr int Nodes = 1 + MaxRings * MaxDoors; // The start, then every door.
// Length in pixels of the shortest route from the start to the goal (Dijkstra
// over the doors), or Unreachable. This is what guarantees every maze is solvable.
float solve(const Maze& m) {
    float distance[Nodes];
    bool done[Nodes] = {};
    for (auto& d : distance) d = Unreachable;
    distance[0] = 0;
    const float width = float(OuterR - ChamberR) / m.rings;
    auto exists = [&](int n) { return n == 0 || ((n - 1) / MaxDoors < m.rings && (n - 1) % MaxDoors < m.door_count[(n - 1) / MaxDoors]); };
    auto angle = [&](int n) { return n == 0 ? StartDeg : m.doors[(n - 1) / MaxDoors][(n - 1) % MaxDoors]; };
    auto touches = [&](int n, int c) { return n == 0 ? c == 0 : (c == (n - 1) / MaxDoors || c == (n - 1) / MaxDoors + 1); };
    for (;;) {
        int u = -1;
        for (int n = 0; n < Nodes; ++n) if (!done[n] && distance[n] < Unreachable && (u < 0 || distance[n] < distance[u])) u = n;
        if (u < 0) return Unreachable;
        done[u] = true;
        if (u > 0 && (u - 1) / MaxDoors == m.rings - 1) return distance[u] + width;
        for (int v = 1; v < Nodes; ++v) {
            if (done[v] || !exists(v)) continue;
            for (int c = 0; c < m.rings; ++c) {
                if (!touches(u, c) || !touches(v, c)) continue;
                const float arc = m.travel(c, angle(u), angle(v));
                if (arc >= Unreachable) continue;
                const float total = distance[u] + arc / Degrees * m.mid(c) + width;
                distance[v] = std::min(distance[v], total);
            }
        }
    }
}

int rings_for(int level) { return std::min(3 + (level - 1) / 2, MaxRings); }
int bars_for(int level) { return level == 1 ? 1 : level < 4 ? 2 : MaxBars; }
int traps_for(int level) { return level < 4 ? 0 : level < 7 ? 1 : MaxTraps; }

Maze candidate(int level) {
    Maze m;
    m.rings = rings_for(level);
    m.ball = m.rings <= 4 ? 12 : 10;
    for (int c = 0; c < m.rings; ++c) {
        // A door clears the marble by 12 px between the rounded wall ends.
        m.half[c] = int(std::ceil((m.ball + 6 + Wall / 2.0f) / m.radius(c + 1) * Degrees));
        m.door_count[c] = c == m.rings - 1 ? 1 : 2;
        m.doors[c][0] = pick(0, 359);
        m.doors[c][1] = wrap(m.doors[c][0] + pick(80, 280));
    }
    for (int c = 0; c < m.rings; ++c) {
        const int budget = c == m.rings - 1 ? std::min(bars_for(level), 2) : bars_for(level);
        // Barriers stay a marble and a bit apart, measured on the corridor's inner wall.
        const int spacing = std::max(40, int(std::ceil((2 * m.ball + Wall + 12) / float(m.radius(c + 1)) * Degrees)));
        for (int tries = budget * 6; tries > 0 && m.bar_count[c] < budget; --tries) {
            const int deg = pick(0, 359);
            bool fits = c > 0 || apart(deg, StartDeg) >= 14;
            // Barriers meet solid wall at both ends, never a doorway.
            for (int side = c - 1; side <= c; ++side) {
                if (side < 0) continue;
                for (int i = 0; i < m.door_count[side]; ++i) if (apart(deg, m.doors[side][i]) < m.half[side] + 2) fits = false;
            }
            for (int i = 0; i < m.bar_count[c]; ++i) if (apart(deg, m.bars[c][i]) < spacing) fits = false;
            if (!fits) continue;
            m.bars[c][m.bar_count[c]++] = deg;
            if (solve(m) >= Unreachable) --m.bar_count[c]; // It would seal the route: leave it out.
        }
    }
    return m;
}

// Trap holes hug one wall, so the marble passes by keeping to the other; they
// never touch solvability.
void add_traps(Maze& m, int count) {
    for (int tries = 60; tries > 0 && m.trap_count < count; --tries) {
        const int c = pick(0, m.rings - 1), deg = pick(0, 359);
        const float half_clear = (m.radius(c) - m.radius(c + 1) - Wall) / 2.0f;
        // Room to pick a side after coming through a door or round a barrier.
        const int room = int(std::ceil((2 * m.ball + 14) / m.mid(c) * Degrees));
        bool fits = c > 0 || apart(deg, StartDeg) >= room + 10;
        for (int side = c - 1; side <= c; ++side) {
            if (side < 0) continue;
            for (int i = 0; i < m.door_count[side]; ++i) if (apart(deg, m.doors[side][i]) < m.half[side] + room) fits = false;
        }
        for (int i = 0; i < m.bar_count[c]; ++i) if (apart(deg, m.bars[c][i]) < room) fits = false;
        for (int i = 0; i < m.trap_count; ++i) if (m.traps[i].ring == c && apart(deg, m.traps[i].deg) < 2 * room) fits = false;
        if (!fits) continue;
        const float hole = m.ball - 2.0f, offset = half_clear - hole - 2;
        auto& trap = m.traps[m.trap_count++];
        trap.ring = c; trap.deg = deg;
        trap.radius = m.mid(c) + (pick(0, 1) ? offset : -offset);
        // A marble rolling along the far wall clears the capture circle by 4 px.
        trap.capture = std::min(0.6f * hole, half_clear - m.ball - 0.5f + offset - 4.0f);
    }
}

// The longest-winded of a few random mazes, so the route is never a straight shot.
Maze generate(int level) {
    seed ^= lv_tick_get() * 2654435761u;
    if (!seed) seed = 1;
    Maze best;
    float longest = -1;
    for (int i = 0; i < Candidates; ++i) {
        const Maze m = candidate(level);
        const float length = solve(m);
        if (length < Unreachable && length > longest) { longest = length; best = m; }
    }
    add_traps(best, traps_for(level));
    return best;
}

void dot(lv_layer_t* layer, float x, float y, float radius, lv_color_t color) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.radius = LV_RADIUS_CIRCLE;
    const int size = std::max(2, int(std::lround(radius * 2)));
    const int left = int(std::lround(x - radius)), top = int(std::lround(y - radius));
    const lv_area_t area = {left, top, left + size - 1, top + size - 1};
    lv_draw_rect(layer, &dsc, &area);
}
} // namespace

// A ball-in-a-maze toy: tilt the badge to roll the marble through concentric
// rings to the hole in the middle. Tap restarts the level.
class LabyrinthPage final : public PageView {
public:
    LabyrinthPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        hack::disc(root_, Cx, Cy, 2 * OuterR, board());
        // The maze is painted once; rolling repaints only the marble's box, and
        // the painter skips every wall that box cannot touch.
        maze_ = hack::surface(root_, 0, 0, Width, Height, [this](lv_layer_t* layer, const lv_area_t& coords) { paint(layer, coords); });
        ball_ = hack::surface(root_, 0, 0, BallBox, BallBox, [this](lv_layer_t* layer, const lv_area_t& coords) { paint_ball(layer, coords); });
        level_text_ = label(root_, "", Cx - 55, 5, 110, &font_mono_18, muted());
        status_ = label(root_, "", Cx - 52, 438, 104, &font_mono_18, cream());
        won_ = hack::disc(root_, Cx, Cy, 216, lv_color_black());
        hack::ring(won_, 108, 108, 216, 3, goal());
        won_level_ = label(won_, "", 18, 50, 180, &font_mono_18, muted());
        won_time_ = label(won_, "", 18, 84, 180, &font_sans_32, cream());
        label(won_, "SOLVED", 18, 138, 180, &font_mono_18, goal());
        set_hidden(won_, true);
        on_tap(root_, [this] { tapped(); });
        level_ = context_.model.labyrinth_finished + 1; // Resume after the last level finished.
        build();
    }
    void update() override {
        if (!clock_.due()) return;
        const uint32_t now = clock_.now();
        const float dt = std::min(clock_.dt(), 0.05f);
        switch (phase_) {
        case Phase::Ready:
        case Phase::Rolling: roll(dt, now); break;
        case Phase::Sinking:
        case Phase::Dropping: {
            const float t = std::min(1.0f, float(now - since_) / FallMs);
            x_ = from_x_ + (hole_x_ - from_x_) * std::min(1.0f, t * 2.5f);
            y_ = from_y_ + (hole_y_ - from_y_) * std::min(1.0f, t * 2.5f);
            scale_ = 1.0f - 0.85f * t * t;
            lv_obj_invalidate(ball_);
            place();
            if (t < 1.0f) break;
            if (phase_ == Phase::Dropping) { spawn(); phase_ = Phase::Rolling; break; }
            char text[24];
            std::snprintf(text, sizeof(text), "LEVEL %d", level_);
            set_text(won_level_, text);
            std::snprintf(text, sizeof(text), "%d.%d s", int(elapsed_ / 1000), int(elapsed_ / 100 % 10));
            set_text(won_time_, text);
            set_hidden(ball_, true);
            set_hidden(won_, false);
            phase_ = Phase::Won; since_ = now;
            if (context_.callbacks.labyrinth_finished) context_.callbacks.labyrinth_finished(level_);
            break;
        }
        case Phase::Won:
            if (now - since_ >= WinMs) next();
            break;
        }
    }
private:
    enum class Phase { Ready, Rolling, Sinking, Dropping, Won };
    struct Arc { float radius; int from, span; float ends[2][2]; int left, top, right, bottom; };
    struct Bar { float ux, uy, inner, outer; int left, top, right, bottom; };

    void tapped() {
        if (phase_ == Phase::Won) { next(); return; }
        spawn();
        phase_ = Phase::Ready;
        show_status();
    }
    void next() {
        ++level_;
        set_hidden(won_, true);
        set_hidden(ball_, false);
        build();
    }
    void build() {
        maze_data_ = generate(level_);
        const Maze& m = maze_data_;
        arc_count_ = bar_count_ = 0;
        add_arc(float(OuterR), 0, 360);
        for (int c = 0; c < m.rings; ++c) {
            const int n = m.door_count[c];
            int doors[MaxDoors];
            std::copy(m.doors[c], m.doors[c] + n, doors);
            std::sort(doors, doors + n);
            for (int i = 0; i < n; ++i) {
                const int from = doors[i] + m.half[c], to = doors[(i + 1) % n] - m.half[c];
                add_arc(float(m.radius(c + 1)), wrap(from), n == 1 ? 360 - 2 * m.half[c] : wrap(to - from));
            }
            for (int i = 0; i < m.bar_count[c]; ++i) {
                auto& bar = bars_[bar_count_++];
                bar.ux = std::cos(m.bars[c][i] / Degrees); bar.uy = std::sin(m.bars[c][i] / Degrees);
                bar.inner = float(m.radius(c + 1)); bar.outer = float(m.radius(c));
                const float x0 = bar.ux * bar.inner, x1 = bar.ux * bar.outer, y0 = bar.uy * bar.inner, y1 = bar.uy * bar.outer;
                bar.left = int(std::min(x0, x1)) - 4; bar.right = int(std::max(x0, x1)) + 4;
                bar.top = int(std::min(y0, y1)) - 4; bar.bottom = int(std::max(y0, y1)) + 4;
            }
        }
        for (int i = 0; i < m.trap_count; ++i) {
            trap_x_[i] = std::cos(m.traps[i].deg / Degrees) * m.traps[i].radius;
            trap_y_[i] = std::sin(m.traps[i].deg / Degrees) * m.traps[i].radius;
        }
        radius_ = float(m.ball);
        reach_ = radius_ + Wall / 2.0f + 0.5f;
        char text[16];
        std::snprintf(text, sizeof(text), "LEVEL %d", level_);
        set_text(level_text_, text);
        spawn();
        phase_ = Phase::Ready;
        show_status();
        lv_obj_invalidate(maze_);
    }
    void add_arc(float radius, int from, int span) {
        auto& arc = arcs_[arc_count_++];
        arc.radius = radius; arc.from = from; arc.span = span;
        float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
        for (int deg = from;; deg = std::min(deg + 10, from + span)) {
            const float x = std::cos(deg / Degrees) * radius, y = std::sin(deg / Degrees) * radius;
            left = std::min(left, x); right = std::max(right, x);
            top = std::min(top, y); bottom = std::max(bottom, y);
            if (deg == from) { arc.ends[0][0] = x; arc.ends[0][1] = y; }
            if (deg == from + span) { arc.ends[1][0] = x; arc.ends[1][1] = y; break; }
        }
        arc.left = int(left) - 5; arc.top = int(top) - 5; arc.right = int(right) + 5; arc.bottom = int(bottom) + 5;
    }
    void spawn() {
        x_ = 0; y_ = maze_data_.mid(0);
        vx_ = vy_ = 0;
        scale_ = 1.0f;
        lv_obj_invalidate(ball_);
        place();
    }
    void place() {
        const int left = Cx + int(std::lround(x_)) - BallBox / 2, top = Cy + int(std::lround(y_)) - BallBox / 2;
        if (left == left_ && top == top_) return;
        left_ = left; top_ = top;
        lv_obj_set_pos(ball_, left, top);
    }
    void show_status() {
        char text[16];
        if (upright_ || phase_ == Phase::Ready) std::snprintf(text, sizeof(text), "%s", upright_ ? "HOLD FLAT" : "TILT ME");
        else std::snprintf(text, sizeof(text), "%d.%d", int(elapsed_ / 1000), int(elapsed_ / 100 % 10));
        set_text(status_, text);
    }
    void roll(float dt, uint32_t now) {
        const auto pull = hack::gravity(context_);
        const float tilt = std::sqrt(pull.x * pull.x + pull.y * pull.y);
        upright_ = tilt > (upright_ ? 0.65f : 0.8f); // Hanging on the lanyard rather than lying in a hand.
        const float gain = tilt < DeadZone ? 0.0f : (std::min(tilt, MaxTilt) - DeadZone) / tilt * Pull;
        // Short substeps, at most ~1.5 px each, so a fast marble cannot skip a 4 px wall.
        const float speed = std::sqrt(vx_ * vx_ + vy_ * vy_) + Pull * MaxTilt * dt;
        const int steps = std::clamp(int(speed * dt / 1.5f) + 1, 1, 24);
        const float h = dt / steps;
        for (int i = 0; i < steps && phase_ <= Phase::Rolling; ++i) {
            vx_ += pull.x * gain * h; vy_ += pull.y * gain * h;
            const float keep = 1.0f - Drag * h;
            vx_ *= keep; vy_ *= keep;
            const float now_speed = std::sqrt(vx_ * vx_ + vy_ * vy_);
            if (now_speed <= Friction * h) vx_ = vy_ = 0;
            else {
                const float scale = (std::min(now_speed, MaxSpeed + Friction * h) - Friction * h) / now_speed;
                vx_ *= scale; vy_ *= scale;
            }
            x_ += vx_ * h; y_ += vy_ * h;
            collide(); collide(); // Twice: the second pass settles corners.
            holes(now);
        }
        place();
        if (phase_ == Phase::Ready && std::fabs(y_ - maze_data_.mid(0)) + std::fabs(x_) > 6) { phase_ = Phase::Rolling; started_ = now; }
        if (phase_ == Phase::Rolling) elapsed_ = now - started_;
        if (phase_ <= Phase::Rolling) show_status();
    }
    void holes(uint32_t now) {
        if (x_ * x_ + y_ * y_ < float((GoalR - 4) * (GoalR - 4))) { fall(0, 0, Phase::Sinking, now); return; }
        for (int i = 0; i < maze_data_.trap_count; ++i) {
            const float dx = x_ - trap_x_[i], dy = y_ - trap_y_[i], capture = maze_data_.traps[i].capture;
            if (dx * dx + dy * dy < capture * capture) { fall(trap_x_[i], trap_y_[i], Phase::Dropping, now); return; }
        }
    }
    void fall(float x, float y, Phase phase, uint32_t now) {
        if (phase_ == Phase::Ready) { started_ = now; elapsed_ = 0; }
        phase_ = phase; since_ = now;
        from_x_ = x_; from_y_ = y_; hole_x_ = x; hole_y_ = y;
    }
    // Push the marble out of whatever it overlaps and cancel its speed into it.
    void collide() {
        float distance = std::sqrt(x_ * x_ + y_ * y_);
        for (int i = 0; i < arc_count_; ++i) {
            const auto& arc = arcs_[i];
            if (std::fabs(distance - arc.radius) >= reach_) continue;
            float along = std::fmod(std::atan2(y_, x_) * Degrees - arc.from + 720.0f, 360.0f);
            if (arc.span >= 360 || along <= arc.span) {
                const float side = distance > arc.radius ? 1.0f : -1.0f;
                const float nx = x_ / distance, ny = y_ / distance;
                x_ = nx * (arc.radius + side * reach_); y_ = ny * (arc.radius + side * reach_);
                rebound(nx * side, ny * side);
            } else {
                const auto& end = arc.ends[along - arc.span < 360.0f - along ? 1 : 0];
                touch(end[0], end[1]);
            }
            distance = std::sqrt(x_ * x_ + y_ * y_);
        }
        for (int i = 0; i < bar_count_; ++i) {
            const auto& bar = bars_[i];
            if (std::fabs(x_ * bar.uy - y_ * bar.ux) >= reach_) continue;
            const float along = x_ * bar.ux + y_ * bar.uy;
            if (along < bar.inner - reach_ || along > bar.outer + reach_) continue;
            const float nearest = std::clamp(along, bar.inner, bar.outer);
            touch(bar.ux * nearest, bar.uy * nearest);
        }
    }
    void touch(float px, float py) {
        const float dx = x_ - px, dy = y_ - py, distance = std::sqrt(dx * dx + dy * dy);
        if (distance >= reach_ || distance < 1e-3f) return;
        x_ = px + dx / distance * reach_; y_ = py + dy / distance * reach_;
        rebound(dx / distance, dy / distance);
    }
    void rebound(float nx, float ny) {
        const float into = vx_ * nx + vy_ * ny;
        if (into >= 0) return;
        const float k = (-into > BounceAbove ? 1.0f + Bounce : 1.0f) * into;
        vx_ -= k * nx; vy_ -= k * ny;
    }

    // True when the region being rendered could touch a circle of this radius.
    static bool crosses(const lv_layer_t* layer, int ox, int oy, float radius) {
        const auto& clip = layer->_clip_area;
        const int x1 = int(clip.x1) - ox, x2 = int(clip.x2) - ox, y1 = int(clip.y1) - oy, y2 = int(clip.y2) - oy;
        const int inside_x = x1 > 0 ? x1 : x2 < 0 ? -x2 : 0, inside_y = y1 > 0 ? y1 : y2 < 0 ? -y2 : 0;
        const int outside_x = std::max(std::abs(x1), std::abs(x2)), outside_y = std::max(std::abs(y1), std::abs(y2));
        const float low = radius - Wall, high = radius + Wall;
        return float(inside_x * inside_x + inside_y * inside_y) <= high * high
            && float(outside_x * outside_x + outside_y * outside_y) >= low * low;
    }
    void paint(lv_layer_t* layer, const lv_area_t& coords) {
        const int ox = int(coords.x1) + Cx, oy = int(coords.y1) + Cy;
        const Maze& m = maze_data_;
        if (hack::needs_paint(layer, ox - GoalR - 4, oy - GoalR - 4, 2 * GoalR + 8, 2 * GoalR + 8)) {
            dot(layer, float(ox), float(oy), GoalR + 3, goal());
            dot(layer, float(ox), float(oy), GoalR, lv_color_black());
        }
        for (int i = 0; i < m.trap_count; ++i) {
            const float x = ox + trap_x_[i], y = oy + trap_y_[i], hole = m.ball - 2.0f;
            if (!hack::needs_paint(layer, int(x) - 14, int(y) - 14, 28, 28)) continue;
            dot(layer, x, y, hole + 2, danger());
            dot(layer, x, y, hole, lv_color_black());
        }
        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.color = wall();
        dsc.width = Wall;
        dsc.center.x = ox; dsc.center.y = oy;
        for (int i = 0; i < arc_count_; ++i) {
            const auto& arc = arcs_[i];
            if (!hack::needs_paint(layer, ox + arc.left, oy + arc.top, arc.right - arc.left + 1, arc.bottom - arc.top + 1)) continue;
            if (!crosses(layer, ox, oy, arc.radius)) continue;
            const bool full = arc.span >= 360;
            dsc.radius = uint16_t(int(arc.radius) + Wall / 2);
            dsc.start_angle = full ? 0 : arc.from;
            dsc.end_angle = full ? 360 : wrap(arc.from + arc.span);
            dsc.rounded = !full;
            lv_draw_arc(layer, &dsc);
        }
        for (int i = 0; i < bar_count_; ++i) {
            const auto& bar = bars_[i];
            if (!hack::needs_paint(layer, ox + bar.left, oy + bar.top, bar.right - bar.left + 1, bar.bottom - bar.top + 1)) continue;
            hack::draw_line(layer, ox + bar.ux * bar.inner, oy + bar.uy * bar.inner, ox + bar.ux * bar.outer, oy + bar.uy * bar.outer, Wall, wall());
        }
    }
    void paint_ball(lv_layer_t* layer, const lv_area_t& coords) {
        const float x = float(coords.x1) + BallBox / 2, y = float(coords.y1) + BallBox / 2, r = radius_ * scale_;
        dot(layer, x, y, r, lv_color_hex(0x59626d));
        if (r < 4) return;
        dot(layer, x - r * 0.12f, y - r * 0.12f, r * 0.8f, lv_color_hex(0xaeb7c1));
        dot(layer, x - r * 0.36f, y - r * 0.38f, r * 0.3f, white());
    }

    lv_obj_t *maze_ = nullptr, *ball_ = nullptr, *level_text_ = nullptr, *status_ = nullptr;
    lv_obj_t *won_ = nullptr, *won_level_ = nullptr, *won_time_ = nullptr;
    Maze maze_data_;
    std::array<Arc, 1 + MaxRings * MaxDoors> arcs_{};
    std::array<Bar, MaxRings * MaxBars> bars_{};
    float trap_x_[MaxTraps] = {}, trap_y_[MaxTraps] = {};
    int arc_count_ = 0, bar_count_ = 0;
    int level_ = 1, left_ = -1, top_ = -1;
    float x_ = 0, y_ = 0, vx_ = 0, vy_ = 0, radius_ = 12, reach_ = 14.5f, scale_ = 1;
    float from_x_ = 0, from_y_ = 0, hole_x_ = 0, hole_y_ = 0;
    Phase phase_ = Phase::Ready;
    uint32_t started_ = 0, elapsed_ = 0, since_ = 0;
    bool upright_ = false;
    hack::FrameClock clock_{16};
};
std::unique_ptr<PageView> make_labyrinth(Context& c, lv_obj_t* p) { return std::make_unique<LabyrinthPage>(c, p); }
} // namespace badge::ui
