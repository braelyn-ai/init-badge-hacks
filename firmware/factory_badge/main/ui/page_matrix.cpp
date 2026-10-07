#include "hack_kit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace badge::ui {
namespace {
constexpr int CellW = 16, CellH = 22, Cols = 29, Rows = 21;
constexpr int Left = (Width - Cols * CellW) / 2, Top = (Height - Rows * CellH) / 2;
// The 26 px line box is taller than a cell; this offset drops the unused
// accent row above it so every glyph in Glyphs stays inside its own cell.
constexpr int GlyphDx = 2, GlyphDy = -3;
constexpr int Shades = 12, DropsPerColumn = 2;
constexpr float MinSpeed = 5, MaxSpeed = 13; // Rows per second.
// LVGL redraws the whole screen once more than 32 areas are pending, so the
// rain stalls instead of queueing more than this between two renders.
constexpr int AreaBudget = 24;
// No katakana in the fonts: digits, operators and Latin-1 oddities instead.
constexpr uint8_t Glyphs[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '<', '>', '=', '+', '*', ':', '"', '#', '%', '&', '?', 'Z', 'X',
    0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xab, 0xac, 0xb1, 0xb5, 0xb6, 0xbb, 0xbf,
    0xc6, 0xd0, 0xd7, 0xd8, 0xde, 0xdf, 0xe6, 0xf0, 0xf7, 0xf8, 0xfe,
};
constexpr const char* Lines[] = {"Wake up, %s...", "The Matrix has you...", "Follow the white rabbit."};
constexpr int LineCount = sizeof(Lines) / sizeof(Lines[0]);
const lv_font_t* const Font = &font_mono_20;

uint8_t random_glyph() { return Glyphs[hack::random_between(0, int(sizeof(Glyphs)) - 1)]; }
} // namespace

// Digital rain. Each column owns up to two drops falling at the column's own
// speed; a tap types the film's opening lines to the badge's owner.
class MatrixPage final : public PageView {
public:
    MatrixPage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        shades_[0] = lv_color_hex(0xe4ffe8);
        shades_[1] = lv_color_hex(0x6cff90);
        for (int i = 2; i < Shades; ++i) {
            const float t = float(i - 2) / (Shades - 3);
            shades_[i] = hack::scaled(lv_color_hex(0x22f050), 0.14f + 0.86f * std::pow(1.0f - t, 1.5f));
        }
        for (int c = 0; c < Cols; ++c) {
            auto& column = columns_[c];
            // Only cells whose centre is on the round panel exist.
            const int dx = Left + c * CellW + CellW / 2 - hack::Cx;
            for (int r = 0; r < Rows; ++r) {
                const int dy = Top + r * CellH + CellH / 2 - hack::Cy;
                if (dx * dx + dy * dy > (hack::ScreenRadius - 5) * (hack::ScreenRadius - 5)) continue;
                if (column.last < column.first) column.first = int8_t(r);
                column.last = int8_t(r);
                glyphs_[c][r] = random_glyph();
            }
            column.speed = MinSpeed + (MaxSpeed - MinSpeed) * hack::random_unit();
            // Start mid-storm rather than with a dry screen.
            for (int i = hack::random_between(20, 70); i > 0; --i) advance(c, false);
        }
        surface_ = hack::surface(root_, 0, 0, Width, Height,
                                 [this](lv_layer_t* layer, const lv_area_t& coords) { paint(layer, coords); });
        // Spans whole cells (columns 4-24, rows 8-12) so no glyph is half covered.
        subtitle_ = std::make_unique<hack::Subtitle>(root_, 66, 178, 336, 110, &font_mono_20, lv_color_hex(0x35ff6a));
        on_tap(root_, [this] { speak(); });
    }
    void update() override {
        if (!clock_.due()) return;
        subtitle_->update();
        if (painted_) { painted_ = false; pending_ = 0; }
        const float dt = clock_.dt();
        // Rotate the starting column so a tight budget starves none of them.
        start_ = (start_ + 7) % Cols;
        for (int i = 0; i < Cols; ++i) {
            const int c = (start_ + i) % Cols;
            auto& column = columns_[c];
            column.credit = std::min(column.credit + column.speed * dt, 1.5f);
            if (column.credit < 1 || pending_ + DropsPerColumn > AreaBudget) continue;
            column.credit -= 1;
            advance(c, true);
        }
    }
