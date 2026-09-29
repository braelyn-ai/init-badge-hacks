#pragma once
#include <cstdint>

// Recognize the seven symbols of init, independent of their spacing.
// Hold duration only distinguishes dot (<400ms) from dash (>=400ms).
// Pauses still add visible letter/word spaces, but never reject or expire a
// correct prefix. An incorrect attempt clears after 2.5s released. Cancellation,
// conflicting inputs and buffer overflow cannot unlock a suffix by accident.
// All timestamps are monotonic uint32 milliseconds, with wrap-safe subtraction.
class MorseUnlock {
 public:
    static constexpr uint32_t UnitMs = 200;
    static constexpr uint32_t DotMs = UnitMs, DashMs = 3 * UnitMs;
    static constexpr uint32_t SymbolGapMs = UnitMs, LetterGapMs = 3 * UnitMs;
    static constexpr uint32_t WordGapMs = 7 * UnitMs;
    static constexpr uint32_t DashThresholdMs = 400, LetterThresholdMs = 400;
    static constexpr uint32_t ResetGapMs = 2500;
    static constexpr uint8_t InputCapacity = 32;
    const char* input() const { return input_; }
    uint32_t restartCount() const { return restartCount_; }
    void reset(bool first = false, bool second = false) {
        clearAttempt(); waitingForRelease_ = first || second;
    }
    bool update(bool first, bool second, uint32_t now, bool enabled = true) {
        const uint8_t held = (first ? 1u : 0u) | (second ? 2u : 0u);
        if (!enabled) { reset(first, second); return false; }
        if (waitingForRelease_) {
            if (!held) waitingForRelease_ = false;
            return false;
        }
        if (!owner_) { if (held) begin(held, now); return false; }
        if (!pressed_ && rejected_ && now - releasedAt_ >= ResetGapMs) {
            clearAttempt(); ++restartCount_;
            if (held) begin(held, now);
            return false;
        }
        if (held && held != owner_) rejected_ = true;
        if (pressed_) {
            if (held) return false;
            pressed_ = false; releasedAt_ = now;
            const char symbol = now - pressedAt_ < DashThresholdMs ? '.' : '-';
            append(symbol); letterOpen_ = wordOpen_ = true;
            if (!rejected_) {
                constexpr char sequence[] = "..-...-";
                if (sequence[symbolIndex_] != symbol) rejected_ = true;
                else if (++symbolIndex_ == 7) { reset(); return true; }
            }
            return false;
        }
        const uint32_t gap = now - releasedAt_;
        if (letterOpen_ && gap >= LetterThresholdMs) { append(' '); letterOpen_ = false; }
        if (wordOpen_ && gap >= WordGapMs) { append(' '); wordOpen_ = false; }
        if (held) { pressed_ = true; pressedAt_ = now; }
        return false;
    }
 private:
    void begin(uint8_t held, uint32_t now) {
        owner_ = held; pressedAt_ = now; pressed_ = true; rejected_ = held == 3;
    }
    void append(char symbol) {
        if (inputLength_ == InputCapacity) { rejected_ = true; return; }
        input_[inputLength_++] = symbol; input_[inputLength_] = '\0';
    }
    void clearAttempt() {
        owner_ = symbolIndex_ = inputLength_ = 0;
        pressed_ = rejected_ = letterOpen_ = wordOpen_ = false;
        input_[0] = '\0';
    }
    uint32_t pressedAt_ = 0, releasedAt_ = 0, restartCount_ = 0;
    char input_[InputCapacity + 1] = {};
    uint8_t owner_ = 0, symbolIndex_ = 0, inputLength_ = 0;
    bool pressed_ = false, waitingForRelease_ = false, rejected_ = false;
    bool letterOpen_ = false, wordOpen_ = false;
};
