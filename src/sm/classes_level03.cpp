// Kalidon (level 03, the Technomite factory): native translations of the level's own classes.
// Function addresses are LEVEL_03.REL file offsets (tools/sm/sm_decomp.py 03 <address>); nothing of it is executed.
#include "objects.h"
#include "encounters.h"
#include "../hig/higactor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace sm {
namespace {
using V = hig::Vec3;
using W = ObjectWorld;
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
bool is(const Object &o, const char *name) { return o.class_hash == hig::class_hash(name); }

// ---- Lvl3MagnaRoom (init 0x18f218, update 0x18f358) ---------------------------------------------------------------
// The round room he crosses on the Magneboots. Its DeathTrigger kills whoever is outside the room's sphere, so the
// room holds that trigger off (the trigger's +0x46) every frame he is not in the room: state 0 until he is in
// `doorway1` or `doorway2`, state 1 while he is in `updateArea`, and held off while he stands in a doorway.
// Not translated: the lightning it draws while he dies there (four bolts every two to four seconds, sound
// 0x4928a48a), the scrolling wall texture (uOffset, vOffset) and the two help lines (after 600 s and 1800 s).
void magna_hold(W &w, Object &o) {
    if (Object *trigger = w.object(o.props.u32("deathTrigger")))
        trigger->n[0] = 1;
}
void magna_start(W &w, Object &o, Gameplay &) {
    o.state = 0;
    magna_hold(w, o);
}
void magna_tick(W &w, Object &o, Gameplay &, float) {
    const V hero = w.hero();
    const bool doorway = w.inside(o.props.u32("doorway1"), hero) || w.inside(o.props.u32("doorway2"), hero);
    if (o.state == 0) {
        magna_hold(w, o);
        if (doorway)
            o.state = 1;
        return;
    }
    if (!w.inside(o.props.u32("updateArea"), hero)) {
        o.state = 0;
        magna_hold(w, o);
        return;
    }
    if (doorway)
        magna_hold(w, o);
}

// ---- Lvl3Door (init 0x18b358, update 0x18b700) ---------------------------------------------------------------------
// A door that slides between the two points of `path` in `moveTime` seconds (a smoothstep of its progress, +0x70).
// Its states are the instance's state byte: 0 shut, opening for the hero in `activate`; 1 opening, and shutting
// again (2) once he has left `activate`, unless something holds it open (+0x46), when it ends open for good (5);
// 2 shutting, to 0, or to 3 with `lockWhenClosed`; 3 locked until `noLockMoby` is gone; 4 waiting for `controller`
// (an EnemyController with no enemy left, or any other object gone); 5 open, shutting once behind the hero when he
// enters `activate`. A door with a `grindLock` does nothing at all while that lock is locked.
// f[0] progress, f[1] its step a frame, f[2..4] and f[5..7] the path's two points; n[0] the hold (+0x46),
// n[1] `activate` spent, n[2] the lock given up.
// Not translated: the portal groups it switches (openOffGroups, outClosedOffGroups, inClosedOffGroups: what is
// drawn behind a shut door), its sound (the variant's row of table 0x2211a0), the save flag 0x2f that leaves the
// first two doors open in a later visit.
void door_place(Object &o, float t, bool opening) {
    t = t <= .0001f ? 0.f : t >= .999f ? 1.f : t * t * 3 - t * t * t * 2;
    const float *from = opening ? &o.f[2] : &o.f[5], *to = opening ? &o.f[5] : &o.f[2];
    for (int k = 0; k < 3; k++)
        o.matrix[12 + k] = from[k] + (to[k] - from[k]) * t;
}
void door_start(W &w, Object &o, Gameplay &) {
    const auto *path = w.curve(o.props.u32("path"));
    o.state = uint8_t(o.flags >> 8);
    o.n[0] = o.n[1] = o.n[2] = 0;
    o.owner = 0;
    if (!path || path->points.size() != 2) { // the original's init refuses the door then
        o.state = 0xff;
        return;
    }
    for (int k = 0; k < 3; k++) {
        o.f[2 + k] = path->points[0][k];
        o.f[5 + k] = path->points[1][k];
    }
    o.f[0] = 0;
    o.f[1] = 1 / std::max(.01f, o.props.f32("moveTime")) * .033333335f;
    door_place(o, o.state == 5 ? 1.f : 0.f, true);
    o.previous = o.matrix;
}
void door_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto &p = o.props;
    if (o.state == 0xff)
        return;
    if (uint32_t lock = p.u32("grindLock"))
        if (const Object *l = w.find(lock); l && l->n[0])
            return;
    const uint32_t activate = o.n[1] ? 0 : o.owner ? o.owner : p.u32("activate"); // owner: an `activate` the hook hall gave it
    const bool near = activate && w.inside(activate, w.hero());
    const float step = o.f[1] * dt * 30;
    switch (o.state) {
    case 0:
        if (near) {
            o.f[0] = 0;
            o.state = 1;
        }
        break;
    case 1:
        if (o.f[0] < 1) {
            door_place(o, o.f[0], true);
            o.f[0] += step;
        }
        if (!o.n[0]) {
            if (activate && !near) {
                o.state = 2;
                o.f[0] = 1 - o.f[0];
            }
        } else if (o.f[0] >= 1)
            o.state = 5;
        break;
    case 2:
        door_place(o, o.f[0], false);
        o.f[0] += step;
        if (o.f[0] > 1)
            o.state = p.toggle("lockWhenClosed") && !o.n[2] ? 3 : 0;
        else if (near) {
            o.state = 1;
            o.f[0] = 1 - o.f[0];
        }
        break;
    case 3:
        if (uint32_t key = p.u32("noLockMoby"); key && w.gone(key, game)) {
            o.n[2] = 1;
            o.state = 1;
            o.f[0] = 0;
        }
        break;
    case 4:
        if (w.gone(p.u32("controller"), game))
            o.state = 1;
        break;
    case 5:
        if (near) {
            o.f[0] = 0;
            o.state = 2;
            o.n[1] = 1;
        }
        break;
    }
}

// ---- HutDoor (init 0x17c770, update 0x17ca10; table 0x220384) --------------------------------------------------------
// A door enemies come out of. It opens while one of its `mobysToCheck` that is alive is inside `trigger`, or the
// hero is in `playerTrigger`, and shuts otherwise: along its own up axis by the variant's row of the table (variant
// 0: 2.5 m up, variant 1: 1.6 m down), its speed a spring to .167 m a frame. States as the original's: 0 shut,
// 3 opening, 2 open, 1 shutting. f[0] how far it is, f[1] its speed, f[2] the spring's rate.
// Not translated: `enemyController` and `radius` (other ways to ask it open), its two sounds.
void hut_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = o.f[1] = o.f[2] = 0;
}
void hut_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto &p = o.props;
    const float way = o.variant == 1 ? -1.f : 1.f, far = o.variant == 0 ? 2.5f : o.variant == 1 ? 1.6f : 2.2f;
    bool wanted = false;
    for (unsigned k = 0; k < 10 && !wanted; k++)
        if (const uint32_t source = p.word(4 * k))
            if (auto e = w.enemy(game, source); e && !e->hidden && e->state != EnemyState::Dead)
                wanted = w.inside(p.u32("trigger"), e->position);
            else if (const Object *thing = w.find(source); thing && thing->name == "ConveyerItem" && thing->n[0] == 2)
                wanted = w.inside(p.u32("trigger"), thing->position());
    if (!wanted && p.u32("playerTrigger"))
        wanted = w.inside(p.u32("playerTrigger"), w.hero());
    if (wanted && o.state < 2)
        o.state = 3;
    else if (!wanted && o.state >= 2)
        o.state = 1;
    if (o.state != 1 && o.state != 3)
        return;
    const float goal = o.state == 3 ? .167f : -.167f;
    for (int step = 0; step < int(std::lround(dt * 60)); step++) { // 0x23bb18: .1, .2
        o.f[2] = (goal - o.f[1]) * .1f - o.f[2] * .2f;
        o.f[1] += o.f[2];
    }
    o.f[0] += o.f[1];
    if (o.state == 3 && o.f[0] >= far) {
        o.f[0] = far;
        o.f[1] = o.f[2] = 0;
        o.state = 2;
    } else if (o.state == 1 && o.f[0] <= 0) {
        o.f[0] = o.f[1] = o.f[2] = 0;
        o.state = 0;
    }
    o.matrix = o.base;
    for (int k = 0; k < 3; k++)
        o.matrix[12 + k] += o.base[4 + k] * way * o.f[0];
}

