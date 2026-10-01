// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>

namespace board {
// RGB565 carries six green bits but five red/blue bits. Grays produced by
// per-channel rounding (alpha blends, anti-aliased edges, low-alpha masks and
// hex values such as #161616) often land one green step off neutral, which
// reads as a green or magenta cast on dark UI. Before scan-out, snap only
// pixels whose red and blue match and whose green is within one step of the
// 6-bit level nearest that gray. Every other color is left untouched.
constexpr uint8_t expand5(unsigned v) { return uint8_t(v << 3 | v >> 2); }
constexpr uint8_t expand6(unsigned v) { return uint8_t(v << 2 | v >> 4); }
constexpr int distance(int a, int b) { return a > b ? a - b : b - a; }
constexpr unsigned neutralGreen(unsigned red5) {
    unsigned best = 0;
    for (unsigned g = 1; g < 64; ++g)
        if (distance(expand6(g), expand5(red5)) < distance(expand6(best), expand5(red5))) best = g;
    return best;
}

struct NeutralGreenTable {
    uint8_t green[32]{};
    constexpr NeutralGreenTable() { for (unsigned r = 0; r < 32; ++r) green[r] = uint8_t(neutralGreen(r)); }
};
inline constexpr NeutralGreenTable kNeutralGreen{};

// Runs for every flushed pixel, so it uses the precomputed table.
constexpr uint16_t neutralizeGray565(uint16_t pixel) {
    const unsigned r = pixel >> 11, g = (pixel >> 5) & 63, b = pixel & 31;
    if (r != b) return pixel;
    const unsigned n = kNeutralGreen.green[r];
    if (distance(int(g), int(n)) != 1) return pixel;
    // Keep a neighboring level that is already equally close to neutral.
    if (distance(expand6(g), expand5(r)) <= distance(expand6(n), expand5(r))) return pixel;
    return uint16_t(r << 11 | n << 5 | b);
}

inline void neutralizeGrays565(uint16_t* pixels, size_t count) {
    for (size_t i = 0; i < count; ++i) pixels[i] = neutralizeGray565(pixels[i]);
}
} // namespace board
