// The Sprout-O-Matic and what it grows: native translations of SproutomaticPickup, Sprout, Electrosoil, LadderSprout
// and CannonSprout. Function addresses are LEVEL_02.REL file offsets (tools/sm/sm_decomp.py 02 <address>); nothing
// of it is executed.
//
// How it plays (the Sproutomatic's stream, 0x1d35c0): the stream that lands within three metres of a Sprout sitting
// in the ground pulls it out, and it follows the hero (one at a time: an earlier follower dies back to its place).
// The stream that lands in an Electrosoil's bound sends the follower to plant itself there, and it grows into the
// plant its model variant names. A plant withers when the stream is held on it, or on its soil.
// Not translated yet: the Sprout's own path (state 12 walks it without the original's navigation), what enemies'
// hits do to the plants and Sprouts, the soil's glow.
#include "hig/higweapons.h"
#include "objects.h"
#include <algorithm>
#include <cmath>

namespace sm {
namespace {
using V = hig::Vec3;
using W = ObjectWorld;
constexpr float FRAME = 1.f / 30;
V add(V a, V b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V mul(V a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float flat(V a, V b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
uint32_t hash_of(const char *name) { return hig::class_hash(name); }
bool clip_over(const W &w, const Object &o) {
    if (o.model < 0 || o.clip < 0)
        return true;
    const auto &clips = w.assets().models[o.model].animations.clips;
    return size_t(o.clip) >= clips.size() || o.time >= clips[size_t(o.clip)].duration;
}
void place(Object &o, V at, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    o.matrix = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, at[0], at[1], at[2], 1};
}

// ---- SproutomaticPickup (init 0x1d3880, update 0x1d3998, its grant 0x1d3a90) -------------------------------------
// The plant man who holds the gadget out. The hero in his trigger is given it (weapon 0x11) and takes it in hand,
// and he is gone; thirty frames of message 586 follow. He is not placed once the gadget is owned.
// Not translated: the gadget drawn in his hand (his bone 0x3e), the acquisition's own presentation (0x27080).
void pickup_tick(W &w, Object &o, Gameplay &game, float) {
    if (o.state != 0)
        return;
    if (game.arsenal.owned[hig::GadgetSproutOMatic]) {
        w.destroy(o);
        return;
    }
    if (!game.alive() || !w.inside(o.props.u32("trigger"), w.hero()))
        return;
    if (!game.grant_weapon(hig::GadgetSproutOMatic, true))
        return;
    o.state = 2;
    o.visible = false;
    game.audio_events.push_back({0xc76fbbf0});
    w.notice_request = "You have acquired the Sprout-O-Matic!"; // text 586
}

// ---- Sprout (init 0x1d0e38, update 0x1d1280, state change 0x1d2468, what to do next 0x1d2ab8) --------------------
// States as the original's: 0 burying itself, 1 in the ground, 2 pulled out, 3 idle, 4 fidgeting, 5 walking and
// 6 running after the hero, 7 going to a soil, 8 planting itself, 10 gone into its plant, 11 dying back, 12 on its
// path, 13 stepping out of the stream.
// f[0] the state's frames (+0x70), f[1..3] its own place, f[4] its heading, n[0] the soil it goes to or stands in
// (+0x164), n[1] its plant (+0x160), n[2] frames since it last arrived (+0x16c), n[3] its path's point (+0x172).
enum Sprouting : uint8_t { Bury, Ground, Pulled, Idle, Fidget, Walk, Run, ToSoil, Plant, Wait = 9, Grown, Die, Path, Aside };
constexpr const char *CLIP[] = {"DEATH", "IDLE", "IDlE_EXT", "IDlE_EX2", "GROUND", "GROUND_T", "PLANT1", "PLANT2", "RUN", "WALK", "BURY_TO_"};
constexpr float WALK_SPEED = .06666667f * 30, RUN_SPEED = .2f * 30; // the two speed records, 0x23fce0 / 0x23fd00
void sprout_clip(W &w, Object &o, unsigned index, bool loop) {
    if (o.model < 0)
        return;
    const int clip = w.assets().models[o.model].clip(CLIP[index]);
    o.clip = clip >= 0 ? clip : int(index);
    o.time = 0;
    o.loop = loop;
}
void sprout_set(W &w, Object &o, uint8_t next) {
    if (o.state == next)
        return;
    Object *soil = w.object(uint32_t(o.n[0]));
    switch (next) {
    case Bury:
        o.visible = true;
        sprout_clip(w, o, 10, false);
        break;
    case Ground:
        sprout_clip(w, o, 4, true);
        break;
    case Pulled:
        sprout_clip(w, o, 5, false);
        break;
    case Idle:
        o.f[0] = float(30 + int(w.random() * 30));
        sprout_clip(w, o, 1, true);
        break;
    case Fidget:
        sprout_clip(w, o, w.random() < .5f ? 2 : 3, false);
        break;
    case Walk:
        sprout_clip(w, o, 9, true);
        break;
    case Run:
    case Aside:
        sprout_clip(w, o, 8, true);
        break;
    case ToSoil:
        if (o.state != Run)
            sprout_clip(w, o, 8, true);
        if (soil)
            soil->n[0] = 1; // taken
        break;
    case Plant:
        sprout_clip(w, o, w.random() < .5f ? 7 : 6, false);
        break;
    case Grown: // inside its plant
        if (soil)
            soil->n[0] = 0;
        if (w.sprout_follower == o.source)
            w.sprout_follower = 0;
        o.visible = false;
        break;
    case Die:
        if (w.sprout_follower == o.source)
            w.sprout_follower = 0;
        if (soil)
            soil->n[0] = 0;
        o.n[0] = 0;
        sprout_clip(w, o, 0, false);
        break;
    case Path:
        o.n[3] = 0;
        sprout_clip(w, o, 9, true);
        break;
    default:
        break;
    }
    o.state = next;
}
uint8_t sprout_next(const W &w, const Object &o) {
    // 0x1d2ab8
    if (o.props.u32("path"))
        return Path;
    if (o.n[2] <= 29)
        return Idle;
    if (const uint32_t area = o.props.u32("followArea"); area && !w.inside(area, w.hero()))
        return Idle;
    const float d = flat(o.position(), w.hero());
    return d * d >= 25 ? Run : d * d >= 9 ? Walk : Idle;
}
// one step over the ground towards a place; true once it is there
bool sprout_step(Object &o, const Gameplay &game, V to, float speed, float dt, float reach) {
    V at = o.position();
    const float d = flat(at, to);
    if (d <= reach)
        return true;
    const float step = std::min(d - reach * .5f, speed * dt);
    at[0] += (to[0] - at[0]) / d * step;
    at[2] += (to[2] - at[2]) / d * step;
    const auto ground = game.terrain().ray(add(at, V{0, 1.2f, 0}), add(at, V{0, -2.5f, 0}));
    if (ground.hit)
        at[1] = ground.point[1];
    o.f[4] = std::atan2(to[0] - at[0], to[2] - at[2]);
    place(o, at, o.f[4]);
    return false;
}
void sprout_home(W &w, Object &o) {
    // 0x1d1e60: back to its own place, into the ground
    o.n[0] = o.n[1] = 0;
    o.matrix = o.base;
    o.state = 0xff;
    sprout_set(w, o, Bury);
}
void sprout_to_soil(W &w, Object &o, Object &soil) {
    // 0x1d1cf8: a free soil inside the Sprout's own area
    if (uint32_t(o.n[0]) == soil.source || soil.n[0] || !w.inside(o.props.u32("setTargetArea"), soil.position()))
        return;
    if (Object *old = w.object(uint32_t(o.n[0])))
        old->n[0] = 0;
    o.n[0] = int32_t(soil.source);
    sprout_set(w, o, ToSoil);
}
void sprout_grow(W &w, Object &o, Gameplay &game) {
    // 0x1d20b8: the plant its model variant names, on the soil it stands in
    Object *soil = w.object(uint32_t(o.n[0]));
    Object *plant = nullptr;
    if (soil) {
        hig::Matrix at = soil->matrix;
        if (o.variant_hash == hash_of("LadderSprout")) { // 0x192e80: a metre and a quarter behind the soil, three up
            for (int k = 0; k < 3; k++)
                at[12 + k] -= soil->matrix[8 + k] * 1.25f;
            at[13] += 3;
            plant = w.spawn(hash_of("LadderSprout"), at, game);
        } else if (o.variant_hash == hash_of("CannonSprout")) // 0x165d28
            plant = w.spawn(hash_of("CannonSprout"), at, game);
        else if (o.variant_hash == hash_of("ExplodeSprout")) // 0x184e88
            plant = w.spawn(hash_of("ExplodeSprout"), at, game);
        if (plant)
            plant->owner = soil->source;
    }
    o.n[1] = plant ? int32_t(plant->source) : 0;
    sprout_set(w, o, Grown);
    if (!plant) // nothing to grow into here: it goes home
        sprout_home(w, o);
}
void sprout_start(W &w, Object &o, Gameplay &) {
    o.f[0] = 0;
    o.f[4] = std::atan2(o.base[8], o.base[10]);
    o.n = {0, 0, 30, 0};
    o.state = 0xff;
    sprout_set(w, o, Bury);
}
void sprout_tick(W &w, Object &o, Gameplay &game, float dt) {
    const float frames = dt * 30;
    const V hero = w.hero();
    const auto &spray = game.sprout_spray;
    const bool sprayed = spray.on && flat(spray.point, o.position()) < .8f && std::abs(spray.point[1] - o.matrix[13]) < 1.2f;
    o.n[2]++;
    switch (o.state) {
    case Bury:
        if (clip_over(w, o))
            sprout_set(w, o, Ground);
        break;
    case Ground:
        // 0x1d1bf0 / 0x1d1c88: the stream within three metres of it, and it within .8 of the stream's height
        if (spray.on && std::abs(o.matrix[13] - spray.point[1]) <= .8f && flat(o.position(), spray.point) < 3) {
            if (Object *old = w.object(w.sprout_follower); old && old != &o && old->state != ToSoil && old->state != Plant)
                sprout_set(w, *old, Die);
            w.sprout_follower = o.source;
            sprout_set(w, o, Pulled);
        }
        break;
    case Pulled: // it turns to the hero as it comes out
        o.f[4] = std::atan2(hero[0] - o.matrix[12], hero[2] - o.matrix[14]);
        place(o, o.position(), o.f[4]);
        if (clip_over(w, o))
            sprout_set(w, o, sprout_next(w, o));
        break;
    case Idle: {
        const uint8_t next = sprout_next(w, o);
        if (next != Idle)
            sprout_set(w, o, next);
        else if (sprayed)
            sprout_set(w, o, Aside);
        else if ((o.f[0] -= frames) <= 0)
            sprout_set(w, o, Fidget);
        break;
    }
    case Fidget: {
        const uint8_t next = sprout_next(w, o);
        if (next != Idle)
            sprout_set(w, o, next);
        else if (sprayed)
            sprout_set(w, o, Aside);
        else if (clip_over(w, o))
            sprout_set(w, o, Idle);
        break;
    }
    case Walk:
    case Run:
        // 0x1d2bf0: after the hero, and by how far he is the next thing to do
        if (sprout_step(o, game, hero, o.state == Run ? RUN_SPEED : WALK_SPEED, dt, 1.6f)) {
            o.n[2] = 0;
            sprout_set(w, o, Idle);
        } else
            sprout_set(w, o, sprout_next(w, o));
        break;
    case ToSoil: {
        Object *soil = w.object(uint32_t(o.n[0]));
        if (!soil) {
            sprout_set(w, o, Idle);
            break;
        }
        V to = soil->position();
        if (o.variant_hash == hash_of("LadderSprout")) // it plants itself where the ladder will stand
            for (int k = 0; k < 3; k++)
                to[k] -= soil->matrix[8 + k] * 1.25f;
        if (sprout_step(o, game, to, RUN_SPEED, dt, .15f))
            sprout_set(w, o, Plant);
        break;
    }
    case Plant:
        if (clip_over(w, o))
            sprout_grow(w, o, game);
        break;
    case Grown: { // until its plant is gone
        const Object *plant = w.find(uint32_t(o.n[1]));
        if (!plant || plant->kind == ObjectKind::Logic || !plant->alive)
            sprout_home(w, o);
        break;
    }
    case Die:
        if (clip_over(w, o))
            sprout_home(w, o);
        break;
    case Path: { // its own way to the soil at the path's end
        const auto *path = w.curve(o.props.u32("path"));
        if (!path || path->points.empty() || size_t(o.n[3]) >= path->points.size()) {
            if (Object *soil = w.object(o.props.u32("endPathSoil")))
                sprout_to_soil(w, o, *soil);
            if (o.state == Path)
                sprout_set(w, o, Idle);
            break;
        }
        const auto &p = path->points[size_t(o.n[3])];
        if (sprout_step(o, game, V{p[0], p[1], p[2]}, WALK_SPEED, dt, .3f))
            o.n[3]++;
        break;
    }
    case Aside: { // a metre and a half out of the stream's way, then idle again
        if ((o.f[0] += frames) > 12 || !sprayed)
            sprout_set(w, o, Idle);
        else {
            const V away = sub(o.position(), hero);
            const float d = std::max(.01f, std::hypot(away[0], away[2]));
            sprout_step(o, game, add(o.position(), V{away[2] / d * 1.5f, 0, -away[0] / d * 1.5f}), RUN_SPEED, dt, .05f);
        }
        break;
    }
    default:
        break;
    }
}

// ---- Electrosoil (init 0x180210, update 0x180460, the stream on it 0x180558) -------------------------------------
// A patch of soil a Sprout can plant itself in. n[0]: a Sprout is on its way or planting (+0x46 bit 1). Its state
// counts down from two while the stream lands in its bound; the follower is sent to it then (0x1d35c0).
void soil_tick(W &w, Object &o, Gameplay &game, float) {
    if (o.state)
        o.state--;
    const auto &spray = game.sprout_spray;
    if (!spray.on || !w.inside(o.props.u32("bound"), spray.point))
        return;
    o.state = 2;
    if (Object *follower = w.object(w.sprout_follower); follower && follower->state != Path)
        sprout_to_soil(w, *follower, o);
}

// ---- the plants ---------------------------------------------------------------------------------------------------
// 0x1d21d8, shared by the plants: f[0] is fifteen frames of life (0x23fb48) that the stream held on the plant, or on
// its soil, runs down; it comes back while neither is so. A follower waiting for the soil ends the plant at once.
bool plant_withers(W &w, Object &o, const Gameplay &game, float frames) {
    const auto &spray = game.sprout_spray;
    const Object *soil = w.find(o.owner);
    if (!soil || soil->state == 0) {
        const bool on_plant = spray.on && flat(spray.point, o.position()) < 1.2f;
        if (!on_plant) {
            o.f[0] = std::min(15.f, o.f[0] + frames);
            return false;
        }
    } else if (const Object *follower = w.find(w.sprout_follower); follower && follower->state != Path)
        return true;
    return (o.f[0] -= frames) <= 0;
}
void plant_clip(W &w, Object &o, const char *name, bool loop) {
    if (o.model < 0)
        return;
    const int clip = w.assets().models[o.model].clip(name);
    if (clip >= 0)
        o.clip = clip;
    o.time = 0;
    o.loop = loop;
}

// ---- LadderSprout (init 0x1929d8, update 0x192a88, its spawn 0x192e80) -------------------------------------------
// A vine of leaves to climb by: nine level leaves a metre apart, 2.1 m wide and half a metre deep, the lowest 1.9 m
// over the soil and the highest 9.9 m; the original's collision holds only what is above a face, so the hero jumps
// up through them (`Object::steps`). It grows (clip GROW); from its eleventh frame it is solid, the hero having been
// moved out of the box it grows into (0x1d2298: half extents 1.75, 7, .75 about a point .4 behind it). States: 0
// growing, 1 standing, 2 withering (clip DIE), then gone.
void ladder_start(W &w, Object &o, Gameplay &) {
    o.steps = true; // leaves a metre apart that he jumps up through and stands on
    o.f[0] = 15;
    o.f[1] = 0; // frames grown
    plant_clip(w, o, "GROW", false);
}
void ladder_tick(W &w, Object &o, Gameplay &game, float dt) {
    const float frames = dt * 30;
    switch (o.state) {
    case 0:
        o.f[1] += frames;
        if (!o.solid && o.f[1] >= 11) {
            // out of its box, along the box's thin axis
            const V right{o.matrix[0], o.matrix[1], o.matrix[2]}, forward{o.matrix[8], o.matrix[9], o.matrix[10]};
            const V centre = sub(o.position(), mul(forward, .4f));
            const V d = sub(game.position, centre);
            const float x = d[0] * right[0] + d[2] * right[2], z = d[0] * forward[0] + d[2] * forward[2];
            if (std::abs(x) < 1.75f && std::abs(z) < .75f && std::abs(d[1]) < 7) {
                const float push = (z < 0 ? -.75f : .75f) - z;
                game.position[0] += forward[0] * push;
                game.position[2] += forward[2] * push;
            }
            o.solid = true;
        }
        if (clip_over(w, o))
            o.state = 1;
        break;
    case 1:
        if (plant_withers(w, o, game, frames)) {
            o.state = 2;
            o.solid = false;
            plant_clip(w, o, "DIE", false);
        }
        break;
    case 2:
        if (clip_over(w, o))
            w.destroy(o);
        break;
    }
}

// ---- CannonSprout (init 0x165900, update 0x165aa8, event 0x165bd0) -----------------------------------------------
// A plant that throws. Struck by the wrench it tosses (clip TOSS), and the hero within two and a half metres is
// thrown with its soil's speeds: throwSpeedXZ along its facing and throwSpeedY up (the hero's state 0x22, 0x6a0c0).
// The original holds him forty passes, draws him into the plant over ten and throws him at the toss; here he is
// thrown .83 s after the blow if he is still in reach. States: 0 growing, 1 standing, 2 tossing, 3 withering.
void cannon_start(W &w, Object &o, Gameplay &) {
    o.f[0] = 15;
    o.f[1] = -1; // seconds to the throw
    o.target = true;
    o.solid = true;
    plant_clip(w, o, "GROW", false);
}
void cannon_tick(W &w, Object &o, Gameplay &game, float dt) {
    const float frames = dt * 30;
    const auto *struck = w.hit(o);
    switch (o.state) {
    case 0:
    case 2:
        if (o.state == 2 && o.f[1] >= 0 && (o.f[1] -= dt) < 0 && game.alive()) {
            const V d = sub(game.position, o.position());
            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= 6.25f) {
                const auto soil = w.instance_props(o.owner);
                const float out = soil.f32("throwSpeedXZ"), up = soil.f32("throwSpeedY");
                const float length = std::max(.001f, std::hypot(o.matrix[8], o.matrix[10]));
                // from over the plant, along its facing (the original has it take him from two metres behind it,
                // 0x223cd0, and toss him over itself)
                const V forward{o.matrix[8] / length, 0, o.matrix[10] / length};
                float top = 2;
                if (o.model >= 0)
                    top = w.assets().models[o.model].maximum[1] + .4f;
                game.heading = std::atan2(forward[0], forward[2]);
                game.position = add(o.position(), V{forward[0] * .5f, top, forward[2] * .5f});
                game.launch(V{forward[0] * out, up, forward[2] * out});
                game.audio_events.push_back({0x7c9706b4}); // level-SPROUT_THROW
            }
        }
        if (clip_over(w, o)) {
            o.state = 1;
            plant_clip(w, o, "IDLE", true);
        }
        break;
    case 1:
        if (plant_withers(w, o, game, frames)) {
            o.state = 3;
            o.target = o.solid = false;
            plant_clip(w, o, "DIE", false);
        } else if (struck && (struck->kind == HitKind::Wrench || struck->kind == HitKind::HyperStrike ||
                              struck->kind == HitKind::WrenchThrow)) {
            o.state = 2;
            o.f[1] = .83f;
            plant_clip(w, o, "TOSS", false);
        }
        break;
    case 3:
        if (clip_over(w, o))
            w.destroy(o);
        break;
    }
}

// ---- ExplodeSprout (init 0x1845c0, update 0x1847d0, event 0x184950) and its ExplodeBerry (0x184070, 0x183d30) ---
// A plant that bears a berry. Struck by the wrench it sways (HIT_L or HIT_R, by the side the hero stands) and the
// berry flies: from the point (0, .927, 1.135) of the plant, 1.1666667 a frame at its soil's `angle` above the
// level, along the hero's facing, or at one of the soil's two `target`s when that lies within 45 degrees of his
// facing (the original takes the wrench's own sweep when the blow is late in its clip). It grows a new one
// (GROW_BER). The berry falls at .02777778 a frame a frame and bursts on what it meets (a ball of .475), twenty
// metres under its start, or back at its plant: 3 to what stands within 2, and the wall pieces there break.
// States: 0 growing, 1 standing, 2 swaying, 3 growing a berry, 4 withering.
void explode_start(W &w, Object &o, Gameplay &) {
    // a quarter turn about the vertical from its soil's facing (the init swaps its axes)
    hig::Matrix m = o.matrix;
    for (int k = 0; k < 3; k++) {
        m[k] = o.matrix[8 + k];
        m[8 + k] = -o.matrix[k];
    }
    o.matrix = o.base = o.previous = m;
    o.f[0] = 15;
    o.target = o.solid = true;
    plant_clip(w, o, "GROW", false);
}
void explode_tick(W &w, Object &o, Gameplay &game, float dt) {
    const float frames = dt * 30;
    const auto *struck = w.hit(o);
    switch (o.state) {
    case 0:
        if (clip_over(w, o))
            o.state = 1;
        [[fallthrough]];
    case 1:
        if (plant_withers(w, o, game, frames)) {
            o.state = 4;
            o.target = o.solid = false;
            plant_clip(w, o, "WITHER", false);
        } else if (o.state == 1 && struck &&
                   (struck->kind == HitKind::Wrench || struck->kind == HitKind::HyperStrike || struck->kind == HitKind::WrenchThrow)) {
            const V forward{o.matrix[8], o.matrix[9], o.matrix[10]};
            const V to_hero = sub(game.position, o.position());
            plant_clip(w, o, forward[2] * to_hero[0] - forward[0] * to_hero[2] < 0 ? "HIT_L" : "HIT_R", false);
            o.state = 2;
            const V start = hig::transform_point(V{0, .927f, 1.135f}, o.matrix);
            V aim{std::sin(game.heading), 0, std::cos(game.heading)};
            const auto soil = w.instance_props(o.owner);
            for (unsigned k = 0; k < 2; k++)
                if (const Object *target = w.find(soil.u32("target", k))) {
                    const V d = sub(target->position(), start);
                    const float length = std::hypot(d[0], d[2]);
                    if (length > .001f && (d[0] * std::sin(game.heading) + d[2] * std::cos(game.heading)) / length >= .7f) {
                        aim = {d[0] / length, 0, d[2] / length};
                        break;
                    }
                }
            hig::Matrix at = o.matrix;
            at[12] = start[0], at[13] = start[1], at[14] = start[2];
            if (Object *berry = w.spawn(hash_of("ExplodeBerry"), at, game)) {
                const float angle = soil.f32("angle") * 3.1415927f / 180, speed = 1.1666667f * 30;
                berry->f[0] = std::cos(angle) * speed * aim[0];
                berry->f[1] = std::sin(angle) * speed;
                berry->f[2] = std::cos(angle) * speed * aim[2];
                berry->f[3] = start[1];
                berry->owner = o.source;
                game.audio_events.push_back({0x7c9706b4}); // level-SPROUT_THROW
            }
        }
        break;
    case 2:
        if (clip_over(w, o)) {
            o.state = 3;
            plant_clip(w, o, "GROW_BER", false);
        }
        break;
    case 3:
        if (clip_over(w, o))
            o.state = 1;
        break;
    case 4:
        if (clip_over(w, o))
            w.destroy(o);
        break;
    }
}
void berry_tick(W &w, Object &o, Gameplay &game, float dt) {
    V at = o.position();
    o.f[1] -= .02777778f * 900 * dt;
    const V next = add(at, V{o.f[0] * dt, o.f[1] * dt, o.f[2] * dt});
    o.n[0]++;
    bool burst = next[1] < o.f[3] - 20;
    const Object *plant = w.find(o.owner);
    V end = next;
    if (o.n[0] > 3) { // clear of its own plant
        if (plant && flat(next, plant->position()) < .475f)
            burst = true;
        if (const auto met = game.terrain().sweep_sphere(at, next, .475f); met.hit) {
            end = add(at, mul(sub(next, at), met.fraction));
            burst = true;
        }
        for (const auto &e : game.enemies)
            if (!e.hidden && e.state != EnemyState::Dead && flat(next, e.position) < 1 && std::abs(next[1] - e.position[1] - .6f) < 1.2f)
                burst = true;
    }
    o.matrix[12] = end[0], o.matrix[13] = end[1], o.matrix[14] = end[2];
    if (!burst)
        return;
    // 0x1842d0
    w.blast(end, 2, o.class_hash);
    game.blast(end, 2, 3);
    game.effects.burst(end, 2.2f, 0xff40a0ff, 26);
    game.audio_events.push_back({0x0840ba74}); // explodeberry-berryexplode (named in the code, 0x184540)
    w.explosions++;
    w.destroy(o);
}

const ObjectClass classes[] = {
    {"SproutomaticPickup", nullptr, pickup_tick, true},
    {"Sprout", sprout_start, sprout_tick, true},
    {"Electrosoil", nullptr, soil_tick, true},
    {"LadderSprout", ladder_start, ladder_tick, true},
    {"CannonSprout", cannon_start, cannon_tick, true},
    {"ExplodeSprout", explode_start, explode_tick, true},
    {"ExplodeBerry", nullptr, berry_tick, true},
};
} // namespace
std::span<const ObjectClass> sprout_object_classes() { return classes; }
} // namespace sm
