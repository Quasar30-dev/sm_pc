#pragma once
#include "hig/higactor.h"
#include "object_assets.h"
#include "scene.h"

namespace sm {
enum class EnemyKind { Crab, TrainingBot, Torso, Head, Generic };
enum class EnemyState { Idle, Alert, Chase, Attack, Hurt, Dead };
struct EnemyModel {
    EnemyKind kind{};
    uint32_t hash = 0;
    std::string name;
    hig::ActorModel model;
    hig::AnimationBank animations;
    unsigned idle = 0, alert = 0, run = 0, attack = 0, hurt = 0, death = 0;
    float radius = .45f, height = 1.f, speed = 2.5f, attack_distance = 1.4f;
    unsigned health = 3, bolt_reward = 3, damage = 1;
    float xp_reward = 0; // weapon experience for defeating one (0x831a8)
    bool vanish = false; // no death clip: gone when defeated
};
struct EncounterAssets {
    std::vector<EnemyModel> models;
    hig::ActorModel vendor;
    hig::AnimationBank vendor_animations;
    struct Prop {
        uint32_t hash = 0;
        hig::ActorModel actor;
        hig::AnimationBank animations;
        hig::LevelModel rigid;
        hig::Collision collision;
    };
    std::vector<Prop> props;
    ObjectAssets objects; // every other placed or pooled class of the level (objects.cpp)
};
bool load_mechanism_assets(const hig::IsoFs &, const World &, EncounterAssets &,
                           std::vector<hig::LevelTexture> &, std::string &);
// Every other enemy class of the level (a class whose record carries a bolt share), from the object assets:
// its own model and clips by name, hit points and damage from the module's level table, bolts from its share.
// Movement and attack patterns are the native chase / strike until the class's own state machine is translated.
void add_generic_enemies(const World &, EncounterAssets &);
bool load_encounter_assets(const hig::IsoFs &, const std::string &level, EncounterAssets &,
                           std::vector<hig::LevelTexture> &, std::string &);
struct Enemy {
    unsigned model = 0;
    float health = 0;
    uint32_t source_offset = 0;
    uint32_t detect_area = 0; // the class's `detectArea` trigger (BOG offset): the hero inside it is noticed
    // The class flies (Kalidon's Drone01, known here by its `yFloor` property): it needs no ground under it and
    // keeps the height it was placed at. NATIVE: the drone's own flight (LEVEL_03 0x160b28) is not translated.
    bool hovers = false;
    // Seen but asleep: it stands in its place and notices nothing until something wakes it (a robot in one of
    // Kalidon's showcases until the glass breaks).
    bool dormant = false;
    // It keeps its station (Kalidon's FactoryWorker, update 0x16df60): it neither walks nor turns, and works its
    // tools while the hero is before it or while its conveyor stands still - which the conveyor (`station`: the
    // BOG offset of its `junkController`) tells it each frame it is so (`station_busy`).
    bool stationed = false, station_busy = false;
    uint32_t station = 0;
    hig::Vec3 position{}, previous{}, home{}, velocity{};
    float heading = 0, previous_heading = 0, time = 0, scale = 1, alert_radius = 10, cooldown = 0;
    EnemyState state = EnemyState::Idle;
    bool grounded = false, action_fired = false, hidden = false;
    // What an armour set's wrench leaves on it: fire and poison (damage a second for a time), a block of ice.
    float burn_time = 0, burn_rate = 0, poison_time = 0, poison_rate = 0, frozen = 0;
    // The class's `initPath` (a BOG curve): the way it comes in by, out of its hut or niche, before it does anything
    // else. `entry_trigger`: it waits for the hero in that trigger first (`initPathTrigger`).
    std::vector<hig::Vec3> entry;
    unsigned entry_at = 0;
    uint32_t entry_trigger = 0;
};
unsigned enemy_clip(const EnemyModel &, EnemyState);
const char *enemy_state_name(EnemyState);
} // namespace sm
