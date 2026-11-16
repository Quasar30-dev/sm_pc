#pragma once
#include "gameplay.h"
#include "object_assets.h"
#include <unordered_map>

namespace sm {
class ObjectWorld;
struct Object;
// The gameplay objects of any level, from its BOG placements and the class records of its code module.
//
// Every placed instance whose class no other native system owns becomes an Object: drawn with its own model (its
// idle clip when it has an animation bank) and solid when it has collision. Instances at the exact origin are the
// game's pools (shots, spare pickups, break pieces): templates here, never drawn in place.
//
// Behaviour comes from two places: the crate / pickup kinds below (objects.cpp), and class behaviours registered by
// class name (object_classes.cpp for classes shared by levels, classes_levelNN.cpp for a level's own). A behaviour is
// the native translation of the class's original init / update callbacks; each cites the functions it was read from.
enum class ObjectKind : uint8_t {
    Prop,  // at its authored place, as authored (plus its class behaviour, when it has one)
    Logic, // no body: controllers, pooled templates, classes owned by another native system
    Crate,
    CrateAmmo,
    CrateHealth,
    CrateExplosive,
    TitaniumBolt,
    ArmorPickup,
};
struct ObjectClass {
    const char *name; // as in the module's class record; matched by the BOG class hash
    // Once, on the first tick, after every object and enemy exists (links resolve).
    void (*start)(ObjectWorld &, Object &, Gameplay &) = nullptr;
    void (*tick)(ObjectWorld &, Object &, Gameplay &, float dt) = nullptr;
    bool body = true; // false: the class is never drawn or solid here even when it carries a model
};
std::span<const ObjectClass> shared_object_classes();  // object_classes.cpp
std::span<const ObjectClass> level02_object_classes(); // classes_level02.cpp (Ryllus)
std::span<const ObjectClass> level03_object_classes(); // classes_level03.cpp (Kalidon)
std::span<const ObjectClass> level04_object_classes(); // classes_level04.cpp (Metalis)
std::span<const ObjectClass> level05_object_classes(); // classes_level05.cpp (Dreamtime)
std::span<const ObjectClass> sprout_object_classes();  // classes_sprout.cpp (the Sprout-O-Matic's plants)
std::span<const ObjectClass> clankbot_object_classes(); // classes_clankbot.cpp (Clank's gadgebots)
std::span<const ObjectClass> grind_object_classes();    // classes_grind.cpp (the Grind Lock and its rails)
struct Object {
    uint32_t source = 0, class_hash = 0; // BOG instance offset; CRC32 of the class name
    uint32_t variant_hash = 0;           // the instance's model variant (which of the class's models it wears)
    std::string name;
    hig::ClassProps props;
    ObjectKind kind = ObjectKind::Prop;
    const ObjectClass *behaviour = nullptr;
    int model = -1, variant = 0;
    uint32_t flags = 0; // the instance's BOG flags (bits 8..15: a class-specific index, e.g. a movie's letter)
    const hig::Collision *collision = nullptr;
    hig::Matrix base{}, matrix{}, previous{};
    bool visible = true, solid = false, alive = true, unsettled = false;
    // A pooled instance (placed at the origin): a template until ObjectWorld::spawn takes it, with the behaviour of
    // its class. `target`: the player's attacks can hit it (ObjectWorld::hit).
    bool pooled = false, target = false;
    // His attacks reach it (with `solid` and `target`), but it is not in his way: its class keeps him off it itself
    // (Kalidon's boss, whose body is a sphere and not its model's box).
    bool ghost = false;
    // Its level faces hold only what is above them (a LadderSprout's leaves): the hero rises through them and
    // stands on them, as the original's one-sided collision lets him.
    bool steps = false;
    const ObjectClass *pool_behaviour = nullptr;
    uint32_t owner = 0; // the object a spawned one belongs to (a plant's soil)
    int clip = -1;
    bool loop = true;
    float time = 0, timer = -1, fall_speed = 0;
    // Behaviour state: `state` mirrors the original's state byte (instance +0x45) where a behaviour keeps one.
    uint8_t state = 0;
    std::array<float, 8> f{};
    std::array<int32_t, 4> n{};
    std::vector<uint32_t> members; // BOG offsets a behaviour gathered (a wave's enemies, a group's members)
    hig::Vec3 position() const { return {matrix[12], matrix[13], matrix[14]}; }
};
struct Pickup {
    enum class Kind : uint8_t { Bolt, Ammo, Health } kind{};
    int model = -1;
    hig::Vec3 position{}, previous{}, velocity{};
    float age = 0, spin = 0, previous_spin = 0;
    unsigned value = 0;
    float scale = 1;
    uint32_t sound = 0, tint = 0xffffffff;
    bool grounded = false;
};
class ObjectWorld {
  public:
    // `owned`: instances (BOG offsets) another native system already runs (enemies, mechanisms, vendors).
    void start(const World &, const ObjectAssets &, std::span<const uint32_t> owned);
    // The interact button (Triangle) edge, from the level loop's input.
    void input(const hig::PadState &);
    void tick(Gameplay &, float dt);
    void render(const PlayerAssets &, float alpha, hig::Vec3 eye, Scene &) const;
    // Solid objects for the player's collision; valid until the next tick.
    std::span<const TerrainQuery::Object> colliders() const { return colliders_; }
    std::vector<Object> objects;
    std::vector<Pickup> pickups;
    unsigned crates_broken = 0, bolts_collected = 0, explosions = 0, titanium_bolts = 0, armor_pickups = 0;
    unsigned waves_started = 0, teleports = 0, deaths = 0;
    uint32_t death_source = 0; // the object that last killed the hero (its BOG offset), for the run logs
    unsigned drawn = 0; // objects appended by the last render
    // What the interact button would do now ("Teleport"); empty when nothing is in reach. Set by behaviours each tick.
    std::string prompt;
    // A level change a behaviour asked for (a teleporter to another level): the level number, 0 = none.
    unsigned travel_level = 0;
    // A movie a behaviour asked for (disc path); the level loop plays it.
    std::string movie_request;
    // The level's bolt cranks as the mechanisms have them (the level loop fills this before each tick): how far
    // each is turned, and whether the hero is at it now. Keyed by the crank bolt's BOG offset.
    struct CrankState {
        float progress = 0;
        bool turning = false;
    };
    std::unordered_map<uint32_t, CrankState> cranks;
    // A line a behaviour wants shown ("You have acquired ..."); the level loop shows it.
    std::string notice_request;
    // The Sprout the Sprout-O-Matic last pulled out of the ground and that follows the hero (LEVEL_02 0x21de0c).
    uint32_t sprout_follower = 0;
    // What blew up last tick, for the objects a blast breaks (a berry's on a wall piece): `blast` adds one, and
    // behaviours read `blasts` the tick after.
    struct Blast {
        hig::Vec3 at{};
        float radius = 0;
        uint32_t by = 0; // the class hash of what blew up
    };
    std::vector<Blast> blasts;
    void blast(hig::Vec3 at, float radius, uint32_t by) { new_blasts_.push_back({at, radius, by}); }
    // Where the camera should look while the hero works a device he aims (the BotFlinger); its behaviour sets it
    // each tick.
    bool aim_camera = false;
    float aim_yaw = 0;
    // What behaviours want drawn this frame besides the models: a glow, a ring on the ground, a beam.
    struct Mark {
        enum class Kind : uint8_t { Glow, Disc, Beam } kind{};
        hig::Vec3 a{}, b{};
        float radius = 0;
        uint32_t color = 0; // 0xAABBGGRR
    };
    std::vector<Mark> marks;

