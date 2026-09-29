#include "touch_mapping.h"
#include <algorithm>
namespace touch_mapping {
namespace {
constexpr touchcal::Point fitPoints[] = {{234,63},{234,403},{64,233},{404,233},{114,113},{354,113},{114,353},{354,353},{234,233}};
constexpr touchcal::Point checkPoints[] = {{234,118},{349,233},{234,348},{119,233},{234,233}};
}
void Session::begin() { *this = Session{}; stage_ = Stage::Fit; message_ = "Hold the target, then lift"; }
void Session::cancel() { *this = Session{}; }
touchcal::Point Session::target() const { return stage_ == Stage::Fit ? fitPoints[index_] : stage_ == Stage::Verify ? checkPoints[index_] : touchcal::Point{234,233}; }
void Session::sample(bool valid, bool sensor, bool pressed, int x, int y, uint32_t now) {
    if (stage_ != Stage::Fit && stage_ != Stage::Verify) return;
    if (!valid || !sensor) { contact_ = false; armed_ = false; count_ = 0; return; }
    if (!pressed && !contact_) { armed_ = true; return; }
    if (!armed_) return; // Discard the contact that opened the calibration view.
    if (pressed) {
        if (!contact_) { contact_ = true; started_ = now; count_ = 0; bad_ = false; }
        if (count_ < samples_.size()) samples_[count_++] = {double(x),double(y)};
        else {
            std::move(samples_.begin()+1, samples_.end(), samples_.begin());
            samples_.back() = {double(x),double(y)};
        }
        return;
    }
    contact_ = false;
    const auto reduced = touchcal::reduceSamples(samples_.data(), count_, now - started_);
    if (bad_ || !reduced.accepted) { message_ = "Hold still briefly, then lift"; return; }
    message_ = "Hold the target, then lift";
    if (stage_ == Stage::Fit) {
        raw_[index_++] = reduced.median;
        if (index_ == 9) {
            if (!touchcal::fitAffine(raw_.data(), fitPoints, 9, &affine_)) {
                stage_ = Stage::Failed; message_ = "Could not fit. Exit and retry."; return;
            }
            touchcal::Point fitted[9];
            for (int i=0;i<9;++i) fitted[i]=touchcal::apply(affine_,raw_[i]);
            if (!touchcal::verify(fitPoints,fitted,9).passed) {
                stage_ = Stage::Failed; message_ = "Measurements disagree. Retry."; return;
            }
            stage_ = Stage::Verify; index_ = 0;
        }
    } else {
        observed_[index_++] = touchcal::apply(affine_,reduced.median);
        if (index_ == 5) {
            const bool ok = touchcal::verify(checkPoints,observed_.data(),5).passed;
            stage_ = ok ? Stage::Ready : Stage::Failed;
            message_ = ok ? "Saving calibration..." : "Check failed. Exit and retry.";
        }
    }
}
void Session::saved(bool ok) { stage_ = ok ? Stage::Saved : Stage::Failed; message_ = ok ? "Calibration saved" : "Could not save. Exit and retry."; }
}