private:
    struct Drop { int16_t head = 0; uint8_t length = 0; }; // length 0: idle.
    struct Column {
        int8_t first = 0, last = -1; // Visible rows.
        uint8_t wait = 0, gap = 0;
        float speed = 0, credit = 0;
        std::array<Drop, DropsPerColumn> drops{};
    };
    // Moves the column's drops down one row and starts a new one when there is room.
    void advance(int c, bool invalidate) {
        auto& column = columns_[c];
        if (column.last < column.first) return;
        int room = Rows; // Topmost row still occupied by a trail.
        Drop* idle = nullptr;
        for (auto& drop : column.drops) {
            if (!drop.length) { idle = &drop; continue; }
            ++drop.head;
            if (drop.head <= column.last) glyphs_[c][drop.head] = random_glyph();
            if (hack::random_between(0, 2) == 0) {
                const int row = drop.head - hack::random_between(1, drop.length - 1);
                if (row >= column.first && row <= column.last) glyphs_[c][row] = random_glyph();
            }
            // One row above the tail is included so it is cleared.
            const int from = std::max<int>(column.first, drop.head - drop.length);
            const int to = std::min<int>(column.last, drop.head);
            for (int r = from; r <= to; ++r) {
                const int distance = drop.head - r;
                levels_[c][r] = distance >= drop.length ? 0
                    : uint8_t(1 + (distance < 2 ? distance : std::min(Shades - 1, 2 + (distance - 2) * (Shades - 2) / (drop.length - 2))));
            }
            if (invalidate && from <= to) {
                lv_area_t area;
                lv_obj_get_coords(surface_, &area);
                area.x1 += Left + c * CellW;
                area.x2 = area.x1 + CellW - 1;
                area.y2 = area.y1 + Top + (to + 1) * CellH - 1;
                area.y1 += Top + from * CellH;
                lv_obj_invalidate_area(surface_, &area);
                ++pending_;
            }
            if (drop.head - drop.length >= column.last) drop.length = 0;
            else room = std::min(room, drop.head - drop.length + 1);
        }
        if (!idle || room < column.first + column.gap) return;
        if (column.wait) { --column.wait; return; }
        idle->head = int16_t(column.first - 1);
        idle->length = uint8_t(hack::random_between(6, 17));
        column.gap = uint8_t(hack::random_between(2, 7));
        column.wait = uint8_t(hack::random_between(0, 5));
    }
    void paint(lv_layer_t* layer, const lv_area_t& coords) {
        painted_ = true;
        for (int c = 0; c < Cols; ++c) {
            const auto& column = columns_[c];
            const int x = coords.x1 + Left + c * CellW;
            if (column.last < column.first || !hack::needs_paint(layer, x, coords.y1, CellW, Height)) continue;
            for (int r = column.first; r <= column.last; ++r) {
                const int y = coords.y1 + Top + r * CellH;
                if (!levels_[c][r] || !hack::needs_paint(layer, x, y, CellW, CellH)) continue;
                hack::draw_glyph(layer, Font, glyphs_[c][r], x + GlyphDx, y + GlyphDy, shades_[levels_[c][r] - 1]);
            }
        }
    }
    void speak() {
        line_ = subtitle_->visible() ? (line_ + 1) % LineCount : 0;
        char text[96];
        std::snprintf(text, sizeof(text), Lines[line_], hack::first_name(context_, "Neo").c_str());
        subtitle_->say(text);
    }
    std::array<Column, Cols> columns_{};
    std::array<std::array<uint8_t, Rows>, Cols> glyphs_{}, levels_{}; // levels_: 0 is dark, else shade + 1.
    std::array<lv_color_t, Shades> shades_{};
    lv_obj_t* surface_ = nullptr;
    std::unique_ptr<hack::Subtitle> subtitle_;
    hack::FrameClock clock_{33};
    int pending_ = 0, start_ = 0, line_ = 0;
    bool painted_ = false;
};
std::unique_ptr<PageView> make_matrix(Context& c, lv_obj_t* p) { return std::make_unique<MatrixPage>(c, p); }
} // namespace badge::ui