// ---- ConveyerJunkController (init 0x156ed0, update 0x157010) and ConveyerItem (init 0x156b20) -----------------------
// A belt. While the hero is in `updateArea` it runs `conveyerSpeed` metres a second for `distBetweenPause` metres
// (state 2), then stands `pauseTime` seconds (state 3), and so on; it does not run while he is in `stopArea`. Its
// `movePlayerHack` carries him at the belt's speed, and not at all while it stands. Each time it stops, the
// first of its items that waits (0x157968) is set on the first point of `junkPath`; a riding item goes from point
// to point at the belt's speed while the belt runs (0x1573f8: the next point when within the root of .5 m),
// and from the last one falls .3267 m a frame until it is in `destroyArea`, where it waits again.
// A ConveyerItem gives itself to its `pConveyer` (0x157d30). Item: n[0] 1 waits, 2 rides, 4 falls; n[1] its next
// point. Controller: f[0] the metres run or the seconds stood; `members` its items.
// Not translated: `activate` (state 1: a belt that waits for him; Kalidon has none), the belt's scrolling texture
// (`uvSpeed`), its loop (sound 0xd7d1ff62, and 0x5adb314b at the two ends of the belt of class variant 0x5c2d7f1c),
// the items' turn into their way (2.8 % a frame).
constexpr uint32_t SND_BELT_START = 0xc2ea0e22;
void item_start(W &w, Object &o, Gameplay &) {
    o.n[0] = 1;
    o.n[1] = 0;
    o.visible = false;
    o.solid = false;
    if (Object *belt = w.object(o.props.u32("pConveyer")); belt && belt->members.size() < 12)
        belt->members.push_back(o.source);
}
void belt_start(W &, Object &o, Gameplay &) {
    o.state = 2;
    o.f[0] = 0;
}
void belt_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto &p = o.props;
    Object *mover = w.object(p.u32("movePlayerHack"));
    if (!w.inside(p.u32("updateArea"), w.hero())) {
        if (mover)
            mover->n[1] = 1, mover->f[0] = 0;
        return;
    }
    const float speed = p.f32("conveyerSpeed");
    const auto *path = w.curve(p.u32("junkPath"));
    if (o.state == 2) {
        if (const uint32_t stop = p.u32("stopArea"); stop && w.inside(stop, w.hero()))
            return;
        o.f[0] += speed * dt;
        if (o.f[0] >= p.f32("distBetweenPause")) {
            o.f[0] = 0;
            o.state = 3;
            if (path && path->points.size() >= 2)
                for (uint32_t source : o.members)
                    if (Object *item = w.object(source); item && item->n[0] == 1) { // the first that waits
                        const auto &a = path->points[0], &b = path->points[1];
                        const float yaw = std::atan2(b[0] - a[0], b[2] - a[2]), c = std::cos(yaw), s = std::sin(yaw);
                        const float size = std::sqrt(item->base[0] * item->base[0] + item->base[1] * item->base[1] + item->base[2] * item->base[2]);
                        item->matrix = {c * size, 0, -s * size, 0, 0, size, 0, 0, s * size, 0, c * size, 0, a[0], a[1], a[2], 1};
                        item->previous = item->matrix;
                        item->n[0] = 2;
                        item->n[1] = 0;
                        item->visible = true;
                        item->solid = item->props.toggle("treadOnMe");
                        break;
                    }
        }
        if (mover)
            mover->n[1] = 1, mover->f[0] = speed;
    } else {
        o.f[0] += dt;
        if (o.f[0] >= p.f32("pauseTime")) {
            o.state = 2;
            o.f[0] = 0;
            game.audio_events.push_back({SND_BELT_START});
        }
        if (mover)
            mover->n[1] = 1, mover->f[0] = 0;
    }
    // its workers work while it stands (0x157c60)
    if (o.state == 3)
        for (auto &e : game.enemies)
            if (e.station == o.source)
                e.station_busy = true;
    for (uint32_t source : o.members) {
        Object *item = w.object(source);
        if (!item)
            continue;
        if (item->n[0] == 2 && path && o.state == 2) {
            const auto &goal = path->points[std::min<size_t>(item->n[1], path->points.size() - 1)];
            V to{goal[0] - item->matrix[12], goal[1] - item->matrix[13], goal[2] - item->matrix[14]};
            const float left2 = to[0] * to[0] + to[1] * to[1] + to[2] * to[2];
            if (left2 >= 2.3841858e-07f) {
                const float step = speed * dt / std::sqrt(left2);
                for (int k = 0; k < 3; k++)
                    item->matrix[12 + k] += to[k] * step;
            }
            if (left2 <= .5f) {
                if (size_t(item->n[1]) + 1 >= path->points.size())
                    item->n[0] = 4;
                else
                    item->n[1]++;
            }
        } else if (item->n[0] == 4) {
            item->matrix[13] -= .32666668f * dt * 30;
            if (w.inside(p.u32("destroyArea"), item->position()) || item->matrix[13] < -200) {
                item->n[0] = 1;
                item->visible = false;
                item->solid = false;
            }
        }
    }
}

