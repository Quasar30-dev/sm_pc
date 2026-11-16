#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "higwad.h"

namespace hig {

// Level content extracted from a decompressed HIG! WAD image: GDE render
// meshes (VIF streams reassembled into triangle lists) + GIM textures,
// bound per material via the model texture tables' name hashes.
// Byte layouts: docs/HIG_FORMATS.md ("Mesh sections", "GIM binding").

// ---- GIM ---------------------------------------------------------------------

struct GimImage {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width*height*4
};

// Decode a Sony "MIG.00.1PSP" picture (RGBA5650/5551/4444/8888, INDEX4/8,
// normal or PSP-swizzled pixel order).
bool decode_gim(std::span<const uint8_t> gim, GimImage& out,
                std::string* err = nullptr);

// ---- GDE meshes ---------------------------------------------------------------

struct LevelMaterial {
    std::string name;             // Maya shading-group / texture record name
    uint32_t tex_hash = 0;        // texture-table hash (0 = none)
    int texture = -1;             // index into LevelData::textures
    std::vector<float> positions; // xyz per vertex (world units)
    std::vector<float> normals;   // signed VIF normals; zero = generate from faces
    std::vector<float> uvs;       // uv per vertex
    std::vector<uint8_t> rgba;    // vertex colour per vertex (4 bytes)
    std::vector<uint32_t> indices;
    float alpha_test = 0.0f;
    bool alpha_blend = false;     // GIF PRIM.ABE, with texture/vertex alpha
};

struct LevelModel {
    std::string name;             // original GDE asset basename
    std::string source_path;
    uint32_t hash = 0;
    int section = 0;              // WAD slot index
    bool terrain = false;         // world geometry placed at identity
    // Layer classification (drives editor zone + default visibility):
    //  "terrain" = the playable arena (section 0)
    //  "shell"   = atmosphere/sky layers (section 1); background draw in the host
    //  "ocean"   = WaterWaves tile asset, exact BOG binding and sea level
    //  "shrub"  = local foliage templates, NOT world-space canopy geometry
    //  "prop"   = local rigid models; unresolved placements use a review grid
    std::string kind = "terrain";
    std::vector<LevelMaterial> materials;
    std::vector<LevelMaterial> distant_materials; // recovered WaterWaves outer-tile LOD
    std::vector<std::array<float, 16>> instances; // exact BOG rigid/shrub placements
    struct NodeLight {
        std::array<int8_t,3> direction{};
        std::array<uint8_t,4> rgba{};
    };
    struct InstanceLighting {
        uint32_t source_offset = 0;
        std::array<uint8_t,4> ambient{};
        std::vector<NodeLight> lights;
    };
    // Parallel to instances only when the original BOG receiver was decoded.
    std::vector<InstanceLighting> instance_lighting;
};

struct LevelTexture {
    uint32_t hash = 0;            // asset hash (as referenced by materials)
    std::string name;             // basename from the GIM fileinfo block
    GimImage image;
};

// Native collision assets are inventoried independently of render meshes.
// Their payload is preserved; higcollision.h decodes/cooks native COL v8.
struct LevelCollisionAsset {
    std::string source_path;
    uint32_t hash = 0, version = 0;
    uint32_t offset = 0, size = 0; // payload in the decompressed WAD
};

// ---- BOG scene graph (instance placement) -----------------------------------

// One placed object instance: a 4x4 world matrix (row-major, row 3 =
// translation) + the model-def it belongs to.
struct BogInstance {
    float matrix[16]{};
    uint32_t model_def = 0;       // index into LevelData::bog_defs
    uint32_t source_offset = 0, flags = 0;
    // Authored 52-byte variant selected by flags' low byte. These are asset
    // signatures, not pointers into the decoded WAD.
    uint32_t model_hash = 0, animation_hash = 0, collision_hash = 0, variant_hash = 0;
    // Bounded source properties, with size from the BOG class definition.
    std::vector<uint8_t> properties;
};
struct BogTrigger {
    uint32_t source_offset=0,source_id=0; // identifier semantics still under RE
    std::array<float,16> matrix{},inverse{};
    uint8_t shape=0;
};
struct BogCurve {
    uint32_t source_offset = 0, source_id = 0;
    uint16_t flags = 0;
    std::string name;
    std::vector<std::array<float, 3>> points;
    // Optional per-point data pointer and its first word. NavBound interprets
    // that word as a jump-clearance float (LEVEL_01.REL 0x18eab0).
    std::vector<uint32_t> point_data_offsets, point_data_words;
};
bool parse_bog_curves(std::span<const uint8_t>, std::vector<BogCurve> &, std::string &error);
bool trigger_contains(const BogTrigger &,std::array<float,3> point);
bool parse_bog_triggers(std::span<const uint8_t>,std::vector<BogTrigger> &,std::string &error);

// A model definition referenced by instances. The `hash` is the engine's
// asset signature (resolved to a GDE mesh via the global object registry —
// not stored in the level WAD, so the mesh link is not yet recoverable
// offline). `placed` = at least one instance has a non-origin translation.
struct BogModelDef {
    uint32_t hash = 0;
    uint32_t first_instance = 0;  // index into LevelData::bog_instances
    uint32_t count = 0;
    uint32_t flags = 0;
    bool placed = false;
    std::string name;             // resolved object class ("" if unknown)
};

// Recovered gameplay environment controllers; indices address LevelData::models.
// WaterWaves uses a tiled GDE mesh and BOG properties, not a terrain plane.
struct LevelEnvironment {
    bool sky_follow_xz = false;
    int rotating_sky = -1;
    float sky_rotation_speed = 0;
    int ocean_model = -1;
    float ocean_height = 0, wave_scale = 0, wave_speed = 0;
};

struct LevelLighting {
    struct Directional { std::array<float,3> direction{};std::array<uint8_t,4> rgba{}; };
    struct Point { std::array<float,3> position{};std::array<uint8_t,4> rgba{};float radius=0; };
    bool valid=false;
    std::array<uint8_t,4> ambient{};
    std::vector<Directional> directional;
    std::vector<Point> points;
};

struct LevelData {
    std::string name;             // exact level name ("levelMP02")
    std::vector<LevelModel> models;
    std::vector<LevelTexture> textures;
    std::vector<LevelCollisionAsset> collision_assets;
    std::vector<BogInstance> bog_instances;   // real object placements
    std::vector<BogModelDef> bog_defs;
    std::vector<BogTrigger> triggers;
    std::vector<BogCurve> curves;
    LevelEnvironment environment;
    LevelLighting lighting;
    std::vector<std::string> warnings; // unsupported/malformed assets, never guessed
    uint32_t unsupported_models = 0;
    uint32_t static_instances = 0;
    uint32_t unresolved_static_instances = 0;
};

// Parse the BOG level-container section (instance transforms + model-defs).
bool parse_bog(std::span<const uint8_t> bog, std::vector<BogInstance>& insts,
               std::vector<BogModelDef>& defs, std::string* err = nullptr);

// Exact level name from the WAD's dev conf path ("levelMP02"); "level"
// when the header carries none.
std::string level_name(const HigWad& hdr);

// One static / rigid / shrub GDE asset by its bytes, for object models that live outside the level's own model
// list (the shared SP.WAD and GLOBAL.WAD: crates, bolts, pickups). Materials carry `tex_hash`; `texture` is unbound.
// The highest-detail LOD; break pieces (stream selector bit 4) are left out as in the level's models.
bool decode_rigid_model(std::span<const uint8_t> gde, LevelModel& out, std::string* err = nullptr, unsigned lod = 0);
// GDE header type word (0x10000000 static, 3 shrub, 4 rigid; others skinned), 0 when the header is not a GDE's.
uint32_t gde_type(std::span<const uint8_t> gde);

// Extract all renderable content from a decompressed WAD image.
bool build_level(std::span<const uint8_t> image, const HigWad& hdr,
                 LevelData& out, std::string* err = nullptr);

}  // namespace hig
