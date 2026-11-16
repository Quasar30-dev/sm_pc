#pragma once
#include "audio.h"
#include "encounters.h"
#include "effects.h"
#include "hig/higactor.h"
#include "hig/higtransition.h"
#include "hig/higweapons.h"
#include "scene.h"
#include "ps2game/hero.h"
#include <memory>

namespace sm {
class Ps2Ratchet;  // optional PS2 Ratchet override (ps2_ratchet.h)
class Ps2Weapons;  // optional original UYA gun runtime, independent inventory
class Ps2Packs;    // original UYA carried packs, independent of the hero model
struct PlayerAssets {
    hig::ActorModel ratchet, blaster, wrench, clank, heli;
    hig::AnimationBank animations, clank_animations, heli_animations, blaster_animations,
        wrench_animations;
    std::vector<hig::LevelTexture> textures;
    EncounterAssets encounters;
    EffectAssets effects;
    std::shared_ptr<Ps2Ratchet> ps2;  // set = draw the PS2 Ratchet instead of SM's model
    std::shared_ptr<Ps2Weapons> ps2_weapons;
    std::shared_ptr<Ps2Packs> ps2_packs;
    unsigned hero_skin = 0; // which of the original's own skins `ratchet` is (load_hero_skin)
};
bool load_player_assets(const hig::IsoFs &, PlayerAssets &, std::string &error);
// The hero's single-player skins of /ART/HEROSKIN.PAK, numbered as the resident's own table has them
// (HEROSKIN_Init 0x1e65a18 reads it at 0x1edcc10: model entry, texture entry, both counted from 1): 0 the default
// Ratchet, 1 Kangaroo, 2 Samurai, 3 Scuba, 4 Zilla, 5 Pirate, 6 Garbage. All seven are the same 112-bone rig.
// The original's SKINS page (its table at LEVEL_02 0x229190: skin, unlock, name) calls them Wear Current Armor,
// Kanga-Ratchet, Hiro Ratchet, Tropical Vacation, Ratchetzilla, Plundering Pirate Captain; the seventh, Super
// Incognito, is on the disc but that page leaves it out.
// Replaces `ratchet`; a skin's own texture is appended to `textures` (existing indices stay).
constexpr unsigned hero_skin_count = 7;
bool load_hero_skin(const hig::IsoFs &, unsigned skin, PlayerAssets &, std::string &error);
void append_animated_actor(const hig::ActorModel &, const hig::AnimationClip &, float time,
                           bool loop, const hig::Matrix &, Scene &,
                           const std::vector<hig::BonePose> *evaluated = nullptr);
struct RayHit {
    bool hit = false;
    float fraction = 1;
    hig::Vec3 point{}, normal{};
    uint32_t surface = 0; // the face's surface hash (its type and flags: World::surface_flags)
};
class TerrainQuery {
  public:
    void build(const hig::Collision &);
    struct Object {
        const hig::Collision *collision;
        hig::Matrix matrix;
    };
    void objects(std::span<const Object>);
    RayHit ray(hig::Vec3 from, hig::Vec3 to) const;
    RayHit sweep_sphere(hig::Vec3 from, hig::Vec3 to, float radius) const;
    // The PS2 heroes' stationary push-out (ps2game::WorldQuery::overlap, after UYA 0x3f9498 / 0x3fa2d8): the one
    // face closest to a sphere at `center`, or with `height` > 0 to an upright capsule whose segment runs that far
    // up from it, within `radius`. `center` comes back moved so the shape clears the face. Faces count from both
    // sides here (this collision's winding is not one-sided); the original's are one-sided.
    struct Overlap {
        bool hit = false;
        hig::Vec3 center{}, normal{}, contact{};
        float distance = 0;
    };
    Overlap overlap(hig::Vec3 center, float radius, float height) const;
    // Native capsule solver. Constants/response are bring-up policy, not a
    // claim that retail character-controller behavior has all been recovered.
    void move(hig::Vec3 &feet, hig::Vec3 &velocity, float dt, bool &grounded, float radius = .23f,
              float height = 1.15f) const;

