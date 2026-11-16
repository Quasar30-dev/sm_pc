// Ryllus (level 02, "jungleruins"): native translations of the level's own classes.
// Function addresses are LEVEL_02.REL file offsets (tools/sm/sm_decomp.py 02 <address>); nothing of it is executed.
#include "objects.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sm {
namespace {
using V = hig::Vec3;
using W = ObjectWorld;
// The original keeps a float at instance +0x70 that other objects add to (a button adds one to the object its
// `triggered` link names). It lives in f[COUNT] for every class of this level.
constexpr int COUNT = 7;
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V cross(V a, V b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// ---- Level02TriggeredDoor (init 0x1a39b8, update 0x1a3bb8) -------------------------------------------------------
// A stone door that sinks 3.46 m, a twentieth of the way a frame, once the count other objects raised on it (+0x70)
// reaches its `progress` property (how many buttons it needs), and rises again when the count falls below it; a
// closing door re-opens while the hero is within half a metre of its plane. States as the original's: 0 closed,
// 1 opening, 2 closing, 3 open.
// Not translated: the save flag (a door open in a later visit), the bit it sets in the gadgebot path it names.
void door_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = o.base[13]; // initY
    o.f[1] = 0;          // amntOpen, 0..1
}
void door_tick(W &w, Object &o, Gameplay &, float dt) {
    const float required = float(o.props.u8("progress")), step = .050000004f * dt * 30;
    switch (o.state) {
    case 0:
        if (o.f[COUNT] < required - 1e-5f)
            break;
        o.state = 1;
        o.f[1] = 0;
        o.f[COUNT] = required;
        break;
    case 1:
        o.f[1] = std::min(1.f, o.f[1] + step);
        if (o.f[1] >= 1)
            o.state = 3;
        break;
    case 2: {
        const V axis{o.matrix[8], o.matrix[9], o.matrix[10]};
        if (std::abs(dot(sub(w.hero(), o.position()), axis)) < .5f) {
            o.state = 1;
            break;
        }
        o.f[1] = std::max(0.f, o.f[1] - step);
        if (o.f[1] <= 0)
            o.state = 0;
        break;
    }
    case 3:
        if (required > o.f[COUNT])
            o.state = 2;
        break;
    }
    o.matrix[13] = o.f[0] - 3.46f * o.f[1];
}

// ---- Level02Button (init 0x19ef40, update 0x19f220: 0x19f290 / 0x19f468 / 0x19f538; test 0x19f610) --------------
// A floor or wall button. Something standing on it for more than twenty frames presses it home and adds one to
// the count of the object its `triggered` link names; when what pressed it leaves, earlier or later, it comes back
// up over the same time (taking its count back, 0x19f5a8) unless it is `permanent`. States as the original's:
// 0 up, 1 going down, 2 coming up, 3 down.
// The test (0x19f610): a floor button (its third axis within 26 degrees of straight up) takes anything within a
// metre of its height and .6 m of its axis; a wall button anything within a metre sideways and 1.05 m of its axis.
// Here the hero (Ratchet or Clank) and the gadgebots press it; the original also lets creatures.
bool on_button(const Object &o, V who) {
    const V axis{o.matrix[8], o.matrix[9], o.matrix[10]};
    const float length = std::sqrt(dot(axis, axis));
    if (length < 1e-6f)
        return false;
    const bool floor = axis[1] / length > .9f;
    V d = sub(who, o.position());
    if (!floor)
        d[1] += .4f;
    const float reach = floor ? std::abs(d[1]) : std::hypot(d[0], d[2]);
    if (reach > 1)
        return false;
    const V unit{axis[0] / length, axis[1] / length, axis[2] / length};
    const V off = cross(d, unit);
    return dot(off, off) <= (floor ? .36f : 1.1025002f);
}
void button_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = 0;          // frames in the current state (+0x70)
    o.f[1] = 0;          // how far down, 0..1 (`step`)
    o.f[2] = o.base[13]; // origY
}
void button_tick(W &w, Object &o, Gameplay &game, float dt) {
    const float frames = dt * 30;
    bool pressed = game.alive() && on_button(o, w.hero());
    static const uint32_t gadgebot = hig::class_hash("ClankBot");
    for (const auto &other : w.objects) // a gadgebot out of its house stands on it (not one held or in the air)
        if (!pressed && other.class_hash == gadgebot && other.alive && other.visible && other.state != 0 &&
            other.state != 0x14 && other.state != 0x15)
            pressed = on_button(o, other.position());
    bool down = false;
    switch (o.state) {
    case 0:
        if (pressed) {
            o.state = 1;
            o.f[0] = 0;
        }
        break;
    case 1:
        down = true;
        if ((o.f[0] += frames) > 20.000101f) {
            o.f[0] = 0;
            o.state = 3;
            if (auto target = w.object(o.props.u32("triggered")))
                target->f[COUNT] += 1;
        } else if (!o.props.toggle("permanent") && !pressed) {
            o.state = 2;
            o.f[0] = 20.000101f - o.f[0];
        }
        break;
    case 2:
        if ((o.f[0] += frames) > 20.000101f) {
            o.state = 0;
            o.f[0] = 0;
        }
        break;
    case 3:
        down = true;
        // 0x19f5a8: one that is not permanent comes up when what pressed it is gone, and takes its count back
        if (!o.props.toggle("permanent") && !pressed) {
            o.state = 2;
            o.f[0] = 0;
            down = false;
            if (auto target = w.object(o.props.u32("triggered")))
                target->f[COUNT] -= 1;
        }
        break;
    }
    o.f[1] = std::clamp(o.f[1] + (down ? .049999747f : -.049999747f) * frames, 0.f, 1.f);
    // it sinks a little along its own axis (the original moves its collision the same way; depth is native)
    const float depth = .12f * o.f[1];
    for (int k = 0; k < 3; k++)
        o.matrix[12 + k] = o.base[12 + k] - o.base[8 + k] * depth;
}

