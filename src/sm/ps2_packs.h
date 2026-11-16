#pragma once
// Original UYA carried packs, attached to either the original SM rig or a
// trilogy hero. SM movement remains owned by Gameplay; this adapter owns the
// original equipment callbacks, meshes, textures and exhaust lifetime.
#include "hig/higactor.h"
#include <memory>
#include <string>
#include <vector>

namespace sm {
class Gameplay;
struct PlayerAssets;
struct Scene;
struct Ps2PackStats {
    unsigned ticks=0, requests=0;
    int equipped=0, desired=0, pack_sequence=0, clank_sequence=0;
    size_t particles=0;
    uint32_t coverage=0;
};
class Ps2Packs {
public:
    Ps2Packs();
    ~Ps2Packs();
    bool load(const std::string& uya_iso,const hig::ActorModel& sm_ratchet,
              std::vector<hig::LevelTexture>& textures,std::string& error);
    bool loaded() const;
    bool replaces_native() const;
    void reset_transients();
    void tick(const Gameplay&,const PlayerAssets&,float dt=1.f/30);
    void append(const hig::Matrix& rendered_back,Scene&) const;
    void append_effects(hig::Vec3 eye,Scene&) const;
    Ps2PackStats stats() const;
    std::string status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