  private:
    void move_once(hig::Vec3 &feet, hig::Vec3 &velocity, float dt, bool &grounded, float radius, float height) const;
    struct Triangle {
        hig::Vec3 a, b, c, normal, minimum, maximum;
        uint32_t surface = 0;
    };
    std::vector<Triangle> triangles_;
    size_t static_count_ = 0;
};
// Anything the player's attacks can reach besides the native enemies. The object world (objects.cpp) lists its
// targets before each player tick and drains the hits after it; ids are BOG instance offsets.
enum class HitKind : uint8_t { Wrench, HyperStrike, WrenchThrow, Shot, Explosion };
struct Target {
    uint32_t id = 0;
    hig::Vec3 center{};
    float radius = .5f;
};
struct TargetHit {
    uint32_t id = 0;
    HitKind kind{};
    float damage = 0;
    hig::Vec3 from{};
};
// What the hero carries, by the module's weapon table (hig::WeaponIndex). Levels and experience follow the
// original's rules (LEVEL_02 0x250f8 / 0x24e20); ammo is kept per weapon.
struct Arsenal {
    std::array<bool, hig::WeaponCount> owned{};
    std::array<uint8_t, hig::WeaponCount> level{};
    std::array<float, hig::WeaponCount> xp{};
    std::array<uint32_t, hig::WeaponCount> ammo{};
    unsigned equipped = hig::WeaponBlaster;
};
// A shot of a weapon other than the Lacerator, or what a shot leaves in the world (weapons.cpp).
struct Shot {
    enum class Kind : uint8_t {
        Pellet,     // Concussion Gun
        AcidBomb,   // Acid Bomb Glove: the bomb
        AcidPool,   //   and its pool
        Rocket,     // Shock Rocket
        RynoRocket, // RYNO
        BeeMine,    // Bee Mine Glove: the mine
        Bee,        //   and a bee
        SniperMine, // Sniper Mine
        ArmorFireball, // the Fire-Bomb set's third swing
        ArmorField,    // what a set's wrench leaves in the world (state: armor.cpp's Field)
        AgentBomb,  // Agents of Doom: the capsule
        Agent,      //   and an agent
        Comet,      // Suck Cannon
        Cow,        // what the Mootator leaves
    };
    Kind kind{};
    uint8_t weapon = 0, state = 0;
    hig::Vec3 position{}, previous{}, velocity{};
    float age = 0, life = 1, damage = 0, radius = 0, gravity = 0, spin = 0, timer = 0, heading = 0;
    float grow = 0, full = 0; // an armour set's crystal, ice or ring: its size a second, and its full size
    int target = -1;          // an enemy it follows
    uint32_t model_class = 0; // drawn with this class's model; 0 = an effect only
};
enum class Motion {
    Idle,
    Run,
    Jump,
    Crouch,
    HighJump,
    LongJump,
    Fall,
    Glide,
    Fire,
    FireRun,
    Wrench,
    WrenchThrow,
    HyperStrike,
    Wade,
    SwimIdle,
    Swim,
    SwimJump,
    Hurt,
    Dead,
    Zipline,
    Crank,
    Slide, // the water slide: on rails along a SlideController's path
    Grind, // the Grind Lock's rails
    Magne, // the Magneboots: on a magnetic surface, whichever way it faces
    Grapple // the Hypershot: fired at a Hookshot target, pulled to it or swinging from it
};
class Gameplay {
  public:
    bool start(const World &, std::string &error, bool allow_exploration = false);
    void start_enemies(const World &, const EncounterAssets &);
    void configure_animations(const hig::AnimationBank &, const hig::ActorModel *hero = nullptr);
    void configure_player(const PlayerAssets &assets) {
        configure_animations(assets.animations, &assets.ratchet);
        player_assets_ = &assets;
    }
    void relocate(hig::Vec3); // explicit debug/test placement, resets interpolation and velocity
    void begin_interaction(Motion, unsigned clip);
    void interaction_clip(unsigned clip);
    void end_interaction(hig::Vec3 release_velocity = {});
    bool interacting() const { return motion == Motion::Zipline || motion == Motion::Crank; }
    // The slide (hero state 0x1a of the level modules: entry LEVEL_01 0x51808, update 0x51a00). A SlideController
    // whose detect area holds the hero starts it: he rides its path on rails, with clip 34 (WATER_SL), to six
    // metres short of its end, eases onto the ground ahead and stands up. `speed` is the path travelled a 30 Hz
    // frame (the controller's slideSp). No input acts on it, as in the original.
    void begin_slide(std::span<const hig::Vec3> path, float speed);
    bool sliding() const { return motion == Motion::Slide; }
    // The Grind Lock's rails (the hero's state 0x37, read in LEVEL_03: 0x56170 mounts, 0x57e50 runs a frame, 0x57848
    // reads the pad, 0x56378 enters a sub-state). rails[0] is the GrindrailController's main path, the others its
    // secondary paths; `speed` is metres a frame along them.
    struct Grind {
        std::vector<std::vector<hig::Vec3>> rails;
        float speed = .2f, distance = 0; // 0xc94: how far along `rail`
        int rail = 0;
        // The original's sub-states: 1 riding, 2 crossing to another rail, 3 in a jump, 5 off the rails, 6 swinging
        // the wrench, 7 thrown back (a bomb, a wall), 8 thrown back by a gate.
        int phase = 0;
        int lean = 0; // 0xce4: 1 left, 2 right
        unsigned clip = 0x42;
        hig::Vec3 at{}, spring{}, forward{0, 0, 1}, forward_spring{}, fall{};
        float height = 0, rise = 0, gravity = 0;                       // 0xcb4, 0xcb8, 0xcbc: a jump over the rail
        float cross = 0, back = 0, swing = 0, safe = 0, fall_from = 0; // 0xccc, 0xcd4, 0xce0, 0xcd0, 0xcc0
        hig::Vec3 moved{}; // this frame's way, for whoever tests it against the lock's bombs and gates
        bool fell = false; // dropped a metre below where he left the rails: the lock ends the try
        unsigned jumps = 0, crossings = 0, swings = 0, knocks = 0;
    };
    void begin_grind(std::vector<std::vector<hig::Vec3>> rails, float speed);
    void end_grind();
    bool grinding() const { return motion == Motion::Grind; }
    const Grind &grind() const { return grind_; }
    bool grind_swing_hits() const { return grinding() && grind_.phase == 6 && grind_.swing >= 5.3333335f && grind_.swing <= 13.333333f; }
    void grind_knock(bool gate, float moved, unsigned damage); // 0x56d18 (a bomb, a wall) / 0x56e30 (a gate)
    // The Hypershot (the hero's grapple state, LEVEL_02 0x6b240: 0x6acd0 finds a target, 0x6ca78 moves him). The
    // object world lists the level's Hookshot targets each tick.
    struct Hook {
        uint32_t id = 0;
        hig::Vec3 position{};
        float range = 0, rope = 0; // how far it can be taken from; the rope a swing settles to
        bool swing = false;        // swung from (yellow) or pulled to (green)
    };
    std::vector<Hook> hooks;
    const Hook *hook_in_reach() const;
    bool grappling() const { return motion == Motion::Grapple; }
    unsigned grapples = 0; // times the Hypershot has taken a target
    int grapple_phase() const { return motion == Motion::Grapple ? grapple_.phase : 0; }
    float slide_pitch = 0; // positive noses down, as swim_pitch
    void dynamic_objects(std::span<const TerrainQuery::Object> objects) {
        terrain_.objects(objects);
    }
    // Add platform displacement after player movement while retaining the
    // previous snapshot, so rendering interpolates both parts of the ride.
    void carry(hig::Vec3 delta);
    hig::Vec3 carried{}; // what carry() has added since a controller that owns the hero last took it (ps2_ratchet)
    void tick(const hig::PadState &, float camera_yaw, float dt = 1.f / 30, bool defer_weapons = false);
    // Main defers this until the PS2 pose and moving-platform transforms have
    // settled. Ordinary tick callers finish it after the SM pose update.
    void tick_weapons(const hig::PadState &, float dt = 1.f / 30);
    void render(const PlayerAssets &, float alpha, Scene &, bool show_player = true,
                bool show_enemies = true) const;
    void render_effects(const PlayerAssets &,float alpha,hig::Vec3 eye,Scene &,bool show_player=true) const;
    hig::Vec3 weapon_muzzle(float alpha) const;
    hig::Vec3 interpolated(float alpha) const;
    float interpolated_heading(float alpha) const;
    const TerrainQuery &terrain() const { return terrain_; }
    hig::Vec3 position{}, velocity{}, spawn{};
    float heading = 0, animation_time = 0, spawn_heading = 0;
    bool grounded = false;
    bool strafe_enabled = true, strafing = false; // Optional L2 camera-facing movement.
    Motion motion = Motion::Idle;
    unsigned jumps = 0, shots = 0, impacts = 0, respawns = 0, weapon = 0;
    unsigned ammo = 40, max_ammo = 40, bolts = 0;
    // Native health/damage state; pickups and saved progression remain pending.
    unsigned health = 10, max_health = 10;
    unsigned defeated = 0, damage_taken = 0, water_entries = 0;
    unsigned wrench_swings = 0;
    unsigned wrench_throws = 0, hyper_strikes = 0, hyper_impacts = 0;
    bool wrench_in_flight() const { return thrown_; }
    hig::Vec3 thrown_wrench_position() const { return throw_position_; }
    bool swimming = false, wading = false, underwater = false;
    float water_height = 0;
    float swim_pitch = 0; // Positive dives; original UYA pitch controller, SM Y-up adapter.
    ps2game::HeroPack air_pack = ps2game::HeroPack::Helipack;
    bool hydro_pack_enabled = true;
    bool swim_boosting() const { return underwater && swim_source_.mode==ps2game::HeroMode::SwimFast; }
    unsigned native_heli_clip() const { return heli_clip_; }
    float native_heli_time() const { return heli_time_; }
    bool camera_underwater(hig::Vec3 eye, bool previous = false) const;
    std::vector<Enemy> enemies;
    std::vector<Target> targets;
    // The weapon tables of the level's module; without them (bare harnesses) the hero has the bring-up Lacerator.
    // `everything`: own every weapon and gadget from the start (testing); else what a new game holds.
    void configure_weapons(const hig::WeaponTables *, unsigned level_number, bool everything = false);
    Arsenal arsenal;
    std::vector<Shot> weapon_shots;
    unsigned explosions = 0;
    float shield_time() const { return shield_time_; } // the Static Barrier is up
    void render_shots(const PlayerAssets &, float alpha, hig::Vec3 eye, Scene &) const;
    bool equip(unsigned weapon); // an owned weapon of the table; false when it is not owned
    const hig::WeaponTier *equipped_tier() const;
    const hig::WeaponTables *weapon_tables() const { return weapon_tables_; }
    // The controlled character is Clank (the level's ClankPlr instance took over: LEVEL_02 0x70c18, the hero's
    // controller in its Clank mode 0x68558). He has no weapons or gadgets; he is drawn with his own model and clips.
    bool clank = false;
    // His body for the level's collision: small enough for the holes in the walls that are his way (the one into
    // Ryllus's temple hall is .8 m wide and .8 m high). NATIVE sizes.
    float body_radius() const { return clank ? .2f : .23f; }
    float body_height() const { return clank ? .7f : 1.15f; }
    // The Sprout-O-Matic's stream (LEVEL_02 0x1d33e0): where it lands while the gadget is held and fired.
    struct SproutSpray {
        bool on = false;
        hig::Vec3 point{};
    } sprout_spray;
    // A weapon or gadget given in the level (0x25a80), and taken in hand when `equip_it` (0x244f0).
    bool grant_weapon(unsigned index, bool equip_it);
    // What a vendor has for sale here: the weapons of the table whose first level this is or was, with a price,
    // and the Hypershot from the second level on (NATIVE rule: where the original first offers the gadgets is not
    // read yet). Not owned ones only.
    std::vector<unsigned> vendor_stock() const;
    unsigned price(unsigned index) const;
    bool buy(unsigned index);
    // Thrown by a CannonSprout (the hero's state 0x22, 0x6a0c0 / 0x6a238): this velocity, falling at 25 m/s2 until
    // he lands, with no control of his own.
    void launch(hig::Vec3 velocity, float gravity = 25);
    bool launched() const { return launch_; }
    // ---- armour (armor.cpp; the level modules' armour code at 0x100..0xfff, LEVEL_02 offsets) ---------------------
    // What is worn at the six places of the body (0 body armor, 1 helmet, 2 / 3 gloves, 4 / 5 boots: a set 1..7,
    // 0 bare) and which pieces of each set are owned (a bit a place), as the save keeps them (+0x854, +0x859).
    struct Armor {
        std::array<uint8_t, 6> worn{};
        std::array<uint8_t, 8> owned{};
    } armor;
    void configure_armor(const hig::ArmorTables *tables, bool everything = false);
    const hig::ArmorTables *armor_tables() const { return armor_tables_; }
    bool armor_owned(unsigned set, unsigned place) const; // set 0 (bare) is always "owned" (0x4f8)
    // A picked-up piece (0x378): owned from now on, and worn at once when its place is bare.
    void grant_armor(unsigned row);
    // Wears an owned piece (0x580): gloves and boots fill both their places. Set 0 takes the place's piece off.
    bool wear_armor(unsigned set, unsigned place);
    // The set worn: 1..7 when every place wears the same one (0x6b0), 8..13 a mixed set by its recipe (0xaa4),
    // else 0.
    unsigned armor_set() const;
    // The share of damage the armour takes off (0x768): the four pieces' protection and the worn set's bonus.
    float armor_protection() const;
    // A set as a whole (the ARMOR SETS page, 0xc5398): whether every piece of its recipe is owned, what it would
    // take off, and putting it on.
    bool armor_set_owned(unsigned set) const;
    float armor_set_protection(unsigned set) const;
    bool wear_armor_set(unsigned set);
    unsigned armor_blocked = 0; // whole points of damage the armour has taken off
    // The worn set's power (the Wrench class's update 0x1dc748 turns the set into an element; the powers are the
    // sets' own descriptions, texts 891..904): see armor.cpp.
    float armor_charge = 0;     // Mega-Bomb: the damage it has taken off hits (0x3b0b48)
    bool armor_charged = false; //   over 15 it is charged and flashes, the charge running down (0x3b0b4c)
    unsigned armor_powers = 0;  // times a set's power has acted
    bool armor_cloaked() const; // Chameleon, moving
    hig::Vec3 armor_ghost() const; //   where enemies then take him to be
    const char *equipped_name() const;
    unsigned level_ups = 0;
    void award_xp(float amount); // to the equipped weapon, for a defeated enemy
    // Bolts a defeated enemy leaves where it fell. With `drop_bolts` set (the object world is running and turns
    // them into pickups) they are listed here; otherwise they are credited at once.
    struct Drop {
        hig::Vec3 at{};
        unsigned bolts = 0;
    };
    std::vector<Drop> drops;
    bool drop_bolts = false;
    std::vector<TargetHit> target_hits;
    // Damage from the world (objects, hazards): the same path as an enemy's hit.
    void damage(unsigned amount, hig::Vec3 from) { hurt_player(amount, from); }
    bool alive() const { return health > 0; }
    // Death from the world (a death trigger, a fall): no invulnerability window applies.
    void kill() {
        invulnerable_ = 0;
        hurt_player(health, position, true);
    }
    // Area damage to the native enemies (an explosion in the world).
    void blast(hig::Vec3 center, float radius, float damage);
    std::vector<AudioEvent> audio_events;
    std::vector<Projectile> projectiles;
    WeaponEffects effects;
    const char *animation_name(const PlayerAssets &) const;
    unsigned clip_index() const;
    float animation_rate() const;
    // Same evaluated/interpolated local pose used to skin Ratchet and mount his gear.
    void sample_pose(float alpha, std::vector<hig::BonePose> &) const;

