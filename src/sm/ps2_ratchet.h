#pragma once
// Size Matters PC port: optional PS2 Ratchet (R&C1 / GC / UYA armour and animation sets) in place of SM's own
// model, a full rig swap rendered from ps2game (docs/ps2/ANIM_PORT.md "Size Matters").
//
// Evidence (animport_sm_probe): SM's 112-bone Ratchet IS the PS2 trilogy skeleton - same parents and bind
// positions (bones 0-97 equal, SM inserts a dummy bone 98, SM 99-111 = PS2 98-110), axes permuted (SM x/y/z =
// PS2 y/z/x) and SM metres = PS2 model units x 1.4174e-4. SM's attachment bones 5 (back) and 56 (right grip) are
// the PS2 joints with the same numbers.
//
// Animation: the PS2 sequences the PS2 hero plays for the matching state (UYA ids from src/ps2game/hero.cpp,
// shared by GC and R&C1 for the basic set), with UYA's blend times and run/walk rates; states without a PS2
// sequence (SM's wrench combo/throw, blaster poses, crank, zipline, hyper-strike) use SM's own evaluated pose
// moved onto the PS2 rig (same skeleton). Added moves: hoverboots (ACiT set retargeted, src/animport/hoverboots.h)
// and the UYA/GC charge boots (double-tap R1; UYA mode 0x80 / GC mode 0x7b, docs/ps2/ANIM_PORT.md).
#include "gameplay.h"
#include "hig/higactor.h"

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace sm {

struct Ps2RatchetSettings {
    std::string model = "sm";       // "sm" (Size Matters default) | "uya" | "gc" | "rac1": armour source
    std::string animations = "uya"; // "uya" | "gc" | "rac1": sequence set (identical skeleton); "sm": SM's own
    int armor = 0;                   // armour index (UYA/GC ARMOR global)
    std::string iso_uya, iso_gc, iso_rac1;  // PS2 discs (read-only); defaults under R&C_PS2PC/extracted
    std::string hoverboots;          // hoverboot pack (animport hoverboots --to <game>); empty = default path
    bool charge_boots = true;        // UYA/GC charge boots: double-tap R1 while standing or running
    int skin = 0;                    // model "sm": which of Size Matters' own skins (load_hero_skin), 0 = default
    // The imported game's own hero controller (ps2game::hero_tick: its run, jumps, flips, pack moves, charge boots
    // and animation requests) owns Ratchet on foot. SM keeps the weapons, the wrench, damage, water and its
    // mechanisms. The animation set is then the model's own game's.
    bool own_controller = false;
    bool active() const { return model != "sm"; }
};
std::filesystem::path ps2_ratchet_settings_path();  // %LOCALAPPDATA%\Theia\SizeMatters\ratchet.ini
Ps2RatchetSettings read_ps2_ratchet_settings(const std::filesystem::path &);
bool save_ps2_ratchet_settings(const std::filesystem::path &, const Ps2RatchetSettings &, std::string &error);

struct Ps2HoverFxStats {
    bool loaded = false, active = false;
    uint64_t ticks = 0;
    size_t particles = 0;
    bool thrusting = false;
};

class Ps2Ratchet {
  public:
    Ps2Ratchet();
    ~Ps2Ratchet();
    // Loads the PS2 Ratchet and appends his textures to `textures` (their base index is remembered).
    bool load(const Ps2RatchetSettings &, const hig::ActorModel &sm_ratchet, std::vector<hig::LevelTexture> &textures,
              std::string &error);
    bool loaded() const;
    std::string description() const;
    // One SM simulation tick (30 Hz): the PS2 state follows the gameplay state; the PS2 clock runs two 60 Hz
    // ticks. `hover_toggle` equips/removes the hoverboots (when a pack is loaded).
    void tick(Gameplay &, const PlayerAssets &, const hig::PadState &, float camera_yaw, bool hover_toggle, float dt = 1.f / 30);
    bool hovering() const;
    bool charging() const;                                     // charge-boot dash or its bonk
    bool charge_dashing() const;                               // dash only: PS2 camera mode 0x80
    Ps2HoverFxStats hover_effects_stats() const;
    bool owns_movement() const { return hovering() || charging(); }  // SM's controller gets a neutral pad
    // The game's own controller (settings.own_controller). Call before SM's tick: true when it moves Ratchet this
    // tick; SM's controller then gets the pad without its movement (controller_pad), and his velocity is cleared
    // so SM's own integration adds nothing.
    bool controller_begin(Gameplay &);
    static hig::PadState controller_pad(const hig::PadState &);
    // Its state while it owns him (the carried packs follow it), else null.
    const ps2game::HeroState *controller() const;
    bool hands_free() const { return hovering() || charging(); }     // SM's held weapon is hidden
    // Pure attachment sampler shared by rendering and weapon simulation. Uses
    // the full source pose, before the charge-boot body's foot contraction.
    void sample_joints(const hig::Matrix &actor, float alpha, std::vector<hig::Matrix> &joints) const;
    // Draws Ratchet and his active hover/charge boots into `scene` at the gameplay transform and returns the 112
    // SM-indexed bone matrices for SM's attachments (row-vector world, as append_actor returns).
    void append(const Gameplay &, const PlayerAssets &, const hig::Matrix &actor, float alpha, Scene &scene,
                std::vector<hig::Matrix> &joints) const;
    // Original UYA attached blue exhaust, adapted to SM world units and billboards.
    void render_effects(const Gameplay &, float alpha, hig::Vec3 eye, Scene &) const;
    std::string state_name() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sm
