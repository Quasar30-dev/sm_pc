// The hero's armour: what is owned and worn, the set it makes and the protection it gives, after the level
// modules' armour code (LEVEL_02 offsets 0x100..0xfff). The sets' powers act through the wrench (weapons.cpp).
#include "gameplay.h"
#include "hig/higwrench.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sm {
namespace {
hig::Vec3 add(hig::Vec3 a, hig::Vec3 b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
hig::Vec3 sub(hig::Vec3 a, hig::Vec3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
} // namespace
void Gameplay::configure_armor(const hig::ArmorTables *tables, bool everything) {
    armor_tables_ = tables;
    if (!tables) {
        armor = {};
        return;
    }
    if (everything) // testing: every piece of every set
        for (unsigned set = 1; set < 8; set++)
            armor.owned[set] = 0x3f;
}
bool Gameplay::armor_owned(unsigned set, unsigned place) const {
    // 0x4f8: bare is always there; a set's byte holds a bit a place
    return set == 0 || (set < 8 && place < 6 && (armor.owned[set] >> place & 1));
}
bool Gameplay::wear_armor(unsigned set, unsigned place) {
    if (set >= 8 || place >= 6 || !armor_owned(set, place))
        return false;
    // 0x580: either glove sets both gloves, either boot both boots
    if (place == 2 || place == 3)
        armor.worn[2] = armor.worn[3] = uint8_t(set);
    else if (place == 4 || place == 5)
        armor.worn[4] = armor.worn[5] = uint8_t(set);
    else
        armor.worn[place] = uint8_t(set);
    return true;
}
void Gameplay::grant_armor(unsigned row) {
    if (!armor_tables_ || row >= hig::ArmorTables::piece_count)
        return;
    const auto &piece = armor_tables_->pieces[row];
    if (piece.set == 0 || piece.set >= 8 || piece.place >= 6)
        return;
    // 0x300: the piece's place and its partner's
    armor.owned[piece.set] |= uint8_t(1u << piece.place);
    if (piece.partner < 6)
        armor.owned[piece.set] |= uint8_t(1u << piece.partner);
    // 0x378: a bare place wears it at once
    if (armor.worn[piece.place] == 0)
        wear_armor(piece.set, piece.place);
}
unsigned Gameplay::armor_set() const {
    if (!armor_tables_)
        return 0;
    // 0x6b0: the same set at every place
    bool whole = armor.worn[0] != 0;
    for (unsigned place = 1; place < 6; place++)
        whole &= armor.worn[place] == armor.worn[0];
    if (whole)
        return armor.worn[0];
    // 0xaa4: a mixed set's recipe names the helmet's, the body armor's, the gloves' and the boots' sets
    for (unsigned set = 8; set < hig::ArmorTables::set_count; set++) {
        const auto &recipe = armor_tables_->sets[set].recipe;
        if (recipe[0] == armor.worn[1] && recipe[1] == armor.worn[0] && recipe[2] == armor.worn[3] &&
            recipe[3] == armor.worn[5])
            return set;
    }
    return 0;
}
float Gameplay::armor_protection() const {
    if (!armor_tables_)
        return 0;
    // 0x768: the body armor, the helmet, one glove and one boot, then the worn set's bonus (0x740)
    float total = 0;
    for (unsigned place : {0u, 1u, 2u, 4u})
        total += armor_tables_->protection[std::min<unsigned>(armor.worn[place], 7)];
    return std::clamp(total + armor_tables_->sets[armor_set()].bonus, 0.f, 1.f);
}
bool Gameplay::armor_set_owned(unsigned set) const {
    if (!armor_tables_ || set == 0 || set >= hig::ArmorTables::set_count)
        return false;
    // the recipe names the helmet's, the body armor's, the gloves' and the boots' sets
    const auto &recipe = armor_tables_->sets[set].recipe;
    constexpr unsigned place[4] = {1, 0, 2, 4};
    for (unsigned k = 0; k < 4; k++)
        if (recipe[k] == 0 || recipe[k] >= 8 || !armor_owned(recipe[k], place[k]))
            return false;
    return true;
}
float Gameplay::armor_set_protection(unsigned set) const {
    if (!armor_tables_ || set == 0 || set >= hig::ArmorTables::set_count)
        return 0;
    float total = armor_tables_->sets[set].bonus;
    for (unsigned piece : armor_tables_->sets[set].recipe)
        total += armor_tables_->protection[std::min<unsigned>(piece, 7)];
    return std::clamp(total, 0.f, 1.f);
}
bool Gameplay::wear_armor_set(unsigned set) {
    if (!armor_set_owned(set))
        return false;
    const auto &recipe = armor_tables_->sets[set].recipe;
    wear_armor(recipe[0], 1);
    wear_armor(recipe[1], 0);
    wear_armor(recipe[2], 2);
    wear_armor(recipe[3], 4);
    return true;
}

// ---- the sets' powers ------------------------------------------------------------------------------------------
// Translated from the Wrench class (LEVEL_02): its update 0x1dc748 keys everything on the worn set (hero +0x5d0);
// 0x1dcf68 is a wrench hit, 0x1dd380 the swing's effects (the third swing's at clip time .4), 0x1dcad0 the thrown
// wrench, 0x1de5b0 the Hyper Strike's impact (at the hero's position + forward * 2.2). What they spawn are classes
// of their own: CrystalGroundShard, ElectroshockWave, GravityCrystal, MobyIce, NapalmBubble, Spitfire,
// TripleWaveBeacon and the Acid Bomb's puddles. Amounts are the damage table's records (0x22dcd8, 16 bytes each:
// kind, amount, type, flags; kind 1 is an amount a frame at 30 frames a second):
//   0xf0 .1333 Wildfire's fire      0xf1 8 Wildfire's Hyper Strike   0xf2 .2 a puddle     0xf3 .1333 its poison
//   0xf4 1.4 a crystal's touch      0xf5 96 Electroshock's wave      0xf6 100 and 0xf7 60 the Mega-Bomb, 0xf8 1.3333
//   0xfb 200 the fireball           0xfe 64, 0xff 1.3333, 0x100 .4 the Triple Wave's three waves
//   0x101 1.3333 and 0x102 4 the Stalker's fire and poison
// A fire lasts 45 frames and an enemy burns with one at a time (0x7aa10 / 0x7aa70); a poison lasts the frames it is
// given and a second dose adds a quarter of its own (0x7b7f0); ice holds for 45 frames (0x1a73a8).
// What stays NATIVE is said where it is.
namespace {
// The sounds are the original's own: the wrench's element code loads two tables of them (0xa08: 0x2208a8, got by
// 0xa68; 0x23910: 0x221708, got by 0x24b98) and the calls name them by index (the blow 0x1de5b0, the Spitfire
// 0x1ce650 / 0x1ceb30, the bubble 0x1adfa0, the rings 0x17f480, the shards 0x179d30, the Mega-Bomb 0x1dcf68).
constexpr uint32_t SND_SPLAT = 0x238758cf, SND_CRYSTAL = 0xd087e5ed, SND_SHATTER = 0x3886ec47, SND_FREEZE = 0xf6e29637;
constexpr uint32_t SND_EXPLO_S1 = 0xcfaf66d4, SND_EXPLO_S2 = 0xb3ce430f, SND_EXPLO_M6 = 0xfc17adff, SND_EXPLO_M2 = 0xe5bb22e4,
                   SND_EXPLO_L1 = 0x1e7ccc7c;
constexpr uint32_t SND_ELECTRIC[3] = {0x2aa7a6d0, 0xc565cdee, 0x27b9d697}; // GENERALELECTRIC_02..04, one at random
enum Set : unsigned { Wildfire = 1, Sludge, Crystallix, Electroshock, MegaBomb, Hyperborean, Chameleon, FireBomb, ShockCrystal,
                      Wildburst, TripleWave, IceII, Stalker };
// ArmorField's kinds (Shot::state)
enum Field : uint8_t { Puddle = 1, Crystal, Quasicrystal, IceBlock, Bubble, Beacon, FireRing, SludgeRing, ShockRing };
constexpr uint32_t CLASS_SHARD = 0x3fdd057b, CLASS_GRAVITY = 0x850fa0b4, CLASS_ICE = 0x702127e9, CLASS_BUBBLE = 0x3448a16b,
                   CLASS_SPITFIRE = 0x3795cb33, CLASS_BEACON = 0xd0f68f21;
constexpr float FRAME = 1.f / 30;
// the eight places of the third swing's crystals and of the Ice II wall, about the hero (0x241770: right, up, ahead)
constexpr float ARC[8][2] = {{-1.481f, 1.28f}, {-1.126f, 1.751f}, {-.728f, 2.12f}, {-.279f, 2.402f},
                             {.279f, 2.402f},  {.728f, 2.12f},    {1.126f, 1.751f}, {1.481f, 1.28f}};
float length3(hig::Vec3 v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
hig::Vec3 scale3(hig::Vec3 v, float s) { return {v[0] * s, v[1] * s, v[2] * s}; }
void ignite(Enemy &e, float per_frame) { // 0x7ac28: 45 frames, and only what is not already burning (0x7aa10)
    if (e.burn_time > 0)
        return;
    e.burn_time = 45 * FRAME;
    e.burn_rate = per_frame * 30;
}
void poison(Enemy &e, float per_frame, float frames) { // 0x7b7f0
    if (e.poison_time > 0) {
        e.poison_time = std::min(1800 * FRAME, e.poison_time + frames * .25f * FRAME);
        return;
    }
    e.poison_time = frames * FRAME;
    e.poison_rate = per_frame * 30;
}
} // namespace

hig::Vec3 Gameplay::armor_ghost() const {
    // "most opponents will aim at a point the user was several moments ago": NATIVE lag of a second (the original
    // leaves a ChameleonDecoy, 0x167418, for its enemies to aim at; how long it stands is not read yet)
    return armor_cloaked() ? armor_trail_[armor_trail_at_ % armor_trail_.size()] : position;
}
bool Gameplay::armor_cloaked() const {
    // "a full set will also render the user nearly invisible while moving"
    return armor_set() == Chameleon && std::hypot(velocity[0], velocity[2]) > 1.f;
}
void Gameplay::armor_absorb(float blocked) {
    // 0x1b8598 / 0x1e8a68: what the armour took off a hit is stored; over 15 the armour is charged
    if (armor_set() != MegaBomb || armor_charged)
        return;
    armor_charge += blocked;
    if (armor_charge > 15) {
        armor_charged = true;
        armor_flash_ = 0;
    }
}
float Gameplay::armor_blast(hig::Vec3 at, float radius, float damage) {
    // the original's sphere query (0x1cd48) takes what an enemy's own bounds reach
    float dealt = 0;
    if (!encounter_assets_)
        return dealt;
    for (size_t i = 0; i < enemies.size(); i++) {
        const auto &e = enemies[i];
        if (e.hidden || e.state == EnemyState::Dead)
            continue;
        const auto &m = encounter_assets_->models[e.model];
        if (length3(sub(add(e.position, hig::Vec3{0, m.height * e.scale * .5f, 0}), at)) <=
            radius + std::max(m.radius * e.scale, m.height * e.scale * .5f)) {
            hurt_enemy(i, damage);
            dealt += damage;
        }
    }
    return dealt;
}
float Gameplay::armor_wrench_hit(size_t index, float damage, bool) {
    // 0x1dcf68
    if (index >= enemies.size())
        return damage;
    auto &e = enemies[index];
    const hig::Vec3 at = add(e.position, hig::Vec3{0, .6f, 0});
    switch (armor_set()) {
    case Stalker: // the ethereal wrench deals nothing itself: fire (0x101) and 180 frames of poison (0x102)
        ignite(e, 1.3333f);
        poison(e, 4, 180);
        effects.burst(at, .8f, 0xff40ff80, 8);
        armor_powers++;
        return 0;
    case Wildfire: // 0xf0
        if (e.burn_time <= 0) {
            ignite(e, .13333f);
            armor_powers++;
        }
        break;
    case Hyperborean: // MobyIce for 45 frames
        e.frozen = 45 * FRAME;
        effects.burst(at, 1.1f, 0xffffe0a0, 10);
        armor_powers++;
        break;
    case MegaBomb:
        if (armor_charged) { // the stored energy: 100 to what is struck, 60 to what stands within 2, and it burns
            armor_charged = false;
            armor_charge = 0;
            armor_powers++;
            effects.burst(at, 2, 0xff60c0ff, 28);
            audio_events.push_back({SND_EXPLO_L1});
            explosions++;
            armor_blast(at, 2, 60);
            ignite(enemies[index], 1.3333f);
            return damage + 100;
        }
        break;
    default:
        break;
    }
    return damage;
}
void Gameplay::armor_swing_time(unsigned stage, float before, float after) {
    // 0x1dd380, each frame of a swing
    const unsigned set = armor_set();
    if (!set)
        return;
    const float c = std::cos(heading), s = std::sin(heading);
    const hig::Vec3 forward{s, 0, c}, right{-c, 0, s}; // the original's world is right-handed
    const hig::Vec3 head = add(position, hig::Vec3{forward[0] * .9f, .9f, forward[2] * .9f});
    if (set == Sludge || set == Stalker) { // sludge flies off the wrench while it cuts (cosmetic: 0x1ebef8 is an emitter)
        constexpr float window[3][2] = {{.56666666f, .73333335f}, {.3f, .46666667f}, {.4f, .53333336f}};
        if (stage < 3 && after > window[stage][0] && before < window[stage][1])
            effects.puff(head, hig::Vec3{forward[0] * 2 + (effects_random() - .5f), 1.5f, forward[2] * 2 + (effects_random() - .5f)},
                         .25f, .5f, 0xc030ff70);
    }
    if (set == Wildfire || set == Stalker) // the super-heated wrench trails flame
        effects.puff(head, hig::Vec3{0, .8f, 0}, .2f, .3f, 0xd04080ff);
    if (set == Hyperborean || set == IceII)
        effects.puff(head, hig::Vec3{0, -.2f, 0}, .12f, .5f, 0xd0fff0c8);
    if (stage != 2 || before >= .4f || after < .4f)
        return;
    if (set == Crystallix) { // eight crystals in an arc ahead, one every two frames from the far side
        for (unsigned i = 0; i < 8; i++) {
            const float *p = ARC[7 - i];
            const float turn = effects_random() * 6.2831855f;
            hig::Vec3 at = add(position, add(scale3(right, p[0] + std::cos(turn) * .2f), scale3(forward, p[1] + std::sin(turn) * .2f)));
            const auto ground = terrain_.ray(add(at, hig::Vec3{0, .9f, 0}), add(at, hig::Vec3{0, -.9f, 0}));
            if (!ground.hit || terrain_.ray(add(ground.point, hig::Vec3{0, .5f, 0}), add(position, hig::Vec3{0, .5f, 0})).hit)
                continue;
            Shot shard;
            shard.kind = Shot::Kind::ArmorField;
            shard.state = Crystal;
            shard.model_class = CLASS_SHARD;
            shard.position = shard.previous = ground.point;
            shard.timer = float(i * 2) * FRAME;
            shard.life = 25 * FRAME;
            shard.radius = .1f;
            shard.grow = .3f * 30;
            shard.full = 1.8f + effects_random() * .3f;
            shard.damage = 1.4f;
            // it leans away from the hero (the original aims it from a point 4.2 above him)
            const hig::Vec3 out = sub(ground.point, position);
            shard.heading = std::atan2(out[0], out[2]);
            shard.velocity = {std::hypot(out[0], out[2]), 0, 0}; // the lean's reach, for the drawing
            weapon_shots.push_back(shard);
        }
        armor_powers++;
    }
    if (set == FireBomb) { // 0x1ce650: a Spitfire that flies .4 a frame for 30 frames
        Shot ball;
        ball.kind = Shot::Kind::ArmorFireball;
        ball.model_class = CLASS_SPITFIRE;
        ball.position = ball.previous = add(position, hig::Vec3{0, .9f, 0}); // from his own middle: nothing stands behind it
        ball.velocity = scale3(forward, .4f * 30);
        ball.life = 30 * FRAME;
        ball.radius = 30 * .003f + .75f; // 0x1ce8a8
        ball.damage = 200;
        weapon_shots.push_back(ball);
        audio_events.push_back({SND_EXPLO_S1});
        armor_powers++;
    }
}
void Gameplay::armor_throw_tick() {
    // 0x1dcad0: the Sludge Mk. 9 wrench in flight leaves a puddle under it every third frame
    if (armor_set() != Sludge || ++armor_throw_frame_ % 3)
        return;
    const auto ground = terrain_.ray(add(throw_position_, hig::Vec3{0, .3f, 0}), add(throw_position_, hig::Vec3{0, -50, 0}));
    effects.puff(throw_position_, hig::Vec3{0, -1, 0}, .2f, .4f, 0xc030ff70);
    if (!ground.hit)
        return;
    armor_puddle(ground.point, false);
    armor_powers++;
}
void Gameplay::armor_puddle(hig::Vec3 at, bool big) {
    // 0x1ebf90: the Acid Bomb's puddle, 120 frames; its size 1 to 1.2, or 2 to 2.2 (0x21e328 / 0x21e348; taken here
    // as its radius)
    Shot pool;
    pool.kind = Shot::Kind::ArmorField;
    pool.state = Puddle;
    pool.position = pool.previous = at;
    pool.life = 120 * FRAME;
    pool.radius = (big ? 2 : 1) + effects_random() * .2f;
    pool.damage = .2f;
    weapon_shots.push_back(pool);
}
void Gameplay::armor_hyper_impact() {
    // 0x1de5b0
    const unsigned set = armor_set();
    const float c = std::cos(heading), s = std::sin(heading);
    const hig::Vec3 forward{s, 0, c}, right{-c, 0, s};
    const hig::Vec3 at = add(position, hig::Vec3{forward[0] * 2.2f, .2f, forward[2] * 2.2f});
    const auto under = terrain_.ray(add(at, hig::Vec3{0, .6f, 0}), add(at, hig::Vec3{0, -1.2f, 0}));
    const hig::Vec3 ground = under.hit ? under.point : hig::Vec3{at[0], position[1], at[2]};
    if (std::getenv("SM_HERO_DEBUG")) {
        std::printf("armor hyper: set=%u hero=%.2f,%.2f,%.2f heading=%.2f at=%.2f,%.2f,%.2f ground=%d %.2f\n", set, position[0],
                    position[1], position[2], heading, at[0], at[1], at[2], int(under.hit), ground[1]);
        for (const auto &e : enemies)
            if (!e.hidden && e.state != EnemyState::Dead && length3(sub(e.position, position)) < 12)
                std::printf("  enemy at %.2f,%.2f,%.2f (%.2f from the blow)\n", e.position[0], e.position[1], e.position[2],
                            length3(sub(e.position, at)));
    }
    auto field =[&](Field kind, uint32_t model, hig::Vec3 where, float life) -> Shot & {
        Shot f;
        f.kind = Shot::Kind::ArmorField;
        f.state = kind;
        f.model_class = model;
        f.position = f.previous = where;
        f.life = life;
        f.heading = effects_random() * 6.2831855f;
        weapon_shots.push_back(f);
        return weapon_shots.back();
    };
    switch (set) {
    case Wildfire: // a burst of fire: 8 to what stands within half a metre of the blow
        effects.burst(at, 1.2f, 0xff4080ff, 20);
        audio_events.push_back({SND_EXPLO_M2});
        armor_blast(at, .5f, 8);
        break;
    case Sludge:
        if (!under.hit)
            return;
        armor_puddle(ground, true);
        audio_events.push_back({SND_SPLAT});
        break;
    case Crystallix: // a ring of eight crystals, three quarters of a metre about the blow
        for (unsigned i = 0; i < 8; i++) {
            const float angle = float(i) * .7853982f;
            const hig::Vec3 out = add(scale3(forward, std::cos(angle)), scale3(right, std::sin(angle)));
            const hig::Vec3 where = add(at, scale3(out, .75f));
            const auto g = terrain_.ray(add(where, hig::Vec3{0, .9f, 0}), add(where, hig::Vec3{0, -.9f, 0}));
            if (!g.hit)
                continue;
            auto &shard = field(Crystal, CLASS_SHARD, g.point, (27 + effects_random() * 5) * FRAME);
            shard.radius = .1f;
            shard.grow = (.42f - effects_random() * .17f) * 30;
            shard.full = 1.8f + effects_random() * .3f;
            shard.damage = 1.4f;
            shard.heading = std::atan2(out[0], out[2]);
            shard.velocity = {3.5f, 0, 0}; // it leans outwards (the original aims it from 1.2 below the blow)
        }
        audio_events.push_back({SND_FREEZE});
        break;
    case Electroshock: { // one wave: 96 to what stands within 4.75
        auto &ring = field(ShockRing, 0, at, .5f);
        ring.radius = .5f;
        ring.grow = .35f * 30;
        ring.full = 8;
        audio_events.push_back({SND_EXPLO_S2});
        armor_blast(at, 4.75f, 96);
        break;
    }
    case ShockCrystal: { // a quasicrystal that stands for 450 frames
        if (!under.hit)
            return;
        auto &crystal = field(Quasicrystal, CLASS_GRAVITY, ground, 450 * FRAME);
        crystal.radius = .1f;
        crystal.grow = .5f * 30;
        crystal.full = 2.5f + effects_random() * .5f;
        audio_events.push_back({SND_CRYSTAL});
        break;
    }
    case Wildburst: { // a bubble that swells for 45 frames and bursts, over a puddle
        if (!under.hit)
            return;
        auto &bubble = field(Bubble, CLASS_BUBBLE, ground, 45 * FRAME);
        bubble.radius = .1f;
        bubble.grow = 30 / 60.f;
        bubble.full = .85f;
        armor_puddle(ground, true);
        audio_events.push_back({SND_SPLAT});
        break;
    }
    case TripleWave: { // a beacon that stands for 270 frames and sends a wave every 45
        if (!under.hit)
            return;
        auto &beacon = field(Beacon, CLASS_BEACON, ground, 270 * FRAME);
        beacon.radius = 1;
        beacon.spin = 22.5f * FRAME; // to the first wave
        beacon.weapon = 0;           // which wave is next: electric, fire, sludge
        audio_events.push_back({SND_ELECTRIC[0]});
        break;
    }
    case IceII: { // a wall of eight blocks of ice, while no other wall stands (0x1a7ba0)
        for (const auto &other : weapon_shots)
            if (other.kind == Shot::Kind::ArmorField && other.state == IceBlock)
                return;
        for (const auto &p : ARC) {
            const hig::Vec3 where = add(position, add(scale3(right, p[0]), scale3(forward, p[1])));
            const auto g = terrain_.ray(add(where, hig::Vec3{0, .9f, 0}), add(where, hig::Vec3{0, -.9f, 0}));
            if (!g.hit || terrain_.ray(add(g.point, hig::Vec3{0, .5f, 0}), add(position, hig::Vec3{0, .5f, 0})).hit)
                continue;
            auto &block = field(IceBlock, CLASS_ICE, g.point, 105 * FRAME);
            block.radius = .1f;
            block.full = .4f + effects_random() * .1f;
            // 0x1a7ce8 grows it .07 to .12 a frame, or .02 to .03, by its kind
            block.grow = (effects_random() < .5f ? .02f + effects_random() * .01f : .07f + effects_random() * .05f) * 30;
        }
        audio_events.push_back({SND_FREEZE});
        break;
    }
    default:
        return;
    }
    armor_powers++;
}
bool Gameplay::tick_armor_shot(Shot &s, float dt) {
    s.previous = s.position;
    if (s.timer > 0) { // a crystal waits its turn
        s.timer -= dt;
        return false;
    }
    s.age += dt;
    bool done = s.age >= s.life;
    if (s.full > 0)
        s.radius = std::min(s.full, s.radius + s.grow * dt);
    // what an enemy's own bounds reach of a ball at a point
    auto reach = [&](size_t i, hig::Vec3 at, float radius) {
        const auto &e = enemies[i];
        if (e.hidden || e.state == EnemyState::Dead)
            return false;
        const auto &m = encounter_assets_->models[e.model];
        return length3(sub(add(e.position, hig::Vec3{0, m.height * e.scale * .5f, 0}), at)) <=
               radius + std::max(m.radius * e.scale, m.height * e.scale * .5f);
    };
    const size_t count = encounter_assets_ ? enemies.size() : 0;
    if (s.kind == Shot::Kind::ArmorFireball) { // Spitfire 0x1ce8c8
        if (int(s.age * 30) != int((s.age - dt) * 30))
            effects.puff(s.position, scale3(s.velocity, -.15f), .45f, .35f, 0xd030a0ff);
        const hig::Vec3 next = add(s.position, scale3(s.velocity, dt));
        const auto wall = terrain_.ray(s.position, next);
        float nearest = wall.hit ? wall.fraction : 1.1f, f;
        // what its flight meets ends it there (the ray at the head of 0x1ce8c8)
        for (size_t i = 0; i < count; i++) {
            const auto &e = enemies[i];
            if (e.hidden || e.state == EnemyState::Dead)
                continue;
            const auto &m = encounter_assets_->models[e.model];
            if (segment_sphere_hit(s.position, next, add(e.position, hig::Vec3{0, m.height * e.scale * .5f, 0}),
                                   std::max(m.radius * e.scale, m.height * e.scale * .5f), f))
                nearest = std::min(nearest, f);
        }
        if (nearest <= 1) {
            s.position = add(s.position, scale3(sub(next, s.position), nearest));
            done = true;
        } else
            s.position = next;
        for (size_t i = 0; i < count && !done; i++)
            if (reach(i, s.position, s.radius) && s.life - s.age > 5 * FRAME)
                s.life = s.age + 5 * FRAME; // what it meets leaves it five frames
        if (done) { // 0x1ceb30: 200 to all it reaches
            effects.burst(s.position, 2.2f, 0xff3090ff, 28);
            audio_events.push_back({SND_EXPLO_M2});
            explosions++;
            impacts++;
            armor_blast(s.position, s.radius, s.damage);
        }
        return done;
    }
    switch (s.state) {
    case Puddle: // 0x1ec0a0: .2 a frame to what stands in it, and what it hurts is poisoned for 120 frames (0xf3)
        for (size_t i = 0; i < count; i++)
            if (reach(i, s.position, s.radius)) {
                burn_enemy(i, s.damage * dt * 30);
                if (enemies[i].state != EnemyState::Dead)
                    poison(enemies[i], .13333f, 120);
            }
        if (int(s.age * 10) != int((s.age - dt) * 10))
            effects.puff(add(s.position, hig::Vec3{(effects_random() - .5f) * s.radius, .1f, (effects_random() - .5f) * s.radius}),
                         hig::Vec3{0, .8f, 0}, .4f, .7f, 0xa030ff70);
        break;
    case Crystal: { // 0x17a2c0: two balls up its length, each 1.4 a frame
        const float cy = std::cos(s.heading), sy = std::sin(s.heading);
        const float lean = std::atan2(s.velocity[0], 4.2f); // from the vertical, away from the hero
        const hig::Vec3 axis{sy * std::sin(lean), std::cos(lean), cy * std::sin(lean)};
        for (float along : {.117f, .55f})
            for (size_t i = 0; i < count; i++)
                if (reach(i, add(s.position, scale3(axis, s.radius * along)), s.radius * .117f))
                    burn_enemy(i, s.damage * dt * 30);
        if (done)
            effects.burst(add(s.position, hig::Vec3{0, .5f, 0}), .9f, 0xffffc0e0, 10);
        break;
    }
    case Quasicrystal: { // GravityCrystal 0x190c90
        // it takes the enemy shots that fly within 2 of it, three at a time (0x1914e8)
        const hig::Vec3 heart = add(s.position, hig::Vec3{0, s.radius * .5f, 0});
        unsigned arcs = 3;
        for (auto &p : projectiles)
            if (arcs && p.hostile && p.life > 0 && length3(sub(p.position, heart)) < 2) {
                effects.burst(p.position, .4f, 0xffffe0c0, 6);
                p.life = 0;
                arcs--;
                armor_powers++;
            }
        // an enemy that walks into it shatters it, and so does the hero
        for (size_t i = 0; i < count; i++)
            done |= reach(i, heart, .25f);
        done |= s.age > .5f && length3(sub(add(position, hig::Vec3{0, .65f, 0}), heart)) < .7f + .4f;
        if (done) {
            effects.burst(heart, 1.5f, 0xffffe0c0, 24);
            audio_events.push_back({SND_SHATTER});
        }
        break;
    }
    case IceBlock: // MobyIce 0x1a7ce8: what comes within its size is frozen for as long as it has left
        for (size_t i = 0; i < count; i++)
            if (reach(i, s.position, s.radius) && enemies[i].frozen <= 0) {
                enemies[i].frozen = std::max(FRAME, s.life - s.age);
                armor_powers++;
            }
        done |= s.age > .5f && length3(sub(position, s.position)) < .7f; // the hero breaks through it
        if (done)
            effects.burst(add(s.position, hig::Vec3{0, .6f, 0}), 1.2f, 0xffffe8c0, 14);
        break;
    case Bubble: // NapalmBubble 0x1adea0 swells to .85 and bursts into burning sludge
        if (done) {
            effects.burst(add(s.position, hig::Vec3{0, .2f, 0}), 2.5f, 0xff40a0ff, 28);
            audio_events.push_back({SND_EXPLO_M6});
            explosions++;
            // NATIVE: the burst's sixteen burning drops are not followed; what stands within 2.5 burns and is poisoned
            for (size_t i = 0; i < count; i++)
                if (reach(i, s.position, 2.5f)) {
                    ignite(enemies[i], 1.3333f);
                    poison(enemies[i], .4f, 180);
                }
        }
        break;
    case Beacon: // TripleWaveBeacon 0x1dbd38
        s.spin -= dt;
        if (s.spin <= 0) {
            s.spin = 45 * FRAME;
            const hig::Vec3 at = add(s.position, hig::Vec3{0, .2f, 0});
            Shot ring;
            ring.kind = Shot::Kind::ArmorField;
            ring.position = ring.previous = at;
            ring.radius = .1f;
            ring.grow = .35f * 30;
            ring.full = 8;
            ring.life = .375f; // .125 and a fade of .25 (0x17f480)
            if (s.weapon == 0) { // electric: 64 to what stands within 3.5
                ring.state = ShockRing;
                audio_events.push_back({SND_ELECTRIC[size_t(effects_random() * 2.999f)]});
                armor_blast(at, 3.5f, 64);
            } else {
                ring.state = s.weapon == 1 ? FireRing : SludgeRing;
                audio_events.push_back({s.weapon == 1 ? SND_EXPLO_S1 : SND_SPLAT});
            }
            weapon_shots.push_back(ring);
            s.weapon = uint8_t((s.weapon + 1) % 3);
            armor_powers++;
        }
        break;
    case FireRing:
    case SludgeRing: // 0x17fe48: what the ring's last metre passes over burns (0xff), or is poisoned (0x100)
        for (size_t i = 0; i < count; i++) {
            const auto &e = enemies[i];
            if (e.hidden || e.state == EnemyState::Dead || e.position[1] - (s.position[1] - .5f) >= 1)
                continue;
            const float d = std::hypot(e.position[0] - s.position[0], e.position[2] - s.position[2]);
            if (d > s.radius || d < s.radius - 1)
                continue;
            if (s.state == FireRing)
                ignite(enemies[i], 1.3333f);
            else if (enemies[i].poison_time <= 0)
                poison(enemies[i], .4f, 180);
        }
        break;
    default:
        break;
    }
    return done;
}
void Gameplay::render_armor_shot(const PlayerAssets &assets, const Shot &s, hig::Vec3 at, hig::Vec3 eye, Scene &scene) const {
    if (s.timer > 0)
        return;
    const auto &objects = assets.encounters.objects;
    const int index = s.model_class ? objects.class_model(s.model_class) : -1;
    const float fade = std::clamp((s.life - s.age) * 4, 0.f, 1.f);
    auto model = [&](float size, float lean) {
        if (index < 0)
            return false;
        const auto &m = objects.models[size_t(index)];
        const float cy = std::cos(s.heading), sy = std::sin(s.heading), cl = std::cos(lean), sl = std::sin(lean);
        // turned about the vertical, then leant forward
        const hig::Matrix matrix{cy * size, 0, -sy * size, 0, sy * sl * size, cl * size, cy * sl * size, 0,
                                 sy * cl * size, -sl * size, cy * cl * size, 0, at[0], at[1], at[2], 1};
        if (m.skinned) {
            if (m.animations.clips.empty())
                return false;
            append_animated_actor(m.actor, m.animations.clips[0], s.age, true, matrix, scene);
        } else
            append_rigid_model(m.rigid, matrix, scene, uint8_t(255 * fade));
        return true;
    };
    if (s.kind == Shot::Kind::ArmorFireball) {
        effects.glow(assets.effects, at, eye, .7f, 0xff40a0ff, scene);
        return;
    }
    switch (s.state) {
    case Puddle:
        effects.disc(assets.effects, add(at, hig::Vec3{0, .06f, 0}), s.radius, 0x30ff60 | uint32_t(0xb0 * fade) << 24, scene);
        break;
    case Crystal:
        if (!model(s.radius, std::atan2(s.velocity[0], 4.2f)))
            effects.glow(assets.effects, add(at, hig::Vec3{0, s.radius * .4f, 0}), eye, s.radius * .35f, 0xffffc0e0, scene);
        break;
    case Quasicrystal:
        if (!model(s.radius, 0))
            effects.glow(assets.effects, add(at, hig::Vec3{0, s.radius * .5f, 0}), eye, s.radius * .4f, 0xffffe0c0, scene);
        break;
    case IceBlock:
        if (!model(s.radius, 0))
            effects.glow(assets.effects, add(at, hig::Vec3{0, .6f, 0}), eye, .6f, 0xc0fff0c8, scene);
        break;
    case Bubble:
        if (!model(s.radius, 0))
            effects.glow(assets.effects, add(at, hig::Vec3{0, s.radius * .5f, 0}), eye, s.radius, 0xc030c0ff, scene);
        break;
    case Beacon:
        if (!model(1, 0))
            effects.glow(assets.effects, add(at, hig::Vec3{0, .4f, 0}), eye, .35f, 0xffa0ffa0, scene);
        break;
    case FireRing:
    case SludgeRing:
    case ShockRing: {
        const uint32_t colour = s.state == FireRing ? 0x2080ff : s.state == SludgeRing ? 0x30ff60 : 0xffd060; // 0xAABBGGRR
        effects.disc(assets.effects, add(at, hig::Vec3{0, .08f, 0}), s.radius, colour | uint32_t(0x90 * fade) << 24, scene);
        break;
    }
    default:
        break;
    }
}
void Gameplay::tick_armor(float dt) {
    // where he was: what the Chameleon set's wearer leaves enemies to aim at
    armor_trail_[armor_trail_at_++ % armor_trail_.size()] = position;
    // fire and poison wear an enemy down (0x7b390 / 0x7bae8: the record's amount every frame)
    for (size_t i = 0; i < enemies.size(); i++) {
        auto &e = enemies[i];
        if (e.hidden || e.state == EnemyState::Dead) {
            e.burn_time = e.poison_time = 0;
            continue;
        }
        const hig::Vec3 body{(effects_random() - .5f) * .4f, .5f + effects_random() * .5f, (effects_random() - .5f) * .4f};
        if (e.burn_time > 0) {
            const float before = e.burn_time;
            e.burn_time = std::max(0.f, e.burn_time - dt);
            burn_enemy(i, e.burn_rate * dt);
            if (int(before * 6) != int(e.burn_time * 6))
                effects.puff(add(e.position, body), hig::Vec3{0, 1.2f, 0}, .35f, .5f, 0xc030a0ff);
        }
        if (e.poison_time > 0 && e.state != EnemyState::Dead) {
            const float before = e.poison_time;
            e.poison_time = std::max(0.f, e.poison_time - dt);
            burn_enemy(i, e.poison_rate * dt);
            if (int(before * 6) != int(e.poison_time * 6))
                effects.puff(add(e.position, body), hig::Vec3{0, .6f, 0}, .3f, .5f, 0xc0008822); // 0x80108: 0xff228800
        }
        // a frozen enemy glints
        if (e.frozen > 0 && effects_random() < dt * 4)
            effects.puff(add(e.position, body), hig::Vec3{0, .2f, 0}, .2f, .5f, 0xd0ffe8c0);
    }
    // The charged Mega-Bomb armour flashes (0x1e8b68): each flash takes one off the charge, and it flashes faster
    // the more is left (9 frames a flash over 10, 18 over 5, then 48: the three records at 0x2247e0); at nothing
    // the charge is lost.
    if (armor_charged) {
        if (armor_set() != MegaBomb)
            armor_charged = false, armor_charge = 0;
        armor_flash_ -= dt;
        if (armor_charged && armor_flash_ <= 0) {
            if (armor_charge <= 0)
                armor_charged = false, armor_charge = 0;
            else {
                armor_flash_ = (armor_charge > 10.0005f ? 9 : armor_charge > 4.9995f ? 18 : 48) * FRAME;
                armor_charge -= 1;
                effects.burst(add(position, hig::Vec3{0, .7f, 0}), .9f, 0xff4040ff, 6);
            }
        }
    }
}
} // namespace sm
