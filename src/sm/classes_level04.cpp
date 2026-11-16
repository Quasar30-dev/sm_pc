// Metalis (level 04, the robot junkyard and Clank's arena): native translations of the level's own classes.
// Function addresses are LEVEL_04.REL file offsets (tools/sm/sm_decomp.py 04 <address>); nothing of it is executed.
#include "objects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sm {
namespace {
using V = hig::Vec3;
using W = ObjectWorld;

// ---- Level04StartMoby (init 0x196490, update 0x196510) ------------------------------------------------------------
// The level's first frame: unless save flag 5 is set the player is the level's Clank (`clankPlayer`, 0x6d5e8), in
// the arena's lobby; and unless save flag 0x1b is set the level's movie is asked for (0x13f720) and the first
// challenge of `arenaController` begins (0x14f840: the first Destruction Derby, whose own movie is the level's
// movie 1). Then the object is gone.
// NATIVE: the save's flags are not kept, so he is Clank at every start of the level. NOT BUILT: the arena and its
// challenges; of the first derby only its movie is asked for (it is what the growth pad waits for: n[0] of the
// pad's own object, set here).
void start_tick(W &w, Object &o, Gameplay &game, float) {
    if (o.state)
        return;
    o.state = 1;
    if (const Object *clank = w.find(o.props.u32("clankPlayer"))) {
        // (a run that put the hero somewhere itself keeps its place: only from the level's own start is he moved)
        bool placed = false;
        for (const auto &other : w.objects)
            if (other.name == "ratchet")
                placed = std::hypot(other.matrix[12] - game.position[0], other.matrix[14] - game.position[2]) > 3;
        if (!placed) {
            V at = clank->position();
            at[1] += .05f;
            game.relocate(at);
            game.heading = std::atan2(clank->matrix[8], clank->matrix[10]);
        }
        game.clank = true;
        game.spawn = game.position;
        game.spawn_heading = game.heading;
    }
    char path[32];
    std::snprintf(path, sizeof path, "/VID/L%02u/%02u%c.BIK", w.assets().level, w.assets().level, 'A' + 1);
    w.movie_request = path;
    for (auto &other : w.objects)
        if (other.name == "GiantClankGrowthPad")
            other.n[0] = 1; // the level's movie 1 has been seen
}

// ---- Level04Crane (init 0x193fa8, update 0x1941b0) ---------------------------------------------------------------
// The crane between the arena's lobby and the ship's ground, 38 m apart: an arm that swings about its own place
// between 3.738 rad (its lobby end; states 1, 5) and 2.390 rad (its ship end; states 0, 4). At rest (0 or 1) it
// swings over when the hero stands on it, or comes over when he walks into the area at the other end (`shipArea`
// while it is at the lobby, `arenaArea` while it is at the ship) (0x194970). A swing (2 to the ship, 3 to the
// lobby) is 1.3478 rad in 130.84 frames: the first 15 frames gathering (angle = t^2 x .000387851), then .01163553
// rad a frame, the last 15 slowing (0x194608); he is carried round with it while he stands on it (0x1946f8).
// After it (4, 5) it waits for him to step off before it is at rest again.
// f[0] the swing's frames, f[1] its angle; n[0], n[1] he was in `shipArea` / `arenaArea` the frame before; n[2]
// the frames he has been in the tray (or, below 0, out of it).
// NATIVE: "standing on it" is the hero within 2.2 m of the middle of its tray at the tray's height (the original
// asks which object his ground is). Not translated: its sound (0x221cbc), a call from the far area during a swing (+0x2e), save
// flag 5 (it starts at the ship's end once that is set).
constexpr float CRANE_LOBBY = 3.738094f, CRANE_SHIP = 2.3902633f;
// NATIVE, measured on the native collision: the tray he rides in is 27.1 m out along the arm and .069 rad round
// from it, its floor 1.81 m above the crane's own place.
constexpr float CRANE_ARM = 27.1f, CRANE_ROUND = .069f, CRANE_FLOOR = 1.81f;
void crane_place(Object &o, float angle) {
    const float size = std::sqrt(o.base[0] * o.base[0] + o.base[1] * o.base[1] + o.base[2] * o.base[2]), c = std::cos(angle), s = std::sin(angle);
    o.matrix = {c * size, 0, -s * size, 0, 0, size, 0, 0, s * size, 0, c * size, 0, o.base[12], o.base[13], o.base[14], 1};
    o.f[1] = angle;
}
V crane_end(const Object &o) {
    const float angle = o.f[1] + CRANE_ROUND;
    return {o.base[12] + std::sin(angle) * CRANE_ARM, o.base[13] + CRANE_FLOOR, o.base[14] + std::cos(angle) * CRANE_ARM};
}
void crane_start(W &, Object &o, Gameplay &) {
    o.state = 1;
    o.f[0] = 0;
    o.n[0] = o.n[1] = o.n[2] = 0;
    crane_place(o, CRANE_LOBBY);
    o.previous = o.matrix;
    o.solid = o.collision != nullptr;
}
void crane_tick(W &w, Object &o, Gameplay &game, float) {
    const V hero = w.hero(), end = crane_end(o);
    const bool aboard = game.alive() && game.grounded && std::hypot(hero[0] - end[0], hero[2] - end[2]) < 2.2f && std::fabs(hero[1] - end[1]) < .8f;
    const bool at_ship = w.inside(o.props.u32("shipArea"), hero), at_arena = w.inside(o.props.u32("arenaArea"), hero);
    auto swing = [&](uint8_t state) {
        o.state = state;
        o.f[0] = 0;
    };
    // NATIVE: he is taken to have boarded once he has stood in the tray a third of a second, and to have left once
    // he has been out of it as long (the test is by place, and he walks through its edge on his way off)
    o.n[2] = aboard ? std::max(o.n[2], 0) + 1 : std::min(o.n[2], 0) - 1;
    const bool boarded = o.n[2] >= 10, left = o.n[2] <= -10;
    switch (o.state) {
    case 0: // at the ship's end
        if ((at_arena && !o.n[1]) || boarded)
            swing(3);
        break;
    case 1: // at the lobby's end
        if ((at_ship && !o.n[0]) || boarded)
            swing(2);
        break;
    case 2:
    case 3: {
        o.f[0] += 1;
        const float t = o.f[0];
        const bool done = t >= 130.83751f;
        const float gone = done ? 1.3478308f : t < 15 ? t * t * .000387851f : t < 115.83751f ? (t - 15) * .01163553f + .087266475f : 1.3478308f - (130.83751f - t) * (130.83751f - t) * .000387851f;
        const float before = o.f[1], angle = o.state == 2 ? CRANE_LOBBY - gone : CRANE_SHIP + gone;
        crane_place(o, angle);
        if (aboard) { // round the crane's own place with it
            const float turn = angle - before, c = std::cos(turn), s = std::sin(turn);
            const float x = hero[0] - o.base[12], z = hero[2] - o.base[14];
            game.carry(V{x * c + z * s - x, 0, -x * s + z * c - z});
        }
        if (done)
            o.state = o.state == 2 ? 4 : 5;
        break;
    }
    case 4:
        if (left)
            o.state = 0;
        break;
    case 5:
        if (left)
            o.state = 1;
        break;
    }
    o.n[0] = at_ship;
    o.n[1] = at_arena;
}

// ---- GiantClankGrowthPad (init 0x185488, update 0x185658) ---------------------------------------------------------
// The pad Clank grows on. It does nothing until the level's movie `movieIndex` has been seen (0x271b8; here movie
// 1, the first derby's). Then, while he stands on it, its prompt shows (table 0x221250 by `promptId`) and Triangle
// (pad bit 0x1000) plays `growMovie` and loads level `giantClankLevel`.
// n[0]: that movie has been seen (set by what plays it). NATIVE: "on it" is within 1.6 m of it. Not translated: its
// rising sparks (0x1fe100), `startAsClank`, `shrinkMovie` (the way back from the giant's level), the prompt's own
// text (a plain line is shown).
void pad_tick(W &w, Object &o, Gameplay &game, float) {
    if (!o.n[0] && o.props.word(0xc) >> 16 != 0xffff)
        return;
    const V hero = w.hero(), at = o.position();
    if (!game.alive() || !game.clank || std::hypot(hero[0] - at[0], hero[2] - at[2]) > 1.6f || std::fabs(hero[1] - at[1]) > 2)
        return;
    w.prompt = "Grow";
    if (!w.interact_pressed())
        return;
    char path[32];
    std::snprintf(path, sizeof path, "/VID/L%02u/%02u%c.BIK", w.assets().level, w.assets().level, char('A' + o.props.u32("growMovie")));
    w.movie_request = path;
    w.travel_level = o.props.u32("giantClankLevel");
}

const ObjectClass classes[] = {
    {"Level04StartMoby", nullptr, start_tick, false},
    {"Level04Crane", crane_start, crane_tick, true},
    {"GiantClankGrowthPad", nullptr, pad_tick, true},
};
} // namespace
std::span<const ObjectClass> level04_object_classes() { return classes; }
} // namespace sm
