#include "mechanisms.h"
#include "../hig/higclass.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace sm {
namespace {
using V = hig::Vec3;
uint32_t word(std::span<const uint8_t> b, size_t p) {
    uint32_t v = 0;
    if (p + 4 <= b.size())
        std::memcpy(&v, b.data() + p, 4);
    return v;
}
float real(std::span<const uint8_t> b, size_t p) {
    uint32_t v = word(b, p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
V add(V a, V b) {
    for (int k = 0; k < 3; k++)
        a[k] += b[k];
    return a;
}
V sub(V a, V b) {
    for (int k = 0; k < 3; k++)
        a[k] -= b[k];
    return a;
}
V mul(V a, float f) {
    for (auto &x : a)
        x *= f;
    return a;
}
float length(V a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
float distance(V a, V b) { return length(sub(a, b)); }
V pos(const hig::Matrix &m) { return {m[12], m[13], m[14]}; }
void position(hig::Matrix &m, V p) { std::copy(p.begin(), p.end(), m.begin() + 12); }
float ease(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
hig::Matrix transform(V p, float a) {
    float c = std::cos(a), s = std::sin(a);
    return {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, p[0], p[1], p[2], 1};
}
bool supported(uint32_t h) {
    return h == 0x8eff9fa6 || h == 0x2caf43b8 || h == 0xfd049856 || h == 0xba827ca5 ||
           h == 0xa9856df3 || h == 0xfb84ad53;
}
const EncounterAssets::Prop *asset(const EncounterAssets *a, uint32_t hash) {
    if (a)
        for (const auto &p : a->props)
            if (p.hash == hash)
                return &p;
    return nullptr;
}
void append_rigid(const hig::LevelModel &model, const hig::Matrix &matrix, Scene &scene) {
    for (const auto &m : model.materials) {
        Batch b;
        b.first = uint32_t(scene.indices.size());
        b.texture = m.texture;
        b.alpha_test = m.alpha_test;
        b.blend = m.alpha_blend;
        uint32_t base = uint32_t(scene.vertices.size());
        for (size_t i = 0; i < m.positions.size() / 3; i++) {
            Vertex v{};
            V p = hig::transform_point(
                {m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]}, matrix);
            std::copy(p.begin(), p.end(), v.position);
            std::copy_n(m.uvs.data() + i * 2, 2, v.uv);
            std::copy_n(m.rgba.data() + i * 4, 4, v.color);
            scene.vertices.push_back(v);
            b.center = add(b.center, p);
        }
        for (auto i : m.indices)
            scene.indices.push_back(base + i);
        b.count = uint32_t(scene.indices.size()) - b.first;
        if (!m.positions.empty())
            b.center = mul(b.center, 3.f / m.positions.size());
        scene.batches.push_back(b);
    }
}
} // namespace
bool load_mechanism_assets(const hig::IsoFs &disc, const World &world, EncounterAssets &out,
                           std::vector<hig::LevelTexture> &textures, std::string &error) {
    std::vector<uint8_t> local, shared, global;
    std::vector<hig::ActorAsset> entries;
    for (auto [name, bytes] : {std::pair{world.disc_path.c_str(), &local},
                               std::pair{"/SP.WAD", &shared}, std::pair{"/GLOBAL.WAD", &global}}) {
        hig::HigWad h;
        if (!hig::unpack_hig_wad(disc.read(name), h, *bytes, &error) ||
            hig::wad_checksum(*bytes) != h.crc) {
            error = "Mechanism asset WAD checksum/decompression failure";
            return false;
        }
        auto es = hig::actor_assets(*bytes);
        entries.insert(entries.end(), es.begin(), es.end());
    }
    auto bytes = [&](uint32_t hash) -> std::span<const uint8_t> {
        for (const auto &e : entries)
            if (e.hash == hash)
                return e.bytes;
        return {};
    };
    std::unordered_map<uint32_t, int> indices;
    auto next_textures = textures;
    for (size_t i = 0; i < textures.size(); i++)
        indices.emplace(textures[i].hash, int(i));
    auto bind = [&](hig::LevelMaterial &m) {
        if (!m.tex_hash) {
            m.texture = -1;
            return true;
        }
        auto it = indices.find(m.tex_hash);
        if (it == indices.end()) {
            hig::LevelTexture t;
            t.hash = m.tex_hash;
            for (const auto &e : entries)
                if (e.hash == t.hash) {
                    t.name = e.path;
                    break;
                }
            if (!hig::decode_gim(bytes(t.hash), t.image, &error))
                return false;
            it = indices.emplace(t.hash, int(next_textures.size())).first;
            next_textures.push_back(std::move(t));
        }
        m.texture = it->second;
        return true;
    };
    std::vector<EncounterAssets::Prop> next;
    for (const auto &i : world.level.bog_instances) {
        if (!supported(world.level.bog_defs[i.model_def].hash) || !i.model_hash)
            continue;
        if (std::any_of(next.begin(), next.end(),
                        [&](const auto &a) { return a.hash == i.model_hash; }))
            continue;
        EncounterAssets::Prop a;
        a.hash = i.model_hash;
        auto gde = bytes(i.model_hash);
        if (gde.empty()) {
            error = "Missing authored mechanism model " + std::to_string(i.model_hash);
            return false;
        }
        if (i.animation_hash) {
            if (!hig::decode_actor(gde, a.actor, &error) ||
                !hig::decode_animation(bytes(i.animation_hash), a.animations, &error))
                return false;
            for (auto &m : a.actor.meshes)
                if (!bind(m.material))
                    return false;
        } else {
            for (const auto &m : world.level.models)
                if (m.hash == i.model_hash) {
                    a.rigid = m;
                    break;
                }
            if (a.rigid.materials.empty()) {
                error = "Missing decoded mechanism rigid model";
                return false;
            }
            for (auto &m : a.rigid.materials)
                if (!bind(m))
                    return false;
        }
        if (i.collision_hash && !bytes(i.collision_hash).empty() &&
            !hig::decode_collision(bytes(i.collision_hash), a.collision, &error))
            return false;
        next.push_back(std::move(a));
    }
    out.props = std::move(next);
    textures = std::move(next_textures);
    error.clear();
    return true;
}
const hig::BogInstance *Mechanisms::instance(uint32_t source) const {
    if (world_)
        for (const auto &i : world_->level.bog_instances)
            if (i.source_offset == source)
                return &i;
    return nullptr;
}
const hig::BogCurve *Mechanisms::curve(uint32_t source) const {
    if (world_)
        for (const auto &c : world_->level.curves)
            if (c.source_offset == source)
                return &c;
    return nullptr;
}
bool Mechanisms::contains(uint32_t source, V p) const {
    if (world_ && source)
        for (const auto &t : world_->level.triggers)
            if (t.source_offset == source)
                return hig::trigger_contains(t, p);
    return false;
}
bool Mechanisms::completed(uint32_t source) const {
    if (!source)
        return true;
    for (const auto &o : objects)
        if (o.source == source)
            return o.complete;
    return false;
}
bool Mechanisms::start(const World &w, const EncounterAssets *a, std::string &error) {
    *this = {};
    world_ = &w;
    assets_ = a;
    // Kalidon's shaft: the lifts a Lvl3Platform names in `platforms` (twenty links at +0xec) rise out of its fluid
    // under its own rule, so the object world has them (classes_level03.cpp), not this file.
    std::vector<uint32_t> shaft;
    for (const auto &i : w.level.bog_instances)
        if (w.level.bog_defs[i.model_def].hash == hig::class_hash("Lvl3Platform") && i.properties.size() >= 0xec + 80)
            for (unsigned k = 0; k < 20; k++)
                shaft.push_back(word(i.properties, 0xec + 4 * k));
    for (const auto &i : w.level.bog_instances) {
        auto h = w.level.bog_defs[i.model_def].hash;
        auto &p = i.properties;
        if (std::find(shaft.begin(), shaft.end(), i.source_offset) != shaft.end())
            continue;
        if (h == 0xfb84ad53)
            camera_model_ = i.model_hash;
        if (h == 0x47b6f174 && p.size() >= 56) {
            auto c = curve(word(p, 32));
            if (c && c->points.size() >= 2) {
                float d = distance(c->points.front(), c->points.back());
                if (d > 2)
                    ziplines.push_back({i.source_offset, c->points.front(), c->points.back(), d});
            }
        }
        if (h == 0xf2d1bbbb && p.size() >= 0x60) {
            movie_first_ = word(p, 0x54);
            movie_second_ = word(p, 0x58);
        }
        if (!supported(h) || h == 0xfb84ad53)
            continue;
        Mechanism o;
        o.source = i.source_offset;
        o.model = i.model_hash;
        o.variant = i.variant_hash;
        std::copy_n(i.matrix, 16, o.base.begin());
        o.matrix = o.previous = o.base;
        o.heading = o.previous_heading = std::atan2(i.matrix[8], i.matrix[10]);
        if (h == 0x8eff9fa6) {
            o.kind = MechanismKind::Crank;
            o.duration = 16.6669998f; // 0x1f479c / 0x4f390
            o.matrix[13] += .25f;     // original inactive bolt height, 0x155824
            o.previous = o.matrix;
        } else if (h == 0x2caf43b8 && p.size() >= 4) {
            o.kind = MechanismKind::Cranked;
            o.link = word(p, 0);
            // CrankedObject table 0x21f040, matched by the variant signature.
            switch (o.variant) {
            case 0x29d7ed46:
                o.amount = -6;
                break;
            case 0xf3261183:
                o.amount = 3;
                break;
            case 0x6e779796:
            case 0xf50c1575:
            case 0x69e886d0:
                o.amount = 5;
                break;
            case 0xf0e1d76a:
                o.amount = 7;
                break;
            case 0x1d97b38f:
            case 0xa5bf1c90:
                o.amount = 21;
                break;
            case 0xcb5fa85c:
                o.amount = 3.887f;
                break;
            default:
                continue;
            }
        } else if (h == 0xfd049856 && p.size() >= 48) {
            auto c = curve(word(p, 0));
            if (!c || c->points.size() != 2)
                continue;
            o.kind = MechanismKind::Elevator;
            o.from = c->points[0];
            o.to = c->points[1];
            o.duration = real(p, 4);
            if (!std::isfinite(o.duration) || o.duration <= 0)
                continue;
            o.reverse = p[0x25] == 0;
            // Loader initializes at the first path point (0x190a58).
            position(o.matrix, o.from);
            o.previous = o.matrix;
        } else if (h == 0xba827ca5) {
            o.kind = MechanismKind::Ship;
        } else if (h == 0xa9856df3 && p.size() >= 200) {
            auto c = curve(word(p, 0x90));
            if (!c || c->points.size() != 12)
                continue;
            o.kind = MechanismKind::Luna;
            o.route = c->points;
            o.clip = 8;
            o.moving = true;
            for (unsigned j = 0; j < 11; j++) {
                o.triggers.push_back(word(p, 0x38 + j * 4));
                o.controllers.push_back(word(p, 0x64 + j * 4));
            }
            for (unsigned j = 0; j < 12; j++)
                o.targets.push_back(word(p, 0x98 + j * 4));
        } else
            continue;
        objects.push_back(std::move(o));
    }
    error.clear();
    return true;
}
void Mechanisms::input(const hig::PadState &pad, Gameplay &game) {
    planet_request = false;
    if (!game.health)
        return;
    if (active_zip >= 0) {
        if (!game.interacting()) {
            active_zip = -1;
            zip_cooldown_ = 1;
            return;
        }
        if (pad.pressed & hig::Cross) {
            V d = mul(sub(ziplines[active_zip].to, ziplines[active_zip].from),
                      1 / ziplines[active_zip].length);
            V v = mul(d, zip_speed_ * 30);
            v[1] = 6;
            game.end_interaction(v);
            active_zip = -1;
            zip_cooldown_ = 1;
        }
        return;
    }
    if (active >= 0) {
        if (!game.interacting()) {
            active = -1;
            return;
        }
        if (pad.pressed & (hig::Cross | hig::Triangle | hig::Circle)) {
            game.end_interaction();
            active = -1;
            return;
        }
        turn_input_ = std::clamp(std::hypot(pad.axes[0], pad.axes[1]), 0.f, 1.f);
        if (pad.raw.buttons & hig::Square)
            turn_input_ = 1; // accessible keyboard hold alternative
        return;
    }
    if (game.swimming)
        return;
    if (nearby_zip >= 0 && zip_cooldown_ <= 0 && (pad.pressed & hig::Cross)) {
        active_zip = nearby_zip;
        zip_origin_ = game.position;
        zip_time_ = zip_distance_ = zip_speed_ = 0;
        game.begin_interaction(Motion::Zipline, 32);
        zip_rides++;
        return;
    }
    if (nearby < 0)
        return;
    auto &o = objects[nearby];
    if (o.kind == MechanismKind::Crank && !o.complete &&
        (pad.pressed & (hig::Square | hig::Triangle))) {
        active = nearby;
        turn_input_ = 0;
        o.time = 0;
        game.begin_interaction(Motion::Crank, 21);
        V d = sub(game.position, pos(o.base));
        d[1] = 0;
        if (length(d) < .01f)
            d = {0, 0, -1};
        d = mul(d, 1 / length(d));
        // 0x4f0bc..0x4f1e8: orbit is perpendicular to the forward vector;
        // Ratchet faces along the circle with the crank on his right.
        o.heading = std::atan2(d[2], -d[0]);
        return;
    }
    if (o.kind == MechanismKind::Ship && (pad.pressed & hig::Triangle))
        planet_request = true;
}
void Mechanisms::update_collision(Gameplay &game) {
    std::vector<TerrainQuery::Object> colliders;
    for (const auto &o : objects)
        if (o.kind != MechanismKind::Luna) {
            auto a = asset(assets_, o.model);
            if (a && !a->collision.faces.empty())
                colliders.push_back({&a->collision, o.matrix});
        }
    colliders.insert(colliders.end(), extra_colliders.begin(), extra_colliders.end());
    game.dynamic_objects(colliders);
}
void Mechanisms::move_luna(Mechanism &o,Gameplay &game,float dt) {
    auto original=instance(o.source);
    auto left=curve(word(original->properties,0x18)),right=curve(word(original->properties,0x1c));
    if(!left || !right || left->points.size()!=right->points.size() || left->points.empty())return;
    auto center=[&](size_t k){return mul(add(left->points[k],right->points[k]),.5f);};
    auto ground=[&](V p,float reference) {
        auto hit=game.terrain().ray({p[0],reference+1.0f,p[2]},{p[0],reference-4.f,p[2]});
        if(hit.hit && hit.normal[1]>.55f)p[1]=hit.point[1]+.005f;
        return p;
    };
    auto clip=[&](unsigned c){if(o.clip!=c){o.clip=c;o.time=0;}};
    V at=pos(o.matrix);
    if(o.jump_time>=0) {
        o.jump_time+=dt;
        at=o.jump.at(o.jump_time);
        if(o.jump_time>=o.jump.duration) {
            o.jump_time=-1;o.nav_point++;o.landing_time=.2f;clip(12);
            at=ground(at,at[1]);
        } else if(o.time>=.2f && o.clip==10)clip(11);
        position(o.matrix,at);return;
    }
    at=ground(at,at[1]);position(o.matrix,at);
    if(o.landing_time>0) {o.landing_time=std::max(0.f,o.landing_time-dt);return;}
    if(!o.moving || o.stage>=o.route.size())return;
    V destination=o.route[o.stage];
    size_t end=o.nav_point;float nearest=1e30f;
    for(size_t j=o.nav_point;j<left->points.size();j++) {
        V d=sub(center(j),destination);float length2=d[0]*d[0]+d[2]*d[2];
        if(length2<nearest){nearest=length2;end=j;}
    }
    if(std::hypot(at[0]-destination[0],at[2]-destination[2])<.2f) {
        destination=ground(destination,at[1]);position(o.matrix,destination);
        o.nav_point=unsigned(end);o.moving=false;clip(4);return;
    }
    if(o.stage && distance(game.position,at)>=30) {clip(4);return;}
    V target=o.nav_point+1<end?center(o.nav_point+1):destination;
    float clearance=0;
    if(o.nav_point<left->point_data_words.size()) {
        auto value=left->point_data_words[o.nav_point];std::memcpy(&clearance,&value,4);
    }
    if(o.nav_point<end && std::isfinite(clearance) && clearance>0 && clearance<50) {
        // Crossing a marked NavBound section launches toward the next section,
        // not toward the remote wait point (0x18eab0 / 0x18f3c8).
        target=center(o.nav_point);
        if(std::hypot(at[0]-target[0],at[2]-target[2])<.25f) {
            V landing=center(o.nav_point+1);landing=ground(landing,landing[1]);
            if(o.jump.start(at,landing,clearance)) {
                o.jump_time=0;o.jumps++;clip(10);
                o.heading=std::atan2(landing[0]-at[0],landing[2]-at[2]);
                o.matrix=transform(at,o.heading);return;
            }
        }
    }
    V d=sub(target,at);float n=std::hypot(d[0],d[2]);
    if(n<.15f && o.nav_point+1<end) {o.nav_point++;return;}
    if(n>.001f) {
        float fraction=std::min(n,4.5f*dt)/n;
        V next=add(at,mul(V{d[0],0,d[2]},fraction));
        next=ground(next,at[1]);
        o.heading=std::atan2(d[0],d[2]);o.matrix=transform(next,o.heading);clip(8);
    }
}
void Mechanisms::tick(Gameplay &game, float dt) {
    if (!world_ || !std::isfinite(dt) || dt <= 0 || dt > .05f)
        return;
    if (respawns_ != game.respawns || !game.health) {
        active = active_zip = -1;
        zip_cooldown_ = 1;
        respawns_ = game.respawns;
    }
    zip_cooldown_ = std::max(0.f, zip_cooldown_ - dt);
    if (active_zip >= 0 && game.motion == Motion::Zipline) {
        auto &z = ziplines[active_zip];
        zip_time_ += dt;
        V d = mul(sub(z.to, z.from), 1 / z.length), start = add(z.from, V{0, -1.5f, 0});
        if (zip_time_ <= .5f)
            game.position = add(zip_origin_, mul(sub(start, zip_origin_), ease(zip_time_ / .5f)));
        else {
            // 0x515a0: +0.004 per 30 Hz tick, capped at 0.3 units/tick.
            zip_speed_ = std::min(.3f, zip_speed_ + .004f * dt * 30);
            zip_distance_ = std::min(z.length - 2, zip_distance_ + zip_speed_ * dt * 30);
            game.position = add(start, mul(d, zip_distance_));
            if (zip_time_ >= 1)
                game.interaction_clip(33);
            if (zip_distance_ >= z.length - 2) {
                V v = mul(d, zip_speed_ * 30);
                v[1] = 0;
                game.end_interaction(v);
                active_zip = -1;
                zip_cooldown_ = 1;
            }
        }
        game.heading = std::atan2(d[0], d[2]);
    }
    for (size_t index = 0; index < objects.size(); index++) {
        auto &o = objects[index];
        o.previous = o.matrix;
        o.previous_heading = o.heading;
        if (o.kind == MechanismKind::Crank) {
            if (active == int(index) && game.motion == Motion::Crank) {
                o.time += dt;
                if (o.time >= .666f) {
                    game.interaction_clip(turn_input_ > .1f ? 22 : 23);
                    if (turn_input_ > .1f) {
                        o.progress = std::min(1.f, o.progress + dt * turn_input_ / o.duration);
                        o.heading += 27.f * 3.14159265359f / 180 * dt * turn_input_;
                    }
                }
                V target = add(pos(o.matrix),
                               V{-std::cos(o.heading) * 1.24f, 0, std::sin(o.heading) * 1.24f});
                game.position =
                    add(game.position, mul(sub(target, game.position), std::min(1.f, dt * 15)));
                game.heading = o.heading;
                o.matrix = transform(pos(o.matrix), o.heading);
                if (o.progress >= 1) {
                    o.complete = true;
                    cranks_completed++;
                    game.end_interaction();
                    active = -1;
                    o.matrix[13] = o.base[13] - .25f;
                }
            }
        } else if (o.kind == MechanismKind::Cranked) {
            float progress = 0;
            for (const auto &crank : objects)
                if (crank.source == o.link)
                    progress = crank.progress;
            o.progress = progress;
            o.complete = progress >= 1;
            o.matrix[13] = o.base[13] + o.amount * progress;
        } else if (o.kind == MechanismKind::Elevator) {
            const auto *i = instance(o.source);
            const auto &p = i->properties;
            V feet = game.position, center = pos(o.matrix);
            auto a = asset(assets_, o.model);
            bool on = false;
            if (a && !a->collision.faces.empty()) {
                TerrainQuery support;
                support.build({});
                TerrainQuery::Object obj{&a->collision, o.matrix};
                support.objects({&obj, 1});
                auto top = support.ray(add(feet, V{0, .12f, 0}), add(feet, V{0, -.25f, 0}));
                on =
                    top.hit && top.normal[1] > .55f && game.velocity[1] <= 0 && !game.interacting();
                // useButton (+0x2a, LEVEL_02 0x1a6400): the lift carries a Button at its own place (0x165608) and
                // goes when that is pressed, which it is while the hero stands within .6 m of it (0x1654c0)
                if (p.size() > 0x2a && p[0x2a] && distance(feet, center) >= .6f)
                    on = false;
            }
            // After a ride such a lift waits for him to step off its button before it goes again (the update's
            // sub-state +0x46). `stage`: 1 while it waits for that.
            bool pressed = on;
            if (p.size() > 0x2a && p[0x2a]) {
                if (!on)
                    o.stage = 0;
                pressed = on && o.stage == 0;
            }
            if (!o.moving && !p[0x27]) {
                bool wants = p[0x28] ? pressed : !p[0x26] || pressed;
                if (o.complete && p[0x24])
                    wants = false;
                // matchHero recalls an empty lift toward Ratchet's elevation.
                if (p[0x28] && !on && distance(feet, center) < 20 &&
                    std::abs(feet[1] - center[1]) > std::max(1.f, real(p, 0x1c))) {
                    float targetY = o.reverse ? o.from[1] : o.to[1];
                    wants = std::abs(feet[1] - targetY) < std::abs(feet[1] - center[1]);
                }
                if (wants) {
                    o.moving = true;
                    o.time = 0;
                    if (on)
                        elevators_used++;
                    if (pressed && p.size() > 0x2a && p[0x2a])
                        o.stage = 1;
                }
            }
            if (o.moving) {
                o.time += dt;
                o.progress = std::min(1.f, o.time / o.duration);
                V from = o.reverse ? o.to : o.from, to = o.reverse ? o.from : o.to;
                position(o.matrix, add(from, mul(sub(to, from), ease(o.progress))));
                if (o.progress >= 1) {
                    o.moving = false;
                    o.reverse = !o.reverse;
                    o.complete = true;
                    o.time = 0;
                }
            }
            o.aboard = on;
            if (on)
                game.carry(sub(pos(o.matrix), center));
        } else if (o.kind == MechanismKind::Luna) {
            o.time += dt;
            if (movie_fired_)
                continue;
            // 0x18ea08..0x18ea9c advances the route and retains +0x4f9
            // independently of the current animation; 0x18ef60 consumes it.
            // A thin volume crossed during a photo (or while she approaches
            // its wait point) must not be lost when Ratchet leaves it.
            if (o.advance_stage < o.triggers.size() &&
                contains(o.triggers[o.advance_stage], game.position))
                ++o.advance_stage;
            if (!o.moving && o.stage < o.advance_stage && o.stage < o.triggers.size()) {
                bool ready = true;
                uint32_t target = o.targets[o.stage];
                auto ti = instance(target);
                if (ti && world_->level.bog_defs[ti->model_def].hash == 0x8eff9fa6)
                    ready = completed(target);
                if (ti && world_->level.bog_defs[ti->model_def].hash == 0x875af5d9)
                    ready = o.clip == 4; // Passing the advance volume does not require taking a photo.
                // +0x64 controller links feed local avoidance, not an arena
                // completion gate. 0x18f7d0 scans its actors within 1.5 units
                // of Luna and steers around them. Waiting for every home point
                // inside the detection volume to die deadlocks the escort.
                if (ready) {
                    o.stage++;
                    o.moving = true;
                    o.clip = 8;
                    o.time = 0;
                }
            }
            move_luna(o,game,dt);
            if (!o.moving && o.stage < o.targets.size()) {
                auto target = instance(o.targets[o.stage]);
                if (target && world_->level.bog_defs[target->model_def].hash == 0x875af5d9) {
                    auto &p = target->properties;
                    if ((contains(word(p, 0), game.position) || o.clip <= 2) &&
                        !photo_taken_.contains(target->source_offset)) {
                        if (o.clip == 4) {
                            o.clip = 2;
                            o.time = 0;
                        } else if (o.clip == 2 && o.time >= 2.033333f) {
                            o.clip = 0;
                            o.time = 0;
                        } else if (o.clip == 0 && o.time >= .666667f) {
                            photo_taken_.insert(target->source_offset);
                            photos++;
                            o.clip = 1;
                            o.time = 0;
                            game.audio_events.push_back({0x4681b921});
                        }
                    }
                    if (o.clip == 1 && o.time >= 1.833333f) {
                        o.clip = 4;
                        o.time = 0;
                    }
                }
            }
        }
        if (o.kind == MechanismKind::Luna) {
            auto a = asset(assets_, o.model);
            if (a && o.clip < a->animations.clips.size()) {
                o.previous_pose = o.pose;
                if (o.pose_clip != o.clip) {
                    o.transition.begin(o.pose, .15f); // Luna selector, 0x18ebb0
                    o.pose_clip = o.clip;
                }
                o.transition.advance(dt);
                hig::sample_animation(a->actor, &a->animations.clips[o.clip], o.time, o.clip > 2 && o.clip != 10 && o.clip != 12,
                                      o.pose);
                o.transition.apply(o.pose);
                if (o.previous_pose.empty())
                    o.previous_pose = o.pose;
            }
        }
    }
    // The original Level01Cutscene01 requires the two volumes in sequence.
    if (!movie_fired_ && movie_first_ && contains(movie_first_, game.position))
        movie_armed_ = true;
    if (!movie_fired_ && movie_armed_ && contains(movie_second_, game.position)) {
        movie_fired_ = true;
        movie_request = "/VID/L01/01B.BIK";
    }
    update_collision(game);
    nearby = nearby_zip = -1;
    float best = 3;
    for (size_t i = 0; i < objects.size(); i++) {
        const auto &o = objects[i];
        if ((o.kind != MechanismKind::Crank || o.complete) && o.kind != MechanismKind::Ship)
            continue;
        float d = distance(game.position, pos(o.base));
        float radius = o.kind == MechanismKind::Ship ? 8.f : 2.5f;
        if (d <= radius && (nearby < 0 || d < best)) {
            best = d;
            nearby = int(i);
        }
    }
    for (size_t i = 0; i < ziplines.size(); i++)
        if (distance(game.position, add(ziplines[i].from, V{0, -1.5f, 0})) < 2.5f)
            nearby_zip = int(i);
}
void Mechanisms::movie_finished() {
    if (movie_fired_)
        for (auto &o : objects)
            if (o.kind == MechanismKind::Luna)
                o.complete = true;
}
std::string Mechanisms::prompt(bool controller) const {
    if (active_zip >= 0)
        return controller ? "A  Jump off zipline" : "Space  Jump off zipline";
    if (active >= 0)
        return controller ? "Move left stick to crank   A / Y: release"
                          : "Hold F or WASD to crank   Space / R: release";
    if (nearby >= 0) {
        if (objects[nearby].kind == MechanismKind::Ship)
            return controller ? "Y  Planet select" : "R  Planet select";
        return controller ? "X  Use bolt crank" : "F  Use bolt crank";
    }
    if (nearby_zip >= 0)
        return controller ? "A  Grab wrench zipline" : "Space  Grab wrench zipline";
    return {};
}
void Mechanisms::render(float alpha, Scene &scene) const {
    if (!assets_)
        return;
    float t = std::clamp(alpha, 0.f, 1.f);
    for (const auto &o : objects) {
        auto a = asset(assets_, o.model);
        if (!a)
            continue;
        if (o.kind == MechanismKind::Luna && o.complete)
            continue;
        hig::Matrix m = o.matrix;
        if (o.kind == MechanismKind::Luna || o.kind == MechanismKind::Crank)
            m = transform(pos(m),
                          o.previous_heading +
                              std::remainder(o.heading - o.previous_heading, 6.28318530718f) * t);
        position(m, add(pos(o.previous), mul(sub(pos(o.matrix), pos(o.previous)), t)));
        if (!a->actor.bones.empty() && !a->animations.clips.empty()) {
            unsigned clip = std::min(o.clip, unsigned(a->animations.clips.size() - 1));
            float seconds = std::max(0.f, o.time + (t - 1) / 30);
            std::vector<hig::BonePose> pose;
            if (!o.pose.empty())
                hig::blend_poses(o.previous_pose, o.pose, t, pose);
            append_animated_actor(a->actor, a->animations.clips[clip], seconds,
                                  o.kind != MechanismKind::Luna || (clip > 2 && clip != 10 && clip != 12), m, scene,
                                  pose.empty() ? nullptr : &pose);
            if (o.kind == MechanismKind::Luna && clip <= 2 && a->actor.bones.size() > 61) {
                auto camera = asset(assets_, camera_model_);
                if (camera && camera->animations.clips.size() >= 2) {
                    std::vector<hig::Matrix> joints, skin;
                    if (pose.empty())
                        hig::sample_animation(a->actor, &a->animations.clips[clip], seconds, false,
                                              pose);
                    hig::pose_matrices(a->actor, pose, joints, skin);
                    unsigned camera_clip = clip == 1 ? 0 : 1;
                    float time = clip == 0 ? camera->animations.clips[1].duration : seconds;
                    append_animated_actor(camera->actor, camera->animations.clips[camera_clip],
                                          time, false, hig::multiply(joints[61], m),
                                          scene); // 0x190354
                }
            }
        } else
            append_rigid(a->rigid, m, scene);
    }
}
} // namespace sm