// ---- Level02Column (init 0x1a0928, update 0x1a0b38; its weight 0x1a0f80, the test 0x1a1160, its move 0x1a0c18) ----
// A column that stands .725 m out of the floor and sinks under weight. Clank and every gadgebot standing on its top
// (within 1.1 m of its axis) weigh one; it settles .725 m x weight / reqWeight down, on a spring (0x23a8b8: .2, .2,
// at most .0241667 a pass). With more than `reqWeight` on it, or just that much without Clank among it, it goes all
// the way down (state 1) and at the bottom adds one to the count of its `triggered` object (state 2). What stands
// on it goes down with it. f[0] its first height, f[1] its speed.
void column_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = o.base[13];
    o.f[1] = 0;
}
void column_tick(W &w, Object &o, Gameplay &game, float dt) {
    if (o.state == 2)
        return;
    static const uint32_t gadgebot = hig::class_hash("ClankBot");
    const float required = std::max(1e-3f, o.props.f32("reqWeight"));
    const auto resting = [&](V who) {
        const float up = who[1] - o.matrix[13] - .725f;
        const float dx = who[0] - o.matrix[12], dz = who[2] - o.matrix[14];
        return up <= .15f && up >= -.05f && dx * dx + dz * dz < 1.21f;
    };
    float weight = 0;
    const bool hero = game.alive() && resting(game.position);
    if (hero)
        weight += 1;
    std::vector<Object *> riders;
    for (auto &other : w.objects)
        if (other.class_hash == gadgebot && other.alive && other.visible && other.state != 0 && other.state != 0x14 &&
            other.state != 0x15 && resting(other.position())) {
            weight += 1;
            riders.push_back(&other);
        }
    if (o.state == 0 && (weight > required || (std::abs(weight - required) <= 1e-5f && !hero)))
        o.state = 1;
    const float carried = o.state == 1 ? required : weight, before = o.matrix[13];
    for (int pass = std::max(1, int(std::lround(dt * 60))); pass > 0; pass--) {
        o.f[1] = std::clamp((o.f[0] - .725f / required * carried - o.matrix[13]) * .2f - o.f[1] * .2f, -.024166668f, .024166668f);
        o.matrix[13] += o.f[1];
    }
    const float moved = o.matrix[13] - before;
    for (auto rider : riders)
        rider->matrix[13] += moved;
    if (hero)
        game.carry({0, moved, 0});
    if (o.state == 1 && std::abs(o.matrix[13] - (o.f[0] - .725f)) <= .01f) {
        o.state = 2;
        if (auto target = w.object(o.props.u32("triggered")))
            target->f[COUNT] += 1;
    }
}

