// Clank's gadgebots: native translations of ClankBotController, clankbot, ClankBotHouse, ClankBotPort, BotFlinger
// and the places that send a thrown bot on (CBEnterCutscene, CBSadPlace). Function addresses are LEVEL_02.REL file
// offsets (tools/sm/sm_decomp.py 02 <address>); nothing of it is executed.
//
// The original (controller update 0x16c088, a command 0x16c270, a bot's state change 0x1698b8, what a bot does next
// 0x16b2f0): a gadgebot sleeps in its house until Clank breaks the house (the house's event 0x16eb48, its end
// 0x16eb58); from then on it takes the controller's commands. A command from the HUD's selector is given to every
// bot within 20 m of him:
//   0 wait (state 3, ears yellow)   1 follow (6: 7 on the way, 8 arrived; green)
//   2 enter (16 to 18; blue)        3 attack (11 to 13; red)
// A bot that follows walks at its clips' own paces (JOG2ms, RUN3ms). It cannot jump: where the way has a step or a
// gap Clank mans a BotFlinger, picks a bot up in its beam and throws it (0x15df80 the aim, 0x15fd78 where it will
// land, 0x15eef8 the throw). A thrown bot waits where it lands (0x16aa98, 0x16ace8), except in the places the level
// marks: by a ClankBotPort it walks in (CBEnterCutscene 0x165f70), in a CBSadPlace it comes back to him (0x167098).
//
// NATIVE, not the original's: how a bot finds its way. The original walks a NavBound along the ClankBotPath's
// segments, which the doors and the bridge open (0x16b230, 0x34100); here a bot retraces the way Clank himself
// walked, a step at a time over ground it can stand on, and stops where that way has a step, a gap or something
// solid. The D-pad gives the commands (up follow, down wait, left enter, right attack); the original's selector is
// not rebuilt. Not translated yet: attack (the target manager and the StoneIdols), their ears' colours, CBHappyPlace and CBWaitForPlayerArea, the BotFlinger's arm and its own camera.
#include "objects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sm {
namespace {
using V = hig::Vec3;
using W = ObjectWorld;
using Mark = ObjectWorld::Mark;
enum Bot : uint8_t {
    Housed = 0,
    Wait = 3,
    Follow = 6,
    OnTheWay = 7,
    Arrived = 8,
    Attack = 11,
    Enter = 16,
    ToPort = 17,
    Entering = 18,
    Held = 0x14,
    Thrown = 0x15,
    Landed = 0x16,
};
// What falls here falls at 0x222b44 a frame a frame: 9.8 m/s2.
constexpr float GRAVITY = .010888890f * 900;
// The level's own sounds, by their names in its bank (level.bnk and MicroBot.bnk; BOTCASE_BREAK and BOTFLINGER_STOP are
// also named in the code, 0x16f018 and 0x37478; the others are matched by name, the code reaching them by bank index).
constexpr uint32_t SND_COMMAND = 0x247dce67, SND_CASE_BREAK = 0x4fec2174, SND_PORT_IN = 0x3fac61ae, SND_BOT_PICKUP = 0x99c473ca,
                   SND_FLINGER_START = 0xe6d21f57, SND_FLINGER_STOP = 0x150a68d2, SND_FLINGER_THROW = 0x5871a775,
                   SND_BOT_RESPAWN = 0xd0612afa, SND_BOT_DEATH = 0x5b1a8363;
V add(V a, V b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V mul(V a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float flat(V a, V b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
bool is(const Object &o, const char *name) { return o.class_hash == hig::class_hash(name); }
void bot_clip(W &w, Object &o, const char *name) {
    if (o.model < 0)
        return;
    const int clip = w.assets().models[o.model].clip(name);
    if (clip >= 0 && clip != o.clip) {
        o.clip = clip;
        o.time = 0;
    }
    o.loop = true;
}
// A bot's fields: f[0..2] its velocity in the air, f[3] frames in the air, f[4] its heading, f[5] the point of
// Clank's way it makes for (-1: not on his way), f[6] frames it has been held up, f[7] the side it last went round
// something by; n[0] the port it goes into, n[1] the last command it took, n[2] its place among its
// controller's bots, n[3] 1 once it has gone into a port, 2 while it is lost and waits to come back.
void place(Object &o, V at) {
    const float c = std::cos(o.f[4]), s = std::sin(o.f[4]);
    o.matrix = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, at[0], at[1], at[2], 1};
}

// ---- Clank's way (NATIVE) -----------------------------------------------------------------------------------------
// Where he has stood, a point every quarter metre, and the length of the way up to each point.
std::vector<V> trail;
std::vector<float> trail_length;
void trail_note(W &w, const Gameplay &game) {
    const V at = game.position;
    if (!game.grounded)
        return;
    if (!trail.empty() && flat(trail.back(), at) < .25f && std::abs(trail.back()[1] - at[1]) < .25f)
        return;
    trail_length.push_back(trail.empty() ? 0 : trail_length.back() + flat(trail.back(), at));
    trail.push_back(at);
    if (trail.size() > 8000) { // the oldest quarter goes; the bots' places on it move down with it
        trail.erase(trail.begin(), trail.begin() + 2000);
        trail_length.erase(trail_length.begin(), trail_length.begin() + 2000);
        for (auto &o : w.objects)
            if (is(o, "ClankBot"))
                o.f[5] = o.f[5] < 0 ? -1 : std::max(0.f, o.f[5] - 2000);
    }
}
// Where a bot can join his way: the latest point of its last hundred metres within two and a half metres of the
// bot, at about its height. -1 when he has not come that near.
int trail_join(V at) {
    for (int i = int(trail.size()) - 1; i >= 0 && i >= int(trail.size()) - 400; i--)
        if (std::abs(trail[size_t(i)][1] - at[1]) < .6f && flat(trail[size_t(i)], at) < 2.5f)
            return i;
    return -1;
}
// One step over the ground towards a place: taken when there is ground to stand on within .55 m above and .8 m
// below and nothing solid in between.
bool step_to(const Gameplay &game, Object &o, V to, float speed, float dt) {
    const V at = o.position();
    const float d = flat(at, to);
    if (d < 1e-4f)
        return true;
    const V way{(to[0] - at[0]) / d, 0, (to[2] - at[2]) / d};
    const float step = std::min(d, speed * dt);
    V next{at[0] + way[0] * step, at[1], at[2] + way[2] * step};
    static const bool debug = std::getenv("SM_BOT_DEBUG") != nullptr;
    if (const auto wall = game.terrain().ray({at[0], at[1] + .4f, at[2]}, {next[0] + way[0] * .2f, at[1] + .4f, next[2] + way[2] * .2f}); wall.hit) {
        if (debug)
            std::printf("  bot %x at %.2f %.2f %.2f to %.2f %.2f: wall at %.2f %.2f %.2f normal %.2f %.2f %.2f\n", o.source, at[0], at[1], at[2],
                        to[0], to[2], wall.point[0], wall.point[1], wall.point[2], wall.normal[0], wall.normal[1], wall.normal[2]);
        return false;
    }
    const auto ground = game.terrain().ray({next[0], at[1] + .55f, next[2]}, {next[0], at[1] - .8f, next[2]});
    if (!ground.hit || ground.normal[1] < .5f) {
        if (debug)
            std::printf("  bot %x at %.2f %.2f %.2f to %.2f %.2f: ground %d at %.2f normal %.2f\n", o.source, at[0], at[1], at[2], to[0], to[2],
                        int(ground.hit), ground.point[1], ground.normal[1]);
        return false;
    }
    next[1] = ground.point[1];
    o.f[4] = std::atan2(way[0], way[2]);
    place(o, next);
    return true;
}
// A step towards a place, or round what stands in the way of one: to one side of it, the side it last took first.
bool walk_to(const Gameplay &game, Object &o, V to, float speed, float dt) {
    if (step_to(game, o, to, speed, dt))
        return true;
    const V at = o.position();
    const float d = flat(at, to);
    if (d < 1e-4f)
        return false;
    const V way{(to[0] - at[0]) / d, 0, (to[2] - at[2]) / d};
    for (int turn = 0; turn < 2; turn++) {
        const float side = (turn == 0) == (o.f[7] >= 0) ? 1.f : -1.f, c = .57357645f, s = .81915206f * side; // 55 degrees
        const V round{at[0] + way[0] * c + way[2] * s, at[1], at[2] - way[0] * s + way[2] * c};
        if (step_to(game, o, round, speed, dt)) {
            o.f[7] = side;
            return true;
        }
    }
    return false;
}

// ---- ClankBotController (init 0x16ba38, update 0x16c088) ----------------------------------------------------------
// state: 0 asleep, else awake. n[0] the command in force, n[1] counts the commands given, n[2] its bots, n[3] the bot
// on its way into a port (one at a time, the original's enterIdx +0x110).
void controller_start(W &w, Object &o, Gameplay &) {
    o.state = 0;
    o.n = {1, 0, 0, 0};
    trail.clear();
    trail_length.clear();
    for (auto &other : w.objects)
        if (is(other, "ClankBot") && !other.pooled && other.props.u32("botController") == o.source)
            other.n[2] = o.n[2]++; // its place among the controller's bots
}
void controller_tick(W &w, Object &o, Gameplay &game, float) {
    if (!game.clank || !game.alive())
        return;
    if (!game.interacting()) // not while he mans a BotFlinger
        trail_note(w, game);
    if (o.state == 0) {
        if (!w.inside(o.props.u32("initTrigger"), w.hero()))
            return;
        o.state = 2; // 0x16c270: the command "follow" on waking
        o.n[0] = 1;
        o.n[1]++;
        return;
    }
    static constexpr struct {
        uint32_t button;
        int command;
        const char *line;
    } keys[] = {{hig::Up, 1, "Gadgebots: Follow"},
                {hig::Down, 0, "Gadgebots: Wait"},
                {hig::Left, 2, "Gadgebots: Enter"},
                {hig::Right, 3, "Gadgebots: Attack"}};
    for (const auto &key : keys)
        if (w.pressed(key.button)) {
            o.n[0] = key.command;
            o.n[1]++;
            w.notice_request = key.line;
            game.audio_events.push_back({SND_COMMAND, false, key.command}); // its program says the command by its number
        }
}

// ---- ClankBotHouse (init 0x16e730, update 0x16e9c8, its event 0x16eb48, its end 0x16eb58) -------------------------
// The pod a gadgebot sleeps in. A hit breaks it (the original throws twelve pieces of it about) and its bot is one
// of the controller's from then on.
void house_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.target = true;
    o.solid = o.model >= 0; // by its model's box where it has no collision of its own
}
void house_tick(W &w, Object &o, Gameplay &game, float) {
    if (!w.hit(o))
        return;
    const V at = w.center(o);
    game.effects.burst(at, 1.1f, 0xff60b0d0, 12);
    game.audio_events.push_back({SND_CASE_BREAK});
    if (Object *bot = w.object(o.props.u32("bot")); bot && bot->state == Housed && !bot->n[3]) {
        const Object *controller = w.find(bot->props.u32("botController"));
        bot->visible = true;
        bot->state = controller && controller->state && controller->n[0] == 0 ? Wait : Follow;
        bot->n[1] = controller ? controller->n[1] : 0;
        bot->f[5] = -1;
    }
    w.destroy(o);
}

// ---- ClankBotPort (init 0x16fc28, update 0x16fed0) ----------------------------------------------------------------
// A hole in the wall that takes `botsNeeded` gadgebots; when it has them it adds one to the count of the object
// its `triggered` link names (the statue). n[0]: how many it still needs.
void port_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.n[0] = o.props.u8("botsNeeded");
}
void port_tick(W &w, Object &o, Gameplay &, float) {
    if (o.state == 0 && o.n[0] <= 0) {
        o.state = 1;
        if (auto target = w.object(o.props.u32("triggered")))
            target->f[7] += 1; // the count other objects raise (+0x70; classes_level02.cpp COUNT)
    }
    if (o.state == 0) // its glow while it wants bots (the original's is a particle of its own, 0x2354a0)
        w.marks.push_back({Mark::Kind::Glow, add(o.position(), V{0, .1f, 0}), {}, .45f, 0xa0ffb040});
}

// ---- clankbot (init 0x168b80, update 0x168f50) --------------------------------------------------------------------
void bot_start(W &, Object &o, Gameplay &) {
    o.state = Housed;
    o.visible = o.solid = false;
    o.f[4] = std::atan2(o.base[8], o.base[10]);
    o.f[5] = -1;
}
// Where a thrown bot has come down (0x16aa98 on landing, then the places' own updates).
void bot_land(W &w, const Gameplay &game, Object &o, V at) {
    // NATIVE: not on top of a bot that already stands there, but beside it where there is ground at that height
    const auto taken = [&](V p) {
        for (const auto &other : w.objects)
            if (&other != &o && is(other, "ClankBot") && other.visible && other.state != Housed && other.state != Thrown &&
                flat(other.position(), p) < .4f && std::abs(other.matrix[13] - p[1]) < .5f)
                return true;
        return false;
    };
    for (int turn = 0; turn < 8 && taken(at); turn++) {
        const float angle = float(turn) * .78539816f + float(o.n[2]);
        const V beside{at[0] + std::sin(angle) * .5f, at[1], at[2] + std::cos(angle) * .5f};
        const auto ground = game.terrain().ray(add(beside, V{0, .3f, 0}), add(beside, V{0, -.3f, 0}));
        if (ground.hit && ground.normal[1] > .5f && !taken(ground.point)) {
            at = ground.point;
            break;
        }
    }
    place(o, at);
    o.state = Wait;
    o.f[0] = o.f[1] = o.f[2] = 0;
    for (const auto &other : w.objects) {
        if (is(other, "CBEnterCutscene") && w.inside(other.props.u32("bound"), at)) {
            // 0x165f70: it walks to the port and goes in, while the port wants bots
            const Object *port = w.find(other.props.u32("botPort"));
            if (port && port->n[0] > 0) {
                o.n[0] = int32_t(port->source);
                o.state = ToPort;
                return;
            }
        }
        if (is(other, "CBSadPlace") && w.inside(other.props.u32("landingArea"), at)) {
            o.state = Follow; // 0x167098: it has missed, and comes back to him
            o.f[5] = -1;
            return;
        }
    }
}
void bot_tick(W &w, Object &o, Gameplay &game, float dt) {
    Object *controller = w.object(o.props.u32("botController"));
    if (!controller)
        return;
    if (o.state == Housed) {
        // 0x16bb18: a bot that was lost is back after a hundred and fifty frames, at its house or at the last of
        // the controller's five respawn points Clank has passed (NATIVE: "passed" by his own way, where the
        // original counts the path's segments)
        if (o.n[3] != 2 || (o.f[3] += dt * 30) <= 150)
            return;
        V at{o.base[12], o.base[13], o.base[14]};
        int latest = -1;
        for (unsigned k = 0; k < 5 && k < controller->props.u8("validRespawnPoints"); k++) {
            const V point{controller->props.real(0x120 + 16 * k), controller->props.real(0x124 + 16 * k), controller->props.real(0x128 + 16 * k)};
            for (int i = int(trail.size()) - 1; i > latest; i--)
                if (std::abs(trail[size_t(i)][1] - point[1]) < 1.5f && flat(trail[size_t(i)], point) < 3) {
                    latest = i, at = point;
                    break;
                }
        }
        o.n[3] = 0;
        o.f[3] = 0;
        o.visible = true;
        game.audio_events.push_back({SND_BOT_RESPAWN});
        o.state = Follow;
        o.n[1] = controller->n[1];
        place(o, at);
        o.f[5] = -1;
        return;
    }
    const V clank = w.hero();
    V at = o.position();
    if (o.state == Held) // the BotFlinger that holds it places it
        return;
    if (o.state == Thrown) {
        // 0x16aa98: it flies until it lands; three hundred frames in the air are its end
        bot_clip(w, o, "IDLE1");
        if ((o.f[3] += dt * 30) > 300) {
            o.visible = false;
            o.state = Housed;
            o.n[3] = 2; // lost: back after a while
            o.f[3] = 0;
            game.audio_events.push_back({SND_BOT_DEATH});
            return;
        }
        V v{o.f[0], o.f[1], o.f[2]};
        V next = add(at, mul(v, dt));
        v[1] -= GRAVITY * dt;
        const auto met = game.terrain().ray(add(at, V{0, .15f, 0}), add(next, V{0, -.02f, 0}));
        if (met.hit && met.normal[1] > .5f && v[1] <= 0) {
            bot_land(w, game, o, met.point);
            return;
        }
        if (met.hit) { // a wall or a ceiling: it drops from there (NATIVE; the original's thrown body bounces, 0x37740)
            next = add(met.point, mul(met.normal, .2f));
            next[1] -= .15f;
            v = {0, std::min(v[1], 0.f), 0};
        }
        if (next[1] < 26.681f) { // the original's own floor under the statue room
            next[1] = 26.681f;
            bot_land(w, game, o, next);
            return;
        }
        o.f[0] = v[0], o.f[1] = v[1], o.f[2] = v[2];
        place(o, next);
        return;
    }
    if (o.state == ToPort) {
        // 0x16a570 / 0x16a7a0: to .8 m in front of the port, then in
        Object *port = w.object(uint32_t(o.n[0]));
        if (!port || port->n[0] <= 0) {
            o.state = Wait;
            if (controller->n[3] == int32_t(o.source))
                controller->n[3] = 0;
            return;
        }
        const V to = sub(port->position(), mul(V{port->matrix[8], port->matrix[9], port->matrix[10]}, .8f));
        const V d = sub(to, at);
        const float left = std::sqrt(dot(d, d));
        if (left < .15f) { // 0x16a830: the port has one more, and the bot is gone
            port->n[0]--;
            game.audio_events.push_back({SND_PORT_IN});
            game.effects.burst(port->position(), .8f, 0xffffb040, 10);
            if (controller->n[3] == int32_t(o.source))
                controller->n[3] = 0;
            o.visible = false;
            o.state = Housed;
            o.n[3] = 1;
            return;
        }
        const float step = std::min(left, 2 * dt);
        o.f[4] = std::atan2(d[0], d[2]);
        place(o, add(at, mul(d, step / left)));
        bot_clip(w, o, "JOG2ms");
        return;
    }
    if (o.n[1] != controller->n[1]) { // a command, to the bots within twenty metres of him
        const V d = sub(at, clank);
        if (dot(d, d) < 400) {
            o.n[1] = controller->n[1];
            switch (controller->n[0]) {
            case 0:
                o.state = Wait;
                break;
            case 1:
                o.state = Follow;
                o.f[5] = -1;
                break;
            case 2:
                o.state = Enter;
                break;
            default: // attack is not translated: the bot keeps what it was doing
                break;
            }
        }
    }
    if (o.state == Enter) {
        // 0x16a570: the nearest port that still wants bots, at its own height (.1 m) and within 4 m, one bot at a
        // time; with none it follows (0x16b2f0)
        Object *best = nullptr;
        float nearest = 16;
        for (auto &other : w.objects)
            if (is(other, "ClankBotPort") && other.alive && other.n[0] > 0 && std::abs(at[1] - other.matrix[13]) <= .1f) {
                const V d = sub(at, other.position());
                if (dot(d, d) < nearest)
                    nearest = dot(d, d), best = &other;
            }
        if (best && (controller->n[3] == 0 || controller->n[3] == int32_t(o.source))) {
            controller->n[3] = int32_t(o.source);
            o.n[0] = int32_t(best->source);
            o.state = ToPort;
            return;
        }
        o.state = Follow;
        o.f[5] = -1;
    }
    if (o.state == Wait) {
        bot_clip(w, o, "IDLE1");
        return;
    }
    // following: along Clank's way to its place behind him, a little further back for each bot before it
    const float back = .8f + .55f * float(o.n[2]);
    if (o.f[5] < 0 || o.f[5] >= float(trail.size())) {
        // not on his way (just out of its house, thrown somewhere, held up too long): it joins where his way
        // comes near it; until then it walks straight to him while he is in the same room and stops short of him
        o.f[5] = float(trail_join(at));
        o.f[6] = 0;
        if (o.f[5] < 0) {
            const bool near = flat(at, clank) < 15 && std::abs(at[1] - clank[1]) < 1 && flat(at, clank) > back + .4f;
            if (near && walk_to(game, o, clank, 2, dt))
                bot_clip(w, o, "JOG2ms");
            else
                bot_clip(w, o, "IDLE1");
            o.state = Follow;
            return;
        }
    }
    int point = int(o.f[5]);
    const float left = trail_length.back() - trail_length[size_t(point)] + flat(at, trail[size_t(point)]);
    if (o.state == Follow)
        o.state = left > back ? OnTheWay : Arrived;
    if (o.state == Arrived && left > back + 1.5f) // it sets off again once he is a metre and a half further on
        o.state = OnTheWay;
    if (o.state == OnTheWay) {
        if (left <= back) {
            o.state = Arrived;
        } else {
            while (point + 1 < int(trail.size()) && flat(at, trail[size_t(point)]) < .15f)
                point++;
            o.f[5] = float(point);
            const bool run = left > back + 3;
            if (walk_to(game, o, trail[size_t(point)], run ? 3.f : 2.f, dt)) { // RUN3ms / JOG2ms
                bot_clip(w, o, run ? "RUN3ms" : "JOG2ms");
                o.f[6] = 0;
            } else {
                bot_clip(w, o, "IDLE1"); // a step, a gap or a door in the way: it stands there
                if ((o.f[6] += dt * 30) > 45)
                    o.f[5] = -1; // and looks for his way again
            }
        }
    }
    if (o.state == Arrived)
        bot_clip(w, o, "IDLE1");
}

// ---- BotFlinger (init 0x15d660, update 0x15d9a8) ------------------------------------------------------------------
// The device Clank throws his gadgebots with. Within a metre and a half of it the interact button puts him in it
// (text 750). In it the left stick turns it and sets its force between 1 and `forceMax` (.08 a frame); its beam
// takes the nearest bot within 15 m and 22 degrees ahead (0x15f298) on the fire button, and the same button throws
// the bot; the interact button leaves it, dropping a held bot.
// Where the throw will land (0x15fd78): it leaves at force x flingStrength / 30 a frame, times .6 along the facing
// and times `upScale` upwards, and falls at 9.8 m/s2; the first thing a line to the top of that arc meets, or else
// a line ten times as far on and down, is the place, and the throw is then timed to come down exactly there
// (0x15eef8). With nothing met it is thrown at that speed.
// States as the original's: 0 empty, 2 manned, 4 holding a bot, 5 throwing, (1, 3 and 6 are its animations).
// f[0] its heading, f[1] the force, f[2..4] where the throw will land, f[5] frames into the throw; n[0] the held
// bot, n[1] 1 with a landing place, 2 when that is on the way up.
// NATIVE: how fast it turns (the original eases towards the camera's facing, moved by the stick), where the held
// bot hangs, the ring and the beam drawn.
V flinger_hold(const Object &o) {
    return add(o.position(), V{std::sin(o.f[0]) * 1.1f, .9f, std::cos(o.f[0]) * 1.1f});
}
void flinger_start(W &, Object &o, Gameplay &) {
    o.state = 0;
    o.f[0] = std::atan2(o.base[8], o.base[10]);
    o.f[1] = 2;
    o.n = {0, 0, 0, 0};
}
void flinger_leave(W &w, Object &o, Gameplay &game) {
    if (Object *bot = w.object(uint32_t(o.n[0])); bot && bot->state == Held) {
        bot->state = Thrown; // dropped where it hangs
        bot->f[0] = -std::sin(o.f[0]) * .1f * 30, bot->f[1] = 0, bot->f[2] = -std::cos(o.f[0]) * .1f * 30;
        bot->f[3] = 0;
    }
    o.n[0] = 0;
    o.state = 0;
    game.end_interaction();
    game.audio_events.push_back({SND_FLINGER_STOP});
}
void flinger_tick(W &w, Object &o, Gameplay &game, float dt) {
    const float frames = dt * 30;
    if (o.state == 0) {
        const V d = sub(o.position(), w.hero());
        if (!game.clank || !game.alive() || dot(d, d) >= 2.25f || game.interacting())
            return;
        w.prompt = "Enter the BotFlinger"; // text 750
        if (!w.interact_pressed())
            return;
        o.state = 2;
        o.f[1] = 2;
        o.n[0] = 0;
        game.begin_interaction(Motion::Crank, 0);
        game.audio_events.push_back({SND_FLINGER_START});
        return;
    }
    if (!game.alive() || !game.interacting()) { // he was hurt out of it
        flinger_leave(w, o, game);
        return;
    }
    // manned: he stands behind it and it faces where the stick turns it
    o.f[0] -= w.stick_x() * 1.8f * dt;
    o.f[1] = std::clamp(o.f[1] + w.stick_y() * .08f * frames, 1.f, o.props.f32("forceMax"));
    const V forward{std::sin(o.f[0]), 0, std::cos(o.f[0])};
    {
        const float c = std::cos(o.f[0]), s = std::sin(o.f[0]);
        const V at = o.position();
        o.matrix = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, at[0], at[1], at[2], 1};
        game.position = sub(at, mul(forward, .9f));
        game.heading = o.f[0];
        w.aim_camera = true;
        w.aim_yaw = o.f[0];
    }
    if (w.interact_pressed()) {
        flinger_leave(w, o, game);
        return;
    }
    const bool fire = w.pressed(hig::Circle | hig::Cross);
    Object *held = w.object(uint32_t(o.n[0]));
    if (o.state == 2) {
        // 0x15f298: the nearest bot ahead, out of its house and not on its way into a port
        Object *best = nullptr;
        float nearest = 15 * 15;
        for (auto &other : w.objects) {
            if (!is(other, "ClankBot") || other.state == Housed || (other.state >= 15 && other.state <= 19) || other.state == Thrown)
                continue;
            const V d = sub(other.position(), o.position());
            const float along = forward[0] * d[0] + forward[2] * d[2], far = dot(d, d);
            if (along >= 0 && far <= nearest && far >= .01f && along / std::sqrt(far) >= .92718f)
                nearest = far, best = &other;
        }
        if (best) {
            w.marks.push_back({Mark::Kind::Beam, add(o.position(), V{0, .9f, 0}), add(best->position(), V{0, .3f, 0}), .04f, 0xc0ffe060});
            w.marks.push_back({Mark::Kind::Glow, add(best->position(), V{0, .3f, 0}), {}, .45f, 0xa0ffe060});
            if (fire) {
                best->state = Held;
                o.n[0] = int32_t(best->source);
                o.state = 4;
                game.audio_events.push_back({SND_BOT_PICKUP});
            }
        }
        return;
    }
    if (!held || held->state != Held) { // the bot is gone from the beam
        o.n[0] = 0;
        o.state = 2;
        return;
    }
    const V from = flinger_hold(o);
    held->f[4] = o.f[0];
    place(*held, from);
    bot_clip(w, *held, "IDLE1");
    // 0x15fd78: where it will land
    const float strength = o.props.f32("flingStrength") / 30; // a frame, as the original scales it on loading
    const float gravity = .010888890f;
    const float along = o.f[1] * strength * .6f, up = o.props.f32("upScale") * o.f[1] * strength;
    const float top = up / gravity; // frames to the top of the arc
    const V peak = add(from, V{forward[0] * along * top, up * top - top * top * gravity * .5f, forward[2] * along * top});
    const float far = top * 10;
    const V end = add(peak, V{forward[0] * along * far, -far * far * gravity * .5f, forward[2] * along * far});
    o.n[1] = 0;
    if (const auto met = game.terrain().ray(from, peak); met.hit) {
        o.n[1] = 3;
        o.f[2] = met.point[0], o.f[3] = met.point[1], o.f[4] = met.point[2];
    } else if (const auto second = game.terrain().ray(peak, end); second.hit) {
        o.n[1] = 1;
        o.f[2] = second.point[0], o.f[3] = second.point[1], o.f[4] = second.point[2];
    }
    const V aim{o.f[2], o.f[3], o.f[4]};
    if (o.n[1]) {
        w.marks.push_back({Mark::Kind::Disc, add(aim, V{0, .06f, 0}), {}, .6f, 0xc060e0ff});
        w.marks.push_back({Mark::Kind::Glow, add(aim, V{0, .3f, 0}), {}, .3f, 0xc060e0ff});
    }
    w.marks.push_back({Mark::Kind::Beam, add(o.position(), V{0, .9f, 0}), from, .04f, 0xc0ffe060});
    if (o.state == 4) {
        if (fire) {
            o.state = 5;
            o.f[5] = 0;
        }
        return;
    }
    // 0x15ddc8: thrown .1333 into the throwing clip (four frames), by 0x15eef8
    if ((o.f[5] += frames) < 4)
        return;
    float speed = along;
    if (o.n[1]) {
        const float rise = aim[1] - from[1];
        const float under = std::max(0.f, up * up - 2 * gravity * rise);
        const float time = (o.n[1] & 2 ? up - std::sqrt(under) : up + std::sqrt(under)) / gravity;
        if (time > 1e-3f)
            speed = flat(from, aim) / time;
    }
    V way = o.n[1] ? V{aim[0] - from[0], 0, aim[2] - from[2]} : forward;
    const float length = std::hypot(way[0], way[2]);
    way = length > 1e-4f ? mul(way, 1 / length) : forward;
    held->state = Thrown;
    held->f[0] = way[0] * speed * 30, held->f[1] = up * 30, held->f[2] = way[2] * speed * 30;
    held->f[3] = 0;
    game.audio_events.push_back({SND_FLINGER_THROW});
    o.n[0] = 0;
    o.n[2]++; // throws, for the test's report
    o.state = 2;
    o.f[1] = 2; // 0x15f968: the force is 2 again
}

const ObjectClass classes[] = {
    {"ClankBotController", controller_start, controller_tick, false},
    {"ClankBot", bot_start, bot_tick, true},
    {"ClankBotHouse", house_start, house_tick, true},
    {"ClankBotPort", port_start, port_tick, true},
    {"BotFlinger", flinger_start, flinger_tick, true},
};
} // namespace
std::span<const ObjectClass> clankbot_object_classes() { return classes; }
} // namespace sm
