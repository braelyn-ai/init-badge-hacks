// Off-device preview of one hack page: runs it against real LVGL with a
// simulated clock, swinging gravity and scripted taps, and writes PPM frames.
//   hack_page_preview OUT_DIR [--seconds N] [--every MS] [--tap MS[:X:Y]]... [--tilt flat|solve]
// --tilt flat: the badge lies flat and is tilted slowly in a circle (games).
#include "ui/ui_internal.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr int Width = 468, Height = 466, StepMs = 10;
std::vector<uint16_t> pixels(Width * Height);
uint16_t buffer[Width * 40];
int touch_x = 234, touch_y = 233;
bool pressed = false;
unsigned long flushed_pixels = 0;
struct Tap { int at, x, y; };

void write_frame(const std::string& directory, int index) {
    char name[32];
    std::snprintf(name, sizeof(name), "/frame-%02d.ppm", index);
    auto* file = std::fopen((directory + name).c_str(), "wb");
    if (!file) { std::perror("frame"); std::exit(2); }
    std::fprintf(file, "P6\n%d %d\n255\n", Width, Height);
    for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x) {
        const auto p = pixels[y * Width + x];
        unsigned char rgb[3] = {uint8_t(((p >> 11) & 31) * 255 / 31),
            uint8_t(((p >> 5) & 63) * 255 / 63), uint8_t((p & 31) * 255 / 31)};
        // Dim everything outside the round panel so clipping is obvious.
        const int dx = x - 234, dy = y - 233;
        if (dx * dx + dy * dy > 233 * 233) { rgb[0] = rgb[0] / 4 + 40; rgb[1] /= 4; rgb[2] = rgb[2] / 4 + 40; }
        std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s OUT_DIR [--seconds N] [--every MS] [--tap MS[:X:Y]]...\n", argv[0]); return 2; }
    const std::string directory = argv[1];
    int seconds = 8, every = 500;
    bool flat = false;
    std::vector<Tap> taps;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--seconds")) seconds = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--every")) every = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--tilt")) flat = true;
        else if (!std::strcmp(argv[i], "--tap")) {
            Tap tap{0, 234, 233};
            std::sscanf(argv[i + 1], "%d:%d:%d", &tap.at, &tap.x, &tap.y);
            taps.push_back(tap);
        }
    }
    std::filesystem::create_directories(directory);
    lv_init();
    auto* display = lv_display_create(Width, Height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t* area, uint8_t* map) {
        auto* source = reinterpret_cast<uint16_t*>(map);
        const int stride = lv_display_get_buf_active(d)->header.stride / sizeof(uint16_t);
        for (int y = area->y1; y <= area->y2; ++y) for (int x = area->x1; x <= area->x2; ++x)
            pixels[y * Width + x] = source[(y - area->y1) * stride + x - area->x1];
        flushed_pixels += (unsigned long)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
        lv_display_flush_ready(d);
    });
    auto* input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_read_cb(input, [](lv_indev_t*, lv_indev_data_t* data) {
        data->point = {touch_x, touch_y};
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    });
    auto* screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    badge::ui::Context context;
    context.page = badge::ui::FirstHackPage;
    context.model.name = "Ada Lovelace";
    if (const char* name = std::getenv("HACK_NAME")) context.model.name = name;
    // A stand-in portrait: the real one is the owner's 160 x 160 RGB565 photo.
    static std::vector<uint16_t> portrait(160 * 160);
    for (int y = 0; y < 160; ++y) for (int x = 0; x < 160; ++x) {
        const int dx = x - 80, dy = y - 70;
        const bool head = dx * dx + dy * dy < 38 * 38, body = y > 115 && dx * dx + (y - 190) * (y - 190) < 70 * 70;
        portrait[y * 160 + x] = head || body ? 0xfe94 : uint16_t((y / 6) << 11 | 12 << 5 | (20 + x / 16));
    }
    context.model.avatar = portrait.data();
    context.model.avatar_width = context.model.avatar_height = 160;
    context.model.profile_revision = 1;
    int scans = 0;
    context.callbacks.radar_scan = [&] {
        ++scans;
        context.model.radar.clear();
        for (int i = 0; i < 14; ++i) {
            badge::RadarContact contact;
            contact.bssid = {0x10, 0x20, 0x30, uint8_t(i * 37), uint8_t(i * 91), uint8_t(i)};
            contact.rssi = int8_t(-35 - (i * 13 + scans * 3) % 55);
            context.model.radar.push_back(contact);
        }
        ++context.model.radar_revision;
    };
    auto page = badge::ui::HACK_FACTORY(context, screen);

    int frame = 0;
    for (int now = 0; now <= seconds * 1000; now += StepMs) {
        // A badge swinging on a lanyard: mostly down, swaying side to side.
        context.motion = {true, 0.45f * std::sin(now / 380.0f), 1.0f + 0.15f * std::cos(now / 190.0f)};
        if (flat) context.motion = {true, 0.4f * std::cos(now / 1100.0f), 0.4f * std::sin(now / 1100.0f)};
        pressed = false;
        for (const auto& tap : taps) if (now >= tap.at && now < tap.at + 80) { pressed = true; touch_x = tap.x; touch_y = tap.y; }
        lv_tick_inc(StepMs);
        page->update();
        lv_timer_handler();
        if (now % every == 0) write_frame(directory, frame++);
    }
    // Leaving the page must release everything it owns (ASan checks this).
    page.reset();
    lv_timer_handler();
    std::printf("%d frames; mean redraw %.0f%% of the screen per 33 ms\n", frame,
                100.0 * flushed_pixels / (double(Width) * Height) / (seconds * 1000 / 33.0));
    return 0;
}