// ---- Level02ExpandableBridge (init 0x1a1338, update 0x1a1508; its count 0x1a2028) --------------------------------
// A bridge that waits inside its ledge. When its count reaches `reqProgress` (its buttons) it slides out along its
// own facing, gathering speed to a thirtieth of a metre a frame, `maxBridgeLength` far (state 1), then rises .246 m
// at half that speed into place (state 3) and is out (state 4). f[0] how far out, f[1] its speed, f[2] the speed's
// own change, f[3] how far up.
// Not translated: sliding back in when its count falls (its buttons here are permanent), the bit it sets in the
// gadgebot path it names, that it stays out on a later visit.
void bridge_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = o.f[1] = o.f[2] = o.f[3] = 0;
}
void bridge_tick(W &, Object &o, Gameplay &, float dt) {
    const int passes = std::max(1, int(std::lround(dt * 60)));
    const auto ease = [&](float wanted) {
        for (int pass = passes; pass > 0; pass--) {
            o.f[2] = (wanted - o.f[1]) * .1f - o.f[2] * .2f;
            o.f[1] += o.f[2];
        }
        return o.f[1] * dt * 30;
    };
    if (o.state == 0) {
        if (o.f[COUNT] >= o.props.f32("reqProgress") - 1e-5f)
            o.state = 1;
        return;
    }
    if (o.state == 1) {
        float step = ease(.033333335f);
        const float length = o.props.f32("maxBridgeLength");
        if (o.f[0] + step >= length) {
            step = length - o.f[0];
            o.state = 3;
            o.f[1] = o.f[2] = 0;
        }
        o.f[0] += step;
        for (int k = 0; k < 3; k++)
            o.matrix[12 + k] += o.matrix[8 + k] * step;
    } else if (o.state == 3) {
        float step = ease(.016666668f);
        if (o.f[3] + step >= .246f) {
            step = .246f - o.f[3];
            o.state = 4;
        }
        o.f[3] += step;
        for (int k = 0; k < 3; k++)
            o.matrix[12 + k] += o.matrix[4 + k] * step;
    }
}

// ---- Level02Statue (init 0x1a2898, update 0x1a2940) ---------------------------------------------------------------
// The statue that closes the way out of the port room. When its count reaches `progress` (both ports have their
// gadgebots) its two parts slide apart along their own first axes, .011 m a frame (0x21d5ec), 2.168 m each
// (0x21d5f0): state 1 opening, 3 open. f[1]: how far (amntOpen).
// Not translated: closing again when the count falls, the parts' sound, Level02StatueCutscene's camera.
void statue_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[1] = 0;
}
void statue_tick(W &w, Object &o, Gameplay &game, float dt) {
    if (o.state == 0) {
        if (o.f[COUNT] < o.props.f32("progress") - 1e-5f)
            return;
        o.state = 1;
        o.f[1] = 0;
        game.audio_events.push_back({0x995215fa}); // level-STATUE_GATE
    }
    if (o.state != 1)
        return;
    float step = .011f * dt * 30;
    if (o.f[1] + step >= 2.168f) {
        step = 2.168f - o.f[1];
        o.state = 3;
    }
    o.f[1] += step;
    if (Object *left = w.object(o.props.u32("leftPart")))
        for (int k = 0; k < 3; k++)
            left->matrix[12 + k] -= left->matrix[k] * step;
    if (Object *right = w.object(o.props.u32("rightPart")))
        for (int k = 0; k < 3; k++)
            right->matrix[12 + k] += right->matrix[k] * step;
}

