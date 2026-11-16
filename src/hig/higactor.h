#pragma once
#include "higlevel.h"

namespace hig {
using Vec3 = std::array<float, 3>;
using Matrix = std::array<float, 16>;
struct BonePose {
    std::array<float, 4> rotation{0, 0, 0, 1};
    Vec3 scale{1, 1, 1}, translation{};
};
struct SkinBone {
    uint8_t parent = 255;
    // The bone's number in the game's shared list of bones (the skeleton's byte table at +0x10). A model worn on
    // another (an armour piece on the hero) takes the pose of the bones with its numbers (LEVEL_02 0x92920).
    uint8_t id = 0;
    Matrix inverse_bind{};
    BonePose bind;
};
struct SkinVertex {
    Vec3 position{}, normal{};
    std::array<float, 2> uv{};
    std::array<uint8_t, 6> bones{};
    std::array<float, 6> weights{};
};
struct SkinMesh {
    LevelMaterial material;
    std::vector<SkinVertex> vertices;
    std::vector<uint32_t> indices;
    uint8_t part = 0;
};
struct ActorModel {
    std::vector<SkinBone> bones;
    std::vector<SkinMesh> meshes;
};
struct AnimationKey {
    float time = 0;
    BonePose pose;
};
struct AnimationTrack {
    uint8_t bone = 0, channels = 0;
    std::vector<AnimationKey> keys;
};
struct AnimationClip {
    struct Event {
        std::string name;
        float time = 0;
        uint32_t sound = 0, flags = 0;
    };
    std::string name;
    float duration = 0;
    std::vector<AnimationTrack> tracks;
    std::vector<Event> events;
};
struct AnimationBank {
    std::vector<AnimationClip> clips;
};
// Bounded readers for retail DATA, usable by the editor and native runtime.
// Highest-detail skeleton/display list; no VU or guest code execution.
bool decode_actor(std::span<const uint8_t>, ActorModel &, std::string *error = nullptr);
bool decode_animation(std::span<const uint8_t>, AnimationBank &, std::string *error = nullptr);
// USA LEVEL_01.REL RUN gait (clip 3). Distance is horizontal travel per
// 1/60-second movement pass, not velocity or display-frame displacement.
// Shared with Theia; raw clip sampling itself must remain at the requested time.
float ratchet_run_playback_rate(float substep_distance);
void sample_animation(const ActorModel &, const AnimationClip *, float seconds, bool loop,
                      std::vector<BonePose> &);
// Overlay authored channels on one skeleton branch, leaving locomotion and
// unrelated limbs untouched. The native pistol uses branch 52 (right arm).
void sample_animation_layer(const ActorModel &, const AnimationClip &, float seconds, bool loop,
                            uint8_t branch, std::vector<BonePose> &);
// Local-space pose interpolation, including scale (weapon deformation). The
// destination can alias either input. No clocks, events or physics are advanced.
void blend_poses(std::span<const BonePose> from, std::span<const BonePose> to, float weight,
                 std::vector<BonePose> &out);
void pose_matrices(const ActorModel &, std::span<const BonePose>, std::vector<Matrix> &world,
                   std::vector<Matrix> &skin);
Matrix multiply(const Matrix &, const Matrix &);
Vec3 transform_point(const Vec3 &, const Matrix &);
// WAD fileinfo records carry asset hashes used by the GDE texture table.
struct ActorAsset {
    uint32_t hash = 0;
    std::string path;
    std::span<const uint8_t> bytes;
};
std::vector<ActorAsset> actor_assets(std::span<const uint8_t> wad);
} // namespace hig
