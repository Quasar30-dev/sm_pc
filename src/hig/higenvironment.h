#pragma once
#include "higactor.h"

namespace hig {
struct OceanTile {
    Matrix transform;
    bool distant = false;
};
// LEVEL_01.REL WaterWaves draw: six by six 70 m cells, snapped around the
// player. Coordinates returned here are already in game metres.
std::vector<OceanTile> ocean_tiles(const LevelEnvironment &, Vec3 anchor, float seconds);
std::array<float, 2> ocean_uv_offset(float seconds);
Matrix sky_transform(const LevelEnvironment &, int model, Vec3 eye, float seconds);
} // namespace hig