// ---- Level02WallPiece (init 0x1a4ff8, event 0x1a5280, its fall 0x1a5448) -----------------------------------------
// A piece of a cracked wall: four pieces to a wall, each naming the other three (`neighbors`). An ExplodeBerry that
// bursts on one brings down it and its neighbours: they are no longer solid and fly from the burst.
// States here: 0 standing, 1 falling. Native: the fall (the original's debris bounces three times, 0x1e48b8 with
// the record at 0x23b658), and that a broken wall stays broken on a later visit (`saveSignature`).
void piece_fall(Object &o, V from) {
    if (o.state != 0)
        return;
    o.state = 1;
    o.solid = false;
    V away = sub(o.position(), from);
    const float length = std::sqrt(dot(away, away));
    away = length > 1e-4f ? V{away[0] / length, away[1] / length, away[2] / length} : V{0, 0, 1};
    o.f[0] = away[0] * 4;
    o.f[1] = away[1] * 4 + 3;
    o.f[2] = away[2] * 4;
    o.f[3] = 0; // seconds fallen
}
void piece_start(W &, Object &o, Gameplay &) {
    // a piece with no collision mesh of its own stands as its bounds (the original registers every piece's box)
    o.solid = o.collision || o.model >= 0;
}
void piece_tick(W &w, Object &o, Gameplay &game, float dt) {
    if (o.state == 0) {
        static const uint32_t berry = hig::class_hash("ExplodeBerry");
        for (const auto &b : w.blasts) {
            if (b.by != berry)
                continue;
            const V d = sub(w.center(o), b.at);
            if (dot(d, d) > (b.radius + 1) * (b.radius + 1))
                continue;
            game.audio_events.push_back({0x9a914c5a});
            piece_fall(o, b.at);
            for (unsigned k = 0; k < 3; k++)
                if (Object *other = w.object(o.props.u32("neighbors", k)))
                    piece_fall(*other, b.at);
            break;
        }
        return;
    }
    o.f[1] -= 22 * dt;
    for (int k = 0; k < 3; k++)
        o.matrix[12 + k] += o.f[k] * dt;
    if ((o.f[3] += dt) > 1.6f)
        w.destroy(o);
}

// ---- Level02WallCutscene (init 0x1a45c0, update 0x1a46c0) and its six Level02WallChunks -------------------------
// The wall of the east area. The original watches, with the hero in its `area`, for an ExplodeBerry flying at the
// wall (its way over the ground within about 16 degrees of -z), follows it with the camera, and when the berry is
// gone throws the six `shrapnel` chunks (0x1a43a0) and gives the camera back thirty frames later.
// Here: a berry's burst that reaches one chunk brings all six down. Native: no camera scene; the chunks fall as
// the wall pieces do.
void wallscene_tick(W &w, Object &o, Gameplay &, float) {
    if (o.state != 0)
        return;
    const Object *fallen = nullptr;
    for (unsigned k = 0; k < 6 && !fallen; k++)
        if (const Object *chunk = w.find(o.props.u32("shrapnel", k)); chunk && chunk->state != 0)
            fallen = chunk;
    if (!fallen)
        return;
    o.state = 2;
    for (unsigned k = 0; k < 6; k++)
        if (Object *chunk = w.object(o.props.u32("shrapnel", k)))
            piece_fall(*chunk, o.position());
}

const ObjectClass classes[] = {
    {"Level02WallPiece", piece_start, piece_tick, true},
    {"Level02WallChunk", piece_start, piece_tick, true},
    {"Level02WallCutscene", nullptr, wallscene_tick, false},
    {"Level02TriggeredDoor", door_start, door_tick, true},
    {"Level02Button", button_start, button_tick, true},
    {"Level02Column", column_start, column_tick, true},
    {"Level02ExpandableBridge", bridge_start, bridge_tick, true},
    {"Level02Statue", statue_start, statue_tick, false},
};
} // namespace
std::span<const ObjectClass> level02_object_classes() { return classes; }
} // namespace sm
