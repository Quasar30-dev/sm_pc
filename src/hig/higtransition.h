#pragma once
#include "higactor.h"
#include <algorithm>
#include <cmath>

namespace hig {
// LEVEL_01.REL 0x88ca0/0x88a90 captures the evaluated pose when a blend is
// interrupted. 0x88ec0 uses a linear elapsed/duration weight; 0x890a8 passes
// controller-rate-scaled time for the main layer. Callers own the clock.
class PoseTransition {
  public:
    void reset() {
        source_.clear();
        elapsed_ = duration_ = 0;
    }
    void begin(std::span<const BonePose> evaluated, float duration) {
        source_.assign(evaluated.begin(), evaluated.end());
        elapsed_ = 0;
        duration_ = std::isfinite(duration) ? std::max(0.f, duration) : 0;
    }
    void advance(float seconds) {
        if (std::isfinite(seconds) && seconds > 0)
            elapsed_ = std::min(duration_, elapsed_ + seconds);
    }
    void apply(std::vector<BonePose> &target) const {
        if (source_.size() == target.size() && elapsed_ < duration_)
            blend_poses(source_, target, elapsed_ / duration_, target);
    }

  private:
    std::vector<BonePose> source_;
    float elapsed_ = 0, duration_ = 0;
};
// Main-layer transition durations in clip seconds, not display-frame counts.
// The fallback is native policy for states whose selector is still under RE.
inline float ratchet_blend_duration(unsigned clip) {
    switch (clip) {
    case 0:
        return .2f; // 0x42be0, return to stand
    case 3:
        return .1f; // 0x4a238, locomotion selector
    case 4:
    case 5:
    case 10:
    case 21:
    case 22:
    case 23:
    case 32:
        return .2f; // 0x40b50 / 0x3f6d4 / 0x3fa3c
    case 33:
        return .3f; // Zipline slide, 0x50f74
    case 6:
    case 11:
    case 12:
    case 55:
        return .1f;
    case 13:
    case 14:
    case 18:
        return .15f;
    case 26:
        return .05f; // 0x3f280, standing blaster
    case 61:
    case 62:
        return .125f; // 0x45fcc / 0x45ebc
    default:
        return .125f;
    }
}
} // namespace hig
