#pragma once
// Original UYA base Shock Blaster/Nitro runtime and assets in Size Matters.
// SM retains its own inventory, wrench and movement. See ps2_weapons.cpp for
// the coordinate/collision/damage adaptation at this cross-game boundary.
#include "hig/higactor.h"
#include "hig/higinput.h"
#include <memory>
#include <string>
#include <vector>

namespace sm {
class Gameplay;
struct PlayerAssets;
struct Scene;
struct Ps2WeaponChoice { int id = 0; std::string name; };
struct Ps2WeaponStats {
    unsigned ticks = 0, shots = 0, enemy_hits = 0;
    size_t projectiles = 0, effects = 0;
    unsigned coverage = 0, effect_coverage = 0;
};
class Ps2Weapons {
public:
    Ps2Weapons();
    ~Ps2Weapons();
    // Empty path uses animport::DataPaths.uya. Original ISO only. Failure
    // retains the previous adapter and does not alter the SM weapon/ammo.
    bool load(const std::string& uya_iso, const hig::ActorModel& sm_ratchet,
              std::vector<hig::LevelTexture>& textures, std::string& error);
    bool loaded() const;
    bool enabled() const;
    void set_enabled(bool);
    const std::vector<Ps2WeaponChoice>& choices() const;
    bool equip(int item, std::string& error);
    int equipped() const;
    int ammo() const;
    int max_ammo() const;
    std::string status() const;
    Ps2WeaponStats stats() const;
    // UYA's OmniWrenches in place of SM's wrench model, in SM's own wrench moves (the swings, the throw, the
    // hyper-strike; hits and damage stay SM's, so whatever acts on its wrench acts on these). `word` is the
    // wrench's entry of the UYA disc's ARMOR global: 118 silver, 122 blue, 126 dark / orange (the one UYA's single
    // player loads); 0 gives SM's wrench back. Needs load(); the UYA guns need not be enabled. Its textures
    // replace this adapter's wrench slots in `textures`: the host uploads them again.
    // SM's animations were made for SM's wrench: the OmniWrench is turned in the grip so its handle-to-head axis
    // and its flat side lie where `sm_wrench`'s do (both measured from the meshes).
    bool set_wrench(int word, const hig::ActorModel& sm_wrench, std::vector<hig::LevelTexture>& textures,
                    std::string& error);
    int wrench() const;
    // The wrench where SM would draw its own: `sm_frame` is the frame SM gives its wrench actor (the right grip
    // in the hand, the spinning frame in flight).
    void append_wrench(const hig::Matrix& sm_frame, Scene&) const;
    // The guns' own sounds (the UYA disc's 989snd banks, through the PS2 engine's sound code and its own output
    // voices): 0 silences them, 1 is full. The host passes its effects volume.
    void set_volume(float);
    // Clear scene-bound projectiles, FX, attachment and targets. Inventory and
    // selected gun survive, including when switching back to SM weapons.
    void reset_transients();
    void tick(Gameplay&, const PlayerAssets&, const hig::PadState&, bool controls,
              float dt = 1.f / 30);
    void append_held(const hig::Matrix& sm_grip, Scene&) const;
    void append_effects(hig::Vec3 eye, Scene&) const;
    hig::Vec3 muzzle() const;
    // Same original gun socket evaluated at a supplied rendered grip. Pure;
    // useful for interpolation and checking simulation/render agreement.
    hig::Vec3 muzzle(const hig::Matrix& sm_grip) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sm
