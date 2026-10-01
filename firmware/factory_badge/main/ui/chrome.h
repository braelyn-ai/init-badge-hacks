#pragma once
#include "widgets.h"
#include <array>

namespace badge::ui {
class Chrome {
public:
    Chrome(Context& context, lv_obj_t* parent);
    ~Chrome();
    void update();
    void reflow();
private:
    Context& context_;
    lv_obj_t *root_ = nullptr, *brand_ = nullptr, *footer_ = nullptr, *left_ = nullptr, *right_ = nullptr;
    lv_obj_t* status_root_ = nullptr; // Footer layer that stays visible with a filled Badge card.
    bool dotless_ = false;
    std::array<lv_obj_t*, PageCount> dots_{};
    int page_ = -1;
    int visible_count_ = -1;
};
} // namespace badge::ui
