#pragma once
#include "higactor.h"
#include <cmath>
#include <cstring>

namespace hig {
// Data recovered from the USA LEVEL_01.REL. Shared by Theia and the native
// renderer; no guest code is executed. Offsets refer to the original file.
struct BlasterFx {
    Vec3 muzzle{};
    uint32_t glow = 0, trail = 0, spark = 0;
    float glow_radius = 0, trail_radius = 0;
    struct Spray {
        float size_min = 0, size_max = 0, frames_min = 0, frames_max = 0;
        float step_min = 0, step_max = 0, gravity = 0;
        uint32_t color = 0;
        unsigned count = 0;
    } sparks[2];
};
struct WeaponFxData {
    // 0x91630 builds the 203-entry SFX registry from this hash table.
    std::array<uint32_t, 203> texture_hashes{};
    BlasterFx blaster;
};
inline bool read_weapon_fx(std::span<const uint8_t> rel, WeaponFxData &out, std::string &error) {
    auto u = [&](size_t p) {
        uint32_t v;
        std::memcpy(&v, rel.data() + p, 4);
        return v;
    };
    auto f = [&](size_t p) {
        float v;
        std::memcpy(&v, rel.data() + p, 4);
        return v;
    };
    if (rel.size() < 0x21cd58 || u(0x91680) != 0x2cc200cb || u(0x21ccac) != 0x148a88 ||
        u(0x21ccd8) != 0x14d660) {
        error = "Unsupported Size Matters weapon-effect table revision";
        return false;
    }
    WeaponFxData data;
    for (size_t i = 0; i < data.texture_hashes.size(); i++)
        data.texture_hashes[i] = u(0x210218 + i * 4);
    auto &b = data.blaster;
    for (unsigned k = 0; k < 3; k++)
        b.muzzle[k] = f(0x21cb10 + k * 4); // 0x1471b0: weapon world * local point
    b.glow = u(0x21cd10) | 0xc8000000;     // 0x148fe8..0x148ff8
    b.trail = u(0x21cd1c);
    b.spark = u(0x21cd34);
    b.glow_radius = f(0x21cd18);
    b.trail_radius = f(0x21cd2c);
    // 0x14d7c0 emits 20 + 7 plasma sparks using templates 0x207268/0x207290.
    // Size and tint are patched by the selected 0x48-byte Blaster variant row.
    for (unsigned i = 0; i < 2; i++) {
        auto &s = b.sparks[i];
        size_t p = i ? 0x207290 : 0x207268;
        s.size_min = f(p);
        s.size_max = f(0x21cd10 + (i ? 0x30 : 0x28));
        s.frames_min = f(p + 8);
        s.frames_max = f(p + 12);
        s.step_min = f(p + 16);
        s.step_max = f(p + 20);
        s.gravity = f(p + 32);
        s.color = u(0x21cd10 + (i ? 0x34 : 0x2c));
        s.count = i ? 7 : 20;
        for (float v : {s.size_min, s.size_max, s.frames_min, s.frames_max, s.step_min, s.step_max,
                        s.gravity})
            if (!std::isfinite(v) || v < 0 || v > 128) {
                error = "Invalid weapon particle template";
                return false;
            }
        if (s.frames_min <= 0 || s.frames_max < s.frames_min || s.step_max < s.step_min ||
            s.size_max < s.size_min) {
            error = "Invalid weapon particle range";
            return false;
        }
    }
    if (!std::isfinite(b.glow_radius) || b.glow_radius <= 0 || b.glow_radius > 8 ||
        !std::isfinite(b.trail_radius) || b.trail_radius <= 0 || b.trail_radius > 8) {
        error = "Invalid weapon effect size";
        return false;
    }
    for (float v : b.muzzle)
        if (!std::isfinite(v) || std::abs(v) > 8) {
            error = "Invalid weapon muzzle";
            return false;
        }
    out = data;
    error.clear();
    return true;
}
} // namespace hig
