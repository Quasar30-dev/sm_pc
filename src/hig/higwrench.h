#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace hig {
// SCUS-97615, LEVEL_01.REL. Shared data/response rules for Theia and the
// native engine; file offsets and remaining controller policy are in
// docs/SIZE_MATTERS_WRENCH.md. No guest instructions are executed.
struct WrenchSwing {
    float start, drive_begin, drive_end, deform_begin;
    float chain_begin, chain_end, chain_after;
};
inline constexpr std::array<WrenchSwing, 3> wrench_swings{
    {{.4f, .4f, .6666667f, .4f, .7f, 1.0666667f, .8f},
     {0, .26666668f, .53333336f, .06666667f, .33333334f, .9f, .6666667f},
     {0, .16666667f, .6f, .2f, 0, 0, 0}}};
inline bool wrench_chain_input(unsigned stage, float time) {
    const auto &s = wrench_swings.at(stage);
    return stage < 2 && time >= s.chain_begin && time <= s.chain_end;
}
// 0x22a170, initial progression row/column. Preserve floating-point damage:
// 0x1c7ba4 multiplies a landed hyper strike by 1.6, not integer rounding to 3.
inline constexpr float wrench_base_damage = 1.f;
inline constexpr float wrench_hyper_multiplier = 1.6f;
inline bool wrench_contact_active(unsigned stage, float time) {
    return stage != 0 || (time > .56666666f && time < .73333335f); // 0x1c9030..68
}
inline float wrench_drive(unsigned stage, float time) {
    const auto &s = wrench_swings.at(stage);
    return time >= s.drive_begin && time < s.drive_end ? .08f : 0.f;
}
// Original +0x23c spring state is displacement per 1/60 movement pass.
inline void wrench_spring(float target, float &current, float &derivative) {
    derivative = std::clamp((target - current) * (target == 0 ? .15f : .1f) - derivative * .2f,
                            -100000.f, 100000.f);
    current += derivative;
}
// Planar form of 0x48948's angular spring. Attack windups select +0x230=.15;
// initial damping/cap are .2/10000 (0x38160..0x38298 -> 0x136a0).
// Other aiming states override those parameters; callers can retain that cap.
inline float wrench_turn(float error, float stick, unsigned passes, float &derivative,
                         float limit = 10000.f) {
    constexpr float pi = 3.14159265359f;
    error = std::remainder(error, 2 * pi);
    const float magnitude = std::abs(error);
    if (magnitude < .017f || stick <= .0001f) {
        derivative = 0;
        return 0;
    }
    const float response =
        stick < 1 ? (magnitude > pi / 2 ? .25f : .1f) : (magnitude > .34906587f ? 1.3f : .65f);
    float angle = 0;
    for (unsigned i = 0; i < passes; ++i) {
        derivative =
            std::clamp(std::remainder(error - angle, 2 * pi) * response * .15f - derivative * .2f,
                       -limit, limit);
        angle = std::remainder(angle + derivative, 2 * pi);
    }
    return angle;
}
inline float wrench_throw_curve(float progress) {
    float t = std::clamp(progress, 0.f, 1.f);
    t = t < .5f ? 2 * t : 2 * (1 - t);
    return 2 * t - t * t; // 0x12f218, symmetric quadratic outbound/return
}
inline constexpr float wrench_throw_step = .010638297535479069f;
inline constexpr float wrench_throw_spin = .41887906193733215f;
inline constexpr std::array<float, 3> wrench_throw_pivot{-.1537400037f, .0321599990f,
                                                         -.1537200063f};
inline constexpr float hyper_acceleration = std::bit_cast<float>(uint32_t(0xbc449ba8));
inline constexpr float hyper_passes = std::bit_cast<float>(uint32_t(0x41600001));
inline constexpr float hyper_launch =
    (1.5f - .5f * hyper_acceleration * hyper_passes * hyper_passes) / hyper_passes;
} // namespace hig
