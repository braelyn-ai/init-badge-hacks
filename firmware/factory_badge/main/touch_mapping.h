#pragma once
#include "touchcal/touch_calibration_core.h"
#include <array>
#include <string>

// Hardware-independent collection; only real raw samples may enter this session.
namespace touch_mapping {
class Session {
public:
    enum class Stage { Idle, Fit, Verify, Ready, Failed, Saved };
    void begin();
    void cancel();
    void sample(bool valid, bool sensor, bool pressed, int x, int y, uint32_t now);
    touchcal::Point target() const;
    int index() const { return index_; }
    Stage stage() const { return stage_; }
    const touchcal::Affine& affine() const { return affine_; }
    const std::string& message() const { return message_; }
    void saved(bool ok);
private:
    Stage stage_ = Stage::Idle;
    int index_ = 0;
    bool contact_ = false, armed_ = false, bad_ = false;
    uint32_t started_ = 0;
    size_t count_ = 0;
    std::array<touchcal::Point, 128> samples_{};
    std::array<touchcal::Point, 9> raw_{};
    std::array<touchcal::Point, 5> observed_{};
    touchcal::Affine affine_{};
    std::string message_;
};
void load();
bool available();
int version();
touchcal::Point map(touchcal::Point raw);
bool save(const touchcal::Affine& affine);
}