// ---- Mungo (init 0x19c2a0, update 0x19cb90, event 0x19c9f8; its states 0x19fdd0, its phases 0x1a0de8) --------------
// Kalidon's boss, on the round platform south of the vendor (28 KB with its drones, crates and shockwaves; 38
// states). What the original does, as read:
//  - 1: he waits, 10 m down with his platform, for the hero in `enterTrigger`. 2: for 4 s the platform and he rise
//    2.5 m/s while both bridges (and the two armour pickups on the far one) sink as far; `door` is set back to open.
//    3: HOP; then `backDoor` shuts. 4: ROAR. Then the fight; his health is 375 (0x2216dc) times the row of his
//    difficulty in 0x23f7c8 (1, .9, .8, .7: the fourth entry into the arena and after make him weaker), and the
//    same row sets `attackSpeed` (0x1a26b8). He can be hurt in states 6 to 0x23 only (the event).
//  - Phase 0, above 3/4 of his health: 5 TURN_LEF / TURN_RIG to the hero (he turns inside each clip's window:
//    0x1a1790) until .96 of facing him; 6 CHARGE_P, and when the hero is more than 7.07 m off, 7 CHARGE: over the
//    clip he goes, by a smoothstep of its time, from where he stood to the hero, until within 7.07 m (0x1a2218);
//    9 ATK_SWIN: for its first .5 s the hero is hit when the line from his feet to his left hand (joint 0x1f)
//    passes within 1 m of the hero's middle (0x10e9f0; the hand comes 5.2 m out); then 5 again when the hero is
//    out of his facing or reach, else 8 ANTICIPA (at attackSpeed) and 9 again. When `reposTimer` (0 at each charge)
//    passes 6 s during an ANTICIPA he charges (0x23) to 5 m beside the hero (0x1a2758: across his line to him, a
//    side by chance, the other when that one has no floor), then ANTICIPA and the swing from there.
//  - Phase 1, at 3/4 (0x1a0de8): he goes to the side the hero is not on and throws his `northDrones` /
//    `southDrones` at him (states 0x13 to 0x1a); a drone struck back at him stuns him and he jumps to the other
//    side (0x1b to 0x20). Phase 2, at 1/2: he tears one of the ten `pipes` out (0xc to 0xe) and swings it (0xf to
//    0x11). Phase 3, at 1/4: he throws the pipe (0x12), slams shockwaves (0xb: `MungoShockwave`) and takes to the
//    drones again. At nothing: 0x24 DEATH, the far bridge rising in 2/3 s to 15.99 with the armour; 0x26 two
//    seconds later `backDoor` opens.
// NATIVE, the first version: the arena (the bridges, both doors, the armour), the entry, phase 0's turn, charge,
// swing with the arm's own reach and re-placing, his health with its phases counted, his death and the way out.
// He keeps to phase 0's moves through all four phases; the side he re-places to alternates. NOT BUILT: the
// platform's rise (the level's platform stands risen and he stands on it from the start; the 4 s pass with him
// idle), the cutscene of the entry (the hero is not held), phases 1 to 3 as above (the drones, the pipes, the
// shockwaves, the crates of `MungoCC`), the hand's own sweep against the hero (0x1fef0: only the arm's line is
// tested), the `hd03_strafe` help line, his health bar, his sounds other than his clips' own. His body is the
// original's sphere of 2.75 m (the hero is moved out of it), not his model's box.
// f[0] health, f[1] the state's seconds, f[2] his full health, f[3] f[4] where a charge began (x, z), f[5] f[6]
// where a re-placing goes, f[7] reposTimer; n[0] phase, n[1] entries into the arena, n[2] 1 the swing has hit,
// 2 the side of the next re-placing; n[3] the state he takes after a turn or a re-placing, and his difficulty << 8.
constexpr float MUNGO_HEALTH = 375, MUNGO_FLOOR = 16.2f, MUNGO_REACH2 = 50;
constexpr float MUNGO_SHARE[4] = {.7f, .8f, .9f, 1.f};
enum : int { M_IDLE = 0, M_SWING = 2, M_ANTICIPATE = 3, M_CHARGE = 5, M_CHARGE_PREP = 6, M_DEATH = 7, M_ROAR = 16, M_HOP = 20, M_TURN_LEFT = 21, M_TURN_RIGHT = 22 };
float mungo_clip_length(const W &w, const Object &o) {
    if (o.model < 0 || o.clip < 0)
        return 0;
    const auto &clips = w.assets().models[size_t(o.model)].animations.clips;
    return size_t(o.clip) < clips.size() ? clips[size_t(o.clip)].duration : 0;
}
void mungo_clip(Object &o, int clip, bool loop = false) {
    o.clip = clip;
    o.time = 0;
    o.loop = loop;
}
void mungo_state(Object &o, uint8_t state) {
    o.state = state;
    o.f[1] = 0;
    o.n[2] &= ~1;
    switch (state) {
    case 1: mungo_clip(o, M_IDLE, true); break;
    case 2: mungo_clip(o, M_IDLE, true); break;
    case 3: mungo_clip(o, M_HOP); break;
    case 4: mungo_clip(o, M_ROAR); break;
    case 6: mungo_clip(o, M_CHARGE_PREP); break;
    case 7:
        o.f[7] = 0;
        o.f[3] = o.matrix[12], o.f[4] = o.matrix[14];
        mungo_clip(o, M_CHARGE);
        break;
    case 8:
        if (o.f[7] >= 6)
            o.f[7] = 0;
        mungo_clip(o, M_ANTICIPATE);
        break;
    case 9: mungo_clip(o, M_SWING); break;
    case 0x23:
        o.f[7] = 0;
        o.f[3] = o.matrix[12], o.f[4] = o.matrix[14];
        mungo_clip(o, M_CHARGE);
        break;
    case 0x24: mungo_clip(o, M_DEATH); break;
    }
}
void mungo_face(Object &o, float yaw) {
    const float size = std::sqrt(o.base[0] * o.base[0] + o.base[1] * o.base[1] + o.base[2] * o.base[2]), c = std::cos(yaw), s = std::sin(yaw);
    const V at = o.position();
    o.matrix = {c * size, 0, -s * size, 0, 0, size, 0, 0, s * size, 0, c * size, 0, at[0], at[1], at[2], 1};
}
float mungo_yaw(const Object &o) { return std::atan2(o.matrix[8], o.matrix[10]); }
// The left hand's place in the world, of the clip as it is now (joint 0x1f).
V mungo_hand(const W &w, const Object &o) {
    const auto &model = w.assets().models[size_t(o.model)];
    std::vector<hig::BonePose> pose;
    std::vector<hig::Matrix> world, skin;
    const auto &clips = model.animations.clips;
    hig::sample_animation(model.actor, size_t(o.clip) < clips.size() ? &clips[size_t(o.clip)] : nullptr, o.time, o.loop, pose);
    hig::pose_matrices(model.actor, pose, world, skin);
    if (world.empty())
        return o.position();
    size_t bone = std::min<size_t>(0x1f, world.size() - 1);
    for (size_t k = 0; k < model.actor.bones.size() && k < world.size(); k++)
        if (model.actor.bones[k].id == 0x1f) {
            bone = k;
            break;
        }
    return hig::transform_point(V{world[bone][12], world[bone][13], world[bone][14]}, o.matrix);
}
void mungo_sink(W &w, uint32_t source, float sunk) {
    if (Object *thing = w.object(source)) {
        thing->matrix = thing->base;
        thing->matrix[13] -= sunk;
    }
}
// The arena with its bridges `sunk` metres down: both while the fight is on, only the far one (with the armour on
// it) before it (`far_only`).
void mungo_arena(W &w, Object &o, float sunk, bool far_only) {
    const auto &p = o.props;
    mungo_sink(w, p.u32("startBridge"), far_only ? 0 : sunk);
    for (const char *name : {"endBridge", "armor", "replayArmor"})
        mungo_sink(w, p.u32(name), sunk);
}
void mungo_door(W &w, uint32_t source, bool opening) {
    if (Object *door = w.object(source)) {
        door->state = opening ? 1 : 2;
        door->f[0] = 0;
    }
}
void mungo_start(W &w, Object &o, Gameplay &) {
    o.matrix = o.base;
    o.matrix[13] = MUNGO_FLOOR; // NATIVE: on the platform, which the level holds risen
    o.previous = o.matrix;
    o.n[0] = o.n[1] = o.n[2] = 0;
    o.n[3] = 6 | 3 << 8;
    o.f[0] = o.f[2] = MUNGO_HEALTH;
    o.f[7] = 0;
    o.target = false;
    o.solid = true;
    o.ghost = true;
    mungo_state(o, 1);
    mungo_arena(w, o, 10, true);
}
void mungo_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto &p = o.props;
    const V hero = w.hero(), at = o.position();
    const V to{hero[0] - at[0], 0, hero[2] - at[2]};
    const float far2 = to[0] * to[0] + to[2] * to[2], far = std::sqrt(std::max(far2, 1e-6f));
    const V facing{std::sin(mungo_yaw(o)), 0, std::cos(mungo_yaw(o))};
    const float toward = (to[0] * facing[0] + to[2] * facing[2]) / far;
    const float tempo = MUNGO_SHARE[(o.n[3] >> 8) & 3];
    const bool done = !o.loop && o.time >= mungo_clip_length(w, o) - 1e-3f;
    auto after = [&]() { return uint8_t(o.n[3] & 0xff); };
    auto then = [&](int state) { o.n[3] = (o.n[3] & ~0xff) | state; };
    o.f[1] += dt;
    o.f[7] += dt;
    // his body: a sphere of 2.75 m about him (the collision the init gives him, +0x14 of his model's shape); the
    // hero is kept out of it
    if (o.solid && game.alive() && far < 2.75f && std::fabs(hero[1] - at[1]) < 4)
        game.carry(V{to[0] / far * (2.75f - far), 0, to[2] / far * (2.75f - far)});
    // The fight is off when he dies in it: the way in again, Mungo whole (the original starts the level's objects
    // anew).
    if (o.state >= 2 && o.state < 0x24 && !game.alive()) {
        o.target = false;
        o.f[0] = o.f[2];
        o.n[0] = 0;
        o.matrix[12] = o.base[12];
        o.matrix[14] = o.base[14];
        mungo_face(o, std::atan2(o.base[8], o.base[10]));
        mungo_arena(w, o, 10, true);
        mungo_door(w, p.u32("backDoor"), true);
        o.solid = true;
        mungo_state(o, 1);
        return;
    }
    // his hits (the event): in states 6 to 0x23
    if (const auto *struck = w.hit(o); struck && o.f[0] > 0 && o.state >= 6 && o.state <= 0x23) {
        o.f[0] = std::clamp(o.f[0] - struck->damage, 0.f, o.f[2]);
        const float left = o.f[0] / o.f[2];
        o.n[0] = left <= 0 ? 4 : left <= .25f ? 3 : left <= .5f ? 2 : left <= .75f ? 1 : 0; // 0x1a0de8
    }
    if (o.f[0] <= 0 && o.state >= 5 && o.state < 0x24) {
        o.target = false;
        o.solid = false; // his collision goes with him (cb1)
        mungo_state(o, 0x24);
        return;
    }
    // he turns to a place, at most `rate` radians a second (NATIVE rate; the original's is a spring, 0x23f8dc)
    auto turn = [&](float x, float z, float rate) {
        const float off = std::remainder(std::atan2(x - at[0], z - at[2]) - mungo_yaw(o), 6.2831855f);
        mungo_face(o, mungo_yaw(o) + std::clamp(off, -rate * dt, rate * dt));
    };
    // The charge (0x1a2218): by a smoothstep of the clip's time from where he stood to the place, until its square
    // distance is `near2` or less. True when he is there.
    auto charge = [&](float x, float z, float near2) {
        const float dx = x - at[0], dz = z - at[2];
        if (dx * dx + dz * dz <= near2)
            return true;
        const float length = mungo_clip_length(w, o), t = length > 0 ? std::clamp(o.time / length, 0.f, 1.f) : 1;
        const float s = t <= .0001f ? 0 : t >= .999f ? 1 : t * t * 3 - t * t * t * 2;
        o.matrix[12] = o.f[3] + (x - o.f[3]) * s;
        o.matrix[14] = o.f[4] + (z - o.f[4]) * s;
        o.matrix[13] = MUNGO_FLOOR;
        turn(x, z, 4);
        return false;
    };
    switch (o.state) {
    case 1:
        if (game.alive() && w.inside(p.u32("enterTrigger"), hero)) {
            // his difficulty by the entries before this one (0x2216ec): 3 for the first three, then 2, 2, 1, 1, 0
            const int difficulty = o.n[1] < 3 ? 3 : o.n[1] < 5 ? 2 : o.n[1] < 7 ? 1 : 0;
            o.n[3] = 6 | difficulty << 8;
            o.n[1]++;
            o.f[0] = o.f[2] = MUNGO_HEALTH * MUNGO_SHARE[difficulty];
            mungo_door(w, p.u32("door"), true);
            mungo_state(o, 2);
        }
        break;
    case 2: // 4 s: the bridges go down 2.5 m/s
        mungo_arena(w, o, std::min(10.f, o.f[1] * 2.5f), false);
        if (o.f[1] >= 4)
            mungo_state(o, 3);
        break;
    case 3:
        if (done) {
            mungo_door(w, p.u32("backDoor"), false);
            mungo_state(o, 4);
        }
        break;
    case 4:
        if (done) {
            then(6);
            o.target = true;
            mungo_state(o, 5);
        }
        break;
    case 5: { // the turn: the clip of the side the hero is on, and in its window he turns
        const bool right = to[2] * facing[0] - to[0] * facing[2] < 0;
        if (o.clip != M_TURN_LEFT && o.clip != M_TURN_RIGHT)
            mungo_clip(o, right ? M_TURN_RIGHT : M_TURN_LEFT, true);
        const float length = mungo_clip_length(w, o), t = length > 0 ? std::fmod(o.time, length) : 0;
        const bool window = o.clip == M_TURN_LEFT ? t >= .5f && t <= .8333333f : t >= .23333333f && t <= .56666666f;
        if (window) {
            turn(hero[0], hero[2], 4);
            if (toward >= .96f)
                mungo_state(o, after());
        }
        break;
    }
    case 6:
        if (far2 <= MUNGO_REACH2)
            mungo_state(o, 9);
        else if (done)
            mungo_state(o, 7);
        break;
    case 7:
        if (done)
            mungo_state(o, 9);
        else
            charge(hero[0], hero[2], MUNGO_REACH2);
        break;
    case 8:
        if (o.f[7] >= 6) { // re-placing (0x19fdd0 case 0x23, 0x1a2758): 5 m beside the hero, across his line to him
            for (int tries = 0; tries < 2; tries++) {
                const float side = (o.n[2] & 2) ? 5.f : -5.f;
                o.n[2] ^= 2;
                const float x = hero[0] + to[2] / far * side, z = hero[2] - to[0] / far * side;
                if (const RayHit floor = game.terrain().ray(V{x, MUNGO_FLOOR + 1.5f, z}, V{x, MUNGO_FLOOR - 1.5f, z}); floor.hit) {
                    o.f[5] = x, o.f[6] = z;
                    then(8);
                    mungo_state(o, 0x23);
                    break;
                }
            }
            if (o.state == 0x23)
                break;
        }
        o.time += dt * (tempo - 1); // the clip at attackSpeed
        turn(hero[0], hero[2], 2);
        if (done)
            mungo_state(o, 9);
        break;
    case 9: {
        turn(hero[0], hero[2], 1);
        if (o.time <= .5f && !(o.n[2] & 1) && game.alive()) {
            // the line from his feet to his left hand, within 1 m of the hero's middle
            const V hand = mungo_hand(w, o), middle{hero[0], hero[1] + 1, hero[2]};
            if (getenv("SM_BOSS_DEBUG")) // research: where his hand is through the swing
                std::printf("MungoHand: t=%.2f hand=%.2f,%.2f,%.2f reach=%.2f\n", o.time, hand[0], hand[1], hand[2],
                            std::hypot(hand[0] - at[0], hand[2] - at[2]));
            const V line = sub(hand, at), from = sub(middle, at);
            const float size2 = line[0] * line[0] + line[1] * line[1] + line[2] * line[2];
            const float along = size2 > 1e-6f ? std::clamp((from[0] * line[0] + from[1] * line[1] + from[2] * line[2]) / size2, 0.f, 1.f) : 0;
            const V gap = sub(middle, V{at[0] + line[0] * along, at[1] + line[1] * along, at[2] + line[2] * along});
            if (gap[0] * gap[0] + gap[1] * gap[1] + gap[2] * gap[2] <= 1) {
                o.n[2] |= 1;
                const auto &tuning = w.assets().level_tuning();
                game.damage(w.assets().has_tuning ? unsigned(std::max(1.f, std::round(tuning.attack[0][1]))) : 2, at);
            }
        }
        if (done) {
            then(6);
            mungo_state(o, toward < .96f || far2 >= MUNGO_REACH2 ? 5 : 8);
        }
        break;
    }
    case 0x23:
        if (charge(o.f[5], o.f[6], .1f) || done)
            mungo_state(o, after());
        break;
    case 0x24: { // the far bridge and the armour come up in 2/3 s
        const float t = std::min(1.f, o.f[1] / .6666667f), s = t * t * 3 - t * t * t * 2;
        mungo_arena(w, o, 10 * (1 - s), true);
        if (done) {
            mungo_door(w, p.u32("backDoor"), true);
            o.state = 0x26;
            o.f[1] = 0;
        }
        break;
    }
    }
}

