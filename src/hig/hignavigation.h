#pragma once
#include "higactor.h"
#include <algorithm>
#include <cmath>

namespace hig {
// Luna launch 0x18f158 and flight 0x18ff78. Horizontal displacement is
// 0.26666668 per 30 Hz tick; the apex is clearance above the higher endpoint.
// Evaluating the same parabola continuously also supports render interpolation.
struct LunaJump {
    Vec3 from{}, to{};
    float duration = 0, vertical_speed = 0, gravity = 0;
    bool start(Vec3 a, Vec3 b, float clearance) {
        float distance = std::hypot(b[0] - a[0], b[2] - a[2]);
        if (!std::isfinite(distance) || distance < .01f || !std::isfinite(clearance) ||
            clearance <= 0)
            return false;
        from = a;
        to = b;
        duration = distance / (.2666666806f * 30.f);
        float apex = std::max(a[1], b[1]) + clearance;
        float rise = apex - a[1], fall = apex - b[1];
        float ascent = duration * std::sqrt(rise) / (std::sqrt(rise) + std::sqrt(fall));
        gravity = -2 * rise / (ascent * ascent);
        vertical_speed = -gravity * ascent;
        return std::isfinite(gravity);
    }
    Vec3 at(float seconds) const {
        float t = std::clamp(seconds, 0.f, duration), f = duration > 0 ? t / duration : 1;
        if (f >= 1)
            return to;
        return {from[0] + (to[0] - from[0]) * f,
                from[1] + vertical_speed * t + .5f * gravity * t * t,
                from[2] + (to[2] - from[2]) * f};
    }
};
} // namespace hig
