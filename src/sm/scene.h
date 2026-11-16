#pragma once
#include "world.h"

namespace sm {
// Owned CPU scene, independent of a graphics API. Retail material composition
// remains research work; original BOG node/actor lights feed these passes.
struct Vertex {
    float position[3], uv[2];
    uint8_t color[4];
};
struct Batch {
    uint32_t first = 0, count = 0;
    int texture = -1;
    float alpha_test = 0;
    bool blend = false, shell = false;
    bool additive = false, effect = false, effect_wrap = false;
    std::array<float,2> uv_offset{};
    bool ocean = false, ocean_distant = false;
    int model = -1;
    std::array<float, 3> center{};
};
struct Scene {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Batch> batches;
    std::array<float, 3> minimum{}, maximum{};
    size_t instances = 0, unplaced_models = 0;
    hig::LevelEnvironment environment;
    const hig::LevelLighting* lighting=nullptr; // borrowed only while appending actor geometry
};
bool build_scene(const hig::LevelData &, Scene &, std::string &error);
} // namespace sm