// ---- Lvl3DeadlyFluid (init 0x18add8, update 0x18ae88) -----------------------------------------------------------------
// The factory's fluid: in `activeArea` and below its surface (the instance's height) he dies his liquid death
// (state 0x2e). Not translated: the surface's swell (its y scale, a sine of 75 frames), the bubbles and steam
// (`useEffects`), the kind of death.
void fluid_tick(W &w, Object &o, Gameplay &game, float) {
    o.solid = false;
    if (game.alive() && w.inside(o.props.u32("activeArea"), w.hero()) && w.hero()[1] < o.matrix[13]) {
        w.deaths++;
        w.death_source = o.source;
        game.kill();
    }
}

// ---- PODBomb (init 0x1ab4c8, sent 0x1abcf0, update 0x1ab548) --------------------------------------------------------
// One of the shaft wall's bombs. Sent, it flies in fifteen frames to a metre short of where the hero stood and
// down onto the platform, rolls that last metre in thirty frames with three bounces and three beeps, sits for a
// second and goes off: one point of damage to a hero within two metres. States as the original's: 0 on the wall,
// 1 flying, 2 rolling, 3 armed, 4 gone. f[0] progress, f[1..2] where it lands (x, z), f[3..4] where it stops,
// f[5..7] where it left from; owner: the platform it was sent at.
// Not translated: its beeps and blast sounds, the smoke the blast leaves on the platform (0x193e90).
constexpr float PLATFORM_TOP = 0; // the shaft platform's deck is at its origin's height
void pod_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.solid = false;
    o.owner = 0;
}
void pod_send(Object &o, const Object &platform, V hero) {
    o.matrix = o.base;
    o.visible = o.alive = true;
    o.owner = platform.source;
    float dx = o.base[12] - hero[0], dz = o.base[14] - hero[2];
    const float length = std::max(1e-4f, std::hypot(dx, dz));
    o.f[0] = 0;
    o.f[1] = hero[0] + dx / length;
    o.f[2] = hero[2] + dz / length;
    o.f[3] = hero[0];
    o.f[4] = hero[2];
    o.f[5] = o.base[12];
    o.f[6] = o.base[13];
    o.f[7] = o.base[14];
    o.state = 1;
}
void pod_tick(W &w, Object &o, Gameplay &game, float dt) {
    const Object *platform = w.find(o.owner);
    if (!platform || o.state == 0 || o.state == 4)
        return;
    const float deck = platform->matrix[13] + PLATFORM_TOP;
    auto smooth = [](float a, float b, float t) { return t <= .0001f ? a : t >= .999f ? b : a + (b - a) * (t * t * 3 - t * t * t * 2); };
    switch (o.state) {
    case 1:
        o.f[0] = std::min(1.f, o.f[0] + .06666667f * dt * 30);
        o.matrix[12] = smooth(o.f[5], o.f[1], o.f[0]);
        o.matrix[14] = smooth(o.f[7], o.f[2], o.f[0]);
        o.matrix[13] = smooth(o.f[6], o.f[6] + (deck - o.f[6]) * o.f[0], o.f[0]);
        if (o.matrix[13] <= deck + .25f || o.f[0] >= 1) {
            o.f[0] = 0;
            o.state = 2;
        }
        break;
    case 2:
        o.f[0] += .033333335f * dt * 30;
        if (o.f[0] > 1) {
            o.f[0] = 0;
            o.state = 3;
            break;
        }
        o.matrix[12] = smooth(o.f[1], o.f[3], o.f[0]);
        o.matrix[14] = smooth(o.f[2], o.f[4], o.f[0]);
        o.matrix[13] = deck + std::fabs(std::sin(o.f[0] * 9.424778f) * .5f);
        break;
    case 3:
        o.matrix[13] = deck + .25f;
        o.f[0] += dt;
        if (o.f[0] < 1)
            break;
        o.state = 4;
        o.visible = false;
        w.explosions++;
        w.marks.push_back({W::Mark::Kind::Glow, o.position(), {}, 2, 0xff30a0ff});
        game.audio_events.push_back({0xe5bb22e4u}); // the level's medium blast (the bomb's own sound is not read)
        if (const float dx = o.matrix[12] - w.hero()[0], dz = o.matrix[14] - w.hero()[2]; dx * dx + dz * dz <= 4)
            game.damage(1, o.position());
        break;
    }
}

