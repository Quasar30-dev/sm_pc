#pragma once
#include "hig/higactor.h"
#include "hig/higinput.h"
#include "ps2game/camera.h"

namespace sm {
class Gameplay;

struct UyaCameraPose {
    hig::Vec3 eye{}, forward{0, 0, 1}, up{0, 1, 0};
    float yaw = 0, pitch = 0; // SM radians: +Z forward, +X at positive yaw, Y up.
    float fov = 50;          // Vertical degrees at 4:3; keep vertical FOV for wider windows.
};

// Optional UYA follow profile on SM terrain. Source springs run at 60 Hz; SM's
// normal 30 Hz gameplay step supplies two camera updates. Terrain contacts and
// SM motion-state mapping are adapters, not a claim of original SM camera parity.
class UyaFollowCamera {
  public:
    void reset(const Gameplay &, float yaw, float pitch = -.25f);
    // Accumulate desired view-angle changes in SM radians once per input sample.
    // tick consumes mouse yaw and holds the selected mouse pitch; do not also
    // apply these deltas to the host orbit camera. Right stick is consumed here.
    void add_look(float yaw_delta, float pitch_delta);
    // Call after the final gameplay/platform/override movement. charge_dash is
    // the override's active dash phase only; bonk/recovery release the request.
    void tick(const Gameplay &, const hig::PadState &, float dt = 1.f / 30, bool charge_dash = false);
    UyaCameraPose pose(float alpha = 1) const;
    bool initialized() const { return camera_.initialized; }
    uint64_t ticks() const { return camera_.ticks; }

  private:
    ps2game::CameraState camera_;
    ps2game::CameraPose previous_;
    double remainder_ = 0;
    float pending_yaw_ = 0, mouse_pitch_ = 0;
    uint32_t ground_ticks_ = 0, floor_ticks_ = 0;
    unsigned respawns_ = 0;
};
} // namespace sm
