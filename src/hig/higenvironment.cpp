#include "higenvironment.h"
#include <cmath>

namespace hig {
std::vector<OceanTile> ocean_tiles(const LevelEnvironment &e, Vec3 anchor, float seconds) {
    std::vector<OceanTile> tiles;
    if (e.ocean_model < 0 || !std::isfinite(seconds) || !std::isfinite(e.ocean_height) ||
        !std::isfinite(e.wave_scale) || !std::isfinite(e.wave_speed) || !std::isfinite(anchor[0]) ||
        !std::isfinite(anchor[2]) || std::abs(anchor[0]) > 1e7f || std::abs(anchor[2]) > 1e7f)
        return tiles;
    // Original data 0x208f7c=100, 0x208f80=.7. The draw's .0001 addition
    // expands the mesh slightly across the cell seam; it is not stored back.
    int cx = int(std::floor(anchor[0] / 70.f + .5f)), cz = int(std::floor(anchor[2] / 70.f + .5f));
    float height_scale = (1.f + .5f * std::sin(seconds * e.wave_speed)) * e.wave_scale;
    for (int x = cx - 3; x < cx + 3; x++)
        for (int z = cz - 3; z < cz + 3; z++) {
            bool distant = (x - cx) * (x - cx) + (z - cz) * (z - cz) > 2;
            Matrix m{
                .7001f,   0, 0, 0,      0, height_scale, 0,
                0,        0, 0, .7001f, 0, x * 70.f,     e.ocean_height - (distant ? .5f : 0.f),
                z * 70.f, 1};
            tiles.push_back({m, distant});
        }
    return tiles;
}
std::array<float, 2> ocean_uv_offset(float seconds) {
    // LEVEL_01.REL 0x1c6c64..0x1c6cd8, constants at 0x208f90..0x208f9c.
    if (!std::isfinite(seconds))
        return {};
    float t = seconds / 10.f;
    return {std::fmod(t / 17.f + std::cos(t / 40.f), 1.f),
            std::fmod(t / 22.f + std::sin(t / 45.f), 1.f)};
}
Matrix sky_transform(const LevelEnvironment &e, int model, Vec3 eye, float seconds) {
    float angle = model == e.rotating_sky ? seconds * e.sky_rotation_speed : 0;
    float c = std::cos(angle), s = std::sin(angle);
    // Source static GDE decoding already applied the controller's .01 scale.
    return {c,
            0,
            -s,
            0,
            0,
            1,
            0,
            0,
            s,
            0,
            c,
            0,
            e.sky_follow_xz ? eye[0] : 0,
            0,
            e.sky_follow_xz ? eye[2] : 0,
            1};
}
} // namespace hig