  private:
    TerrainQuery terrain_;
    hig::LevelLighting lighting_;
    friend class Ps2Weapons; // routes original gun damage into SM's enemy owner
    hig::Vec3 previous_{};
    float fire_cooldown_ = 0, action_time_ = 0, floor_limit_ = -100;
    float fire_time_ = 0, attachment_time_ = 0, run_rate_ = 1;
    Motion previous_motion_ = Motion::Idle;
    Motion pack_jump_ = Motion::Idle;
    float pack_jump_heading_ = 0;
    float previous_heading_ = 0, pose_time_ = 0;
    unsigned pose_clip_ = 0, pose_shots_ = 0;
    bool pose_fire_layer_ = false;
    unsigned interaction_clip_ = 0;
    float interaction_time_ = 0;
    std::vector<hig::BonePose> pose_, previous_pose_;
    hig::PoseTransition pose_transition_, arm_transition_;
    void update_pose(float dt);
    void reset_pose();
    const EncounterAssets *encounter_assets_ = nullptr;
    const World *world_ = nullptr; // trigger volumes for the enemies' detect areas
    const hig::WeaponTables *weapon_tables_ = nullptr;
    const hig::ArmorTables *armor_tables_ = nullptr;
    float armor_carry_ = 0; // the fraction of a point of damage the armour has not yet taken off
    struct Grapple {
        int phase = 0; // 1 firing, 2 pulled along, 4 swinging (the original's sub-states)
        hig::Vec3 anchor{}, start{}, velocity{}, push{};
        float length = 0, length_speed = 0, rope = 0, progress = 0, step = 0;
        bool swing = false, pushing = false, cancelled = false;
    } grapple_;
    void tick_grapple(const hig::PadState &, float camera_yaw, float dt);
    bool launch_ = false;
    bool launch_steer_ = false; // off a Hypershot swing: the stick bends his way a little (a jump, not a throw)
    hig::Vec3 launch_velocity_{};
    float launch_gravity_ = 25;
    float armor_flash_ = 0;
    unsigned armor_throw_frame_ = 0;
    std::array<hig::Vec3, 30> armor_trail_{};
    unsigned armor_trail_at_ = 0;
    float armor_wrench_hit(size_t enemy, float damage, bool thrown); // a wrench hit: the damage it deals
    void armor_swing_time(unsigned stage, float before, float after);
    void armor_throw_tick();
    void armor_hyper_impact();
    void armor_absorb(float blocked);
    float armor_blast(hig::Vec3 at, float radius, float damage);
    void armor_puddle(hig::Vec3 at, bool big);
    bool tick_armor_shot(Shot &, float dt);
    void render_armor_shot(const PlayerAssets &, const Shot &, hig::Vec3 at, hig::Vec3 eye, Scene &) const;
    void tick_armor(float dt);
    unsigned level_number_ = 1;
    float shot_damage() const; // one Lacerator shot at the weapon's level
    // weapons.cpp
    unsigned fire_clip_ = 26, fire_layer_clip_ = 25; // Ratchet's clips for the equipped weapon's shot
    float weapon_timer_ = 0, shield_time_ = 0, moo_time_ = 0, fire_latch_ = 0;
    int moo_target_ = -1;
    bool beam_ = false;
    hig::Vec3 beam_from_{}, beam_to_{};
    uint32_t weapon_random_ = 0x9e3779b9;
    float effects_random() {
        weapon_random_ = weapon_random_ * 1664525u + 1013904223u;
        return float(weapon_random_ >> 8) * (1.f / 16777216.f);
    }
    float damage_row(unsigned base, unsigned weapon) const;
    int aim_enemy(hig::Vec3 from, float range, float cone_cos, hig::Vec3 *aim) const;
    void explode_at(hig::Vec3 at, float radius, float damage, uint32_t sound, uint32_t color);
    void tick_arsenal(const hig::PadState &, float dt);
    void tick_shots(float dt);
    void burn_enemy(size_t index, float damage);
    static bool segment_sphere_hit(hig::Vec3 from, hig::Vec3 to, hig::Vec3 center, float radius, float &fraction);
    const hig::ActorModel *hero_model_ = nullptr;
    const hig::AnimationBank *hero_animations_ = nullptr;
    const PlayerAssets *player_assets_ = nullptr;
    bool has_water_ = false;
    ps2game::HeroState swim_source_;
    double swim_remainder_ = 0;
    float swim_frame_ = 0, previous_swim_pitch_ = 0;
    struct Slide {
        std::vector<hig::Vec3> path;
        float speed = .4f, distance = 0;
        int phase = 0; // 1 riding, 2 easing off
        hig::Vec3 spring{}, forward{0, 0, 1}, forward_spring{}, forward_goal{0, 0, 1}, exit{};
    } slide_;
    float previous_slide_pitch_ = 0;
    void tick_slide();
    Grind grind_;
    // The Magneboots (the hero's state 0x1f: his ground is of type 3, a surface whose flags have 4). `normal` is the
    // face under him, `up` his own up eased to it, `forward` the way he faces along the surface and `view` the
    // camera's way along it; `jump` his jump's velocity (`air`: 1 going up, 2 coming down).
    struct Magne {
        hig::Vec3 normal{0, 1, 0}, up{0, 1, 0}, forward{0, 0, 1}, view{0, 0, 1}, jump{};
        float speed = 0, landing = 0;
        int air = 0;
        bool moving = false;
    } magne_;
    void begin_magne(const RayHit &);
    void tick_magne(const hig::PadState &, float dt);
    bool magnetic(const RayHit &) const;

