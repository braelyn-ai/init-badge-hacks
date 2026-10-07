#include "hack_kit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace badge::ui {
namespace {
using hack::Cx, hack::Cy;
constexpr int Range = 222;
constexpr int Trail = 6, Bands = 6, Slots = 24, BlipSize = 10;
constexpr float TrailStep = 4.0f;      // Degrees per afterglow wedge.
constexpr float Lead = 1.2f;           // Degrees of bright leading edge.
constexpr float DegreesPerSecond = 90; // One revolution every four seconds.
constexpr float Radians = 3.14159265f / 180.0f;
constexpr uint32_t RescanMs = 6000, RetryMs = 1000;

lv_color_t grid() { return lv_color_hex(0x0b4d1c); }
lv_color_t phosphor() { return lv_color_hex(0x35ff6a); }
lv_color_t echo() { return lv_color_hex(0xb4ffc4); }

// Stronger signals sit nearer the centre: -30 dBm at the hub, -95 dBm at the rim.
float range_for(int rssi) {
    const float t = std::clamp((-30.0f - rssi) / 65.0f, 0.0f, 1.0f);
    return 28.0f + t * (Range - 28 - BlipSize);
}
// Wi-Fi gives no direction, so each access point gets a stable made-up bearing.
float bearing_for(const std::array<uint8_t, 6>& id) {
    uint32_t hash = 2166136261u;
    for (uint8_t byte : id) hash = (hash ^ byte) * 16777619u;
    return float(hash % 360);
}
} // namespace

