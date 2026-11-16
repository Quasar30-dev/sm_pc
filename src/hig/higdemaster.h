#pragma once
#include "higcollision.h"
#include "higlevel.h"

namespace hig {
struct DemasterImage : GimImage {
    bool srgb = false; // decode stored colour before filtering and lighting
};
struct DemasterVertex {
    std::array<float, 3> position{};
    std::array<float, 2> uv{};
    std::array<float, 3> color{1, 1, 1}; // baked diffuse lighting / material tint
    std::array<float, 2> mask_uv{};
};
struct DemasterTriangle {
    std::array<DemasterVertex, 3> vertices;
    int texture = -1;
    int material = -1;
};
struct DemasterLayer {
    int texture = -1, height = -1;
    std::array<float, 3> hsv{.5f, .5f, .5f};
    std::array<float, 2> tile{1, 1};
    float contrast = 1, threshold = .33f;
    bool invert = false;
};
struct DemasterMaterial {
    std::array<DemasterLayer, 4> layers; // base, R, G, B
    int mask = -1;
    std::array<float, 2> mask_tile{1, 1};
};
struct DemasterOptions {
    uint32_t triangle_budget = 1200;
    uint32_t tile_size = 8;
    float grid_step = .05f;
    float scale = 1;
    std::array<float, 3> offset{};
    bool fit_to_host = true;
    bool remove_host_scenery = true;
};
struct DemasterResult {
    std::vector<uint8_t> wad;
    LevelData preview;
    Collision collision;
    uint32_t source_triangles = 0, triangles = 0, capacity = 0, textures = 0;
    uint32_t geometry_bytes = 0, collision_bytes = 0, compressed_bytes = 0;
    float applied_scale = 1, applied_grid = 0;
    std::array<float, 3> translation{};
    std::vector<std::string> notes;
};
// Convert into the host's existing PS2 packet/texture/collision allocation.
// Asset addresses, WAD sections, ISO extents and gameplay code stay in place.
// Failure publishes nothing. The native output is reparsed before success.
bool demaster_wad(std::span<const uint8_t> host, std::span<const DemasterTriangle> triangles,
                  std::span<const DemasterImage> textures, const DemasterOptions &options,
                  DemasterResult &out, std::string *error = nullptr,
                  std::span<const DemasterMaterial> materials = {});
bool tjzip_compress(std::span<const uint8_t> image, std::vector<uint8_t> &out,
                    std::string *error = nullptr);
// Copies the ISO to a NEW path and changes one existing WAD extent only.
// Refuses to overwrite either the source ISO or an existing output.
bool write_demaster_iso(const std::wstring &source, const std::string &disc_path,
                        std::span<const uint8_t> wad, const std::wstring &output,
                        std::string *error = nullptr);
} // namespace hig