  public:
    bool on_magneboots() const { return motion == Motion::Magne; }
    hig::Vec3 magne_normal() const { return magne_.normal; }
    hig::Vec3 magne_up() const { return magne_.up; }
    hig::Vec3 magne_view() const { return magne_.view; }
    hig::Vec3 magne_forward() const { return magne_.forward; }
    bool magne_in_air() const { return magne_.air != 0; }
    void magne_turn_view(float angle); // the camera's stick: about his up

  private:
    bool double_rise_ = false; // rising on the second jump: the original's light gravity until its top
    void tick_grind(const hig::PadState &);
    void reset_swim();
    void move_underwater(const hig::PadState &, float analog, float desired_heading, float dt);
    unsigned heli_state_=0,heli_clip_=0;
    float heli_time_=0,heli_rate_=1;
    std::vector<hig::BonePose> heli_pose_,previous_heli_pose_;
    hig::PoseTransition heli_transition_;
    void tick_native_heli(float dt);
    float invulnerable_ = 0, death_time_ = 0, swim_exit_ = 0, wrench_time_ = -1, hurt_time_ = 0;
    hig::Vec3 knockback_{};
    unsigned wrench_stage_ = 0;
    bool wrench_queued_ = false;
    std::vector<uint8_t> melee_hits_;
    std::vector<uint32_t> melee_target_hits_, throw_target_hits_; // target ids (throw: id * 4 + leg)
    std::array<hig::Vec3, 2> melee_previous_{};
    bool melee_previous_valid_ = false;
    float attack_speed_ = 0, attack_derivative_ = 0;
    float attack_turn_derivative_ = 0, attack_turn_limit_ = 10000;
    float throw_time_ = -1, throw_progress_ = 0, throw_spin_ = 0, throw_previous_spin_ = 0;
    bool thrown_ = false, throw_released_ = false;
    hig::Vec3 throw_position_{}, throw_previous_{}, throw_origin_{}, throw_target_{};
    float throw_heading_ = 0;
    std::vector<uint8_t> throw_hits_;
    // 0 = inactive, 1 = windup, 2 = falling, 3 = impact/recovery.
    unsigned hyper_phase_ = 0;
    float hyper_time_ = 0, hyper_weapon_time_ = 0;
    float throw_duration_ = 3.9333334f, hyper_duration_ = .76666665f;
    std::array<float, 3> wrench_duration_{1.2f, .9f, 1.2f};
    void begin_wrench(unsigned stage);
    void cancel_wrench();
    void tick_thrown_wrench(float dt);
    void attack_events(unsigned clip, float before, float after);
    void melee_contacts();
    void tick_enemies(float dt);
    void tick_projectiles(float dt);
    void hurt_enemy(size_t index, float damage);
    // `absolute`: a death from the world; armour takes nothing off it.
    void hurt_player(unsigned damage, hig::Vec3 from, bool absolute = false);
};
} // namespace sm
