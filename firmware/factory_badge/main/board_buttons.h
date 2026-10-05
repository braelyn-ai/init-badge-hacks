#pragma once
#include <atomic>
#include <cstdint>

namespace board {
// Pusher sampling that does not depend on the UI loop rate. A periodic timer
// calls sample() every few milliseconds; the main task calls take() once per
// loop. A slow frame (the init() GIF takes ~170 ms) would otherwise let a short
// press start and end between two main-loop reads and be lost.
class ButtonLatch {
public:
    static constexpr uint32_t DebounceMs = 10;
    static constexpr uint8_t Yellow = 1, Blue = 2;

    // Timer side, single writer: raw active-high levels.
    void sample(bool yellow, bool blue, uint32_t now) {
        const uint8_t stable = uint8_t(yellowKey_.update(yellow, now) ? Yellow : 0) |
                               uint8_t(blueKey_.update(blue, now) ? Blue : 0);
        const uint8_t rising = stable & ~previous_;
        previous_ = stable;
        if (rising) pressed_.fetch_or(rising, std::memory_order_relaxed);
        stable_.store(stable, std::memory_order_release);
    }

    // Main side: debounced held pushers, plus any that were pressed since the
    // previous take() even if already released. A latched short press is
    // therefore reported as held for exactly one read, then released.
    uint8_t take() {
        return stable_.load(std::memory_order_acquire) |
               pressed_.exchange(0, std::memory_order_acq_rel);
    }

private:
    struct Debouncer {
        bool candidate = false, stable = false;
        uint32_t changedAt = 0;
        bool update(bool raw, uint32_t now) {
            if (candidate != raw) { candidate = raw; changedAt = now; }
            if (uint32_t(now - changedAt) >= DebounceMs) stable = candidate;
            return stable;
        }
    };
    Debouncer yellowKey_, blueKey_;
    uint8_t previous_ = 0;
    std::atomic<uint8_t> stable_{0}, pressed_{0};
};
} // namespace board