// ---- Lvl3StaticPlatform (init 0x1970f0; put away 0x197298, brought back 0x197308) -----------------------------------
// A ledge of the shaft. It has no life of its own: the shaft's platform puts the ledges away at the start and
// brings them back for the climb, and raises the one that is its `bridge`.
void ledge_show(Object &o, bool shown) {
    o.visible = shown;
    o.solid = shown && o.collision;
}

// ---- Lvl3Elevator, the shaft's four (0x18c2a8 / 0x18c630; ends 0x18d038 / 0x18d070; away 0x18d0a8, back 0x18d118) ----
// The generic lift is the mechanisms' (mechanisms.cpp). These four are put away until the shaft's platform sends
// them up out of the fluid to their low end (state 1: .2333 m a frame); from then on each carries the hero from the
// end he boards at to the other in `moveTime` seconds and goes to whichever end he is nearer in height when he is
// off it (`matchHero`; the top lift is `oneWay` and stays up). States here: 0 away, 1 rising to its low end, 2 at
// an end, 3 travelling. f[0] progress, f[1] where it is (0 low end, 1 high end), f[2..4] low end, f[5..7] high end.
// NATIVE in the last two rules: the original's lift class decides by `radius` and `heightDiff` (0x18c630, read for
// Ryllus's lifts); here it is "he stands on it" and the nearer end.
bool on_deck(const W &w, const Object &o, float radius, float top) {
    const V hero = w.hero();
    const float dx = hero[0] - o.matrix[12], dz = hero[2] - o.matrix[14], dy = hero[1] - (o.matrix[13] + top);
    return dx * dx + dz * dz < radius * radius && dy > -.35f && dy < .6f;
}
void lift_start(W &w, Object &o, Gameplay &) {
    o.state = 0;
    ledge_show(o, false);
    const auto *path = w.curve(o.props.u32("path"));
    V low = o.position(), high = o.position();
    if (path && path->points.size() == 2) {
        low = path->points[0];
        high = path->points[1];
        if (low[1] > high[1])
            std::swap(low, high);
    } else
        high[1] += 15.6f; // the lift at (44.8, -43.4): its path is not in the level's curves; it serves the next ledges up
    for (int k = 0; k < 3; k++) {
        o.f[2 + k] = low[k];
        o.f[5 + k] = high[k];
        o.matrix[12 + k] = low[k]; // the lift's own init puts it at its path's first point (0x18c2a8)
    }
    o.previous = o.matrix;
    o.f[0] = o.f[1] = 0;
}
void lift_tick(W &w, Object &o, Gameplay &game, float dt) {
    if (o.state < 2)
        return; // the platform moves it
    const float radius = std::max(1.f, o.props.f32("radius"));
    const bool riding = on_deck(w, o, radius, 0);
    const V before = o.position();
    if (o.state == 2) {
        float goal = o.f[1];
        if (!riding)
            o.n[1] = 0; // he has stepped off since its last ride
        if (riding && !o.n[1])
            goal = 1 - o.f[1];
        else if (riding)
            ;
        else if (!o.props.toggle("oneWay") || o.f[1] < .5f)
            goal = std::fabs(w.hero()[1] - o.f[6]) < std::fabs(w.hero()[1] - o.f[3]) ? 1.f : 0.f;
        if (o.props.toggle("oneWay") && o.f[1] > .5f)
            goal = 1;
        if (goal != o.f[1]) {
            o.n[0] = goal > .5f;
            o.f[0] = 0;
            o.state = 3;
        }
        return;
    }
    o.f[0] = std::min(1.f, o.f[0] + dt / std::max(.1f, o.props.f32("moveTime")));
    const float t = o.f[0] * o.f[0] * 3 - o.f[0] * o.f[0] * o.f[0] * 2, at = o.n[0] ? t : 1 - t;
    for (int k = 0; k < 3; k++)
        o.matrix[12 + k] = o.f[2 + k] + (o.f[5 + k] - o.f[2 + k]) * at;
    if (riding)
        game.carry(sub(o.position(), before));
    if (o.f[0] >= 1) {
        o.f[1] = float(o.n[0]);
        o.n[1] = riding;
        o.state = 2;
    }
}

