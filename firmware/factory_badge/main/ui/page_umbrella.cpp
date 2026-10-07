#include "hack_kit.h"
#include "design_assets.h"
#include <cmath>
#include <cstdio>

namespace badge::ui {
namespace {
using hack::Cx;
constexpr int PhotoSide = 140, PhotoTop = 112;
constexpr uint32_t UmbrellaRed = 0xd4141c;

struct Tier {
    const char* name;
    uint32_t color;
    int weight; // Chances in 1000.
    const char* const* titles;
    int count;
};
constexpr const char* Common[] = {
    "Janitor, Hive Sublevel 4", "Lab Assistant", "Security Guard, Raccoon City", "Data Entry Clerk",
    "Cafeteria Staff, The Hive", "Unpaid Intern", "Elevator Maintenance", "Vending Machine Restocker",
    "Specimen Jar Labeler", "Mansion Groundskeeper",
};
constexpr const char* Uncommon[] = {
    "Virologist", "U.B.C.S. Mercenary", "Containment Technician", "B.O.W. Handler",
    "Field Researcher, Arklay Labs", "Licker Feeding Specialist", "Herb Garden Botanist",
};
constexpr const char* Rare[] = {
    "U.S.S. Operative", "Senior Researcher, T-Virus Program", "Head of Security",
    "Nemesis Program Engineer", "Tyrant Calibration Lead", "Typewriter Ribbon Quartermaster",
};
constexpr const char* Epic[] = {
    "S.T.A.R.S. Captain (Double Agent)", "Director, NEST Facility", "Red Queen Sysadmin", "G-Virus Project Lead",
};
constexpr const char* Legendary[] = {
    "Founder", "Patient Zero", "The Red Queen", "Chief Executive Zombie", "Keeper of Wesker's Sunglasses",
};
template <size_t N> constexpr int size(const char* const (&)[N]) { return int(N); }
// Rarest first. Weights sum to 1000.
constexpr Tier Tiers[] = {
    {"LEGENDARY", 0xffc21a, 15, Legendary, size(Legendary)},
    {"EPIC", 0xb866ff, 55, Epic, size(Epic)},
    {"RARE", 0x3d9bff, 130, Rare, size(Rare)},
    {"UNCOMMON", 0x35d07f, 250, Uncommon, size(Uncommon)},
    {"COMMON", 0x9a9a9a, 550, Common, size(Common)},
};

// The same name always gets the same job: FNV-1a over the name without case
// or surrounding spaces, then a final mix so similar names land far apart.
uint32_t name_hash(const std::string& name) {
    const auto begin = name.find_first_not_of(' ');
    if (begin == std::string::npos) return 0;
    const auto end = name.find_last_not_of(' ');
    uint32_t hash = 2166136261u;
    for (auto i = begin; i <= end; ++i) {
        const char c = name[i];
        hash = (hash ^ uint8_t(c >= 'A' && c <= 'Z' ? c + 32 : c)) * 16777619u;
    }
    hash ^= hash >> 16; hash *= 0x7feb352du; hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
    return hash;
}
} // namespace

// An Umbrella Corporation employee ID built from the Badge page's name and
// photo. The job title and its rarity are decided by a hash of the name.
class UmbrellaPage final : public PageView {
public:
    UmbrellaPage(Context& context, lv_obj_t* parent) : PageView(context, parent) { rebuild(); }
    ~UmbrellaPage() override {
        lv_obj_clean(root_);
#if LV_CACHE_DEF_SIZE > 0
        lv_image_cache_drop(&image_);
#endif
    }
    void update() override {
        const auto& model = context_.model;
        if (revision_ != model.profile_revision || name_ != model.name || avatar_ != model.avatar) rebuild();
    }
private:
    void rebuild() {
        const auto& model = context_.model;
        // Remove every image user before changing its descriptor or backing data.
        lv_obj_clean(root_);
#if LV_CACHE_DEF_SIZE > 0
        lv_image_cache_drop(&image_);
#endif
        revision_ = model.profile_revision; name_ = model.name; avatar_ = model.avatar;
        image_ = {};
        image_.header.magic = LV_IMAGE_HEADER_MAGIC;
        image_.header.cf = LV_COLOR_FORMAT_RGB565;
        image_.header.w = model.avatar_width; image_.header.h = model.avatar_height;
        image_.header.stride = model.avatar_width * 2;
        image_.data_size = model.avatar_width * model.avatar_height * 2;
        image_.data = reinterpret_cast<const uint8_t*>(avatar_);

        const lv_color_t red = lv_color_hex(UmbrellaRed);
        // The logo: eight wedges, alternately red and white.
        hack::surface(root_, Cx - 30, 18, 60, 60, [red](lv_layer_t* layer, const lv_area_t& coords) {
            const float cx = coords.x1 + 30, cy = coords.y1 + 30;
            for (int i = 0; i < 8; ++i) {
                const float a = (i * 45 - 22.5f) * 0.0174533f, b = (i * 45 + 22.5f) * 0.0174533f;
                hack::fill_triangle(layer, cx, cy, cx + std::sin(a) * 29, cy - std::cos(a) * 29,
                                    cx + std::sin(b) * 29, cy - std::cos(b) * 29, i % 2 ? white() : red);
            }
        });
        label(root_, "UMBRELLA CORPORATION", 84, 82, 300, &font_mono_18, white());

        auto* frame = container(root_, Cx - PhotoSide / 2 - 3, PhotoTop - 3, PhotoSide + 6, PhotoSide + 6);
        lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(frame, red, 0);
        lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
        auto* photo = container(frame, 3, 3, PhotoSide, PhotoSide);
        lv_obj_remove_flag(photo, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(photo, panel(), 0);
        lv_obj_set_style_bg_opa(photo, LV_OPA_COVER, 0);
        auto* image = lv_image_create(photo);
        if (avatar_ && image_.header.w && image_.header.h) {
            lv_image_set_src(image, &image_);
            lv_image_set_scale(image, 256 * PhotoSide / image_.header.w);
        } else {
            lv_image_set_src(image, &supplied_empty_portrait);
            lv_image_set_scale(image, 256 * PhotoSide / 160);
            lv_obj_set_style_image_recolor(image, white(), 0);
            lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
        }
        lv_image_set_antialias(image, true);
        lv_obj_center(image);

        const uint32_t hash = name_hash(name_);
        const bool registered = name_.find_first_not_of(' ') != std::string::npos;
        tier_ = 4;
        int roll = int(hash % 1000);
        for (int i = 0; i < 5; ++i) {
            if (roll < Tiers[i].weight) { tier_ = i; break; }
            roll -= Tiers[i].weight;
        }
        // The author's privilege: anyone called Braelyn is always legendary.
        std::string first = hack::first_name(context_, "");
        for (auto& c : first) if (c >= 'A' && c <= 'Z') c += 32;
        const bool author = first == "braelyn";
        if (author) tier_ = 0;
        const Tier& tier = Tiers[tier_];
        const lv_color_t color = lv_color_hex(tier.color);
        label(root_, registered ? name_.c_str() : "UNREGISTERED", 74, 260, 320, &font_mono_24, white());
        auto* title = label(root_, !registered ? "Visitor: escort required" : author ? "Head of Human Research"
                                : tier.titles[(hash >> 10) % tier.count],
                            84, 296, 300, &font_sans_20, registered ? color : muted());
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        if (!registered) return;
        char text[48];
        std::snprintf(text, sizeof(text), "%s \xc2\xb7 ID %06u", tier.name, unsigned((hash >> 4) % 1000000));
        label(root_, text, 94, 356, 280, &font_mono_12, color);
        // A barcode of the hash: thin and thick bars, as on a real ID.
        hack::surface(root_, Cx - 80, 384, 160, 26, [hash](lv_layer_t* layer, const lv_area_t& coords) {
            int x = int(coords.x1);
            uint32_t bits = hash | 1;
            while (x < int(coords.x2) - 6) {
                const int bar = 2 + int(bits & 3), gap = 2 + int((bits >> 2) & 1) * 2;
                hack::fill_rect(layer, x, int(coords.y1), bar, 26, white());
                x += bar + gap;
                bits = bits >> 3 | bits << 29;
            }
        });
    }
    int tier_ = 4;
    uint32_t revision_ = 0;
    const uint16_t* avatar_ = nullptr;
    lv_image_dsc_t image_{};
    std::string name_;
};
std::unique_ptr<PageView> make_umbrella(Context& c, lv_obj_t* p) { return std::make_unique<UmbrellaPage>(c, p); }
} // namespace badge::ui
