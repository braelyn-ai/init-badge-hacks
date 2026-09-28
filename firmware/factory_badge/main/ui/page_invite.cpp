#include "widgets.h"
#include "intro_loop.h"
#include "../morse_unlock.h"
#include <algorithm>
#include <cstdlib>

namespace badge::ui {
class InvitePage final : public PageView {
public:
    InvitePage(Context& context, lv_obj_t* parent) : PageView(context, parent) {
        title_ = label(root_, "Developers", 54, 78, 360, &font_sans_32, cream());
        subtitle_ = label(root_, "After Dark", 54, 120, 360, &font_sans_32, cream());
        lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(root_, panel(), LV_STATE_PRESSED);
        prompt_ = label(root_, "", 80, 216, 308, &font_sans_24, white());
        lv_label_set_long_mode(prompt_, LV_LABEL_LONG_WRAP);
        input_display_ = container(root_, 66, 100, 336, 86);
        lv_obj_remove_flag(input_display_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(input_display_, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(input_display_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(input_display_, 24, 0);
        lv_obj_set_style_pad_row(input_display_, 10, 0);
        set_hidden(input_display_, true);
        footer_ = label(root_, "", 64, 407, 340, &font_mono_semibold_12, muted());
        invitation_qr_ = qr(root_, "https://luma.com/developers-after-dark", 134, 166, 200, 24);
        // Ordinary on_tap rejects long presses. This surface instead feeds
        // native press durations to the existing bounded Morse recognizer.
        // Non-clickable labels pass through to the whole page; the separate
        // chrome layer retains its native previous/next controls.
        lv_obj_add_event_cb(root_, [](lv_event_t* event) {
            const auto code = lv_event_get_code(event);
            if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
                code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST &&
                code != LV_EVENT_GESTURE && code != LV_EVENT_SCROLL_BEGIN) return;
            // In particular, deletion occurs from PageView's destructor after
            // InvitePage members are gone; never dereference them for DELETE.
            auto& self = *static_cast<InvitePage*>(lv_event_get_user_data(event));
            if (lv_event_get_target(event) != self.root_) return;
            self.contact(event);
        }, LV_EVENT_ALL, this);
        // A pusher can enter this page while a finger is already held. That
        // old contact must lift before it can become the first code symbol.
        for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
            if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER &&
                lv_indev_get_display(input) == lv_obj_get_display(root_) &&
                lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED)
                lv_indev_wait_release(input);
        }
        update();
    }
    ~InvitePage() override {
        set_pressed_feedback(false);
        lv_anim_delete(this, nullptr);
    }