// ---- Lvl3Platform (init 0x190430, update 0x190d38) ------------------------------------------------------------------
// The shaft. Its states are in the ledger ("The shaft: Lvl3Platform"); the numbers here are the original's: the
// stops (table 0x23dc88), the bomb stops (0x23dc98), the speeds, the 1.75 s between bombs, the heights the fluid
// and the first lift are put at, where he is put for the climb (0x23dca0), the fluid's spring.
// n[0] curStop, n[1] curWave, n[2] curBombSet, n[3] curBombId; f[0] a timer, f[1] a frame count, f[2] the fluid's
// speed and f[3] its spring's rate, f[4] the height the fluid chases, f[5] the bridge's progress, f[6] where the
// bridge rises from.
// Not translated: the camera that looks down the shaft (states 2 to 5) and at the console's pipes, the pad being
// off for those moves, the platform's sparks, smoke and shrapnel, its sounds, the save flags (2, 1) and (2, 2)
// that restore the climb after a death (a death here starts the level's shaft from its last continue point), the
// slower fluid after each death (0x221420), the pipes' pouring, the second blasts while it burns at the bottom.
constexpr float SHAFT_STOPS[4] = {-4.1f, -14.f, -38.7f, -62.7f}, SHAFT_BOMBS[2] = {-26.f, -49.f};
constexpr float SHAFT_RADIUS = 8.3f;
uint32_t shaft_link(const Object &o, const char *name, unsigned index) {
    return o.props.u32(name, index);
}
void shaft_move(W &w, Object &o, Gameplay &game, float dy) {
    const bool riding = on_deck(w, o, SHAFT_RADIUS, PLATFORM_TOP);
    o.matrix[13] += dy;
    if (riding)
        game.carry({0, dy, 0});
}
void shaft_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.n = {};
    o.f = {};
}
void shaft_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto &p = o.props;
    const V hero = w.hero();
    const float frames = dt * 30;
    Object *fluid = w.object(p.u32("fluidMoby"));
    Object *bridge = w.object(p.u32("bridge"));
    auto platform = [&](unsigned k) { return w.object(shaft_link(o, "platforms", k)); };
    switch (o.state) {
    case 0:
        for (unsigned k = 0; k < 20; k++)
            if (Object *ledge = platform(k)) {
                ledge_show(*ledge, false);
                if (is(*ledge, "Lvl3Elevator"))
                    ledge->state = 0;
            }
        if (bridge)
            ledge_show(*bridge, false);
        o.state = 1;
        break;
    case 1:
        if (w.inside(p.u32("startTrigger"), hero) && on_deck(w, o, SHAFT_RADIUS, PLATFORM_TOP)) {
            o.n[0] = 0;
            o.state = 2;
        }
        break;
    case 2:
        shaft_move(w, o, game, -.13333334f * frames);
        if (o.n[2] < 2 && o.matrix[13] <= SHAFT_BOMBS[o.n[2]]) {
            o.state = 4;
            o.f[0] = 1.75f;
            o.n[3] = 0;
        } else if (o.matrix[13] <= SHAFT_STOPS[o.n[0]]) {
            o.state = 3;
            o.n[1] = 0;
        }
        break;
    case 3: {
        static const char *const waves[4] = {"stop1Waves", "stop2Waves", "stop3Waves", "stop4Waves"};
        const int count = o.n[0] == 0 ? 1 : 2; // 0x193860
        if (w.gone(shaft_link(o, waves[o.n[0]], unsigned(o.n[1])), game))
            o.n[1]++;
        if (o.n[1] < count)
            break;
        if (++o.n[0] < 4)
            o.state = 2;
        else {
            o.state = 5;
            o.f[0] = o.f[1] = 0;
        }
        break;
    }
    case 4: {
        const char *set = o.n[2] == 0 ? "firstBombs" : "secondBombs";
        shaft_move(w, o, game, -.033333335f * frames);
        o.f[0] += dt;
        if (o.f[0] >= 1.75f && o.n[3] < 4) {
            if (Object *bomb = w.object(shaft_link(o, set, unsigned(o.n[3]))))
                pod_send(*bomb, o, hero);
            o.n[3]++;
            o.f[0] = 0;
        }
        bool busy = o.n[3] < 4;
        for (unsigned k = 0; k < 4; k++)
            if (const Object *bomb = w.find(shaft_link(o, set, k)))
                busy |= bomb->state >= 1 && bomb->state <= 3;
        if (!busy) {
            o.n[2]++;
            o.state = 2;
        }
        break;
    }
    case 5: {
        const bool in_shaft = w.inside(p.u32("mainShaft"), hero);
        shaft_move(w, o, game, (in_shaft ? -.13333334f : -.40000004f) * frames);
        if (in_shaft && o.matrix[13] < -76)
            w.prompt = "Jump off the elevator, Ratchet!"; // text 435, a help line in the original
        if ((in_shaft || hero[1] >= -84) && hero[1] >= -92)
            break;
        if ((o.f[1] += frames) >= 20) {
            o.state = 6;
            o.f[1] = 0;
        }
        break;
    }
    case 6:
        if (o.f[1] == 0) { // the plunge
            shaft_move(w, o, game, -.6666667f * frames);
            if (o.matrix[13] > -109)
                break;
            ledge_show(o, false);
            w.explosions++;
            w.marks.push_back({W::Mark::Kind::Glow, o.position(), {}, 6, 0xff30a0ff});
            game.audio_events.push_back({0x1e7ccc7cu}); // the level's large blast (its own sound 0x221434 is not read)
            o.f[1] = 1;
            o.f[0] = 0;
            break;
        }
        if (hero[1] <= -92) { // he went down with it (0x4a430)
            if (game.alive()) {
                w.deaths++;
                w.death_source = o.source;
                game.kill();
            }
            break;
        }
        if ((o.f[0] += dt) >= 1) // native: a second of its wreck burning, in place of the ten blasts over 7.5 s
            o.state = 7;
        break;
    case 7:
        if (!w.inside(p.u32("consoleArea"), hero))
            break;
        w.prompt = "Activate console"; // text 434
        if (!w.interact_pressed())
            break;
        if (fluid)
            fluid->matrix[13] = -109.5f;
        if (Object *first = platform(0)) {
            ledge_show(*first, true);
            first->matrix[13] = -107;
            first->state = 1;
        }
        o.f[0] = 0;
        o.state = 8;
        break;
    case 8:
        if (fluid)
            fluid->matrix[13] += .033333335f * frames;
        if (Object *first = platform(0); first && fluid && fluid->matrix[13] > -107)
            first->matrix[13] += .04433334f * frames;
        if ((o.f[0] += dt) <= 5)
            break;
        game.relocate({36.922f, -89, -9.418f}); // 0x23dca0
        if (fluid)
            fluid->matrix[13] = -92;
        if (Object *first = platform(0)) {
            first->matrix[13] = -89.2269f;
            first->f[1] = 0;
            first->state = 2;
        }
        for (unsigned k = 4; k < 20; k++)
            if (Object *ledge = platform(k); ledge && is(*ledge, "Lvl3StaticPlatform"))
                ledge_show(*ledge, true);
        o.f[2] = .010000001f; // the init puts the speed at its base (0x22138c), not at the instance's `fluidSpeed`
        o.f[3] = 0;
        o.f[4] = hero[1];
        o.f[5] = -1;
        o.state = 9;
        break;
    case 9: {
        if (bridge && o.f[5] >= 0) { // the bridge rises in a second, and that is the end of it
            o.f[5] = std::min(1.f, o.f[5] + .033333335f * frames);
            const float t = o.f[5] <= .0001f ? 0.f : o.f[5] >= .999f ? 1.f : o.f[5] * o.f[5] * 3 - o.f[5] * o.f[5] * o.f[5] * 2;
            bridge->matrix[13] = o.f[6] + (10.8f - o.f[6]) * t;
            if (o.f[5] >= 1)
                o.state = 10;
        } else if (hero[1] > 10 && game.grounded && fluid && std::hypot(hero[0] - o.base[12], hero[2] - o.base[14]) < 14) {
            o.f[5] = 0;
            o.f[6] = fluid->matrix[13] - 5;
            if (bridge) {
                ledge_show(*bridge, true);
                bridge->matrix[13] = o.f[6];
            }
        }
        if (game.grounded)
            o.f[4] = hero[1];
        if (fluid && fluid->matrix[13] < 10) {
            fluid->matrix[13] = std::min(10.f, fluid->matrix[13] + o.f[2] * (game.interacting() ? .5f : 1.f) * frames);
            const float goal = o.f[4] - fluid->matrix[13] > 3 ? .10000001f : .010000001f; // 0x22138c at a first try
            for (int step = 0; step < int(std::lround(dt * 60)); step++) {
                o.f[3] = std::clamp((goal - o.f[2]) * .1f - o.f[3] * .2f, -.20000002f, .20000002f);
                o.f[2] += o.f[3];
            }
        } else if (fluid)
            fluid->matrix[13] = 10;
        // the three upper lifts come up out of the fluid to their low ends once he stands on the ledge before each
        static const unsigned before[3] = {12, 17, 9}; // 0x23dd38
        for (unsigned k = 1; k < 4; k++) {
            Object *lift = platform(k);
            if (!lift || !fluid)
                continue;
            if (lift->state == 0) {
                const Object *ledge = platform(before[k - 1]);
                if (!ledge || !on_deck(w, *ledge, 2.5f, 0))
                    continue;
                ledge_show(*lift, true);
                lift->matrix[12] = lift->f[2];
                lift->matrix[14] = lift->f[4];
                lift->matrix[13] = std::min(fluid->matrix[13], lift->f[3]);
                lift->state = 1;
            }
            if (lift->state == 1) {
                const float left = lift->f[3] - lift->matrix[13], step = std::min(left, .23333335f * frames);
                lift->matrix[13] += step;
                if (step >= left) {
                    lift->f[1] = 0;
                    lift->state = 2;
                }
            }
        }
        break;
    }
    }
}

