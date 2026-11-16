// The Grind Lock (levels 03, 06, 07, 08, 09 and 10): the lock at a door, and the rails inside it that a shrunken
// Ratchet rides. Function addresses are LEVEL_03.REL file offsets (tools/sm/sm_decomp.py 03 <address>); nothing of it
// is executed. The ride itself is the hero's (Gameplay::tick_grind); this file is what the rails carry.
#include "objects.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sm {
namespace {
using V = hig::Vec3;
using W = ObjectWorld;
V add(V a, V b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V mul(V a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
bool is(const Object &o, const char *name) { return o.class_hash == hig::class_hash(name); }

constexpr uint32_t SND_SHRINK = 0x647b49d7, SND_RESIZE = 0xb70abc1b; // ratchet-GRINDRAIL_SHRINK, -GRINDRAIL_RESIZE
constexpr uint32_t SND_SWITCH = 0xa3c1cbdb;                          // ratchet-GRINDRAIL_HTSWITCH (0x17a350)
constexpr uint32_t SND_GATE = 0x928266f2;                            // the gate opening (0x178fe0)
constexpr uint32_t SND_BOMB = 0xe5bb22e4;                            // the bomb going off (0x177e20)

// The gap between a point and the segment a..b, squared.
float gap2(V point, V a, V b) {
    const V d = sub(b, a);
    const float length2 = dot(d, d);
    const float t = length2 > 1e-10f ? std::clamp(dot(sub(point, a), d) / length2, 0.f, 1.f) : 0.f;
    const V g = sub(point, add(a, mul(d, t)));
    return dot(g, g);
}
// A model's middle in the world and the radius of the ball about its box.
float ball(const W &w, const Object &o, V &middle) {
    middle = w.center(o);
    if (o.model < 0)
        return .5f;
    const auto &m = w.assets().models[o.model];
    const V half = mul(sub(m.maximum, m.minimum), .5f);
    const float scale = std::sqrt(o.matrix[0] * o.matrix[0] + o.matrix[1] * o.matrix[1] + o.matrix[2] * o.matrix[2]);
    return std::max({half[0], half[1], half[2]}) * scale;
}

// ---- GrindrailSwitch (init 0x17b1a0, hit 0x17afc0) and GrindrailGate (init 0x179240, 0x179668, update 0x1794d8) ----
// A switch the wrench hits turns on, or off again (state 1 / 0); with `toggleOnOff` clear it can be hit once. It sets
// or clears its bit `gateMember` in its gate. A gate is open while the bits its switches have set are all of its
// switches' bits; it opens and shuts over thirty frames.
// Gate: n[0] the bits its switches own, n[1] the bits set, f[0] how far open. Switch: n[0] hit once and done,
// n[1] the swing that last hit it.
// Not translated: the switch's and the gate's own animations (the gate is lowered out of the way here), the gate's
// swing when he is thrown back off it (0x1798f8, 0x179930).
void gate_start(W &, Object &o, Gameplay &) {
    o.state = 0; // n[0] is left alone: a switch that started first has put its bit there
    o.n[1] = 0;
    o.f[0] = 0;
}
void gate_tick(W &w, Object &o, Gameplay &, float dt) {
    const float before = o.f[0];
    o.f[0] = std::clamp(o.f[0] + (o.state == 1 ? 1.f : -1.f) * dt, 0.f, 1.f); // 0x179668: 1/30 a frame either way
    if (o.f[0] != before) {
        V middle;
        const float drop = ball(w, o, middle) * 2 * o.f[0];
        o.matrix = o.base;
        o.matrix[12] -= o.base[4] * drop;
        o.matrix[13] -= o.base[5] * drop;
        o.matrix[14] -= o.base[6] * drop;
    }
    o.solid = false;
}
void switch_start(W &w, Object &o, Gameplay &) {
    o.state = 0;
    o.n[0] = 0;
    o.n[1] = -1;
    if (Object *gate = w.object(o.props.u32("gate"))) // 0x179618
        gate->n[0] |= 1 << (o.props.u32("gateMember") & 31);
}
void switch_tick(W &w, Object &o, Gameplay &, float) {
    if (o.state == 1)
        w.marks.push_back({W::Mark::Kind::Glow, w.center(o), {}, .6f, 0xff40ff40});
}
void switch_hit(W &w, Object &o, Gameplay &game) {
    if (o.n[0])
        return;
    o.state = o.state == 1 ? 0 : 1;
    if (!o.props.toggle("toggleOnOff"))
        o.n[0] = 1;
    game.audio_events.push_back({SND_SWITCH});
    Object *gate = w.object(o.props.u32("gate"));
    if (!gate)
        return;
    const int bit = 1 << (o.props.u32("gateMember") & 31);
    gate->n[1] = o.state == 1 ? gate->n[1] | bit : gate->n[1] & ~bit;
    const int open = gate->n[1] == gate->n[0];
    if (open != gate->state) {
        gate->state = uint8_t(open);
        if (open)
            game.audio_events.push_back({SND_GATE});
    }
}

// ---- GrindrailBomb (init 0x177f00, its ball 0x178048, going off 0x177d00) -------------------------------------------
// It sits on a rail; riding into it costs `bombDamageFrac` of his health and throws him back, and the bomb is gone
// for the try. The ball here is the one about the model's box (the original's is the model's own collision ball).
void bomb_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.solid = false;
}

// ---- GrindrailLock (init 0x179b80, update 0x179e28) -----------------------------------------------------------------
// The lock beside a door. In `promptArea` with the Shrink Ray's rails gear (item 20 of the weapon table; the line
// shown without it names the Shrink Ray), Triangle shrinks him into the lock: he is put at `matrixMoby` on the rails
// of the GrindrailController there and rides them. In `endArea` the lock is open: thirty frames on he is back at the
// door, and the door (which does nothing while its lock's +0x50 is set) works from then on.
// state: 0 waiting, 1 shrinking, 2 on the rails, 3 done and leaving. n[0]: locked (+0x50). n[1]: tries. n[2]: opened
// locks counter for the status line. f[0..3]: where he stood and faced. f[4]: a timer. f[5], f[6]: frames until a
// bomb, a gate can hurt again (the hero's +0xcd8, +0xcdc). members: the try's bombs, switches and gates.
// Not translated: the shrinking itself (his state 0x4b: here a pause of twenty frames with its sound), the fade
// at the end (0xa5d80), the lock's save flag (0x4e, lockID), `destroyMobys` (objects put away for the ride), the
// rails' camera (camDefault*: the camera stays behind him here), the sparks under his boots (0x54f58).
constexpr int LOCKED = 0;
void lock_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.n[LOCKED] = 1;
    o.f[4] = 0;
}
// The controller whose rails pass nearest a point, and its rails: the main path, then the secondary ones.
const Object *lock_rails(W &w, V at, std::vector<std::vector<V>> &rails) {
    const Object *best = nullptr;
    float nearest = 1e30f;
    for (const auto &c : w.objects) {
        if (!is(c, "GrindrailController"))
            continue;
        const auto *path = w.curve(c.props.u32("path"));
        if (!path || path->points.size() < 2)
            continue;
        for (size_t i = 0; i + 1 < path->points.size(); i++)
            if (const float g = gap2(at, path->points[i], path->points[i + 1]); g < nearest) {
                nearest = g;
                best = &c;
            }
    }
    if (!best)
        return nullptr;
    rails.clear();
    rails.push_back(w.curve(best->props.u32("path"))->points);
    for (unsigned k = 0; k < 19; k++) // 0x178bb0: nineteen secondary links after the main one
        if (const auto *side = w.curve(best->props.word(0x18 + 4 * k)); side && side->points.size() >= 2)
            rails.push_back(side->points);
    return best;
}
void lock_leave(W &w, Object &o, Gameplay &game, bool opened) {
    game.end_grind();
    if (game.alive()) {
        game.relocate({o.f[0], o.f[1], o.f[2]});
        game.heading = o.f[3];
        game.audio_events.push_back({SND_RESIZE});
    }
    if (opened) {
        o.n[LOCKED] = 0;
        o.n[2]++;
    }
    o.members.clear();
    o.state = 0;
    w.aim_camera = false;
}
void lock_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto &p = o.props;
    const V hero = w.hero();
    switch (o.state) {
    case 0: {
        if (o.n[3]) // switched off (its flags' 0x10) until what owns it lets it work: Kalidon's hook hall
            break;
        if (!o.n[LOCKED] || !game.alive() || !game.grounded || game.interacting() || !w.inside(p.u32("promptArea"), hero))
            break;
        if (!game.arsenal.owned[hig::ItemGrindBoots]) {
            if (o.f[4] <= 0) // text 659, once a visit
                w.notice_request = "You'll need the Shrink Ray to get through this door. Come back when you've acquired one.";
            o.f[4] = 1;
            break;
        }
        w.prompt = "Enter the Grind Lock"; // text 657
        if (!w.interact_pressed())
            break;
        o.f[0] = game.position[0];
        o.f[1] = game.position[1];
        o.f[2] = game.position[2];
        o.f[3] = game.heading;
        o.f[4] = 20;
        o.state = 1;
        o.n[1]++;
        game.audio_events.push_back({SND_SHRINK});
        break;
    }
    case 1: {
        o.f[4] -= dt * 30;
        if (o.f[4] > 0)
            break;
        const Object *place = w.find(p.u32("matrixMoby"));
        std::vector<std::vector<V>> rails;
        const Object *controller = place ? lock_rails(w, place->position(), rails) : nullptr;
        if (!controller) {
            o.state = 0;
            break;
        }
        // 0x5a448: what of the lock lies in the box about the controller's rails (five metres wider) is set for the try
        V low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
        for (const auto &rail : rails)
            for (const auto &point : rail)
                for (int k = 0; k < 3; k++) {
                    low[k] = std::min(low[k], point[k] - 5);
                    high[k] = std::max(high[k], point[k] + 5);
                }
        o.members.clear();
        for (auto &m : w.objects) {
            const bool bomb = is(m, "GrindrailBomb"), gate = is(m, "GrindrailGate"), lever = is(m, "GrindrailSwitch");
            if (!bomb && !gate && !lever)
                continue;
            const V at = {m.base[12], m.base[13], m.base[14]};
            if (at[0] < low[0] || at[1] < low[1] || at[2] < low[2] || at[0] > high[0] || at[1] > high[1] || at[2] > high[2])
                continue;
            o.members.push_back(m.source);
            m.matrix = m.previous = m.base;
            m.alive = m.visible = true;
            m.state = 0;
            if (gate) {
                m.n[1] = 0;
                m.f[0] = 0;
            } else if (lever) {
                m.n[0] = 0;
                m.n[1] = -1;
            }
        }
        game.relocate(place->position());
        game.begin_grind(std::move(rails), controller->props.f32("grindrailSp")); // 0x178c20: .5 * (1/30) * 60 of it a frame
        o.f[5] = o.f[6] = 0;
        o.state = game.grinding() ? 2 : 0;
        break;
    }
    case 2: {
        if (!game.alive() || !game.grinding()) { // died on the rails: the level puts him back, the lock waits again
            game.end_grind();
            o.members.clear();
            o.state = 0;
            w.aim_camera = false;
            break;
        }
        const auto &g = game.grind();
        w.aim_camera = true;
        w.aim_yaw = game.heading;
        if (g.fell) { // 0x57e50 case 5: his state 0x2c
            game.kill();
            break;
        }
        if (w.inside(p.u32("endArea"), hero)) {
            o.state = 3;
            o.f[4] = 30;
            break;
        }
        o.f[5] = std::max(0.f, o.f[5] - 1);
        o.f[6] = std::max(0.f, o.f[6] - 1);
        const V from = sub(game.position, g.moved);
        const float moved = std::sqrt(dot(g.moved, g.moved));
        float bomb_part = .2f, gate_part = .5f;
        for (const auto &c : w.objects)
            if (is(c, "GrindrailController")) { // every lock of the game carries the same two
                bomb_part = c.props.f32("bombDamageFrac");
                gate_part = c.props.f32("gateDamageFrac");
                break;
            }
        const bool can_hit = g.safe <= 0 && g.phase != 5 && g.phase != 7 && g.phase != 8;
        const bool swinging = game.grind_swing_hits();
        const V forward{std::sin(game.heading), 0, std::cos(game.heading)};
        for (auto source : o.members) {
            Object *m = w.object(source);
            if (!m || !m->alive)
                continue;
            V middle;
            const float radius = ball(w, *m, middle);
            if (is(*m, "GrindrailSwitch")) {
                // 0x56a80: frames 5.33 to 13.33 of the swing hit what is within 2 m of half a metre ahead of him, or
                // within 1 m of 1.75 m ahead
                if (!swinging || m->n[1] == int(g.swings))
                    continue;
                const V a = add(game.position, mul(forward, .5f)), b = add(game.position, mul(forward, 1.75f));
                const V da = sub(middle, a), db = sub(middle, b);
                if (dot(da, da) < (2 + radius) * (2 + radius) || dot(db, db) < (1 + radius) * (1 + radius)) {
                    m->n[1] = int(g.swings);
                    switch_hit(w, *m, game);
                }
            } else if (!can_hit) {
                continue;
            } else if (is(*m, "GrindrailBomb")) {
                if (o.f[5] > 0 || gap2(middle, from, game.position) >= (.4f + radius) * (.4f + radius))
                    continue;
                o.f[5] = 60;
                m->alive = m->visible = false;
                w.explosions++;
                game.audio_events.push_back({SND_BOMB});
                w.marks.push_back({W::Mark::Kind::Glow, middle, {}, 1.5f, 0xff30a0ff});
                game.grind_knock(false, moved, unsigned(std::ceil(bomb_part * float(game.max_health)))); // 0x57368
            } else if (is(*m, "GrindrailGate")) {
                if (o.f[6] > 0 || m->state == 1 || m->f[0] > 0)
                    continue;
                // the shut gate fills its frame: a ball of .4 m meeting the ball about it, within a metre of its plane
                const V to = sub(add(game.position, V{0, .75f, 0}), middle);
                const V axis{m->base[8], m->base[9], m->base[10]};
                const float off = dot(to, axis) / std::max(1e-6f, std::sqrt(dot(axis, axis)));
                if (std::fabs(off) > 1 || dot(to, to) >= (.4f + radius) * (.4f + radius))
                    continue;
                o.f[6] = 60;
                game.grind_knock(true, moved, unsigned(std::ceil(gate_part * float(game.max_health))));
            }
            if (!game.grinding())
                break;
        }
        break;
    }
    case 3:
        w.aim_camera = true;
        w.aim_yaw = game.heading;
        o.f[4] -= dt * 30;
        if (o.f[4] <= 0 || !game.alive())
            lock_leave(w, o, game, game.alive());
        break;
    }
    if (o.state == 0 && !w.inside(p.u32("promptArea"), hero))
        o.f[4] = 0;
}

const ObjectClass classes[] = {
    {"GrindrailLock", lock_start, lock_tick, true},
    {"GrindrailController", nullptr, nullptr, false},
    {"GrindrailBomb", bomb_start, nullptr, true},
    {"GrindrailSwitch", switch_start, switch_tick, true},
    {"GrindrailGate", gate_start, gate_tick, true},
    {"matrixMoby", nullptr, nullptr, false},
};
} // namespace
std::span<const ObjectClass> grind_object_classes() { return classes; }
} // namespace sm
