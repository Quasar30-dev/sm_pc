// Dreamtime (level 05, Ratchet's dream of Pokitaru): native translations of the level's own classes.
// Function addresses are LEVEL_05.REL file offsets (tools/sm/sm_decomp.py 05 <address>); nothing of it is executed.
#include "objects.h"
#include <algorithm>
#include <cmath>

namespace sm {
namespace {
using V = hig::Vec3;
using W = ObjectWorld;

// ---- Lvl05Portal (init 0x18a2e0, update 0x18a4a8) -----------------------------------------------------------------
// A doorway of the dream. A portal with `levelChanger` exists only once level 6 is open in the save (0x263c0(6)) and
// then leaves the level (to level 0x17). The others, while the hero is within 30 m: when he comes within the root
// of 1.5 m of the point half a metre before the portal and half a metre under its place, he is held, the picture
// swirls away over a second (state 1, phase 0), he is carried over the next second from where he stood to the place
// of `newPos` while it swirls back (phase 1), and he has control again; with no `newPos` the level changes to 6
// instead. A portal with a `respawner` starts it (0x162870) once he has stood within those 30 m for 5 s.
// f[0] the phase's seconds, f[1..3] where he stood; n[0] the phase; n[1] its respawner has been started.
// NATIVE: he is not held (he is placed each frame of the carry), and a `levelChanger` portal is never there (the
// save's opened levels are not kept). Not translated: the swirl and the colour's fade (the render effects
// SWIRLY and COLORMUL), the portal's own three effects (0x106f00), the hum (0x16670).
void portal_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = 0;
    o.n[0] = o.n[1] = 0;
    if (o.props.toggle("levelChanger")) {
        o.visible = false;
        o.alive = false;
    }
}
void portal_tick(W &w, Object &o, Gameplay &game, float dt) {
    if (!o.alive)
        return;
    const auto &p = o.props;
    const V hero = w.hero(), at = o.position();
    if (o.state == 0) {
        const V d{hero[0] - at[0], hero[1] - at[1], hero[2] - at[2]};
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] >= 900 || !game.alive())
            return;
        const float size = std::max(1e-6f, std::sqrt(o.matrix[8] * o.matrix[8] + o.matrix[9] * o.matrix[9] + o.matrix[10] * o.matrix[10]));
        const V facing{o.matrix[8] / size, o.matrix[9] / size, o.matrix[10] / size};
        const V e{hero[0] - (at[0] - facing[0] * .5f), hero[1] - (at[1] - facing[1] * .5f - .5f), hero[2] - (at[2] - facing[2] * .5f)};
        if (e[0] * e[0] + e[1] * e[1] + e[2] * e[2] <= 1.5f) {
            o.state = 1;
            o.n[0] = 0;
            o.f[0] = 0;
        } else if (const uint32_t respawner = p.u32("respawner"); respawner && !o.n[1]) {
            o.f[0] += dt;
            if (o.f[0] >= 5) {
                o.n[1] = 1;
                if (Object *other = w.object(respawner))
                    other->n[3] = 1; // asked to start (EnemyRespawner is not built)
            }
        }
        return;
    }
    o.f[0] = std::min(1.f, o.f[0] + dt);
    const Object *to = w.find(p.u32("newPos"));
    if (o.n[0] == 0) {
        if (o.f[0] < 1)
            return;
        if (!to) {
            w.travel_level = 6;
            o.state = 0;
            o.f[0] = 0;
            return;
        }
        o.f[1] = hero[0], o.f[2] = hero[1], o.f[3] = hero[2];
        o.n[0] = 1;
        o.f[0] = 0;
        return;
    }
    if (to) {
        const V goal = to->position();
        game.relocate(V{o.f[1] + (goal[0] - o.f[1]) * o.f[0], o.f[2] + (goal[1] - o.f[2]) * o.f[0], o.f[3] + (goal[2] - o.f[3]) * o.f[0]});
        if (o.f[0] >= 1)
            w.teleports++;
    }
    if (o.f[0] >= 1) {
        o.state = 0;
        o.f[0] = 0;
    }
}

const ObjectClass classes[] = {
    {"Lvl05Portal", portal_start, portal_tick, true},
};
} // namespace
std::span<const ObjectClass> level05_object_classes() { return classes; }
} // namespace sm