    // ---- for class behaviours -------------------------------------------------------------------------------
    const World &world() const { return *world_; }
    const ObjectAssets &assets() const { return *assets_; }
    Object *object(uint32_t source);
    const Object *find(uint32_t source) const;
    // The hero's position used for trigger tests (his feet).
    hig::Vec3 hero() const { return player_; }
    // A BOG trigger volume by its offset; false for 0.
    bool inside(uint32_t trigger, hig::Vec3 point) const;
    // A TriggerCollection instance (MobyLink): the point is inside any of its triggers.
    bool inside_collection(uint32_t collection, hig::Vec3 point) const;
    const hig::BogTrigger *trigger(uint32_t source) const; // a trigger volume itself (its matrix: where it faces)
    const hig::BogCurve *curve(uint32_t source) const;
    // Any instance's properties with its class's schema.
    hig::ClassProps instance_props(uint32_t source) const;
    // Any MobyLink field of instance `from` names `to`.
    bool links_to(uint32_t from, uint32_t to) const;
    Enemy *enemy(Gameplay &, uint32_t source) const;
    // A linked object no longer stands in the way: a destroyed object, a defeated enemy, or nothing there at all.
    bool gone(uint32_t source, const Gameplay &) const;
    void destroy(Object &); // the original's object delete (0x82f58): no body, `gone`
    // The original's pool (0x82e40): a spare instance of the class takes the place and runs its class's behaviour.
    // Null when the class has no spare left. `destroy` returns it to the pool.
    Object *spawn(uint32_t class_hash, const hig::Matrix &at, Gameplay &);
    // What of the player's attacks reached a `target` object since the last tick.
    const TargetHit *hit(const Object &) const;
    // Enemies are shown when neither their wave nor their controller's area holds them back.
    void hold_enemy(uint32_t source, bool by_wave, bool held);
    bool interact_pressed() const { return interact_; }
    // A pad button pressed since the last tick (the D-pad for the gadgebots' commands).
    bool pressed(uint32_t button) const { return (pressed_ & button) != 0; }
    // The left stick (x to the right, y forward) for a device the hero aims.
    float stick_x() const { return stick_[0]; }
    float stick_y() const { return stick_[1]; }
    float random();
    hig::Vec3 center(const Object &) const;
    void play(Object &, std::string_view clip_prefix, bool loop);

  private:
    const World *world_ = nullptr;
    const ObjectAssets *assets_ = nullptr;
    std::vector<TerrainQuery::Object> colliders_;
    std::unordered_map<uint32_t, hig::Collision> stepped_; // what of a `steps` object's collision holds him now
    std::vector<TargetHit> hits_;
    std::vector<Blast> new_blasts_;
    std::unordered_map<uint32_t, const hig::BogInstance *> instances_;
    struct Hold {
        bool wave = false, area = false, initial = false;
    };
    std::unordered_map<uint32_t, Hold> holds_;
    hig::Vec3 player_{};
    uint32_t random_ = 0x2f6e2b1;
    bool started_ = false, interact_ = false, interact_latch_ = false, interact_down_ = false;
    uint32_t pressed_ = 0;
    std::array<float, 2> stick_{};
    // BoltController (LEVEL_02 0x15ba78): 30% of the level's bolts shared among its crates.
    unsigned crate_base_ = 3, crate_range_ = 2;
    int bolt_models_[3]{-1, -1, -1}, ammo_model_ = -1;
    // The bolt draw (LEVEL_02 0x15a3b0) scales by the model's own unit and its inverse: the models' natural size.
    float bolt_scale_ = 1;
    void break_crate(Object &, Gameplay &, bool silent = false);
    void explode(Object &, Gameplay &);
    void spawn_bolts(hig::Vec3 at, unsigned amount);
    void settle(Object &, Gameplay &, float dt);
    void tick_pickups(Gameplay &, float dt);
};
} // namespace sm