// A sweeping scope of nearby Wi-Fi access points from the receive-only scan:
// distance is signal strength, bearing is decorative.
class RadarPage final : public PageView {
public:
    RadarPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        // The device renders slowly, so the sweep is a few opaque wedges under the
        // grid, and each frame repaints only the boxes along the part that moved.
        sweep_ = hack::surface(root_, 0, 0, Width, Height, [this](lv_layer_t* layer, const lv_area_t&) { paint(layer); });
        for (int i = 1; i <= 4; ++i) hack::ring(root_, Cx, Cy, 2 * Range * i / 4, 2, grid());
        for (bool vertical : {false, true}) {
            auto* hair = vertical ? container(root_, Cx - 1, Cy - Range, 2, 2 * Range)
                                  : container(root_, Cx - Range, Cy - 1, 2 * Range, 2);
            lv_obj_remove_flag(hair, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(hair, grid(), 0);
            lv_obj_set_style_bg_opa(hair, LV_OPA_COVER, 0);
        }
        for (auto& blip : blips_) {
            blip.dot = hack::disc(root_, 0, 0, BlipSize, echo());
            lv_obj_set_style_bg_opa(blip.dot, LV_OPA_TRANSP, 0);
        }
        count_ = label(root_, "", 134, 366, 200, &font_mono_18, phosphor());
        detail_ = label(root_, "LISTENING...", 134, 392, 200, &font_mono_12, phosphor());
        next_request_ = lv_tick_get();
        revision_ = context_.model.radar_revision;
        contacts(); // Show the previous scan while the next one runs.
    }
    void update() override {
        const uint32_t now = lv_tick_get();
        const auto& model = context_.model;
        if (revision_ != model.radar_revision) {
            revision_ = model.radar_revision;
            heard_ = true;
            next_request_ = now + RescanMs;
            contacts();
        }
        if (!model.radar_scanning && int32_t(now - next_request_) >= 0) {
            next_request_ = now + RetryMs;
            if (context_.callbacks.radar_scan) context_.callbacks.radar_scan();
        }
        if (!clock_.due()) return;
        const float dt = clock_.dt();
        const float before = angle_;
        angle_ += DegreesPerSecond * dt;
        repaint(before - Trail * TrailStep - 1, angle_ + Lead + 1);
        angle_ = std::fmod(angle_, 360.0f);
        for (auto& blip : blips_) {
            int opa = 0;
            if (blip.live) {
                blip.range += (blip.target - blip.range) * std::min(1.0f, dt * 2.0f);
                const int x = Cx + int(std::lround(std::cos(blip.bearing * Radians) * blip.range)) - BlipSize / 2;
                const int y = Cy + int(std::lround(std::sin(blip.bearing * Radians) * blip.range)) - BlipSize / 2;
                if (x != blip.x || y != blip.y) { blip.x = x; blip.y = y; lv_obj_set_pos(blip.dot, x, y); }
                // Brightest as the sweep passes, then decays until the next pass.
                const float since = std::fmod(angle_ - blip.bearing + 360.0f, 360.0f);
                opa = (255 - int(since / 360.0f * 200.0f)) & ~7;
            }
            if (opa != blip.opa) { blip.opa = opa; lv_obj_set_style_bg_opa(blip.dot, lv_opa_t(opa), 0); }
        }
    }
private:
    struct Blip {
        lv_obj_t* dot = nullptr;
        std::array<uint8_t, 6> id{};
        float bearing = 0, range = 0, target = 0;
        int x = -1, y = -1, opa = 0;
        bool live = false, seen = false;
    };
    static void point(float degrees, float radius, float& x, float& y) {
        x = Cx + std::cos(degrees * Radians) * radius;
        y = Cy + std::sin(degrees * Radians) * radius;
    }
    void paint(lv_layer_t* layer) {
        float x0, y0, x1, y1;
        // Dimmest wedge first; neighbours overlap slightly so no seams show.
        for (int i = Trail - 1; i >= 0; --i) {
            const float fade = 1.0f - float(i) / Trail;
            point(angle_ - i * TrailStep, Range, x0, y0);
            point(angle_ - (i + 1) * TrailStep - 0.4f, Range, x1, y1);
            hack::fill_triangle(layer, Cx, Cy, x0, y0, x1, y1, hack::scaled(phosphor(), 0.05f + 0.5f * fade * fade));
        }
        point(angle_ + Lead, Range, x0, y0);
        point(angle_, Range, x1, y1);
        hack::fill_triangle(layer, Cx, Cy, x0, y0, x1, y1, echo());
    }
    // Invalidate the sector between two angles as a chain of small boxes, one
    // per radial band, instead of one box around the whole sector.
    void repaint(float from, float to) {
        const float middle = (from + to) / 2;
        for (int band = 0; band < Bands; ++band) {
            const float inner = float(Range) * band / Bands, outer = float(Range) * (band + 1) / Bands + 2;
            float x, y, left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
            for (float degrees : {from, middle, to}) for (float radius : {inner, outer}) {
                point(degrees, radius, x, y);
                left = std::min(left, x); right = std::max(right, x);
                top = std::min(top, y); bottom = std::max(bottom, y);
            }
            const lv_area_t area = {int(left) - 2, int(top) - 2, int(right) + 2, int(bottom) + 2};
            lv_obj_invalidate_area(sweep_, &area);
        }
    }
    void contacts() {
        const auto& heard = context_.model.radar;
        for (auto& blip : blips_) blip.seen = false;
        int nearest = -127;
        for (const auto& contact : heard) {
            nearest = std::max(nearest, int(contact.rssi));
            Blip* slot = nullptr;
            for (auto& blip : blips_) if (blip.live && blip.id == contact.bssid) { slot = &blip; break; }
            if (!slot) {
                for (auto& blip : blips_) if (!blip.live && !blip.seen) { slot = &blip; break; }
                if (!slot) continue; // More access points than the scope can hold.
                slot->id = contact.bssid;
                slot->bearing = bearing_for(contact.bssid);
                slot->range = range_for(contact.rssi);
                slot->live = true;
            }
            slot->seen = true;
            slot->target = range_for(contact.rssi);
        }
        for (auto& blip : blips_) if (!blip.seen) blip.live = false;
        char text[32];
        if (!heard_ && heard.empty()) return;
        std::snprintf(text, sizeof(text), "%d CONTACT%s", int(heard.size()), heard.size() == 1 ? "" : "S");
        set_text(count_, text);
        if (heard.empty()) std::snprintf(text, sizeof(text), "ALL QUIET");
        else std::snprintf(text, sizeof(text), "NEAREST %d dBm", nearest);
        set_text(detail_, text);
    }
    lv_obj_t* sweep_ = nullptr;
    std::array<Blip, Slots> blips_{};
    lv_obj_t *count_ = nullptr, *detail_ = nullptr;
    float angle_ = 270; // Start pointing up.
    hack::FrameClock clock_{60};
    uint32_t next_request_ = 0, revision_ = 0;
    bool heard_ = false;
};
std::unique_ptr<PageView> make_radar(Context& c, lv_obj_t* p) { return std::make_unique<RadarPage>(c, p); }
} // namespace badge::ui
