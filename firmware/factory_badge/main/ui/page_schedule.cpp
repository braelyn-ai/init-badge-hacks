#include "widgets.h"
#include "../schedule.h"
#include <array>

namespace badge::ui {
namespace {
constexpr int RowWidth = 273;
constexpr int ContentWidth = RowWidth - 32; // Fourteen-pixel padding plus the reserved border.
constexpr int ListTop = 100;
constexpr int ListHeight = 280;
// RGB565 maps #161616 to (16,20,16). Use equal representable channel levels.
constexpr uint32_t CardGray = 0x181818;
constexpr int FocusY = Height / 2; // Same vertical center as the navigation arrows.

lv_obj_t* wrapped_label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* text_object = label(parent, text, 0, 0, ContentWidth, font, color);
    lv_label_set_long_mode(text_object, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(text_object, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(text_object, LV_TEXT_ALIGN_LEFT, 0);
    return text_object;
}

struct ScheduleRow {
    lv_obj_t* box = nullptr;
    lv_obj_t* now = nullptr;
    badge_schedule::State state = badge_schedule::State::Unknown;
    bool styled = false;
};

// LVGL still owns the card, clipping, border, opacity and invalidation. These
// tiny native draw primitives cut the design's square eight-pixel corner steps.
void draw_notches(lv_event_t* event) {
    auto& row = *static_cast<ScheduleRow*>(lv_event_get_user_data(event));
    lv_area_t bounds;
    lv_obj_get_coords(row.box, &bounds);
    auto* layer = lv_event_get_layer(event);
    lv_draw_rect_dsc_t shape;
    lv_draw_rect_dsc_init(&shape);
    shape.bg_color = lv_color_black();
    shape.bg_opa = LV_OPA_COVER;
    auto rect = [&](int x, int y, int width, int height) {
        lv_area_t area = {x, y, x + width - 1, y + height - 1};
        lv_draw_rect(layer, &shape, &area);
    };
    const int xs[] = {bounds.x1, bounds.x2 - 7};
    const int ys[] = {bounds.y1, bounds.y2 - 7};
    for (int x : xs) for (int y : ys) rect(x, y, 8, 8);
    if (row.state != badge_schedule::State::OnNow) return;
    shape.bg_color = white();
    for (int side = 0; side < 2; ++side) {
        // The two-pixel outline follows the inside of the stepped silhouette;
        // none of its pixels may enter the transparent eight-pixel corners.
        const int x = side ? bounds.x2 - 9 : bounds.x1;
        const int vertical_x = side ? x : x + 8;
        rect(vertical_x, bounds.y1, 2, 10);
        rect(x, bounds.y1 + 8, 10, 2);
        rect(vertical_x, bounds.y2 - 9, 2, 10);
        rect(x, bounds.y2 - 9, 10, 2);
    }
}


}

class SchedulePage final : public PageView {
public:
    SchedulePage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        label(root_, "Schedule", 84, 52, 300, &font_sans_24, cream());
        list_ = container(root_, 98, ListTop, RowWidth, ListHeight);
        lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(list_, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
        // LVGL centers snap targets within the padded viewport. Keep the
        // design's clipping bounds, with its focus aligned to the arrows.
        lv_obj_set_style_pad_bottom(list_, ListHeight - 2 * (FocusY - ListTop), 0);
        lv_obj_set_scroll_snap_y(list_, LV_SCROLL_SNAP_CENTER);
        lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list_, 12, 0);

        for (size_t i = 0; i < badge_schedule::Items.size(); ++i) {
            const auto& item = badge_schedule::Items[i];
            auto& row = rows_[i];
            row.box = container(list_, 0, 0, RowWidth, 1);
            lv_obj_add_flag(row.box, LV_OBJ_FLAG_SNAPPABLE);
            lv_obj_set_height(row.box, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row.box, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_all(row.box, 12, 0);
            lv_obj_set_style_pad_left(row.box, 14, 0);
            lv_obj_set_style_pad_right(row.box, 14, 0);
            lv_obj_set_style_pad_bottom(row.box, 14, 0);
            lv_obj_set_style_pad_row(row.box, 0, 0);
            lv_obj_set_style_bg_opa(row.box, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(row.box, 0, 0);
            // Clock changes never change row geometry or discard
            // the attendee's current native scrolling position.
            lv_obj_set_style_border_width(row.box, 2, 0);
            lv_obj_set_style_border_color(row.box, white(), 0);
            lv_obj_add_event_cb(row.box, draw_notches, LV_EVENT_DRAW_MAIN_END, &row);
            auto* header = container(row.box, 0, 0, ContentWidth, 26);
            auto* time = label(header, item.time, 0, 0, 145, &font_mono_18, white());
            lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_LEFT, 0);
            row.now = label(header, "", ContentWidth - 82, 4, 82, &font_mono_12, muted());
            lv_obj_set_style_text_align(row.now, LV_TEXT_ALIGN_RIGHT, 0);
            auto* title = wrapped_label(row.box, item.title, &font_sans_24, white());
            lv_obj_set_style_margin_top(title, 3, 0);
            if (item.detail && item.detail[0]) {
                auto* detail = wrapped_label(row.box, item.detail, &font_mono_12, muted());
                lv_obj_set_style_margin_top(detail, 13, 0);
            }

        }
        label(root_, "Swipe up or down", 84, 394, 300, &font_mono_semibold_12, muted());
        update();
        lv_obj_update_layout(list_);
        const int current = context_.model.schedule_current;
        const int focus = current >= 0 && current < int(rows_.size()) &&
            rows_[current].state == badge_schedule::State::OnNow ? current : 0;
        // Native snapping also centers the first/last row beyond the normal
        // scroll bounds; no spacer widgets or custom gesture/animation code.
        lv_obj_scroll_to_view(rows_[focus].box, LV_ANIM_OFF);
    }

    void update() override {
        const int minute = context_.model.clock_valid ? context_.model.schedule_minute : -1;
        if (minute == minute_) return;
        minute_ = minute;
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& row = rows_[i];
            const auto state = badge_schedule::state(i, minute);
            if (row.styled && row.state == state) continue;
            row.state = state;
            row.styled = true;
            const bool current = state == badge_schedule::State::OnNow;
            set_text(row.now, current ? "On now" : "");
            lv_obj_set_style_border_opa(row.box, current ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            // The dimmed card also uses RGB565-aligned levels so opacity stays neutral.
            lv_obj_set_style_bg_color(row.box, lv_color_hex(state == badge_schedule::State::Passed ? 0x101010 : CardGray), 0);
            lv_obj_set_style_opa(row.box, state == badge_schedule::State::Passed ? LV_OPA_50 : LV_OPA_COVER, 0);
        }
    }
private:
    int minute_ = -2;
    lv_obj_t* list_ = nullptr;
    std::array<ScheduleRow, badge_schedule::Items.size()> rows_{};
};
std::unique_ptr<PageView> make_schedule(Context& c, lv_obj_t* p) { return std::make_unique<SchedulePage>(c, p); }
} // namespace badge::ui
