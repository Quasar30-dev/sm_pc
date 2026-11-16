#pragma once
#include "hig/higactor.h"
#include "hig/higclass.h"
#include "hig/higcollision.h"
#include "hig/higweapons.h"
#include "hig/higarmor.h"
#include "scene.h"
#include <string_view>

namespace sm {
// Decoded assets of every gameplay object class a level places or pools, found by the BOG instances' own asset
// signatures (model / animation bank / collision) in the level WAD, then SP.WAD and GLOBAL.WAD. One entry per
// (model, animation bank) pair. Nothing here depends on the class: behaviour lives in objects.cpp.
struct ObjectModel {
    uint32_t model = 0, animation = 0;
    std::string name; // asset basename without its extensions ("crate", "Pygmy01Master")
    bool skinned = false;
    hig::ActorModel actor;
    hig::AnimationBank animations;
    hig::LevelModel rigid;
    hig::Vec3 minimum{}, maximum{}; // rest-pose bounds in model space
    hig::Collision box;             // the bounds as a closed box, for solid objects with no COL asset
    // First clip whose (eight-character, upper-cased) name starts with `prefix`; -1 when the bank has none.
    int clip(std::string_view prefix) const;
    float height() const { return maximum[1] - minimum[1]; }
    float radius() const; // horizontal half-extent
};
struct ObjectAssets {
    std::vector<ObjectModel> models;
    struct Col {
        uint32_t hash = 0;
        hig::Collision collision;
    };
    std::vector<Col> collisions;
    std::vector<hig::ClassRecord> classes; // the level module's class records (property schemas)
    unsigned level = 0;                    // the WAD's number (/WAD/02.WAD: 2)
    bool has_tuning = false;
    std::array<hig::LevelTuning, 10> tuning{};
    const hig::LevelTuning &level_tuning() const { return tuning[std::clamp(level, 1u, 10u) - 1]; }
    bool has_weapons = false;
    hig::WeaponTables weapons; // the module's weapon / gadget definitions and damage rows
    bool has_armor = false;
    hig::ArmorTables armor; // the module's armour pieces, sets and their protection
    // The worn pieces' models, by place (0 body armor, 1 helmet, 2 / 3 gloves, 4 / 5 boots) and set (1..7): the
    // pooled instances of the six piece classes, told apart by their variant; -1 where the level has none.
    std::array<std::array<int, 8>, 6> armor_models{};
    std::vector<std::string> warnings;     // models that did not decode; never fatal
    int model(uint32_t model_hash, uint32_t animation_hash) const;
    // The model a class's instances carry (its first variant): weapons in hand, shots, spawned creatures.
    std::vector<std::pair<uint32_t, int>> class_models;
    int class_model(uint32_t class_hash) const {
        for (const auto &[hash, index] : class_models)
            if (hash == class_hash)
                return index;
        return -1;
    }
    const hig::Collision *collision(uint32_t hash) const;
    const hig::ClassRecord *record(uint32_t class_hash) const;
};
bool load_object_assets(const hig::IsoFs &, const World &, ObjectAssets &, std::vector<hig::LevelTexture> &,
                        std::string &error);
// A rigid model at a place, lit by the scene's authored lighting when it is set.
void append_rigid_model(const hig::LevelModel &, const hig::Matrix &, Scene &, uint8_t alpha = 255);
} // namespace sm
