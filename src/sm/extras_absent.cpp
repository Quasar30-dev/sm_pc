// The borrowed play options of sm_pc (src/sm/extras.h) as this repository has them: not at all. Their classes keep
// the shape the rest of the game calls - a Ratchet, weapons, packs and a follow camera read from the other PS2
// Ratchet & Clank discs - and load nothing, so every caller takes the path it takes when a disc is not there.
#include "camera.h"
#include "ps2_packs.h"
#include "ps2_ratchet.h"
#include "ps2_weapons.h"

namespace sm {
namespace {
const char *const ABSENT = "This build of sm_pc does not have the play options borrowed from the other games.";
}

// ---- the Ratchet of another game --------------------------------------------------------------------------------
struct Ps2Ratchet::Impl {};
Ps2Ratchet::Ps2Ratchet() = default;
Ps2Ratchet::~Ps2Ratchet() = default;
bool Ps2Ratchet::load(const Ps2RatchetSettings &, const hig::ActorModel &, std::vector<hig::LevelTexture> &, std::string &error) {
    error = ABSENT;
    return false;
}
bool Ps2Ratchet::loaded() const { return false; }
std::string Ps2Ratchet::description() const { return {}; }
void Ps2Ratchet::tick(Gameplay &, const PlayerAssets &, const hig::PadState &, float, bool, float) {}
bool Ps2Ratchet::hovering() const { return false; }
bool Ps2Ratchet::charging() const { return false; }
bool Ps2Ratchet::charge_dashing() const { return false; }
Ps2HoverFxStats Ps2Ratchet::hover_effects_stats() const { return {}; }
bool Ps2Ratchet::controller_begin(Gameplay &) { return false; }
hig::PadState Ps2Ratchet::controller_pad(const hig::PadState &pad) { return pad; }
const ps2game::HeroState *Ps2Ratchet::controller() const { return nullptr; }
void Ps2Ratchet::sample_joints(const hig::Matrix &, float, std::vector<hig::Matrix> &) const {}
void Ps2Ratchet::append(const Gameplay &, const PlayerAssets &, const hig::Matrix &, float, Scene &, std::vector<hig::Matrix> &) const {}
void Ps2Ratchet::render_effects(const Gameplay &, float, hig::Vec3, Scene &) const {}
std::string Ps2Ratchet::state_name() const { return {}; }

// ---- the weapons and wrenches of another game -------------------------------------------------------------------
struct Ps2Weapons::Impl {};
Ps2Weapons::Ps2Weapons() = default;
Ps2Weapons::~Ps2Weapons() = default;
bool Ps2Weapons::load(const std::string &, const hig::ActorModel &, std::vector<hig::LevelTexture> &, std::string &error) {
    error = ABSENT;
    return false;
}
bool Ps2Weapons::loaded() const { return false; }
bool Ps2Weapons::enabled() const { return false; }
void Ps2Weapons::set_enabled(bool) {}
const std::vector<Ps2WeaponChoice> &Ps2Weapons::choices() const {
    static const std::vector<Ps2WeaponChoice> none;
    return none;
}
bool Ps2Weapons::equip(int, std::string &error) {
    error = ABSENT;
    return false;
}
int Ps2Weapons::equipped() const { return 0; }
int Ps2Weapons::ammo() const { return 0; }
int Ps2Weapons::max_ammo() const { return 0; }
std::string Ps2Weapons::status() const { return {}; }
Ps2WeaponStats Ps2Weapons::stats() const { return {}; }
bool Ps2Weapons::set_wrench(int word, const hig::ActorModel &, std::vector<hig::LevelTexture> &, std::string &error) {
    if (!word)
        return true; // Size Matters' own wrench: it never left
    error = ABSENT;
    return false;
}
int Ps2Weapons::wrench() const { return 0; }
void Ps2Weapons::append_wrench(const hig::Matrix &, Scene &) const {}
void Ps2Weapons::set_volume(float) {}
void Ps2Weapons::reset_transients() {}
void Ps2Weapons::tick(Gameplay &, const PlayerAssets &, const hig::PadState &, bool, float) {}
void Ps2Weapons::append_held(const hig::Matrix &, Scene &) const {}
void Ps2Weapons::append_effects(hig::Vec3, Scene &) const {}
hig::Vec3 Ps2Weapons::muzzle() const { return {}; }
hig::Vec3 Ps2Weapons::muzzle(const hig::Matrix &) const { return {}; }

// ---- the packs of another game ----------------------------------------------------------------------------------
struct Ps2Packs::Impl {};
Ps2Packs::Ps2Packs() = default;
Ps2Packs::~Ps2Packs() = default;
bool Ps2Packs::load(const std::string &, const hig::ActorModel &, std::vector<hig::LevelTexture> &, std::string &error) {
    error = ABSENT;
    return false;
}
bool Ps2Packs::loaded() const { return false; }
bool Ps2Packs::replaces_native() const { return false; }
void Ps2Packs::reset_transients() {}
void Ps2Packs::tick(const Gameplay &, const PlayerAssets &, float) {}
void Ps2Packs::append(const hig::Matrix &, Scene &) const {}
void Ps2Packs::append_effects(hig::Vec3, Scene &) const {}
Ps2PackStats Ps2Packs::stats() const { return {}; }
std::string Ps2Packs::status() const { return {}; }

// ---- the follow camera of another game --------------------------------------------------------------------------
void UyaFollowCamera::reset(const Gameplay &, float, float) {}
void UyaFollowCamera::add_look(float, float) {}
void UyaFollowCamera::tick(const Gameplay &, const hig::PadState &, float, bool) {}
UyaCameraPose UyaFollowCamera::pose(float) const { return {}; }
} // namespace sm
