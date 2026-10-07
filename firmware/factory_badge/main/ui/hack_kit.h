#pragma once
#include "widgets.h"
#include <cstdint>
#include <functional>
#include <string>

// Shared pieces for the full-screen hack pages (Eyes, Radar, HAL, Third Eye,
// Matrix, Bit). Pages still receive everything through Context: no hardware,
// storage or radio access from here.
namespace badge::ui::hack {
constexpr int Cx = Width / 2, Cy = Height / 2;
constexpr int ScreenRadius = 233; // Visible round panel, centred on (Cx, Cy).

// --- Shapes built from native objects. None are clickable. ---
// A filled circle centred on (cx, cy) in the parent's coordinates.
lv_obj_t* disc(lv_obj_t* parent, int cx, int cy, int diameter, lv_color_t color);
// A hollow circle: outer diameter, band thickness measured inward.
lv_obj_t* ring(lv_obj_t* parent, int cx, int cy, int diameter, int thickness, lv_color_t color);
void set_fill(lv_obj_t* object, lv_color_t color); // Works for disc and ring.

// --- Colour ---
lv_color_t hsv(int hue, int saturation = 100, int value = 100); // Hue wraps; others 0..100.
lv_color_t scaled(lv_color_t color, float level);               // Brightness; clamps at white.
lv_color_t mix(lv_color_t from, lv_color_t to, float amount);   // amount 0..1.

// --- Time ---
// Paces update(), which the app calls every main-loop pass (about 200 Hz).
//   if (!clock_.due()) return;   then use clock_.dt() and clock_.seconds().
class FrameClock {
public:
    explicit FrameClock(uint32_t interval_ms = 33);
    bool due();                                          // True once per interval.
    float dt() const { return dt_; }                     // Seconds since the last due(), at most 0.1.
    uint32_t now() const { return now_; }                // lv_tick_get() at the last due().
    float seconds() const { return (now_ - start_) / 1000.0f; } // Since construction.
private:
    uint32_t interval_, start_, last_, now_;
    float dt_ = 0;
};

// --- Inputs from the model ---
// Gravity along the displayed screen's right/down axes in g. Straight down
// (0, 1) until the accelerometer reports.
UiMotion gravity(const Context& context);
// The owner's first name from the Badge page, or fallback when it is blank.
std::string first_name(const Context& context, const char* fallback);
float random_unit();                 // 0 <= value < 1.
int random_between(int low, int high); // Inclusive.

// --- Custom painting ---
// For shapes native widgets cannot make (polygons, glyph grids). The painter
// runs inside LVGL's render pass with the object's absolute coordinates; draw
// in those coordinates. Request a repaint with lv_obj_invalidate(object), or
// lv_obj_invalidate_area() for part of it. LVGL keeps at most 32 pending areas
// and does not merge neighbours: past that it repaints the whole screen. The painter is destroyed with the
// object. after_children paints over the object's children (e.g. eyelids).
using Painter = std::function<void(lv_layer_t* layer, const lv_area_t& coords)>;
void on_paint(lv_obj_t* object, Painter painter, bool after_children = false);
// A transparent, non-clickable object that exists only to be painted.
lv_obj_t* surface(lv_obj_t* parent, int x, int y, int width, int height, Painter painter);
// True when any part of the box is inside the region being rendered now.
// Use it to skip work in painters that cover many small cells.
bool needs_paint(const lv_layer_t* layer, int x, int y, int width, int height);
void fill_triangle(lv_layer_t* layer, float x0, float y0, float x1, float y1, float x2, float y2,
                   lv_color_t color, lv_opa_t opa = LV_OPA_COVER);
void fill_rect(lv_layer_t* layer, int x, int y, int width, int height,
               lv_color_t color, lv_opa_t opa = LV_OPA_COVER);
void fill_circle(lv_layer_t* layer, int cx, int cy, int radius, lv_color_t color, lv_opa_t opa = LV_OPA_COVER);
void draw_line(lv_layer_t* layer, float x0, float y0, float x1, float y1, int width,
               lv_color_t color, lv_opa_t opa = LV_OPA_COVER);
// One character with its top-left at (x, y). The design fonts cover U+0020-007E
// and U+00A0-00FF only.
void draw_glyph(lv_layer_t* layer, const lv_font_t* font, uint32_t codepoint, int x, int y,
                lv_color_t color, lv_opa_t opa = LV_OPA_COVER);

// --- Typed-out caption ---
// A translucent rounded panel whose text appears a character at a time, lingers,
// then hides itself. Call update() every frame.
class Subtitle {
public:
    Subtitle(lv_obj_t* parent, int x, int y, int width, int height,
             const lv_font_t* font = &font_sans_20, lv_color_t color = cream());
    void say(const std::string& text, uint32_t linger_ms = 3500);
    void hide();
    void update();
    bool typing() const { return typing_; }   // Characters are still appearing.
    bool visible() const { return visible_; }
private:
    lv_obj_t *panel_ = nullptr, *text_ = nullptr;
    std::string line_;
    size_t shown_ = 0;
    uint32_t started_ = 0, linger_ = 0;
    bool typing_ = false, visible_ = false;
};
} // namespace badge::ui::hack
