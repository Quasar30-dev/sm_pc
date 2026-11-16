#pragma once
// Stand-in for a header of the PS2 engine sm_pc's borrowed play options are built on (see src/sm/extras.h and
// hero.h beside this file). Only what src/sm/camera.h names; the follow camera itself is not in this repository.
#include <cstdint>

namespace ps2game {
struct CameraState {
    bool initialized = false;
    uint64_t ticks = 0;
};
struct CameraPose {};
} // namespace ps2game
