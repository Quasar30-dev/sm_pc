// Class behaviours shared by the campaign levels: native translations of the original classes' callbacks.
// Function addresses are LEVEL_02.REL file offsets (tools/sm/sm_decomp.py 02 <address>); nothing of it is executed.
#include "objects.h"
#include <algorithm>
#include <cmath>

namespace sm {
namespace {
using V = hig::Vec3;
using W = ObjectWorld;
float distance_xz(V a, V b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }

// ---- EnemyWave (init 0x183580, update 0x1835d8, start 0x183990, spawn 0x183ab0 / 0x183c60) ----------------------
// Enemies name their wave in a MobyLink property and stay out of the level until the wave releases them. A wave
// waits for: its `destroyedMoby` to be destroyed (or to reach `mobyToCheckState`), its `previousWave` (released, or
// destroyed when `prevWaveDestroyed`), the hero inside `trigger`, and room in `enemyControllerToCheck`. Then it
// counts `timer` down and releases its enemies at once, or one at a time every `delayRandMax` * random seconds.
// States as the original's: 0 waiting, 1 timer, 2 releasing one by one, 3 all released.
void wave_start(W &w, Object &o, Gameplay &game) {
    for (const auto &e : game.enemies)
        if (w.links_to(e.source_offset, o.source)) {
            o.members.push_back(e.source_offset);
            w.hold_enemy(e.source_offset, true, true);
        }
    o.state = 0;
    o.f[0] = o.props.f32("timer");
    o.n[0] = 0; // released so far
    o.n[1] = o.props.u32("previousWave") == 0; // the previous wave's condition is met
}
void wave_release(W &w, Object &o, size_t index) {
    w.hold_enemy(o.members[index], true, false);
    o.n[0]++;
}
void wave_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto &p = o.props;
    switch (o.state) {
    case 0: {
        if (o.members.empty()) {
            w.destroy(o);
            return;
        }
        if (uint32_t blocker = p.u32("destroyedMoby")) {
            const Object *b = w.find(blocker);
            const bool reached = b && b->alive && b->state == p.u8("mobyToCheckState") && p.u8("mobyToCheckState");
            if (!w.gone(blocker, game) && !reached)
                return;
        }
        if (!o.n[1]) {
            const uint32_t previous = p.u32("previousWave");
            const Object *wave = w.find(previous);
            o.n[1] = !wave || !wave->alive || (!p.toggle("prevWaveDestroyed") && wave->state == 3);
        }
        if (uint32_t trigger = p.u32("trigger"))
            if (!w.inside(trigger, w.hero()))
                return;
        if (!o.n[1])
            return;
        if (uint32_t controller = p.u32("enemyControllerToCheck")) {
            // room for this wave among the controller's enemies already in the level
            unsigned present = 0;
            for (const auto &e : game.enemies)
                if (!e.hidden && e.state != EnemyState::Dead && w.links_to(e.source_offset, controller))
                    present++;
            if (present + o.members.size() > p.u8("enemyInControllerCount") && p.u8("enemyInControllerCount"))
                return;
        }
        w.waves_started++;
        if (o.f[0] > 0)
            o.state = 1;
        else if (std::abs(p.f32("delayRandMax")) > 1e-5f)
            o.state = 2;
        else {
            for (size_t i = 0; i < o.members.size(); i++)
                wave_release(w, o, i);
            o.state = 3;
        }
        break;
    }
    case 1:
        if ((o.f[0] -= dt) <= 0) {
            if (std::abs(p.f32("delayRandMax")) > 1e-5f) {
                o.state = 2;
                o.f[0] = 0;
            } else {
                for (size_t i = 0; i < o.members.size(); i++)
                    wave_release(w, o, i);
                o.state = 3;
            }
        }
        break;
    case 2:
        if ((o.f[0] -= dt) <= 0) {
            // one of the enemies still held, picked at random
            std::vector<size_t> held;
            for (size_t i = 0; i < o.members.size(); i++)
                if (auto e = w.enemy(game, o.members[i]); e && e->hidden)
                    held.push_back(i);
            if (!held.empty())
                wave_release(w, o, held[size_t(w.random() * float(held.size())) % held.size()]);
            if (held.size() <= 1)
                o.state = 3;
            o.f[0] = p.f32("delayRandMax") * w.random();
        }
        [[fallthrough]];
    case 3: {
        if (o.state != 3)
            break;
        bool all = true;
        for (auto member : o.members)
            all &= w.gone(member, game);
        if (all)
            w.destroy(o);
        break;
    }
    }
}

// ---- EnemyController (init 0x180ba0, update 0x180ed8, areas 0x1810a0) --------------------------------------------
// Groups enemies (each names its controller in a MobyLink property). Its enemies are in the level only while the hero
// is in `updateArea` or `updateArea2` (they fade over thirty frames in the original; here they appear and go).
// The hero entering `destroyTrigger` removes them all, and the controller too when `destroyControllerToo`.
// Here the controller is also `gone` once every enemy of it is defeated, which is what doors and groups ask of it.
void controller_start(W &w, Object &o, Gameplay &game) {
    for (const auto &e : game.enemies)
        if (w.links_to(e.source_offset, o.source))
            o.members.push_back(e.source_offset);
    o.n[0] = 0; // frames the hero has been on the other side of the area test
    o.n[1] = 1; // enemies are in the level
}
void controller_tick(W &w, Object &o, Gameplay &game, float) {
    const auto &p = o.props;
    if (uint32_t trigger = p.u32("destroyTrigger"); trigger && w.inside(trigger, w.hero())) {
        for (auto member : o.members)
            if (auto e = w.enemy(game, member)) {
                e->state = EnemyState::Dead;
                e->health = 0;
                e->time = 1e6f; // no corpse
            }
        if (p.toggle("destroyControllerToo")) {
            w.destroy(o);
            return;
        }
    }
    if (uint32_t area = p.u32("updateArea")) {
        const bool in = w.inside(area, w.hero()) || w.inside(p.u32("updateArea2"), w.hero());
        if (in != bool(o.n[1])) {
            if (++o.n[0] > 29) { // 0x1810a0: thirty frames on the other side before the switch
                o.n[1] = in;
                o.n[0] = 0;
            }
        } else
            o.n[0] = 0;
        for (auto member : o.members)
            w.hold_enemy(member, false, !o.n[1]);
    }
    if (!o.members.empty()) {
        bool all = true;
        for (auto member : o.members)
            all &= w.gone(member, game);
        if (all)
            w.destroy(o);
    }
}

// ---- MobyGroup (init 0x1a7238, update 0x1a7298) -----------------------------------------------------------------
// `mobysToCheck[8]`: destroyed itself when every listed object is; its state counts the destroyed ones.
void group_tick(W &w, Object &o, Gameplay &game, float) {
    unsigned listed = 0, destroyed = 0;
    for (unsigned k = 0; k < 8; k++)
        if (uint32_t member = o.props.u32("mobysToCheck", k)) {
            listed++;
            destroyed += w.gone(member, game);
        }
    o.state = uint8_t(destroyed);
    if (listed && destroyed == listed)
        w.destroy(o);
}

// ---- MovedObject (init 0x1ab130, update 0x1ab370; its table 0x23c308) --------------------------------------------
// A gate that goes once what its `target` names (a MobyGroup of enemies) is gone. Its model's row of the table says
// how: Ryllus's one row (model 0x21e4c89e) is a speed of -.0333 a frame, an amount of -6 and a spring of .1 / .2,
// which lifts it six metres at up to a metre a second. States: 0 standing, 1 moving, 2 up. f[0] how far, f[1] its
// speed, f[2] the speed's own change.
// Not translated: other levels' rows (this one's numbers are used for any model), its sound, the save flag.
void moved_tick(W &w, Object &o, Gameplay &game, float dt) {
    if (o.state == 0) {
        if (!w.gone(o.props.u32("target"), game))
            return;
        o.state = 1;
        game.audio_events.push_back({0x35a61db1}); // level-L02_GATE01, the sound its init asks for (0x1ab130)
    }
    if (o.state != 1)
        return;
    for (int pass = std::max(1, int(std::lround(dt * 60))); pass > 0; pass--) {
        o.f[2] = (.033333335f - o.f[1]) * .1f - o.f[2] * .2f;
        o.f[1] += o.f[2];
    }
    float step = o.f[1] * dt * 30;
    if (o.f[0] + step >= 6) {
        step = 6 - o.f[0];
        o.state = 2;
        game.audio_events.push_back({0x35a61db1, true}); // its loop ends; the sound's own ending plays
    }
    o.f[0] += step;
    o.matrix[13] += step;
}

// ---- DeathTrigger (init 0x17bd50, update 0x17bdb0) ---------------------------------------------------------------
// DeathTrigger (LEVEL_02 init 0x17bd50, update 0x17bdb0).
// The hero dies below `minYHeight`, or when he is (`killHeroInTriggers`) or is not inside the triggers of the
// TriggerCollection `killAreas`; never while inside those of `ignoreAreas`.
// Not translated: the kind of death the instance's state byte picks (hero states 0x2c, 0x2e, 0x2b).
void death_tick(W &w, Object &o, Gameplay &game, float) {
    // +0x46: another object (Kalidon's magnet room) holds the trigger off for the frame by setting it.
    if (o.n[0]) {
        o.n[0] = 0;
        return;
    }
    if (!game.alive())
        return;
    const auto &p = o.props;
    const V hero = w.hero();
    if (uint32_t ignore = p.u32("ignoreAreas"); ignore && w.inside_collection(ignore, hero))
        return;
    bool kill = hero[1] < p.f32("minYHeight");
    if (uint32_t areas = p.u32("killAreas"))
        kill |= w.inside_collection(areas, hero) == p.toggle("killHeroInTriggers");
    if (kill) {
        w.deaths++;
        w.death_source = o.source;
        game.kill();
    }
}

// ---- MovePlayerHack (LEVEL_03 init 0x19c0e0, update 0x19c198) ----------------------------------------------------
// A conveyor's push: while the hero is in `area` he is carried `speed` metres a second along the area's own
// z axis (the original adds it to the velocity the world gives him, 0x250a64).
void mover_tick(W &w, Object &o, Gameplay &game, float dt) {
    const auto *area = w.trigger(o.props.u32("area"));
    if (!area || !game.alive() || !hig::trigger_contains(*area, w.hero()))
        return;
    const V axis{area->matrix[8], area->matrix[9], area->matrix[10]};
    const float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (length < 1e-6f)
        return;
    // n[1]: a conveyor has taken the speed over (the original writes it into these props each frame): f[0]
    const float step = (o.n[1] ? o.f[0] : o.props.f32("speed")) * dt / length;
    game.carry({axis[0] * step, axis[1] * step, axis[2] * step});
    o.n[0]++; // frames he has been carried (status)
}

// ---- SlideController (LEVEL_01 init 0x1b03e0, update 0x1b0670) ---------------------------------------------------
// While the hero is in `detectArea` the controller names itself to him (0x2356d8), and his state check (0x517a8)
// starts the slide along `path` at `slideSp` a frame. Levels 01, 06 and 09 have one or two.
// Not translated: the camera the controller sets for the ride (camDist, camHeight, camOfsY, azimuth), the spray
// effect and loop sound of variant 1 (sound 0x56a4062e; variant 2 0x27d0ab9e).
void slide_start(W &, Object &o, Gameplay &) { o.state = 0; }
void slide_tick(W &w, Object &o, Gameplay &game, float) {
    if (game.sliding() || !game.alive())
        return;
    const auto *path = w.curve(o.props.u32("path"));
    if (!path || path->points.size() < 2 || !w.inside(o.props.u32("detectArea"), w.hero()))
        return;
    game.begin_slide(path->points, o.props.f32("slideSp"));
}

// ---- Teleporter (init 0x1d9770, update 0x1d9c20) -----------------------------------------------------------------
// State 0: off until the hero enters `activateTrigger` (the instance's flags start an already working pad in
// state 1). State 1: on; the hero standing within half a metre of its middle is offered the trip: to
// `destinationPreferred` when that pad is on, else to `destination`, or to level `changeToLevel`.
// Not translated: the per-pad save flags (a pad stays on in a later visit), `secretActivation` (the PS2 clock),
// the original's prompt text and its beam effect.
void teleporter_start(W &w, Object &o, Gameplay &) {
    o.state = uint8_t(o.flags >> 8);
    if (!o.state && !o.props.u32("activateTrigger") && !o.props.toggle("secretActivation"))
        o.state = 1; // nothing here could ever switch it on
    w.play(o, o.state ? "Idle" : "Contract", o.state != 0);
}
void teleporter_tick(W &w, Object &o, Gameplay &game, float) {
    const auto &p = o.props;
    if (o.state == 0) {
        if (w.inside(p.u32("activateTrigger"), w.hero())) {
            o.state = 1;
            w.play(o, "Expand", false);
        }
        return;
    }
    if (o.clip >= 0 && o.model >= 0 && !o.loop &&
        o.time >= w.assets().models[o.model].animations.clips[o.clip].duration)
        w.play(o, "Idle", true);
    const V at = o.position();
    if (!game.alive() || distance_xz(w.hero(), at) > .7f || std::abs(w.hero()[1] - at[1]) > 1.5f)
        return;
    const int level = p.s32("changeToLevel");
    const Object *preferred = w.find(p.u32("destinationPreferred"));
    const Object *target = preferred && preferred->state ? preferred : w.find(p.u32("destination"));
    if (!target && level <= 0)
        return;
    w.prompt = "Teleport";
    if (!w.interact_pressed())
        return;
    w.teleports++;
    if (target) {
        V to = target->position();
        to[1] += .1f;
        game.relocate(to);
        if (auto pad = w.object(target->source); pad && !pad->state) {
            pad->state = 1; // arriving switches the far pad on
            w.play(*pad, "Idle", true);
        }
    } else
        w.travel_level = unsigned(level);
}

// ---- the level's movies and the change of character -------------------------------------------------------------
// A movie class plays the level's movie of its own letter (0x27080: "L%02i/%02i%c", the letter 'A' + the instance's
// state byte, which the BOG gives in bits 8..15 of its flags), once.
std::string level_movie(const W &w, const Object &o) {
    char path[64];
    std::snprintf(path, sizeof(path), "/VID/L%02u/%02u%c.BIK", w.assets().level, w.assets().level, 'A' + int((o.flags >> 8) & 255));
    return path;
}
void stand_at(Gameplay &game, const Object &place) { // 0x50e30: the hero at an object's place and facing
    V at = place.position();
    at[1] += .05f;
    game.relocate(at);
    game.heading = std::atan2(place.matrix[8], place.matrix[10]);
}
// MovieTeleport (init 0x1abc50, update 0x1abdc0, after its movie 0x1abef0): the hero in its `trigger` sees the movie
// and stands where the instance is afterwards.
void movie_teleport_tick(W &w, Object &o, Gameplay &game, float) {
    if (o.state || !game.alive() || !w.inside(o.props.u32("trigger"), w.hero()))
        return;
    o.state = 2;
    w.movie_request = level_movie(w, o);
    stand_at(game, o);
}
// MoviePlayerChange (init 0x1ab9d0, update 0x1abaf0, after its movie 0x1abb68): the hero in its `trigger` sees the
// movie, and its `switchPlayer` (the level's ClankPlr) is the controlled character from then on, where he stands.
void movie_change_tick(W &w, Object &o, Gameplay &game, float) {
    if (o.state || game.clank || !game.alive() || !w.inside(o.props.u32("trigger"), w.hero()))
        return;
    o.state = 1;
    w.movie_request = level_movie(w, o);
    if (const Object *other = w.find(o.props.u32("switchPlayer"))) {
        o.f[1] = game.position[0], o.f[2] = game.position[1], o.f[3] = game.position[2]; // where Ratchet waits
        stand_at(game, *other);
        game.clank = true;
        game.spawn = game.position; // he comes back here until he reaches a continue point of his own
        game.spawn_heading = game.heading;
    }
}
// MovieButtonTriggered (init 0x1ab718, update 0x1ab7b0, after its movie 0x1ab888): once the button that names it
// has been pressed it plays its movie; afterwards the first character is back in control at the instance's place,
// and the next level is open (0x27e90).
void movie_button_tick(W &w, Object &o, Gameplay &game, float) {
    if (o.state || o.f[7] <= 0 || !game.alive()) // f[7]: what a button adds to (classes_level02.cpp COUNT)
        return;
    o.state = 2;
    w.movie_request = level_movie(w, o);
    game.clank = false;
    stand_at(game, o);
}
// PlayerChange (init 0x1aea28, update 0x1aeb48: 0x1aebe8 .. 0x1af0b8): with the first character in its
// `switchTrigger` and the interact button (text 520), a fade of thirty frames and the second character is in
// control at `switchStartMatrix`; in `revertTrigger` he changes back the same way, to `revertStartMatrix`.
// States as the original's: 0 the first character, 1 fading out, 2 the change, 3 fading in, 4 the second.
// Native: no fade is drawn; the character left behind is not shown waiting.
void change_tick(W &w, Object &o, Gameplay &game, float dt) {
    const float frames = dt * 30;
    switch (o.state) {
    case 0:
        if (game.clank) {
            o.state = 4;
            break;
        }
        if (!game.alive() || !w.inside(o.props.u32("switchTrigger"), w.hero()))
            break;
        w.prompt = "Have Clank enter the hole in the wall"; // text 520
        if (w.interact_pressed()) {
            o.state = 1;
            o.f[0] = 30;
        }
        break;
    case 4:
        if (!game.clank) {
            o.state = 0;
            break;
        }
        if (game.alive() && w.inside(o.props.u32("revertTrigger"), w.hero())) {
            o.state = 1;
            o.f[0] = 30;
        }
        break;
    case 1:
        if ((o.f[0] -= frames) > 0)
            break;
        o.state = 2;
        o.f[0] = 12;
        if (const Object *place = w.find(o.props.u32(game.clank ? "revertStartMatrix" : "switchStartMatrix")))
            stand_at(game, *place);
        game.clank = !game.clank;
        break;
    case 2:
        if ((o.f[0] -= frames) <= 0) {
            o.state = 3;
            o.f[0] = 30;
        }
        break;
    case 3:
        if ((o.f[0] -= frames) <= 0)
            o.state = game.clank ? 4 : 0;
        break;
    }
}

// ---- Hookshot (init 0x191b78, update 0x191ce8) -------------------------------------------------------------------
// A target for the Hypershot: a glow in the air, green to be pulled to, yellow to swing from (`swing`). The
// hero's grapple state does the rest (Gameplay::hook_in_reach / tick_grapple). Its layout as the instances hold it:
// path, pathPoint, then speed, ropeLength and range at 0x10, 0x14 and 0x18, and the swing flag at 0x1d.
// Not translated: a target that rides its `path` (0x15a20), the `attached` flag, the multiplayer modes' switches.
void hookshot_tick(W &, Object &o, Gameplay &game, float) {
    if (o.n[3]) // put away (Kalidon's bolt cranker keeps its targets under the fluid until the bolt is turned)
        return;
    Gameplay::Hook hook;
    hook.id = o.source;
    hook.position = o.position();
    hook.rope = o.props.f32("ropeLength");
    hook.range = o.props.f32("range");
    hook.swing = o.props.u8("swing") != 0;
    game.hooks.push_back(hook);
}

const ObjectClass classes[] = {
    {"EnemyWave", wave_start, wave_tick, false},
    {"EnemyController", controller_start, controller_tick, false},
    {"MobyGroup", nullptr, group_tick, false},
    {"MovedObject", nullptr, moved_tick, true},
    {"DeathTrigger", nullptr, death_tick, false},
    {"MovePlayerHack", nullptr, mover_tick, false},
    {"Teleporter", teleporter_start, teleporter_tick, true},
    {"SlideController", slide_start, slide_tick, false},
    {"Hookshot", nullptr, hookshot_tick, false},
    {"MovieTeleport", nullptr, movie_teleport_tick, false},
    {"MoviePlayerChange", nullptr, movie_change_tick, false},
    {"MovieButtonTriggered", nullptr, movie_button_tick, false},
    {"PlayerChange", nullptr, change_tick, false},
};
} // namespace
std::span<const ObjectClass> shared_object_classes() { return classes; }
} // namespace sm
