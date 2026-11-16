#pragma once
#include "hig/higcollision.h"
#include "hig/higinput.h"
#include "hig/higlevel.h"
#include <algorithm>
#include <chrono>

namespace sm {
// The native runtime consumes disc DATA through the same readers as Theia.
// No dependency on the oracle/REL execution or PS2 hardware emulation.
struct World {
    std::string disc_path;
    hig::LevelData level;
    hig::Collision terrain_collision;
    // The level's surface types ("STD " version 3, LEVEL_03 0x110b88): sorted by hash, each with the flags a face of
    // that surface takes at load (0x10e330: the collision face's +0x48 hash becomes {flags, index}) and its footstep
    // sound. A hero's ground of flag 4 is ground type 3 (0x47ee8).
    struct SurfaceType {
        uint32_t hash = 0, sound = 0;
        uint16_t flags = 0;
    };
    std::vector<SurfaceType> surface_types;
    uint16_t surface_flags(uint32_t hash) const {
        for (const auto &s : surface_types)
            if (s.hash == hash)
                return s.flags;
        return 0;
    }
    std::vector<std::string> pending;
};
class Runtime {
  public:
    bool mount(const std::wstring &iso, std::string &error);
    bool load(const std::string &wad, std::string &error);
    const World &world() const { return world_; }
    const hig::IsoFs &disc() const { return disc_; }

  private:
    hig::IsoFs disc_;
    World world_;
};
// LEVEL_01.REL 0x1adf8..0x1ae00 loads 0x3d088889 (1/30 second),
// passed to multiple normal-loop update systems. Keep this independent of
// presentation; other retail modes still need their own timing audit.
// Present rate must never multiply simulation speed or replay input edges.
class FixedClock {
  public:
    explicit FixedClock(uint32_t hz = 30) : hz_(std::clamp(hz, 1u, 1000u)) {}
    uint32_t advance(std::chrono::nanoseconds elapsed);
    double alpha() const { return double(remainder_) / 1000000000.; }
    uint64_t ticks() const { return ticks_; }
    uint64_t dropped_nanoseconds() const { return dropped_; }
    double step_seconds() const { return 1. / hz_; }

  private:
    uint32_t hz_;
    uint64_t remainder_ = 0, ticks_ = 0, dropped_ = 0;
};
} // namespace sm