// ---- Lvl3SinkingPlatform (init 0x196180, update 0x1962f0) -----------------------------------------------------------
// A plate on the fluid. With the hero on it it trembles for 1.25 s, drops .2667 m a frame to y -2, stays under for
// four seconds and rises back in sixty frames. States as the original's: 0 still, 1 trembling, 2 dropping, 3 under,
// 4 rising. f[0] a timer, f[1] the height it rests at, f[2] where it rises from.
// Not translated: the trembling itself (a new lean of its up axis every .15 s).
void sink_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = 0;
}
void sink_tick(W &w, Object &o, Gameplay &game, float dt) {
    if (!o.visible) // put away by the bolt cranker
        return;
    const bool riding = on_deck(w, o, 2.2f, 0);
    switch (o.state) {
    case 0:
        if (riding && game.grounded) {
            o.state = 1;
            o.f[0] = 0;
        }
        break;
    case 1:
        if ((o.f[0] += dt) >= 1.25f) {
            o.f[1] = o.matrix[13];
            o.state = 2;
        }
        break;
    case 2: {
        const float step = -.26666668f * dt * 30;
        o.matrix[13] += step;
        if (riding)
            game.carry({0, step, 0});
        if (o.matrix[13] <= -2) {
            o.state = 3;
            o.f[0] = 0;
        }
        break;
    }
    case 3:
        if ((o.f[0] += dt) >= 4) {
            o.f[2] = o.matrix[13];
            o.f[0] = 0;
            o.state = 4;
        }
        break;
    case 4: {
        o.f[0] = std::min(1.f, o.f[0] + .016666668f * dt * 30);
        const float t = o.f[0] * o.f[0] * 3 - o.f[0] * o.f[0] * o.f[0] * 2;
        o.matrix[13] = o.f[2] + (o.f[1] - o.f[2]) * t;
        if (o.f[0] >= 1)
            o.state = 0;
        break;
    }
    }
}

// ---- Lvl3BoltCranker (init 0x189a58, update 0x189bf0) -------------------------------------------------------------
// The bolt crank beside the fluid lake raises its ten `platforms` (four Hypershot targets and six sinking plates)
// out of the fluid: while the hero turns `bolt` they stand at the crank's progress, a smoothstep of ten metres (the
// first of them from y -3 to 7, the others keeping their places about it); at .99 they stay. Let go before that
// and they sink back in a second. States as the original's: 0 waiting or being turned, 1 done, 2 sinking back.
// f[0] progress; n[0] it was being turned.
// Not translated: the save flag (0x2d, 1) that leaves them raised in a later visit.
void cranker_put(W &w, Object &o, float first, bool shown) {
    const Object *lead = w.find(o.props.u32("platforms", 0));
    const float delta = lead ? first - lead->matrix[13] : 0;
    for (unsigned k = 0; k < 10; k++)
        if (Object *m = w.object(o.props.u32("platforms", k))) {
            if (is(*m, "Hookshot"))
                m->n[3] = !shown; // a target put away is no target (hookshot_tick)
            else {
                m->visible = shown;
                m->solid = shown && m->collision;
            }
            m->matrix[13] += delta;
            if (m->state == 0 || !shown)
                m->f[1] = m->matrix[13]; // a plate's resting height moves with it
        }
}
void cranker_start(W &w, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = 0;
    o.n[0] = 0;
    for (unsigned k = 0; k < 10; k++)
        if (Object *m = w.object(o.props.u32("platforms", k))) {
            if (is(*m, "Hookshot"))
                m->n[3] = 1;
            else
                m->visible = m->solid = false;
        }
}
void cranker_tick(W &w, Object &o, Gameplay &, float dt) {
    auto smooth = [](float t) { return t <= .0001f ? 0.f : t >= .999f ? 1.f : t * t * 3 - t * t * t * 2; };
    if (o.state == 0) {
        const auto crank = w.cranks.find(o.props.u32("bolt"));
        const bool turning = crank != w.cranks.end() && crank->second.turning;
        const float turned = crank != w.cranks.end() ? crank->second.progress : 0;
        if (turning || (turned >= .99f && !o.n[0] && o.f[0] < .99f)) {
            o.f[0] = turned;
            o.n[0] = 1;
            cranker_put(w, o, smooth(std::min(turned, 1.f)) * 10 - 3, true);
            if (turned >= .99f)
                o.state = 1;
        } else if (o.n[0]) {
            if (turned >= .99f) { // the crank ended the turn itself at its last step
                cranker_put(w, o, 7, true);
                o.state = 1;
            } else {
                o.f[0] = 1 - o.f[0];
                o.state = 2;
            }
            o.n[0] = 0;
        }
    } else if (o.state == 2) {
        cranker_put(w, o, 7 - smooth(o.f[0]) * 10, true);
        o.f[0] += .033333335f * dt * 30;
        if (o.f[0] > 1) {
            cranker_put(w, o, -3, false);
            o.f[0] = 0;
            o.state = 0;
        }
    }
}

