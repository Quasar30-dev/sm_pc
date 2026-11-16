#pragma once
// Stand-in for a header of the PS2 engine sm_pc's borrowed play options are built on (see src/sm/extras.h). That
// engine is not part of this repository; this declares only the few names Size Matters' own code spells, so the
// shared sources compile unchanged. Nothing here does anything.
#include <cstdint>

namespace ps2game {
enum class HeroGame : uint8_t { Uya, Gc, Rac1 };
enum class HeroMode : uint32_t { Idle = 0, Swim, SwimFast };
enum class HeroPack : uint8_t { None = 0, Helipack = 2, Thrusterpack = 3 };
struct HeroState {
    HeroMode mode = HeroMode::Idle;
    struct {
        float rate = 1;
    } animation;
};
} // namespace ps2game
