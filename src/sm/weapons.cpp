// The weapons beyond the Lacerator: firing, shots, their damage and effects (members of Gameplay).
//
// From the original (LEVEL_02.REL offsets; docs/SIZE_MATTERS_CAMPAIGN.md "Weapons and gadgets"): which weapon is
// which class, ammo and capacity, levels and experience, the damage row each shot carries, the sounds, the models
// of the weapon in hand and of its shots. Flight paths, rates of fire, blast radii and the weapons' targeting are
// native for now: each is marked `native` below until its original function is translated.
#include "gameplay.h"
#include <algorithm>
#include <cmath>

namespace sm {
namespace {
using V = hig::Vec3;
V add(V a, V b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V mul(V a, float f) { return {a[0] * f, a[1] * f, a[2] * f}; }
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(V a) { return std::sqrt(dot(a, a)); }
V unit(V a) {
    const float l = length(a);
    return l > 1e-6f ? mul(a, 1 / l) : V{0, 0, 1};
}
// Damage rows (0x22dcd8), base + the weapon's level; the functions that read them:
constexpr unsigned ROW_BLITZ = 101;       // native choice of row group: the reader passes the row in (0x155a90)
constexpr unsigned ROW_ACID = 158;        // native choice of row group: the reader takes it from the bomb (0x13d320)
constexpr unsigned ROW_SHOCK_HIT = 0x55;  // ShockRocketShot hit, 0x1ca758
constexpr unsigned ROW_SHOCK_BLAST = 0x5d; // ShockRocketShot update, 0x1c9e30
constexpr unsigned ROW_CROSSBOW = 0xae;   // CrossbowShot blast, 0x178f00
constexpr unsigned ROW_FLAME = 0x41;      // the Scorcher's flame, per frame, 0x1890c0
constexpr unsigned ROW_LASER = 0x6d;      // the Laser Tracer's beam, per frame, 0x194ec0
constexpr unsigned ROW_BEE = 0x8d;        // ExplosiveBee, 0x185680
constexpr unsigned ROW_COMET = 0xb6;      // SuckCannonComet, 0x1f0b00
constexpr unsigned ROW_SHIELD = 0xd7;     // the Static Barrier's touch, per frame, 0x1c5a30
constexpr unsigned ROW_RYNO = 0xe7;       // RynoRocket, 0x1c3bf0
constexpr unsigned ROW_AGENT = 174;       // native choice: the agents read rows through per-level tables (0x13f9c0)
// Sound signatures (bank bindings of the level; tools/sm/sm_sounds.py)
constexpr uint32_t SND_BLITZ = 0x6cd520e6, SND_ACID_EXPLODE = 0x94c7661d, SND_ACID_POOL = 0xc12fd5fe,
                   SND_SHOCK = 0xda845aee, SND_SNIPER = 0x4fe967b3, SND_SCORCHER = 0xdabb9e47,
                   SND_LASER = 0x7ca9ac90, SND_BEE_EXPLODE = 0x58482e4e, SND_BEE_BUZZ = 0x4172c8a9,
                   SND_RYNO_FIRE = 0xfe00ca14, SND_RYNO_ROCKET = 0x2ac52a27, SND_MOO = 0x71ee0217,
                   SND_SUCK = 0x49409c4f, SND_SPIT = 0x3cd26ea5, SND_SHIELD = 0x650f1bd8;
} // namespace

float Gameplay::damage_row(unsigned base, unsigned weapon) const {
    if (!weapon_tables_)
        return 1;
    const size_t row = base + std::min<unsigned>(arsenal.level[weapon], 3);
    return row < weapon_tables_->damage.size() ? weapon_tables_->damage[row].amount : 1;
}
// The living enemy nearest the line ahead, within a range and a cone, in sight; its index or -1.
int Gameplay::aim_enemy(V from, float range, float cone_cos, V *aim) const {
    const V forward{std::sin(heading), 0, std::cos(heading)};
    int best = -1;
    float best_dot = cone_cos;
    if (encounter_assets_)
        for (size_t i = 0; i < enemies.size(); i++) {
            const auto &e = enemies[i];
            if (e.hidden || e.state == EnemyState::Dead)
                continue;
            const auto &m = encounter_assets_->models[e.model];
            const V target = add(e.position, V{0, m.height * e.scale * .55f, 0}), delta = sub(target, from);
            const float d = length(delta);
            if (d > range || d < 1e-3f)
                continue;
            const float facing = dot(unit(V{delta[0], 0, delta[2]}), forward);
            if (facing > best_dot && std::abs(delta[1]) < range * .6f && !terrain_.ray(from, target).hit) {
                best_dot = facing;
                best = int(i);
                if (aim)
                    *aim = unit(delta);
            }
        }
    if (best < 0 && aim)
        *aim = forward;
    return best;
}
void Gameplay::explode_at(V at, float radius, float damage, uint32_t sound, uint32_t color) {
    explosions++;
    blast(at, radius, damage);
    for (const auto &t : targets)
        if (length(sub(t.center, at)) < radius + t.radius)
            target_hits.push_back({t.id, HitKind::Explosion, damage, at});
    effects.burst(at, radius * .9f, color, 28);
    if (sound)
        audio_events.push_back({sound});
}

void Gameplay::tick_arsenal(const hig::PadState &pad, float dt) {
    beam_ = false;
    sprout_spray.on = false;
    if (clank) // Clank carries nothing (0x70c18 switches the weapons off)
        return;
    shield_time_ = std::max(0.f, shield_time_ - dt);
    if (!weapon_tables_ || arsenal.equipped == hig::WeaponBlaster)
        return;
    const unsigned w = arsenal.equipped;
    if (w == hig::GadgetHypershot) {
        // 0x6b240: fire with a target in reach takes it
        if ((pad.pressed & hig::Circle) && !swimming && hurt_time_ <= 0 && wrench_time_ < 0 && !hyper_phase_ && throw_time_ < 0 &&
            health && motion != Motion::Grapple) {
            // in the air too: off one swing he takes the next target (Kalidon's lake has two with nothing between)
            if (const Hook *hook = hook_in_reach()) {
                launch_ = false;
                grapple_ = {};
                grapple_.phase = 1;
                grapple_.anchor = hook->position;
                grapple_.swing = hook->swing;
                grapple_.rope = hook->rope;
                motion = Motion::Grapple;
                animation_time = 0;
                velocity = {};
                strafing = false;
                pack_jump_ = Motion::Idle;
                grapples++;
                audio_events.push_back({0x8d38adfe}); // Hypershot-FIRE
            }
        }
        return;
    }
    if (w == hig::GadgetSproutOMatic) {
        // Sproutomatic 0x1d3220: while fire is held it sprays. The original follows the stream's own drops to where
        // they land (0x711f0 / 0x71520); here the stream lands three metres ahead of him (NATIVE).
        if (!swimming && hurt_time_ <= 0 && wrench_time_ < 0 && !hyper_phase_ && throw_time_ < 0 && health && !launch_ &&
            (pad.raw.buttons & hig::Circle)) {
            const V forward{std::sin(heading), 0, std::cos(heading)};
            const V ahead = add(position, mul(forward, 3));
            const auto ground = terrain_.ray(add(ahead, V{0, 1.5f, 0}), add(ahead, V{0, -3, 0}));
            sprout_spray.on = true;
            sprout_spray.point = ground.hit ? ground.point : ahead;
            if (pad.pressed & hig::Circle)
                audio_events.push_back({0x40077ac9}); // SproutOMatic-SPROUT_FIRE (0x1d35c0 names it)
            action_time_ = 1.f / 3;
            const V from = weapon_muzzle(1);
            effects.puff(from, mul(sub(add(sprout_spray.point, V{0, .9f, 0}), from), 2.4f), .16f, .42f, 0xd060ff60);
        }
        return;
    }
    if (weapon_tables_->weapons[w].type != 2)
        return;
    const bool can = !swimming && hurt_time_ <= 0 && wrench_time_ < 0 && !hyper_phase_ && throw_time_ < 0 && health;
    const bool held = can && (pad.raw.buttons & hig::Circle);
    // a press during the last third of a second still counts when the weapon comes ready (a press is not lost to
    // the pause after a shot or a weapon change)
    fire_latch_ = can && (pad.pressed & hig::Circle) ? .35f : std::max(0.f, fire_latch_ - dt);
    const bool pressed = fire_latch_ > 0;
    const bool ready = fire_cooldown_ <= 0;
    const bool glove = w == hig::WeaponBombGlove || w == hig::WeaponAgentsGlove || w == hig::WeaponBeeMineGlove;
    fire_clip_ = glove ? 39 : w == hig::WeaponFlamethrower ? 76 : 26; // BT / FLAMETHR / FBOD_BLA
    fire_layer_clip_ = 25;
    V muzzle = weapon_muzzle(1);
    const V chest = add(position, V{0, .85f, 0});
    if (auto wall = terrain_.ray(chest, muzzle); wall.hit)
        muzzle = add(wall.point, mul(wall.normal, .02f));
    auto begin = [&](float cooldown, float pose_time, uint32_t sound) {
        fire_cooldown_ = cooldown;
        fire_latch_ = 0;
        fire_time_ = 0;
        action_time_ = pose_time;
        shots++;
        if (sound)
            audio_events.push_back({sound});
    };
    auto shot = [&](Shot::Kind kind, V velocity, float life, float damage, float radius, float gravity,
                    const char *model) -> Shot & {
        Shot s;
        s.kind = kind;
        s.weapon = uint8_t(w);
        s.position = s.previous = muzzle;
        s.velocity = velocity;
        s.life = life;
        s.damage = damage;
        s.radius = radius;
        s.gravity = gravity;
        s.heading = heading;
        s.model_class = model ? hig::class_hash(model) : 0;
        weapon_shots.push_back(s);
        return weapon_shots.back();
    };
    // a lob that lands on the aimed enemy, or a throw's length ahead (native arc)
    auto lob = [&](float reach, float time, V *target_out = nullptr) {
        V aim;
        const int victim = aim_enemy(muzzle, reach * 1.6f, .85f, &aim);
        V target = victim >= 0 ? enemies[victim].position
                               : add(position, V{std::sin(heading) * reach, 0, std::cos(heading) * reach});
        if (target_out)
            *target_out = target;
        const V delta = sub(target, muzzle);
        return V{delta[0] / time, delta[1] / time + .5f * 22 * time, delta[2] / time};
    };
    switch (w) {
    case hig::WeaponBlitzGun: // the Concussion Gun: a cone of pellets (native: nine, 20 degrees, 11 m)
        if (pressed && ready && ammo) {
            ammo--;
            begin(.75f, .35f, SND_BLITZ);
            V aim;
            aim_enemy(muzzle, 14, .9f, &aim);
            const V right = unit(V{aim[2], 0, -aim[0]}), up{0, 1, 0};
            for (int i = 0; i < 9; i++) {
                const float a = float(i) * .698f, r = i ? .17f : 0;
                const V dir = unit(add(aim, add(mul(right, std::cos(a) * r), mul(up, std::sin(a) * r))));
                shot(Shot::Kind::Pellet, mul(dir, 70), .16f, damage_row(ROW_BLITZ, w), 0, 0, nullptr);
            }
            effects.burst(muzzle, .5f, 0xff70d0ff, 6);
        }
        break;
    case hig::WeaponBombGlove: // the Acid Bomb Glove: a lobbed bomb, a blast and a pool that goes on burning
        if (pressed && ready && ammo) {
            ammo--;
            begin(.9f, .8f, 0);
            auto &s = shot(Shot::Kind::AcidBomb, lob(9, .75f), 4, damage_row(ROW_ACID, w), 3.2f, 22, "Acidbomb");
            s.timer = .25f; // leaves the hand a quarter of a second into the throw
        }
        break;
    case hig::WeaponAgentsGlove: // Agents of Doom: a thrown capsule that opens into four small robots
        if (pressed && ready && ammo) {
            ammo--;
            begin(.9f, .8f, 0);
            auto &s = shot(Shot::Kind::AgentBomb, lob(6, .7f), 4, damage_row(ROW_AGENT, w), 0, 22, "AgentsBomb");
            s.timer = .25f;
        }
        break;
    case hig::WeaponBeeMineGlove: // the Bee Mine Glove: a mine that lets its bees out at an enemy coming near
        if (pressed && ready && ammo) {
            ammo--;
            begin(.9f, .8f, 0);
            auto &s = shot(Shot::Kind::BeeMine, lob(7, .7f), 30, damage_row(ROW_BEE, w), 6, 22, "BeeMine");
            s.timer = .25f;
        }
        break;
    case hig::WeaponShieldCharger: // the Static Barrier: a charge keeps a damaging shield up (native: twelve seconds)
        if (pressed && ready && ammo && shield_time_ <= 0) {
            ammo--;
            begin(.6f, .3f, SND_SHIELD);
            shield_time_ = 12;
        }
        break;
    case hig::WeaponShockRocket: // a rocket that bursts on what it meets
        if (pressed && ready && ammo) {
            ammo--;
            begin(.8f, .35f, SND_SHOCK);
            V aim;
            const int victim = aim_enemy(muzzle, 40, .9f, &aim);
            auto &s = shot(Shot::Kind::Rocket, mul(aim, 32), 3, damage_row(ROW_SHOCK_HIT, w), 3, 0, "ShockRocketShot");
            s.target = victim;
        }
        break;
    case hig::WeaponCrossbowGun: // the Sniper Mine: a fast bolt that bursts where it lands
        if (pressed && ready && ammo) {
            ammo--;
            begin(1.1f, .4f, SND_SNIPER);
            V aim;
            aim_enemy(muzzle, 70, .97f, &aim);
            shot(Shot::Kind::SniperMine, mul(aim, 95), 1.2f, damage_row(ROW_CROSSBOW, w), 3, 0, "CrossbowShot");
        }
        break;
    case hig::WeaponFlamethrower: // the Scorcher: a tongue of flame while the trigger is held
        if (held && ammo) {
            if (ready) {
                ammo--;
                begin(.1f, .3f, weapon_timer_ <= 0 ? SND_SCORCHER : 0);
                weapon_timer_ = .5f;
            }
            action_time_ = .2f;
            V aim;
            aim_enemy(muzzle, 8, .8f, &aim);
            for (int i = 0; i < 2; i++) {
                const V jitter{(effects_random() - .5f) * .25f, (effects_random() - .5f) * .2f,
                               (effects_random() - .5f) * .25f};
                effects.puff(muzzle, mul(unit(add(aim, jitter)), 13), .35f + .5f * effects_random(), .5f,
                             i ? 0xc0207aff : 0xd040d0ff);
            }
            // everything in the tongue burns each frame (native reach: 7 m, 30 degrees)
            const float per_frame = damage_row(ROW_FLAME, w) * dt * 30;
            if (encounter_assets_)
                for (size_t i = 0; i < enemies.size(); i++) {
                    const auto &e = enemies[i];
                    if (e.hidden || e.state == EnemyState::Dead)
                        continue;
                    const V delta = sub(add(e.position, V{0, .5f, 0}), muzzle);
                    if (length(delta) < 7 && dot(unit(delta), aim) > .86f)
                        burn_enemy(i, per_frame);
                }
            for (const auto &t : targets)
                if (length(sub(t.center, muzzle)) < 6 && dot(unit(sub(t.center, muzzle)), aim) > .8f)
                    target_hits.push_back({t.id, HitKind::Shot, 1, muzzle});
        }
        weapon_timer_ = std::max(0.f, weapon_timer_ - dt);
        break;
    case hig::WeaponLaserTracer: // a steady beam while the trigger is held
        if (held && ammo) {
            if (ready) {
                ammo--;
                begin(1.f / 15, .3f, weapon_timer_ <= 0 ? SND_LASER : 0);
                weapon_timer_ = .6f;
            }
            action_time_ = .2f;
            V aim;
            const int victim = aim_enemy(muzzle, 30, .9f, &aim);
            V end = add(muzzle, mul(aim, 30));
            if (auto wall = terrain_.ray(muzzle, end); wall.hit)
                end = wall.point;
            if (victim >= 0) {
                const auto &m = encounter_assets_->models[enemies[victim].model];
                end = add(enemies[victim].position, V{0, m.height * enemies[victim].scale * .55f, 0});
                burn_enemy(size_t(victim), damage_row(ROW_LASER, w) * dt * 30);
            }
            for (const auto &t : targets) {
                const V to = sub(t.center, muzzle);
                const float along = dot(to, aim);
                if (along > 0 && along < length(sub(end, muzzle)) && length(sub(to, mul(aim, along))) < t.radius)
                    target_hits.push_back({t.id, HitKind::Shot, 1, muzzle});
            }
            beam_ = true;
            beam_from_ = muzzle;
            beam_to_ = end;
        }
        weapon_timer_ = std::max(0.f, weapon_timer_ - dt);
        break;
    case hig::WeaponSuckCannon: // draws small enemies in while held, then fires what it holds
        if (held) {
            action_time_ = .2f;
            bool pulling = false;
            if (encounter_assets_ && ammo < max_ammo)
                for (size_t i = 0; i < enemies.size(); i++) {
                    auto &e = enemies[i];
                    const auto &m = encounter_assets_->models[e.model];
                    if (e.hidden || e.state == EnemyState::Dead || m.health > 3 * (level_number_ + 1))
                        continue; // only the level's small fry fit down the barrel (native measure)
                    const V delta = sub(muzzle, add(e.position, V{0, m.height * e.scale * .5f, 0}));
                    const float d = length(delta);
                    if (d > 9 || dot(unit(mul(delta, -1)), V{std::sin(heading), 0, std::cos(heading)}) < .8f)
                        continue;
                    pulling = true;
                    e.position = add(e.position, mul(unit(delta), std::min(d, 9 * dt)));
                    if (d < .9f) {
                        hurt_enemy(i, 1e6f);
                        ammo++;
                        audio_events.push_back({SND_SUCK});
                    }
                }
            if (pulling && weapon_timer_ <= 0) {
                audio_events.push_back({SND_SUCK});
                weapon_timer_ = .8f;
            }
            if (!pulling && pressed && ready && ammo) {
                ammo--;
                begin(.5f, .35f, SND_SPIT);
                V aim;
                aim_enemy(muzzle, 30, .9f, &aim);
                shot(Shot::Kind::Comet, mul(aim, 34), 2.5f, damage_row(ROW_COMET, w), 2.5f, 4, "SuckCannonComet");
            }
        }
        weapon_timer_ = std::max(0.f, weapon_timer_ - dt);
        break;
    case hig::WeaponMootator: // holds a beam on an enemy until it turns into a cow (native: a second and a half)
        if (held) {
            action_time_ = .2f;
            V aim;
            const int victim = aim_enemy(muzzle, 18, .85f, &aim);
            if (victim >= 0) {
                const auto &m = encounter_assets_->models[enemies[victim].model];
                beam_ = true;
                beam_from_ = muzzle;
                beam_to_ = add(enemies[victim].position, V{0, m.height * enemies[victim].scale * .55f, 0});
                if (moo_target_ != victim) {
                    moo_target_ = victim;
                    moo_time_ = 0;
                    audio_events.push_back({SND_MOO});
                }
                if ((moo_time_ += dt) > 1.5f) {
                    auto &cow = shot(Shot::Kind::Cow, {}, 9, 0, 0, 0, "MutantCow");
                    cow.position = cow.previous = enemies[victim].position;
                    cow.heading = enemies[victim].heading;
                    enemies[victim].time = 1e6f; // no corpse: it left as a cow
                    hurt_enemy(size_t(victim), 1e6f);
                    effects.burst(beam_to_, 1.2f, 0xff90ffa0, 14);
                    moo_target_ = -1;
                    shots++;
                }
            } else
                moo_target_ = -1;
        } else
            moo_target_ = -1;
        break;
    case hig::WeaponRyno: // a volley of rockets that find their own targets (native: six)
        if (pressed && ready && ammo) {
            ammo--;
            begin(1.2f, .5f, SND_RYNO_FIRE);
            for (int i = 0; i < 6; i++) {
                const float a = heading + (float(i) - 2.5f) * .22f;
                auto &s = shot(Shot::Kind::RynoRocket, V{std::sin(a) * 14, 6 + float(i % 3) * 2, std::cos(a) * 14}, 4,
                               damage_row(ROW_RYNO, w), 3.5f, 0, "RynoRocket");
                s.timer = float(i) * .07f;
            }
        }
        break;
    default:
        break;
    }
}

void Gameplay::burn_enemy(size_t index, float damage) {
    auto &e = enemies[index];
    if (e.hidden || e.state == EnemyState::Dead)
        return;
    // per-frame damage wears the enemy down without restarting its flinch every frame
    if (e.health - damage <= 0)
        hurt_enemy(index, damage);
    else {
        e.health -= damage;
        if (e.state == EnemyState::Idle)
            e.state = EnemyState::Alert;
    }
}

void Gameplay::tick_shots(float dt) {
    tick_armor(dt);
    if (shield_time_ > 0 && encounter_assets_) { // the Static Barrier burns what touches it
        const float per_frame = damage_row(ROW_SHIELD, hig::WeaponShieldCharger) * dt * 30;
        for (size_t i = 0; i < enemies.size(); i++)
            if (length(sub(enemies[i].position, position)) < 1.7f)
                burn_enemy(i, std::max(per_frame, .05f));
    }
    const size_t count = weapon_shots.size(); // shots spawned below wait for the next tick
    for (size_t index = 0; index < count; index++) {
        auto s = weapon_shots[index]; // by value: the vector grows when a shot spawns others
        if (s.kind == Shot::Kind::ArmorFireball || s.kind == Shot::Kind::ArmorField) { // an armour set's (armor.cpp)
            const bool over = tick_armor_shot(s, dt);
            weapon_shots[index] = s;
            if (over)
                weapon_shots[index].life = -1;
            continue;
        }
        s.previous = s.position;
        if (s.timer > 0) { // not yet out of the hand
            s.timer -= dt;
            s.position = s.previous = weapon_muzzle(1);
            weapon_shots[index] = s;
            continue;
        }
        s.age += dt;
        s.spin += dt * 9;
        bool done = s.age >= s.life;
        const auto &row_model = [&](size_t i) -> const EnemyModel & { return encounter_assets_->models[enemies[i].model]; };
        auto nearest = [&](V from, float range) {
            int best = -1;
            float best_d = range;
            if (encounter_assets_)
                for (size_t i = 0; i < enemies.size(); i++) {
                    if (enemies[i].hidden || enemies[i].state == EnemyState::Dead)
                        continue;
                    const float d = length(sub(enemies[i].position, from));
                    if (d < best_d) {
                        best_d = d;
                        best = int(i);
                    }
                }
            return best;
        };
        // straight or falling flight; what it meets first: the ground, an enemy, a world target
        auto fly = [&](float body, int *victim, const Target **struck, RayHit *ground) {
            s.velocity[1] -= s.gravity * dt;
            const V next = add(s.position, mul(s.velocity, dt));
            auto hit = terrain_.ray(s.position, next);
            float closest = hit.hit ? hit.fraction : 1.1f;
            *victim = -1;
            *struck = nullptr;
            float f;
            if (encounter_assets_)
                for (size_t i = 0; i < enemies.size(); i++) {
                    const auto &e = enemies[i];
                    if (e.hidden || e.state == EnemyState::Dead)
                        continue;
                    const auto &m = row_model(i);
                    if (segment_sphere_hit(s.position, next, add(e.position, V{0, m.height * e.scale * .5f, 0}),
                                           std::max(m.radius * e.scale, m.height * e.scale * .5f) + body, f) &&
                        f < closest) {
                        closest = f;
                        *victim = int(i);
                    }
                }
            for (const auto &t : targets)
                if (segment_sphere_hit(s.position, next, t.center, t.radius + body, f) && f < closest) {
                    closest = f;
                    *struck = &t;
                    *victim = -1;
                }
            *ground = hit;
            const bool met = closest <= 1;
            s.position = met ? add(s.position, mul(sub(next, s.position), closest)) : next;
            return met;
        };
        int victim;
        const Target *struck;
        RayHit ground;
        switch (s.kind) {
        case Shot::Kind::Pellet:
            if (fly(.1f, &victim, &struck, &ground)) {
                if (victim >= 0)
                    hurt_enemy(size_t(victim), s.damage);
                if (struck)
                    target_hits.push_back({struck->id, HitKind::Shot, s.damage, s.previous});
                effects.burst(s.position, .25f, 0xff70d0ff, 3);
                impacts++;
                done = true;
            }
            break;
        case Shot::Kind::Rocket:
        case Shot::Kind::RynoRocket:
        case Shot::Kind::SniperMine:
        case Shot::Kind::Comet: {
            if (s.kind == Shot::Kind::RynoRocket && s.age > .25f) { // turns towards the nearest enemy
                if (s.target < 0 || enemies[size_t(s.target)].state == EnemyState::Dead)
                    s.target = nearest(s.position, 45);
                const V wanted = s.target >= 0 ? unit(sub(add(enemies[size_t(s.target)].position, V{0, .6f, 0}), s.position))
                                               : unit(s.velocity);
                s.velocity = mul(unit(add(mul(unit(s.velocity), 1 - 6 * dt), mul(wanted, 6 * dt))), 30);
            }
            if (s.kind != Shot::Kind::Comet && int(s.age * 30) != int((s.age - dt) * 30))
                effects.puff(s.position, mul(unit(s.velocity), -1.5f), .25f, .35f,
                             s.kind == Shot::Kind::Rocket ? 0xb0ffc080 : 0xb060b0ff);
            if (fly(.15f, &victim, &struck, &ground) || done) {
                const bool shock = s.kind == Shot::Kind::Rocket;
                if (victim >= 0)
                    hurt_enemy(size_t(victim), s.damage);
                explode_at(s.position, s.radius,
                           shock ? damage_row(ROW_SHOCK_BLAST, hig::WeaponShockRocket) * .5f : s.damage,
                           s.kind == Shot::Kind::RynoRocket ? SND_RYNO_ROCKET : SND_BEE_EXPLODE,
                           shock ? 0xffffb070 : 0xff40a0ff);
                impacts++;
                done = true;
            }
            break;
        }
        case Shot::Kind::AcidBomb:
            if (fly(.15f, &victim, &struck, &ground) || done) {
                explode_at(s.position, s.radius, s.damage, SND_ACID_EXPLODE, 0xff40ff80);
                Shot pool;
                pool.kind = Shot::Kind::AcidPool;
                pool.weapon = s.weapon;
                pool.position = pool.previous = s.position;
                pool.life = 4; // native: the pool burns for four seconds
                pool.radius = 2.2f;
                pool.damage = s.damage * .04f; // native: a twenty-fifth of the blast a frame
                weapon_shots.push_back(pool);
                audio_events.push_back({SND_ACID_POOL});
                impacts++;
                done = true;
            }
            break;
        case Shot::Kind::ArmorFireball: // ticked above
        case Shot::Kind::ArmorField:
            break;
        case Shot::Kind::AcidPool:
            if (encounter_assets_)
                for (size_t i = 0; i < enemies.size(); i++)
                    if (length(sub(enemies[i].position, s.position)) < s.radius)
                        burn_enemy(i, s.damage * dt * 30);
            if (int(s.age * 10) != int((s.age - dt) * 10))
                effects.puff(add(s.position, V{(effects_random() - .5f) * s.radius, .1f, (effects_random() - .5f) * s.radius}),
                             V{0, .8f, 0}, .5f, .7f, 0xa030ff70);
            break;
        case Shot::Kind::BeeMine:
            if (s.state == 0) { // in the air
                if (fly(.1f, &victim, &struck, &ground)) {
                    s.state = 1;
                    s.velocity = {};
                    s.gravity = 0;
                }
            } else if (int near = nearest(s.position, s.radius); near >= 0) {
                for (int i = 0; i < 5; i++) { // its bees
                    Shot bee;
                    bee.kind = Shot::Kind::Bee;
                    bee.weapon = s.weapon;
                    bee.position = bee.previous = add(s.position, V{0, .4f, 0});
                    const float a = float(i) * 1.2566f;
                    bee.velocity = V{std::cos(a) * 4, 3, std::sin(a) * 4};
                    bee.life = 6;
                    bee.damage = s.damage;
                    bee.radius = 1.5f;
                    bee.model_class = hig::class_hash("ExplosiveBee");
                    weapon_shots.push_back(bee);
                }
                audio_events.push_back({SND_BEE_BUZZ});
                effects.burst(s.position, .8f, 0xff60e0ff, 10);
                done = true;
            }
            break;
        case Shot::Kind::Bee: {
            const int near = nearest(s.position, 25);
            if (near >= 0) {
                const auto &m = row_model(size_t(near));
                const V wanted = unit(sub(add(enemies[size_t(near)].position, V{0, m.height * .5f, 0}), s.position));
                s.velocity = mul(unit(add(mul(unit(s.velocity), 1 - 5 * dt), mul(wanted, 5 * dt))), 10);
            }
            s.position = add(s.position, mul(s.velocity, dt));
            if (near >= 0 && length(sub(enemies[size_t(near)].position, s.position)) < 1.2f) {
                hurt_enemy(size_t(near), s.damage);
                effects.burst(s.position, .7f, 0xff60e0ff, 8);
                audio_events.push_back({SND_BEE_EXPLODE});
                impacts++;
                done = true;
            }
            break;
        }
        case Shot::Kind::AgentBomb:
            if (fly(.1f, &victim, &struck, &ground) || done) {
                for (int i = 0; i < 4; i++) { // the agents
                    Shot agent;
                    agent.kind = Shot::Kind::Agent;
                    agent.weapon = s.weapon;
                    const float a = float(i) * 1.5708f;
                    agent.position = agent.previous = add(s.position, V{std::cos(a) * .7f, 0, std::sin(a) * .7f});
                    agent.life = 18; // native: they stay eighteen seconds
                    agent.damage = s.damage * .12f;
                    agent.radius = 1.3f;
                    agent.model_class = hig::class_hash("AgentOfDoom");
                    weapon_shots.push_back(agent);
                }
                effects.burst(s.position, 1, 0xffffc0a0, 12);
                done = true;
            }
            break;
        case Shot::Kind::Agent: {
            const int near = nearest(s.position, 22);
            if (near >= 0) {
                const V to = sub(enemies[size_t(near)].position, s.position);
                const float d = std::hypot(to[0], to[2]);
                s.heading = std::atan2(to[0], to[2]);
                if (d > s.radius) {
                    V velocity{to[0] / d * 5.5f, s.velocity[1] - 22 * dt, to[2] / d * 5.5f};
                    bool grounded_now = false;
                    terrain_.move(s.position, velocity, dt, grounded_now);
                    s.velocity = velocity;
                    s.state = 1; // running
                } else {
                    s.state = 2; // attacking
                    if ((s.timer -= dt) <= -.5f) {
                        s.timer = 0;
                        hurt_enemy(size_t(near), s.damage);
                    }
                }
            } else
                s.state = 0;
            if (s.age >= s.life) {
                explode_at(add(s.position, V{0, .4f, 0}), 2, s.damage * 2, SND_BEE_EXPLODE, 0xffffc0a0);
                done = true;
            }
            break;
        }
        case Shot::Kind::Cow:
            if (done)
                effects.burst(add(s.position, V{0, .6f, 0}), 1.2f, 0xffffffff, 12);
            break;
        }
        weapon_shots[index] = s;
        if (done)
            weapon_shots[index].life = -1;
    }
    std::erase_if(weapon_shots, [](const Shot &s) { return s.life < 0; });
}

void Gameplay::render_shots(const PlayerAssets &assets, float alpha, V eye, Scene &scene) const {
    const auto &objects = assets.encounters.objects;
    const float a = std::clamp(alpha, 0.f, 1.f);
    for (const auto &s : weapon_shots) {
        const V at = add(s.previous, mul(sub(s.position, s.previous), a));
        if (s.kind == Shot::Kind::Pellet) {
            effects.glow(assets.effects, at, eye, .12f, 0xffa0e0ff, scene);
            continue;
        }
        if (s.kind == Shot::Kind::AcidPool) {
            effects.disc(assets.effects, add(at, V{0, .06f, 0}), s.radius, 0xb030ff60, scene);
            continue;
        }
        if (s.kind == Shot::Kind::ArmorField || s.kind == Shot::Kind::ArmorFireball) {
            render_armor_shot(assets, s, at, eye, scene);
            continue;
        }
        const int index = s.model_class ? objects.class_model(s.model_class) : -1;
        if (index < 0) {
            effects.glow(assets.effects, at, eye, .3f, 0xffffffff, scene);
            continue;
        }
        const auto &model = objects.models[index];
        // facing: along the flight for shots, the kept heading for what stands on the ground
        float yaw = s.heading, pitch = 0;
        if (length(s.velocity) > .5f && s.kind != Shot::Kind::Agent && s.kind != Shot::Kind::Cow) {
            const V d = unit(s.velocity);
            yaw = std::atan2(d[0], d[2]);
            pitch = -std::asin(std::clamp(d[1], -1.f, 1.f));
        }
        const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        const hig::Matrix m{cy, 0, -sy, 0, sy * sp, cp, cy * sp, 0, sy * cp, -sp, cy * cp, 0, at[0], at[1], at[2], 1};
        if (model.skinned) {
            int clip = model.clip("IDLE");
            if (s.kind == Shot::Kind::Agent)
                clip = model.clip(s.state == 1 ? "RUN" : s.state == 2 ? "ATTACK" : "IDLE");
            if (s.kind == Shot::Kind::Bee)
                clip = model.clip("FLY");
            if (clip < 0)
                clip = 0;
            if (!model.animations.clips.empty())
                append_animated_actor(model.actor, model.animations.clips[size_t(clip)], s.age + a / 30, true, m, scene);
        } else
            append_rigid_model(model.rigid, m, scene);
        if (s.kind == Shot::Kind::Rocket || s.kind == Shot::Kind::RynoRocket)
            effects.glow(assets.effects, at, eye, .35f, s.kind == Shot::Kind::Rocket ? 0xffffc080 : 0xff60b0ff, scene);
    }
    if (beam_) {
        const bool moo = arsenal.equipped == hig::WeaponMootator;
        effects.beam(assets.effects, beam_from_, beam_to_, eye, moo ? .12f : .07f, moo ? 0xff80ff90 : 0xff4040ff,
                     fire_time_ * 6, scene);
    }
    if (shield_time_ > 0) { // the barrier: a ring of glows about the hero (its sphere model is not decoded yet)
        const V centre = add(interpolated(alpha), V{0, .7f, 0});
        for (int i = 0; i < 8; i++) {
            const float angle = float(i) * .7854f + shield_time_ * 2;
            effects.glow(assets.effects, add(centre, V{std::cos(angle) * 1.1f, std::sin(angle * 2) * .3f, std::sin(angle) * 1.1f}),
                         eye, .45f, 0xb0ffb070, scene);
        }
    }
}
} // namespace sm