// ---- Lvl3Hooks (init 0x18e030, update 0x18e370) ---------------------------------------------------------------------
// The hall whose overhead hooks bring enemies in. The hero in `activate` shuts the front door behind him; four waves
// follow (six drones, two robots, six drones, three robots), one enemy every 2.5 s (0x2212cc), each wave only when
// the one before is dead; then the hall's Grind Lock is switched on, the back door is set to open (it waits for
// that lock) and the front door is given `doorTrigger` to work by. States as the original's: 0 waiting, 1 shutting
// the door, 2 bringing a wave in, 5 the wave fights, 6 done.
// n[0] the wave, n[1] how many of it are in, f[0] the timer.
// NATIVE: the hooks themselves (eight ties riding `positions`, an enemy hanging from each until it lets go) are not
// translated - an enemy is set down in the hall under the hooks' line when its turn comes. Also not: the camera
// the hall takes while a wave comes in, the save flag 0x30 (the hall stays cleared in a later visit).
constexpr int LOCK_OFF = 3; // a GrindrailLock's n[3]: switched off (0x10 in its flags) until its hall is cleared
const char *hooks_wave(int wave, unsigned &count) {
    static const char *const names[4] = {"wave1", "wave2", "wave3", "wave4"};
    static const unsigned counts[4] = {6, 2, 6, 3}; // 0x18ec20, the hall's four lists
    count = counts[wave];
    return names[wave];
}
void hooks_start(W &w, Object &o, Gameplay &game) {
    o.state = 0;
    o.n[0] = o.n[1] = 0;
    o.f[0] = 0;
    for (int wave = 0; wave < 4; wave++) {
        unsigned count;
        const char *name = hooks_wave(wave, count);
        for (unsigned k = 0; k < count; k++)
            if (const uint32_t source = o.props.u32(name, k))
                w.hold_enemy(source, true, true);
    }
    if (Object *lock = w.object(o.props.u32("grindLock")))
        lock->n[LOCK_OFF] = 1;
    (void)game;
}
void hooks_door(W &w, uint32_t source, bool hold, uint32_t activate) {
    if (Object *door = w.object(source)) { // 0x18e370: the door at its path's first point, state 1, progress 0
        door->state = 1;
        door->f[0] = 0;
        door->n[0] = hold;
        door->owner = activate;
    }
}
void hooks_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto &p = o.props;
    switch (o.state) {
    case 0:
        if (w.inside(p.u32("activate"), w.hero()))
            o.state = 1;
        break;
    case 1:
        hooks_door(w, p.u32("frontDoor"), true, 0);
        o.n[0] = o.n[1] = 0;
        o.f[0] = 0;
        o.state = 2;
        break;
    case 2: {
        unsigned count;
        const char *name = hooks_wave(o.n[0], count);
        if ((o.f[0] -= dt) > 0)
            break;
        if (unsigned(o.n[1]) >= count) {
            o.state = 5;
            break;
        }
        const uint32_t source = p.u32(name, unsigned(o.n[1]));
        if (Enemy *e = w.enemy(game, source)) {
            // under the hooks' line through the hall (x -6.7 to 12.5 at z -23.5), spread by its place in the wave
            const float x = -5 + 15 * (float(o.n[1]) + .5f) / float(count);
            // at the hall's floor: the level's drones are placed at their floors' height (the shaft's at -4.1 on a deck at -4.13)
            e->position = {x, 4.03f, -23.5f + (o.n[1] % 2 ? 1.5f : -1.5f)};
            e->previous = e->home = e->position;
            e->velocity = {};
            w.hold_enemy(source, true, false);
        }
        o.n[1]++;
        o.f[0] = 2.5f;
        break;
    }
    case 5: {
        unsigned count;
        const char *name = hooks_wave(o.n[0], count);
        for (unsigned k = 0; k < count; k++)
            if (!w.gone(p.u32(name, k), game))
                return;
        if (++o.n[0] < 4) {
            o.n[1] = 0;
            o.f[0] = 0;
            o.state = 2;
        } else
            o.state = 6;
        break;
    }
    case 6:
        if (Object *lock = w.object(p.u32("grindLock")))
            lock->n[LOCK_OFF] = 0;
        hooks_door(w, p.u32("backDoor"), true, 0);
        hooks_door(w, p.u32("frontDoor"), false, p.u32("doorTrigger"));
        o.state = 7;
        break;
    }
}

// ---- Lvl3Showcase (init 0x195950, update 0x195be8, hit 0x195b58, break 0x195c48) --------------------------------------
// A glass case with a robot sitting in it (the robot names its case in `showcase`). The glass breaks when the hero
// is in `breakTrigger`, when it is hit, or when its `enemyWave` lets go (a wave with no enemies of its own: only
// its `destroyedMoby` condition, a group of the room's first robots); then the case is gone and its robot wakes.
// owner: its robot. Not translated: the shards and the sound of the break, the broken glass left standing (a tie
// the break shows, `brokenGlass`).
void showcase_start(W &w, Object &o, Gameplay &game) {
    o.state = 0;
    o.owner = 0;
    for (auto &e : game.enemies)
        if (w.links_to(e.source_offset, o.source)) {
            o.owner = e.source_offset;
            e.dormant = true;
        }
}
void showcase_tick(W &w, Object &o, Gameplay &game, float) {
    if (o.state)
        return;
    const auto &p = o.props;
    Enemy *robot = w.enemy(game, o.owner);
    bool breaks = w.inside(p.u32("breakTrigger"), w.hero()) || w.hit(o) || (robot && !robot->dormant);
    if (const uint32_t wave = p.u32("enemyWave"); wave && !breaks) {
        const auto held_by = w.instance_props(wave);
        const uint32_t group = held_by.u32("destroyedMoby");
        breaks = group && w.gone(group, game);
    }
    if (!breaks)
        return;
    o.state = 1;
    o.visible = o.solid = false;
    if (robot)
        robot->dormant = false;
    w.marks.push_back({W::Mark::Kind::Glow, w.center(o), {}, 1.5f, 0xc0ffffe0});
}

const ObjectClass classes[] = {
    {"Lvl3MagnaRoom", magna_start, magna_tick, false},
    {"Lvl3Showcase", showcase_start, showcase_tick, true},
    {"Lvl3Hooks", hooks_start, hooks_tick, false},
    {"Lvl3SinkingPlatform", sink_start, sink_tick, true},
    {"Lvl3BoltCranker", cranker_start, cranker_tick, false},
    {"Lvl3Door", door_start, door_tick, true},
    {"HutDoor", hut_start, hut_tick, true},
    {"ConveyerItem", item_start, nullptr, true},
    {"ConveyerJunkController", belt_start, belt_tick, true},
    {"Lvl3DeadlyFluid", nullptr, fluid_tick, true},
    {"PODBomb", pod_start, pod_tick, true},
    {"Lvl3StaticPlatform", nullptr, nullptr, true},
    {"Lvl3Elevator", lift_start, lift_tick, true},
    {"Lvl3Platform", shaft_start, shaft_tick, true},
    {"Mungo", mungo_start, mungo_tick, true},
};
} // namespace
std::span<const ObjectClass> level03_object_classes() { return classes; }
} // namespace sm
