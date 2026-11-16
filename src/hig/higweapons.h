#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hig {
// The weapon / gadget tables of a level code module (/LVL/LEVEL_nn.REL), read as DATA; nothing is executed.
// Layouts and the functions that read each field: docs/SIZE_MATTERS_CAMPAIGN.md, "Weapons and gadgets: the tables"
// (tools/sm/sm_weapon_table.py prints them all).
struct WeaponTier {
    uint32_t class_hash = 0, class2_hash = 0; // the weapon's class, and a second (its pair or HUD model)
    uint32_t name_text = 0, description_text = 0, icon = 0, price = 0, xp_next = 0;
    uint32_t ammo_price = 0, max_ammo = 0, ammo_pickup = 0;
};
struct WeaponDefinition {
    std::string name;         // "blaster", "blitzgun", ...
    uint32_t type = 0;        // 2 weapon, 1 gadget, 3 passive item
    uint32_t first_level = 0; // the level index it first appears at (experience scaling, 0x250f8)
    std::vector<WeaponTier> tiers; // by weapon level: 0-3 the first playthrough, 4-7 challenge mode
    const WeaponTier &tier(unsigned level) const { return tiers[level < tiers.size() ? level : tiers.size() - 1]; }
};
struct DamageRow {
    int32_t kind = 0; // 0 a hit, 1 per frame
    float amount = 0; // in the enemy table's hit point units
    uint32_t effect = 0, strength = 0;
};
struct WeaponTables {
    // 23 entries in the module's order; the original's weapon id is the index + 2 (Blaster 2 ... BoxBreaker 24).
    std::vector<WeaponDefinition> weapons;
    std::vector<DamageRow> damage; // LEVEL_02 0x22dcd8; a shot carries a row index
    // The experience a level's enemies are worth in all, by level number (LEVEL_02 0x220dc0: {first playthrough,
    // challenge mode} a level; 0x18ac0). An enemy pays its class's share divided by the class's instances (0x831a8).
    std::array<uint32_t, 16> level_xp{}, level_xp_challenge{};
};
enum WeaponIndex : unsigned {
    WeaponBlaster, WeaponBlitzGun, WeaponBombGlove, WeaponAgentsGlove, WeaponBeeMineGlove, WeaponShieldCharger,
    WeaponShockRocket, WeaponCrossbowGun, WeaponFlamethrower, WeaponLaserTracer, WeaponSuckCannon, WeaponMootator,
    WeaponMiniTurretGlove, WeaponRyno, GadgetHypershot, GadgetSproutOMatic, GadgetPolarizer, GadgetPda,
    ItemShrinkRay, ItemBoltGrabber, ItemGrindBoots, ItemMapOMatic, ItemBoxBreaker, WeaponCount
};
bool decode_weapon_tables(std::span<const uint8_t> module, WeaponTables &out, std::string *error = nullptr);
} // namespace hig