    void cancel_input() override { cancel(); }
    void update() override {
        const bool locked = !context_.model.after_dark_unlocked;
        if (!locked || context_.setup || context_.touch_test || context_.reset) cancel();
        else sample(); // Observe letter spacing and the idle restart deadline.
        const bool revealed = context_.model.after_dark_unlocked;
        if (!revealing())
            set_text(prompt_, revealed ? "You're invited" : "tap the code to reveal a secret invitation");
        set_text(footer_, revealed ? "Scan to RSVP" : "");
        set_hidden(invitation_qr_, !revealed || revealing());
        if (displayed_state_ != int(revealed)) {
            const bool code_accepted = revealed && submitted_ && displayed_state_ == 0;
            displayed_state_ = int(revealed);
            set_hidden(title_, !revealed);
            set_hidden(subtitle_, !revealed);
            lv_obj_set_y(prompt_, revealed ? 374 : 216);
            if (revealed) lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
            else lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
            if (code_accepted) celebrate();
        }
        if (celebration_done_) {
            // READY runs inside LVGL's decoder; release its object and timer
            // here, after that callback has returned to the ordinary UI tick.
            lv_obj_delete(celebration_);
            celebration_ = nullptr;
            celebration_done_ = false;
        }
    }
private:
    void cancel() {
        held_ = false;
        armed_ = false;
        set_pressed_feedback(false);
        morse_.reset();
        clear_feedback();
        lv_obj_remove_state(root_, LV_STATE_PRESSED);
    }
    void set_pressed_feedback(bool pressed) {
        if (pressed_feedback_ == pressed) return;
        pressed_feedback_ = pressed;
        if (context_.callbacks.morse_pressed) context_.callbacks.morse_pressed(pressed);
    }
    void render_input(const char* text) {
        if (displayed_input_ == text) return;
        displayed_input_ = text;
        lv_obj_clean(input_display_);
        set_hidden(input_display_, displayed_input_.empty());
        if (displayed_input_.empty()) return;
        // LVGL lays out letter groups and wraps long incorrect attempts. These
        // visual-only children let contacts pass through to the page surface.
        lv_obj_t* group = nullptr;
        int group_width = 0;
        for (char symbol : displayed_input_) {
            if (!group || symbol == ' ') {
                group = container(input_display_, 0, 0, 24, LV_SIZE_CONTENT);
                group_width = 0;
                lv_obj_remove_flag(group, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_min_height(group, 8, 0);
                lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW_WRAP);
                lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_column(group, 8, 0);
                lv_obj_set_style_pad_row(group, 10, 0);
                if (symbol == ' ') {
                    // Reserve the next letter's space as soon as the pause
                    // registers, before its first symbol is released.
                    lv_obj_set_style_min_width(group, 24, 0);
                    continue;
                }
            }
            auto* mark = container(group, 0, 0, symbol == '.' ? 8 : 24, 8);
            lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(mark, white(), 0);
            lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(mark, symbol == '.' ? LV_RADIUS_CIRCLE : 0, 0);
            group_width += (group_width ? 8 : 0) + (symbol == '.' ? 8 : 24);
            // Flex needs a bounded width to wrap; content-sized width would
            // expand beyond the display even with a max-width style.
            lv_obj_set_width(group, std::min(group_width, 336));
        }
    }
    void clear_feedback() {
        if (!restarting_ && displayed_input_.empty()) return;
        lv_anim_delete(this, restart_feedback);
        restarting_ = false;
        lv_obj_set_style_translate_x(input_display_, 0, 0);
        lv_obj_set_style_opa(input_display_, LV_OPA_COVER, 0);
        render_input("");
    }
    void restart_feedback_animation() {
        restarting_ = true;
        lv_anim_t feedback;
        lv_anim_init(&feedback);
        lv_anim_set_var(&feedback, this);
        lv_anim_set_exec_cb(&feedback, restart_feedback);
        lv_anim_set_values(&feedback, 0, 380);
        lv_anim_set_duration(&feedback, 380);
        lv_anim_start(&feedback);
    }
    static void restart_feedback(void* view, int32_t progress) {
        auto& self = *static_cast<InvitePage*>(view);
        // One native animation shakes for 220 ms, then fades for 160 ms.
        constexpr int offsets[] = {0, -8, 8, -6, 6, 0};
        int x = 0;
        if (progress < 220) {
            const int step = progress / 44;
            x = offsets[step] + (offsets[step + 1] - offsets[step]) * (progress % 44) / 44;
        }
        lv_obj_set_style_translate_x(self.input_display_, x, 0);
        lv_obj_set_style_opa(self.input_display_, progress <= 220 ? int(LV_OPA_COVER) : (380 - progress) * 255 / 160, 0);
        if (progress == 380) {
            self.restarting_ = false;
            self.render_input("");
            lv_obj_set_style_opa(self.input_display_, LV_OPA_COVER, 0);
        }
    }
    void celebrate() {
        celebration_ = lv_gif_create(root_);
        lv_gif_set_color_format(celebration_, LV_COLOR_FORMAT_RGB565);
        lv_gif_set_src(celebration_, &intro_loop);
        lv_gif_set_loop_count(celebration_, 1);
        lv_obj_center(celebration_);
        lv_obj_move_to_index(celebration_, 0);
        lv_obj_remove_flag(celebration_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(celebration_, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(celebration_, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(celebration_, [](lv_event_t* event) {
            static_cast<InvitePage*>(lv_event_get_user_data(event))->celebration_done_ = true;
        }, LV_EVENT_READY, this);
        // LVGL advances the words; the page destructor cancels this animation
        // before its callback can outlive the view. The GIF keeps its own timing.
        reveal_stage_ = -1;
        lv_anim_t words;
        lv_anim_init(&words);
        lv_anim_set_var(&words, this);
        lv_anim_set_exec_cb(&words, reveal_word);
        lv_anim_set_values(&words, 0, 3);
        lv_anim_set_duration(&words, 2100);
        lv_anim_start(&words);
    }
    bool revealing() const { return reveal_stage_ >= 0 && reveal_stage_ < 3; }
    static void reveal_word(void* view, int32_t stage) {
        auto& self = *static_cast<InvitePage*>(view);
        if (self.reveal_stage_ == stage) return;
        self.reveal_stage_ = stage;
        const bool active = self.revealing();
        set_hidden(self.title_, active);
        set_hidden(self.subtitle_, active);
        set_hidden(self.footer_, active);
        set_hidden(self.invitation_qr_, active);
        set_font(self.prompt_, active ? &font_sans_32 : &font_sans_24);
        lv_obj_set_y(self.prompt_, active ? 212 : 374);
        constexpr const char* words[] = {"You're", "Invited", "To", "You're invited"};
        set_text(self.prompt_, words[stage]);
    }
    void sample() {
        if (submitted_ || context_.model.after_dark_unlocked) return;
        if (held_ && uint32_t(lv_tick_get() - pressed_at_) >= MorseUnlock::MaxHoldMs)
            set_pressed_feedback(false);
        const bool accepted = morse_.update(held_, false, lv_tick_get());
        if (morse_.restartCount() != restart_count_) {
            restart_count_ = morse_.restartCount();
            if (!held_ && !displayed_input_.empty()) restart_feedback_animation();
            else clear_feedback();
        }
        if (accepted) {
            clear_feedback();
            submitted_ = true;
            auto action = context_.callbacks.unlock_after_dark;
            if (action) action();
        } else if (!restarting_) render_input(morse_.input());
    }
    void contact(lv_event_t* event) {
        if (context_.model.after_dark_unlocked || submitted_) return;
        const auto code = lv_event_get_code(event);
        auto* input = lv_indev_active();
        if (code == LV_EVENT_PRESSED && input) {
            // Feedback never locks input: the first new press immediately
            // dismisses the old attempt and starts recording the next one.
            if (restarting_) clear_feedback();
            lv_indev_get_point(input, &start_);
            held_ = armed_ = true;
            pressed_at_ = lv_tick_get();
            set_pressed_feedback(true);
            sample();
        } else if (code == LV_EVENT_PRESSING && input && armed_) {
            lv_point_t point;
            lv_indev_get_point(input, &point);
            if (std::abs(point.x - start_.x) > 10 || std::abs(point.y - start_.y) > 10) cancel();
        } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_GESTURE || code == LV_EVENT_SCROLL_BEGIN) {
            cancel();
        } else if (code == LV_EVENT_RELEASED && armed_) {
            held_ = armed_ = false;
            set_pressed_feedback(false);
            sample();
        }
        // LONG_PRESSED is intentionally accepted: holds are Morse dashes.
    }
    lv_obj_t *title_ = nullptr, *subtitle_ = nullptr, *prompt_ = nullptr,
             *invitation_qr_ = nullptr, *footer_ = nullptr, *celebration_ = nullptr, *input_display_ = nullptr;
    MorseUnlock morse_;
    std::string displayed_input_;
    lv_point_t start_{};
    uint32_t restart_count_ = 0, pressed_at_ = 0;
    int displayed_state_ = -1, reveal_stage_ = 3;
    bool held_ = false, armed_ = false, submitted_ = false, celebration_done_ = false, restarting_ = false,
         pressed_feedback_ = false;
};
std::unique_ptr<PageView> make_after_dark(Context& c, lv_obj_t* p) { return std::make_unique<InvitePage>(c, p); }
} // namespace badge::ui
