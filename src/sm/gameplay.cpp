#include "gameplay.h"
#include "hig/higjump.h"
#include "hig/higlighting.h"
#include "hig/higwrench.h"
#include "ps2_ratchet.h"
#include "ps2_weapons.h"
#include "ps2_packs.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace sm {
namespace {
using V = hig::Vec3;
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
V mul(V a, float s) {
    for (auto &x : a)
        x *= s;
    return a;
}
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V unit(V a) {
    float n = std::sqrt(dot(a, a));
    return n > 1e-8f ? mul(a, 1 / n) : V{0, 1, 0};
}
V closest_triangle(V p, V a, V b, V c) {
    V ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return a;
    V bp = sub(p, b);
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return add(a, mul(ab, d1 / (d1 - d3)));
    V cp = sub(p, c);
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return add(a, mul(ac, d2 / (d2 - d6)));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
        return add(b, mul(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    float inv = 1 / (va + vb + vc);
    return add(a, add(mul(ab, vb * inv), mul(ac, vc * inv)));
}
void closest_segments(V p, V q, V a, V b, V &x, V &y) {
    V d = sub(q, p), e = sub(b, a), r = sub(p, a);
    float dd = dot(d, d), ee = dot(e, e), de = dot(d, e), dr = dot(d, r), er = dot(e, r);
    float den = dd * ee - de * de;
    float s = den > 1e-12f ? std::clamp((de * er - dr * ee) / den, 0.f, 1.f) : 0;
    float t = ee > 1e-12f ? (de * s + er) / ee : 0;
    if (t < 0) {
        t = 0;
        s = dd > 1e-12f ? std::clamp(-dr / dd, 0.f, 1.f) : 0;
    } else if (t > 1) {
        t = 1;
        s = dd > 1e-12f ? std::clamp((de - dr) / dd, 0.f, 1.f) : 0;
    }
    x = add(p, mul(d, s));
    y = add(a, mul(e, t));
}
bool intersect(V p, V delta, V a, V b, V c, float &t) {
    V e = sub(b, a), f = sub(c, a), h = cross(delta, f);
    float det = dot(e, h);
    if (std::abs(det) < 1e-8f)
        return false;
    float inv = 1 / det;
    V s = sub(p, a);
    float u = dot(s, h) * inv;
    if (u < 0 || u > 1)
        return false;
    V q = cross(s, e);
    float v = dot(delta, q) * inv;
    if (v < 0 || u + v > 1)
        return false;
    t = dot(f, q) * inv;
    return t >= 0 && t <= 1;
}
hig::Matrix actor_transform(V pos, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, pos[0], pos[1], pos[2], 1};
}
void append_actor(const hig::ActorModel &actor, const hig::AnimationClip *clip, float time,
                  bool loop, const hig::Matrix &transform, Scene &scene, uint32_t hide = 0,
                  std::vector<hig::Matrix> *bones = nullptr,
                  const hig::AnimationClip *layer = nullptr, float layer_time = 0,
                  const std::vector<hig::BonePose> *evaluated = nullptr,
                  const std::vector<hig::Matrix> *bone_world = nullptr) {
    const bool authored_light=scene.lighting&&scene.lighting->valid;
    const auto lighting=authored_light?hig::actor_lighting(*scene.lighting,{transform[12],transform[13],transform[14]}):hig::ActorLighting{};
    std::vector<hig::BonePose> pose;
    std::vector<hig::Matrix> world, skin;
    if (bone_world && bone_world->size() == actor.bones.size()) {
        // the bones' places in model space given outright (a piece worn on another actor's bones)
        world = *bone_world;
        skin.resize(world.size());
        for (size_t i = 0; i < world.size(); i++)
            skin[i] = hig::multiply(actor.bones[i].inverse_bind, world[i]);
    } else {
    if (evaluated)
        pose = *evaluated;
    else
        hig::sample_animation(actor, clip, time, loop, pose);
    if (layer)
        hig::sample_animation_layer(actor, *layer, layer_time, false, 52, pose);
    hig::pose_matrices(actor, pose, world, skin);
    }
    if (bones) {
        *bones = world;
        for (auto &m : *bones)
            m = hig::multiply(m, transform);
    }
    for (const auto &mesh : actor.meshes) {
        if (mesh.part < 32 && (hide & (1u << mesh.part)))
            continue;
        Batch batch;
        batch.first = uint32_t(scene.indices.size());
        batch.texture = mesh.material.texture;
        batch.alpha_test = .1f;
        uint32_t base = uint32_t(scene.vertices.size());
        for (const auto &v : mesh.vertices) {
            V p{}, n{};
            for (int k = 0; k < 6; k++)
                if (v.weights[k] > 0) {
                    const auto &mat = skin[v.bones[k]];
                    p = add(p, mul(hig::transform_point(v.position, mat), v.weights[k]));
                    auto direction = mat;
                    direction[12] = direction[13] = direction[14] = 0;
                    n = add(n, mul(hig::transform_point(v.normal, direction), v.weights[k]));
                }
            p = hig::transform_point(p, transform);
            Vertex out{};
            std::copy(p.begin(), p.end(), out.position);
            std::copy(v.uv.begin(), v.uv.end(), out.uv);
            if(authored_light) {
                const auto light=hig::actor_light_rgb(lighting,hig::lighting_normal(n,transform));
                for(int k=0;k<3;++k)out.color[k]=uint8_t(std::clamp(255.f*light[k],0.f,255.f));
            } else {
                // Preview only when no original scene receiver is available.
                float light = .65f + .35f * std::max(0.f, dot(unit(n), unit(V{-.4f, .8f, -.5f})));
                out.color[0] = out.color[1] = out.color[2] = uint8_t(255 * light);
            }
            out.color[3] = 255;
            scene.vertices.push_back(out);
            batch.center = add(batch.center, p);
        }
        for (auto index : mesh.indices)
            scene.indices.push_back(base + index);
        batch.count = uint32_t(scene.indices.size()) - batch.first;
        batch.center = mul(batch.center, 1.f / mesh.vertices.size());
        scene.batches.push_back(batch);
    }
}
bool segment_sphere(V from, V to, V center, float radius, float &fraction) {
    V d = sub(to, from), offset = sub(from, center);
    float a = dot(d, d), c = dot(offset, offset) - radius * radius;
    if (c <= 0) {
        fraction = 0;
        return true;
    }
    if (a < 1e-10f)
        return false;
    float b = dot(offset, d), discriminant = b * b - a * c;
    if (discriminant < 0)
        return false;
    fraction = (-b - std::sqrt(discriminant)) / a;
    return fraction >= 0 && fraction <= 1;
}
} // namespace
void append_animated_actor(const hig::ActorModel &model, const hig::AnimationClip &clip, float time,
                           bool loop, const hig::Matrix &matrix, Scene &scene,
                           const std::vector<hig::BonePose> *evaluated) {
    append_actor(model, &clip, time, loop, matrix, scene, 0, nullptr, nullptr, 0, evaluated);
}
bool load_player_assets(const hig::IsoFs &disc, PlayerAssets &out, std::string &error) {
    PlayerAssets a;
    std::vector<uint8_t> global, sp;
    hig::HigWad header;
    for (auto pair : {std::pair{"/GLOBAL.WAD", &global}, std::pair{"/SP.WAD", &sp}}) {
        if (!hig::unpack_hig_wad(disc.read(pair.first), header, *pair.second, &error))
            return false;
        if (hig::wad_checksum(*pair.second) != header.crc) {
            error = "Player WAD checksum mismatch";
            return false;
        }
    }
    auto entries = hig::actor_assets(global), single = hig::actor_assets(sp);
    entries.insert(entries.end(), single.begin(), single.end());
    auto asset = [&](const char *name) -> std::span<const uint8_t> {
        for (const auto &e : entries)
            if (e.path.ends_with(name))
                return e.bytes;
        return {};
    };
    auto pak = disc.read("/ART/HEROSKIN.PAK");
    std::vector<hig::PakEntry> skins;
    if (!hig::parse_pak(pak, skins, &error))
        return false;
    // SCUS-97615 default, multipart Ratchet (body/pants + armor texture slots).
    if (skins.size() != 56) {
        error = "Unsupported hero skin directory";
        return false;
    }
    auto entry = skins[29];
    if (!hig::decode_actor(std::span<const uint8_t>(pak).subspan(entry.offset, entry.size),
                           a.ratchet, &error) ||
        !hig::decode_animation(asset("RatchetStdMaster.mb.anim"), a.animations, &error))
        return false;
    if (a.ratchet.bones.size() != 112 || a.animations.clips.size() != 100) {
        error = "Unexpected Ratchet model/animation revision";
        return false;
    }
    for (auto pair : {std::pair{"BlasterMaster.mb.ps2.gde", &a.blaster},
                      std::pair{"WrenchDefaultMaster.mb.ps2.gde", &a.wrench},
                      std::pair{"ClankPackMaster.mb.ps2.gde", &a.clank},
                      std::pair{"HelipackMaster.mb.ps2.gde", &a.heli}})
        if (!hig::decode_actor(asset(pair.first), *pair.second, &error)) {
            error = std::string(pair.first) + ": " + error;
            return false;
        }
    for (auto pair : {std::pair{"ClankPackMaster.mb.anim", &a.clank_animations},
                      std::pair{"HelipackMaster.mb.anim", &a.heli_animations},
                      std::pair{"BlasterMaster.mb.anim", &a.blaster_animations},
                      std::pair{"WrenchDefaultMaster.mb.anim", &a.wrench_animations}})
        if (!hig::decode_animation(asset(pair.first), *pair.second, &error))
            return false;
    if (a.clank_animations.clips.size() != 5 || a.heli_animations.clips.size() != 7 ||
        a.blaster_animations.clips.size() != 4 || a.wrench_animations.clips.size() != 5) {
        error = "Unexpected attachment animation revision";
        return false;
    }
    std::unordered_map<uint32_t, int> textures;
    for (auto *model : {&a.ratchet, &a.blaster, &a.wrench, &a.clank, &a.heli})
        for (auto &mesh : model->meshes) {
            uint32_t hash = mesh.material.tex_hash;
            if (!hash)
                continue;
            auto it = textures.find(hash);
            if (it == textures.end()) {
                auto e = std::find_if(entries.begin(), entries.end(),
                                      [&](const auto &e) { return e.hash == hash; });
                if (e == entries.end()) {
                    error = "Missing player texture " + std::to_string(hash);
                    return false;
                }
                hig::LevelTexture t;
                t.hash = hash;
                t.name = e->path;
                if (!hig::decode_gim(e->bytes, t.image, &error))
                    return false;
                it = textures.emplace(hash, int(a.textures.size())).first;
                a.textures.push_back(std::move(t));
            }
            mesh.material.texture = it->second;
        }
    if(!load_effect_assets(disc,entries,a.effects,a.textures,error))return false;
    out = std::move(a);
    error.clear();
    return true;
}
bool load_hero_skin(const hig::IsoFs &disc, unsigned skin, PlayerAssets &assets, std::string &error) {
    if (skin >= hero_skin_count) {
        error = "No such hero skin";
        return false;
    }
    const auto resident = disc.read("/SCUS_976.15");
    const auto pak = disc.read("/ART/HEROSKIN.PAK");
    std::vector<hig::PakEntry> entries;
    if (!hig::parse_pak(pak, entries, &error))
        return false;
    auto word = [&](size_t at) {
        uint32_t v = 0;
        if (at + 4 <= resident.size())
            std::memcpy(&v, resident.data() + at, 4);
        return v;
    };
    // the resident's one load segment maps the table's address to its place in the file
    const uint32_t program = word(28), segment_offset = word(program + 4), segment_address = word(program + 8);
    constexpr uint32_t table = 0x1edcc10;
    const size_t row = size_t(table) - segment_address + segment_offset + size_t(skin) * 0x18;
    const uint32_t model_entry = word(row), texture_entry = word(row + 4);
    if (word(0) != 0x464c457f || table < segment_address || !model_entry || model_entry > entries.size() ||
        texture_entry > entries.size() || (skin == 0) != (texture_entry == 0)) {
        error = "Unsupported hero skin table";
        return false;
    }
    hig::ActorModel model;
    const auto &m = entries[model_entry - 1];
    if (!hig::decode_actor(std::span<const uint8_t>(pak).subspan(m.offset, m.size), model, &error))
        return false;
    if (model.bones.size() != assets.ratchet.bones.size()) {
        error = "Hero skin has another skeleton";
        return false;
    }
    // Its meshes name their textures by signature. The shared ones are the default Ratchet's, already loaded; the
    // one that is not is the skin's own, the table's texture entry.
    int own = -1;
    for (auto &mesh : model.meshes) {
        const uint32_t hash = mesh.material.tex_hash;
        if (!hash)
            continue;
        auto known = std::find_if(assets.textures.begin(), assets.textures.end(), [&](const auto &t) { return t.hash == hash; });
        if (known != assets.textures.end()) {
            mesh.material.texture = int(known - assets.textures.begin());
            continue;
        }
        if (own < 0) {
            if (!texture_entry) {
                error = "Missing hero skin texture " + std::to_string(hash);
                return false;
            }
            hig::LevelTexture t;
            t.hash = hash;
            t.name = "HEROSKIN.PAK/" + std::to_string(texture_entry - 1);
            const auto &e = entries[texture_entry - 1];
            if (!hig::decode_gim(std::span<const uint8_t>(pak).subspan(e.offset, e.size), t.image, &error))
                return false;
            own = int(assets.textures.size());
            assets.textures.push_back(std::move(t));
        } else if (assets.textures[size_t(own)].hash != hash) {
            error = "Hero skin names two textures of its own";
            return false;
        }
        mesh.material.texture = own;
    }
    assets.ratchet = std::move(model);
    assets.hero_skin = skin;
    error.clear();
    return true;
}
void TerrainQuery::build(const hig::Collision &col) {
    triangles_.clear();
    for (const auto &f : col.faces) {
        Triangle t;
        t.a = col.vertices[f.vertices[0]];
        t.b = col.vertices[f.vertices[1]];
        t.c = col.vertices[f.vertices[2]];
        V n = cross(sub(t.b, t.a), sub(t.c, t.a));
        if (dot(n, n) < 1e-12f)
            continue;
        t.normal = unit(n);
        t.surface = f.surface;
        for (int k = 0; k < 3; k++) {
            t.minimum[k] = std::min({t.a[k], t.b[k], t.c[k]});
            t.maximum[k] = std::max({t.a[k], t.b[k], t.c[k]});
        }
        triangles_.push_back(t);
    }
    static_count_ = triangles_.size();
}
void TerrainQuery::objects(std::span<const Object> objects) {
    triangles_.resize(static_count_);
    for (const auto &object : objects) {
        if (!object.collision) continue;
        for (const auto &f : object.collision->faces) {
            Triangle t;
            t.a=hig::transform_point(object.collision->vertices[f.vertices[0]],object.matrix);
            t.b=hig::transform_point(object.collision->vertices[f.vertices[1]],object.matrix);
            t.c=hig::transform_point(object.collision->vertices[f.vertices[2]],object.matrix);
            V n=cross(sub(t.b,t.a),sub(t.c,t.a));
            if(dot(n,n)<1e-14f)continue;
            t.normal=unit(n);
            for(int k=0;k<3;k++) {
                t.minimum[k]=std::min({t.a[k],t.b[k],t.c[k]});
                t.maximum[k]=std::max({t.a[k],t.b[k],t.c[k]});
            }
            triangles_.push_back(t);
        }
    }
}
RayHit TerrainQuery::ray(V from, V to) const {
    RayHit hit;
    V d = sub(to, from);
    for (const auto &t : triangles_) {
        bool skip = false;
        for (int k = 0; k < 3; k++)
            skip |=
                std::max(from[k], to[k]) < t.minimum[k] || std::min(from[k], to[k]) > t.maximum[k];
        if (skip)
            continue;
        float f;
        if (intersect(from, d, t.a, t.b, t.c, f) && f < hit.fraction) {
            hit.hit = true;
            hit.fraction = f;
            hit.point = add(from, mul(d, f));
            hit.normal = dot(t.normal, d) > 0 ? mul(t.normal, -1) : t.normal;
            hit.surface = t.surface;
        }
    }
    return hit;
}
RayHit TerrainQuery::sweep_sphere(V from, V to, float radius) const {
    RayHit best;
    V delta = sub(to, from);
    const float length = std::sqrt(dot(delta, delta));
    if (length < 1e-7f || !std::isfinite(radius) || radius <= 0)
        return best;
    for (const auto &tri : triangles_) {
        bool outside = false;
        for (int k = 0; k < 3; ++k)
            outside |= std::min(from[k], to[k]) - radius > tri.maximum[k] ||
                       std::max(from[k], to[k]) + radius < tri.minimum[k];
        if (outside)
            continue;
        float t = 0;
        for (unsigned iteration = 0; iteration < 32 && t <= best.fraction; ++iteration) {
            V center = add(from, mul(delta, t));
            V contact = closest_triangle(center, tri.a, tri.b, tri.c);
            V normal = sub(center, contact);
            float distance = std::sqrt(dot(normal, normal));
            if (distance <= radius + .0001f) {
                normal = distance > 1e-7f ? mul(normal, 1 / distance) : tri.normal;
                if (dot(delta, normal) < -1e-7f)
                    best = {true, t, contact, normal};
                break;
            }
            // Distance to a triangle is Lipschitz continuous: this advance
            // cannot step over first contact, including edges and vertices.
            t += std::max(.000001f, (distance - radius) / length);
        }
    }
    return best;
}
TerrainQuery::Overlap TerrainQuery::overlap(V center, float radius, float height) const {
    Overlap out;
    if (!(radius > 0) || height < 0 || !std::isfinite(radius) || !std::isfinite(height))
        return out;
    constexpr float shrink = .9995f; // a later face replaces the kept one only when it is this much nearer (0x3f7fdf3b)
    float best = radius * radius * shrink;
    const V top{center[0], center[1] + height, center[2]};
    for (const auto &t : triangles_) {
        if (t.maximum[0] < center[0] - radius || t.minimum[0] > center[0] + radius || t.maximum[2] < center[2] - radius ||
            t.minimum[2] > center[2] + radius || t.maximum[1] < center[1] - radius || t.minimum[1] > top[1] + radius)
            continue;
        // the point of the segment the face is measured against (0x3fa928): on an upright face its first vertex's
        // height; else where the segment's line meets the plane, taken to the face
        V s = center;
        if (height > 0) {
            float y;
            if (std::fabs(t.normal[1]) < 1e-4f)
                y = t.a[1];
            else if (center[1] >= t.maximum[1])
                y = center[1];
            else if (top[1] <= t.minimum[1])
                y = top[1];
            else {
                V on_plane = center;
                on_plane[1] -= dot(sub(center, t.a), t.normal) / t.normal[1];
                y = closest_triangle(on_plane, t.a, t.b, t.c)[1];
            }
            s[1] = std::clamp(y, center[1], top[1]);
        }
        const V close = closest_triangle(s, t.a, t.b, t.c), apart = sub(s, close);
        const float d2 = dot(apart, apart);
        if (d2 > best)
            continue;
        best = d2 * shrink;
        out.hit = true;
        out.contact = close;
        out.normal = dot(sub(s, t.a), t.normal) < 0 ? mul(t.normal, -1) : t.normal;
    }
    if (!out.hit)
        return out;
    // 0x3fa0b0 / 0x3fb138: the shape's nearest point is put at the radius from the closest point of the face
    V v = sub(center, out.contact);
    const float lift = height > 0 ? std::clamp(-v[1], 0.f, height) : 0.f;
    v[1] += lift;
    v = best > 0 ? mul(v, radius / std::sqrt(best)) : V{};
    v[1] -= lift;
    out.center = add(out.contact, v);
    out.distance = std::sqrt(best / shrink);
    return out;
}
void TerrainQuery::move(V &feet, V &velocity, float dt, bool &grounded, float radius, float height) const {
    const V start = feet, wanted = velocity;
    const bool was_grounded = grounded;
    move_once(feet, velocity, dt, grounded, radius, height);
    // A kerb or a lip (Kalidon's factory stairs begin with one of .4 m). The original's ground probe starts .75 m
    // above his feet (LEVEL_03 0x47ee8, called with .75 from 0x48fc0), so what he walks into below that is stepped
    // onto. NATIVE rule in its place: on the ground, when a wall took more than half of this step's way, the step
    // is tried again from half a metre higher and kept when it gets further and ends on ground above where he stood.
    const float want = std::hypot(wanted[0], wanted[2]) * dt;
    if (!was_grounded || want < 1e-4f)
        return;
    const float got = std::hypot(feet[0] - start[0], feet[2] - start[2]);
    if (got > want * .5f)
        return;
    V lifted = add(start, V{0, .5f, 0}), level{wanted[0], 0, wanted[2]};
    bool on_ground = false;
    move_once(lifted, level, dt, on_ground, radius, height);
    if (std::hypot(lifted[0] - start[0], lifted[2] - start[2]) <= got + 1e-4f)
        return;
    // the ground under him there, or under his leading edge (one frame's way is less than his radius)
    auto under = ray(add(lifted, V{0, .1f, 0}), add(lifted, V{0, -.6f, 0}));
    if (!under.hit || under.normal[1] <= .55f || under.point[1] < start[1] + .05f) {
        const float speed = std::hypot(wanted[0], wanted[2]);
        const V ahead = add(lifted, V{wanted[0] / speed * radius, 0, wanted[2] / speed * radius});
        under = ray(add(ahead, V{0, .1f, 0}), add(ahead, V{0, -.6f, 0}));
    }
    if (!under.hit || under.normal[1] <= .55f || under.point[1] < start[1] + .05f)
        return;
    feet = {lifted[0], under.point[1] + .001f, lifted[2]};
    velocity = level;
    grounded = true;
}
void TerrainQuery::move_once(V &feet, V &velocity, float dt, bool &grounded, float radius, float height) const {
    int steps = std::clamp(int(std::ceil(std::sqrt(dot(velocity, velocity)) * dt / .1f)), 1, 32);
    bool was_grounded = grounded;
    grounded = false;
    for (int step = 0; step < steps; step++) {
        feet = add(feet, mul(velocity, dt / steps));
        for (int pass = 0; pass < 4; pass++) {
            bool changed = false;
            for (const auto &t : triangles_) {
                if (feet[0] + radius < t.minimum[0] || feet[0] - radius > t.maximum[0] ||
                    feet[2] + radius < t.minimum[2] || feet[2] - radius > t.maximum[2] ||
                    feet[1] + height < t.minimum[1] || feet[1] > t.maximum[1])
                    continue;
                V a = add(feet, V{0, radius, 0}), b = add(feet, V{0, height - radius, 0}), x = a,
                  y = closest_triangle(a, t.a, t.b, t.c);
                float best = dot(sub(x, y), sub(x, y));
                V end = closest_triangle(b, t.a, t.b, t.c);
                float d = dot(sub(b, end), sub(b, end));
                if (d < best) {
                    best = d;
                    x = b;
                    y = end;
                }
                for (auto edge : {std::pair{t.a, t.b}, std::pair{t.b, t.c}, std::pair{t.c, t.a}}) {
                    V ex, ey;
                    closest_segments(a, b, edge.first, edge.second, ex, ey);
                    d = dot(sub(ex, ey), sub(ex, ey));
                    if (d < best) {
                        best = d;
                        x = ex;
                        y = ey;
                    }
                }
                float fraction;
                if (intersect(a, sub(b, a), t.a, t.b, t.c, fraction)) {
                    best = 0;
                    x = y = add(a, mul(sub(b, a), fraction));
                }
                if (best >= radius * radius)
                    continue;
                float length = std::sqrt(best);
                V n = length > 1e-6f ? mul(sub(x, y), 1 / length) : t.normal;
                if (length <= 1e-6f && dot(n, velocity) > 0)
                    n = mul(n, -1);
                feet = add(feet, mul(n, radius - length + .0005f));
                float into = dot(velocity, n);
                if (into < 0)
                    velocity = sub(velocity, mul(n, into));
                if (n[1] > .55f)
                    grounded = true;
                changed = true;
            }
            if (!changed)
                break;
        }
    }
    if (velocity[1] <= 0) {
        auto hit = ray(add(feet, V{0, .1f, 0}), add(feet, V{0, was_grounded ? -.24f : -.025f, 0}));
        if (hit.hit && hit.normal[1] > .55f) {
            feet[1] = hit.point[1] + .001f;
            velocity[1] = 0;
            grounded = true;
        }
    }
}
bool Gameplay::start(const World &world, std::string &error, bool allow_exploration) {
    *this = {};
    lighting_=world.level.lighting;
    terrain_.build(world.terrain_collision);
    bool found = false;
    for (const auto &def : world.level.bog_defs)
        if (def.hash == 0x36919224 && def.count &&
            def.first_instance < world.level.bog_instances.size()) {
            auto &m = world.level.bog_instances[def.first_instance].matrix;
            spawn = {m[12], m[13], m[14]};
            heading = std::atan2(m[8], m[10]);
            found = true;
            break;
        }
    auto ground = found ? terrain_.ray(add(spawn,V{0,3,0}),add(spawn,V{0,-10,0})) : RayHit{};
    if((!ground.hit || ground.normal[1]<.55f) && allow_exploration) {
        // Prefer authored checkpoints/ship positions in non-Ratchet modes.
        // A walkable terrain face is the final native exploration fallback.
        for(const auto &i:world.level.bog_instances) {
            auto h=world.level.bog_defs[i.model_def].hash;
            if(h!=0x9f887978 && h!=0xba827ca5)continue;
            V candidate{i.matrix[12],i.matrix[13],i.matrix[14]};
            auto hit=terrain_.ray(add(candidate,V{0,3,0}),add(candidate,V{0,-10,0}));
            if(hit.hit && hit.normal[1]>.55f) {ground=hit;heading=std::atan2(i.matrix[8],i.matrix[10]);break;}
        }
        if(!ground.hit || ground.normal[1]<.55f) {
            float best=std::numeric_limits<float>::max();
            for(const auto &f:world.terrain_collision.faces) {
                const auto &a=world.terrain_collision.vertices[f.vertices[0]],&b=world.terrain_collision.vertices[f.vertices[1]],&c=world.terrain_collision.vertices[f.vertices[2]];
                V n=cross(sub(b,a),sub(c,a));float area=std::sqrt(dot(n,n));
                if(area<.25f || std::abs(n[1])/area<.8f)continue;
                V candidate=mul(add(add(a,b),c),1.f/3);
                float score=dot(sub(candidate,spawn),sub(candidate,spawn));
                if(score>=best)continue;
                auto hit=terrain_.ray(add(candidate,V{0,2,0}),add(candidate,V{0,-.1f,0}));
                if(hit.hit && hit.normal[1]>.55f) {ground=hit;best=score;}
            }
        }
    }
    if(!ground.hit || ground.normal[1]<.55f) {
        error=found?"Ratchet placement has no walkable terrain":"Level has no walkable player placement";
        return false;
    }
    spawn = ground.point;
    spawn[1] += .005f;
    position = previous_ = spawn;
    spawn_heading = heading;
    grounded = true;
    floor_limit_ = world.terrain_collision.minimum[1] - 15;
    has_water_ = world.level.environment.ocean_model >= 0;
    water_height = world.level.environment.ocean_height;
    error.clear();
    return true;
}
void Gameplay::begin_interaction(Motion state, unsigned clip) {
    if (state!=Motion::Zipline && state!=Motion::Crank) return;
    cancel_wrench();
    pack_jump_ = Motion::Idle;
    velocity={};jumps=0;swimming=wading=strafing=false;reset_swim();action_time_=fire_time_=0;
    motion=state;interaction_clip_=clip;animation_time=interaction_time_=0;
}
void Gameplay::interaction_clip(unsigned clip) {
    if (interacting() && interaction_clip_!=clip) {
        interaction_clip_=clip;animation_time=0;
    }
}
void Gameplay::end_interaction(V release_velocity) {
    if(!interacting())return;
    motion=Motion::Fall;animation_time=0;velocity=release_velocity;grounded=false;
    audio_events.push_back({0xf5f63c3d,true});
}
// The Hypershot, after the hero's grapple state (LEVEL_02: 0x6acd0 the target, 0x6ab98 its start, 0x6ca78 a frame).
const Gameplay::Hook *Gameplay::hook_in_reach() const {
    // the nearest target ahead of him: within its range, within 45 degrees of his facing over the ground, not
    // straight overhead, and in plain sight
    const V forward{std::sin(heading), 0, std::cos(heading)};
    const Hook *best = nullptr;
    float best_d = 0;
    for (const auto &h : hooks) {
        const V d = sub(h.position, position);
        const float flat2 = d[0] * d[0] + d[2] * d[2], ahead = forward[0] * d[0] + forward[2] * d[2];
        const float d2 = flat2 + d[1] * d[1];
        if (ahead < 0 || d2 < .0001f || d2 > h.range * h.range || flat2 < (d[1] * .1f) * (d[1] * .1f) ||
            ahead / std::sqrt(flat2) < .707106f)
            continue;
        if (terrain_.ray(add(position, V{0, 1, 0}), h.position).hit)
            continue;
        if (!best || d2 < best_d) {
            best = &h;
            best_d = d2;
        }
    }
    return best;
}
void Gameplay::tick_grapple(const hig::PadState &pad, float camera_yaw, float dt) {
    auto &g = grapple_;
    const bool held = (pad.raw.buttons & hig::Circle) && arsenal.equipped == hig::GadgetHypershot;
    const int frames = std::max(1, int(std::lround(dt * 30)));
    auto let_go = [&](V v) {
        motion = Motion::Fall;
        animation_time = 0;
        velocity = v;
        grounded = false;
        jumps = 1;
        g.phase = 0;
    };
    const V to = sub(g.anchor, position);
    const float flat = std::hypot(to[0], to[2]);
    if (g.phase == 1) { // GRPL_FIR at twice its speed (0x6ab98): the line goes out
        if (flat > .001f)
            heading = std::atan2(to[0], to[2]);
        g.cancelled |= !held;
        if (animation_time * 2 < .96666667f)
            return;
        if (g.cancelled) {
            let_go({});
            return;
        }
        g.length = std::sqrt(dot(to, to));
        g.length_speed = 0;
        audio_events.push_back({0x8de63d54}); // swingtarget-CONNECT: the line has its target
        if (!g.swing) { // pulled along the line, .4 a frame, to three metres short of it
            g.phase = 2;
            g.start = position;
            g.length -= 1;
            g.progress = 0;
            g.step = g.length > 3 ? .40000004f / (g.length - 3) : 1;
        } else { // swinging: a first push of .1 a frame towards it over the ground
            g.phase = 4;
            const V push = flat > .001f ? V{to[0] / flat, 0, to[2] / flat} : V{std::sin(heading), 0, std::cos(heading)};
            g.velocity = mul(push, .10000001f);
            g.push = mul(push, .004f);
            g.pushing = true;
        }
        animation_time = 0;
        return;
    }
    if (g.phase == 2) {
        g.cancelled |= !held;
        g.progress = std::min(1.f, g.progress + g.step * float(frames));
        const V line = sub(g.anchor, g.start);
        const float whole = std::sqrt(dot(line, line));
        const float reach = g.length > 3 ? g.length - 3 : g.length;
        if (whole > .001f)
            position = add(g.start, mul(line, reach / whole * g.progress));
        if (g.progress >= 1 || g.cancelled) {
            // Off the line with a little of its way: +0x23c, his speed over the ground, at most .08 a pass (4.8 m/s).
            // Native: he is carried on at that until he lands, as off a swing (the stick would stop him at once).
            g.phase = 0;
            launch(whole > .001f ? V{line[0] / whole * 4.8f, 0, line[2] / whole * 4.8f} : V{}, 22);
            jumps = 1;
            motion = Motion::Fall;
        }
        return;
    }
    // swinging (sub-state 4)
    if (!held) {
        // He lets go into the jump state (0x40): its rise (+0x1b8, a pass of a sixtieth) is what the swing had
        // and a tenth more, at most .2, and its way over the ground (+0x23c) .21 a pass. Native: he is carried as
        // a throw is, with no control until he lands.
        const float along = std::hypot(g.velocity[0], g.velocity[2]);
        const float up = std::min(g.velocity[1] + .10000001f, .20000002f) * 60, out = .21f * 60;
        const V way = along > .0001f ? V{g.velocity[0] / along, 0, g.velocity[2] / along} : V{std::sin(heading), 0, std::cos(heading)};
        g.phase = 0;
        launch(V{way[0] * out, up, way[2] * out}, 22);
        launch_steer_ = true;
        return;
    }
    float x = pad.axes[0], z = -pad.axes[1];
    if (const float len = std::hypot(x, z); len > 1)
        x /= len, z /= len;
    const V wish{-x * std::cos(camera_yaw) + z * std::sin(camera_yaw), 0, z * std::cos(camera_yaw) + x * std::sin(camera_yaw)};
    auto hold = [&] { // on the rope's end
        V d = sub(position, g.anchor);
        const float length = std::sqrt(dot(d, d));
        if (length > 1e-4f)
            position = add(g.anchor, mul(d, g.length / length));
    };
    for (int frame = 0; frame < frames; frame++) {
        for (int pass = 0; pass < 2; pass++) { // the rope comes to the target's own length
            g.length_speed = std::clamp((g.rope - g.length) * .1f - g.length_speed * .2f, -.5f, .5f);
            g.length += g.length_speed;
        }
        hold();
        const V before = position;
        position = add(position, g.velocity);
        position[1] -= .01f;
        hold();
        g.velocity = mul(sub(position, before), .99f);
        V add_in = mul(wish, .006f);
        if (g.pushing) {
            add_in = g.push;
            if (dot(g.push, g.velocity) < 0 || g.anchor[1] - position[1] < 2)
                g.pushing = false;
        }
        g.velocity = add(g.velocity, add_in);
        // not through the ground (0x6c9a0)
        if (const auto ground = terrain_.ray(add(position, V{0, .75f, 0}), add(position, V{0, -.4f, 0})); ground.hit)
            position[1] = std::max(position[1], ground.point[1] + .4f);
    }
    if (std::hypot(g.velocity[0], g.velocity[2]) > .01f)
        heading = std::atan2(g.velocity[0], g.velocity[2]);
}
// The slide, after the level modules' hero state 0x1a (LEVEL_01: entry 0x51808, update 0x51a00). One call is one
// 30 Hz frame of the original; its springs step twice a frame (dt * 60 steps).
void Gameplay::begin_slide(std::span<const V> path, float speed) {
    if (path.size() < 2 || motion == Motion::Slide || !health || interacting())
        return;
    slide_ = {};
    slide_.path.assign(path.begin(), path.end());
    slide_.speed = speed;
    slide_.phase = 1;
    slide_.forward = slide_.forward_goal = {std::sin(heading), 0, std::cos(heading)};
    motion = Motion::Slide;
    animation_time = 0;
    velocity = {};
    strafing = false;
    pack_jump_ = Motion::Idle;
}
// ---- The Grind Lock's rails ---------------------------------------------------------------------------------------
namespace {
float rail_length(const std::vector<V> &rail) {
    float total = 0;
    for (size_t i = 0; i + 1 < rail.size(); i++) {
        const V d = sub(rail[i + 1], rail[i]);
        total += std::sqrt(dot(d, d));
    }
    return total;
}
// The point `distance` along a rail and the segment it is on; past the end, two metres on along the last segment
// (0x57a18's return 3).
V rail_point(const std::vector<V> &rail, float distance, size_t &segment, bool &ended) {
    float along = std::max(distance, 0.f);
    for (size_t i = 0; i + 1 < rail.size(); i++) {
        const V d = sub(rail[i + 1], rail[i]);
        const float length = std::sqrt(dot(d, d));
        if (along <= length && length > 1e-6f) {
            segment = i;
            ended = false;
            return add(rail[i], mul(d, along / length));
        }
        along -= length;
    }
    segment = rail.size() - 2;
    ended = true;
    return add(rail.back(), mul(unit(sub(rail.back(), rail[rail.size() - 2])), 2));
}
// The nearest place on a rail to a point: its distance along the rail, the segment, and the squared gap.
float rail_nearest(const std::vector<V> &rail, V point, float &along, size_t &segment) {
    float best = 1e30f, walked = 0;
    for (size_t i = 0; i + 1 < rail.size(); i++) {
        const V d = sub(rail[i + 1], rail[i]);
        const float length2 = dot(d, d);
        if (length2 < 1e-10f)
            continue;
        const float length = std::sqrt(length2);
        const float t = std::clamp(dot(sub(point, rail[i]), d) / length2, 0.f, 1.f);
        const V gap = sub(point, add(rail[i], mul(d, t)));
        if (const float g = dot(gap, gap); g < best) {
            best = g;
            along = walked + length * t;
            segment = i;
        }
        walked += length;
    }
    return best;
}
} // namespace
void Gameplay::begin_grind(std::vector<std::vector<V>> rails, float speed) {
    if (rails.empty() || rails[0].size() < 2 || !health)
        return;
    cancel_wrench();
    end_interaction();
    grind_ = {};
    grind_.rails = std::move(rails);
    grind_.speed = speed;
    grind_.phase = 1;
    grind_.safe = 60; // 0x56170: +0xcd0
    size_t segment = 0;
    rail_nearest(grind_.rails[0], position, grind_.distance, segment);
    grind_.at = position;
    grind_.forward = unit(sub(grind_.rails[0][segment + 1], grind_.rails[0][segment]));
    motion = Motion::Grind;
    audio_events.push_back({0x18f5f2beu}); // ratchet-GRIND_RAIL1_LOOP (sound 0)
    animation_time = 0;
    velocity = {};
    jumps = 0;
    strafing = swimming = wading = false;
    pack_jump_ = Motion::Idle;
    grounded = true;
}
void Gameplay::end_grind() {
    if (motion != Motion::Grind)
        return;
    audio_events.push_back({0x18f5f2beu, true});
    grind_ = {};
    motion = Motion::Idle;
    animation_time = 0;
    velocity = {};
    slide_pitch = 0;
}
void Gameplay::grind_knock(bool gate, float moved, unsigned damage) {
    auto &g = grind_;
    if (motion != Motion::Grind || g.phase == 5 || !health)
        return;
    g.phase = gate ? 8 : 7;
    g.rise = .06666667f;    // 0x3d888889
    g.gravity = .00555556f; // 0x3bb60b62
    // 0x56d18: back along the rail at between .1333 and .2667 a frame by how fast he met it; a gate holds him (0x56e30)
    g.back = gate ? 0 : std::min(std::max(moved * .6f, .13333334f) * 2, .53333336f) * .5f;
    g.spring = {};
    g.clip = gate ? 0x3d : 0x13;
    g.knocks++;
    animation_time = 0;
    const unsigned hit = std::min(health, damage); // 0x56ba8: straight off his health, no armour, no flinch
    health -= hit;
    damage_taken += hit;
    if (gate) { // sounds 3 and 5: ratchet-GRIND_SHOCK1, ratchet-GRIND_SHOCK_VO
        audio_events.push_back({0x0a653cd9u});
        audio_events.push_back({0x15c067feu});
    } else
        audio_events.push_back({0x2b583bc3u}); // sound 4: ratchet-GET_HIT
    if (!health) {
        audio_events.push_back({0x18f5f2beu, true});
        audio_events.push_back({0xced82c61u});
        grind_ = {};
        motion = Motion::Dead;
        animation_time = 0;
        velocity = {};
    }
}
void Gameplay::tick_grind(const hig::PadState &pad) {
    auto &g = grind_;
    constexpr int steps = 2;
    auto ease = [](float &value, float &rate, float goal) { // 0x57e50: (goal - value) * .2 - rate * .4, within .8
        for (int i = 0; i < steps; i++) {
            rate = std::clamp((goal - value) * .2f - rate * .4f, -.8f, .8f);
            value += rate;
        }
    };
    auto turn = [](float &value, float &rate, float goal) {
        for (int i = 0; i < steps; i++) {
            rate = std::clamp(rate + (goal - value) * .1f - rate * .2f, -.4f, .4f);
            value += rate;
        }
    };
    auto play = [&](unsigned clip) {
        if (g.clip != clip) {
            g.clip = clip;
            animation_time = 0;
        }
    };
    const float clip_length = hero_animations_ && g.clip < hero_animations_->clips.size() ? hero_animations_->clips[g.clip].duration : .5f;
    const V before = position;
    if (g.phase == 5) { // off the rails (0x57e50 case 5): he falls on, and a metre down the try is over
        g.fall[1] -= g.gravity;
        position = add(position, g.fall);
        if (g.fall_from - position[1] > 1)
            g.fell = true;
        g.moved = sub(position, before);
        velocity = mul(g.moved, 30);
        return;
    }
    const auto &rail = g.rails[g.rail];
    size_t segment = 0;
    bool ended = false;
    V goal;
    if (g.phase == 7 || g.phase == 8) {
        g.safe = 15;
        g.distance = std::max(0.f, g.distance - g.back);
        goal = rail_point(rail, g.distance, segment, ended);
    } else {
        g.distance += g.speed;
        goal = rail_point(rail, g.distance, segment, ended);
        if (g.phase == 2) { // crossing: the place he springs to slides from where he is to the new rail
            const float t = g.cross / 60.001194f;
            g.cross -= 1;
            goal = add(mul(goal, 1 - t), mul(g.at, t));
            if (g.cross <= 0) {
                g.cross = 0;
                g.phase = 3;
            }
        }
        // 0x57c08: past the rail's last point he is off it
        if (ended && dot(sub(rail.back(), position), g.forward) < 0) {
            if (g.phase != 2 && g.phase != 3)
                g.rise = 0;
            g.phase = 5;
            g.gravity = .0064f;
            g.fall_from = position[1];
            const float flat = std::sqrt(g.spring[0] * g.spring[0] + g.spring[2] * g.spring[2]);
            g.fall = {g.forward[0] * flat, g.rise, g.forward[2] * flat};
            play(5);
            audio_events.push_back({0x18f5f2beu, true});
            g.moved = {};
            return;
        }
        // the pad (0x57848)
        const float x = pad.axes[0];
        g.lean = x < -.5f || (pad.held & hig::Left) ? 1 : x > .5f || (pad.held & hig::Right) ? 2 : 0;
        if ((pad.pressed & hig::Cross) && (g.phase == 1 || g.phase == 6)) {
            bool crossing = false;
            if (g.lean) { // 0x56688: the nearest other rail within six metres on that side, running his way
                const V left{g.forward[2], 0, -g.forward[0]};
                float best = 36;
                int to = -1;
                float to_along = 0;
                for (size_t k = 0; k < g.rails.size(); k++) {
                    if (int(k) == g.rail || g.rails[k].size() < 2)
                        continue;
                    float along = 0;
                    size_t at_segment = 0;
                    const float gap = rail_nearest(g.rails[k], position, along, at_segment);
                    const V other = rail_point(g.rails[k], along, at_segment, ended);
                    const float side = dot(sub(other, position), left) * (g.lean == 1 ? 1.f : -1.f);
                    const V way = sub(g.rails[k][at_segment + 1], g.rails[k][at_segment]);
                    if (side > 0 && at_segment + 2 < g.rails[k].size() && dot(way, g.forward) > 0 && gap < best) {
                        best = gap;
                        to = int(k);
                        to_along = along;
                    }
                }
                if (to >= 0) {
                    g.rail = to;
                    g.distance = to_along;
                    g.phase = 2;
                    g.cross = 60.001194f; // 0x42700139
                    g.crossings++;
                    crossing = true;
                }
            }
            if (!crossing)
                g.phase = 3;
            g.jumps++;
            g.height = 0;
            g.rise = .16f;      // 0x3e23d634
            g.gravity = .0064f; // 0x3bd1b4f3
            play(0x45);
            audio_events.push_back({0xf4f95945u}); // ratchet-JUMP (the rails' sound 1, table 0x23b108)
        } else if (g.phase == 1 && (pad.pressed & hig::Square)) {
            g.phase = 6;
            g.swing = 0;
            g.swings++;
            play(0x46);
            audio_events.push_back({0x09b077b5u}); // WrenchDefault-WRENCH_SWING
        } else if (g.phase == 1)
            play(0x42 + unsigned(g.lean)); // 0x2277c0: 0x42, 0x43 left, 0x44 right
    }
    for (int k = 0; k < 3; k++)
        ease(g.at[k], g.spring[k], goal[k]);
    const auto &now = g.rails[g.rail];
    const V way = unit(sub(now[std::min(segment + 1, now.size() - 1)], now[std::min(segment, now.size() - 2)]));
    for (int k = 0; k < 3; k++)
        turn(g.forward[k], g.forward_spring[k], way[k]);
    g.forward = unit(g.forward);
    if (g.phase == 6) {
        g.swing += 1;
        if (animation_time * 1.5f >= clip_length) {
            g.phase = 1;
            play(0x42);
        }
    }
    if (g.phase == 2 || g.phase == 3) {
        g.height += g.rise;
        g.rise -= g.gravity;
        if (g.height < 0) {
            g.height = 0;
            g.phase = 1;
            play(0x42);
            audio_events.push_back({0xbed6cb15u}); // ratchet-GRIND_LAND (sound 2)
        }
    } else if (g.phase == 7 || g.phase == 8) {
        g.height = std::max(0.f, g.height + g.rise);
        g.rise -= g.gravity;
        if (animation_time >= clip_length && g.height <= 0) {
            g.phase = 1;
            g.height = 0;
            play(0x42);
        }
    }
    position = {g.at[0], g.at[1] + g.height, g.at[2]};
    heading = std::atan2(g.forward[0], g.forward[2]);
    slide_pitch = -std::asin(std::clamp(g.forward[1], -1.f, 1.f));
    g.moved = sub(position, before);
    velocity = mul(g.moved, 30);
    g.safe = std::max(0.f, g.safe - 1);
}
// ---- The Magneboots ----------------------------------------------------------------------------------------------
// The hero's state 0x1f (LEVEL_03 0x528a0), asked for by ground of type 3 (0x3fc48: the probe 0x47ee8 found a face
// whose surface type has flag 4). From it:
//  - his up (+0x2f4) eases to the normal of the face under him (+0x304; 0x4b370), and the camera takes his up for
//    its own (0x53000: 5 m behind him);
//  - the stick, in the camera's frame and laid on the surface, is the way he turns to about his up (0x53550); he
//    walks the way he faces at .0225 a call, or .0675 with the stick full over (0x542f0 case 1: 1.35 and 4.05 m/s
//    at 60 calls a second);
//  - a sphere of .4 m, .75 m up his own up, keeps him out of walls (0x53d38);
//  - the probe (0x53ba0): from 1 m up his up, 3 m down it. Nothing there, a face that is not magnetic, or one more
//    than .5 m under his feet, and he falls off (0x530d0: state 4, his up back to the world's); else he stands
//    where it hit (0x53e28);
//  - Cross (0x529a0 case 2): a jump of .07 a call along his up under .003 a call each call (4.2 m/s, 10.8 m/s^2:
//    .82 m high); coming down, the probe from .75 m up lands him (0x54168) on a magnetic face, with a landing of
//    .1 s when he came down faster than .156 a call (9.4 m/s), .233 s when faster than .38.
// NATIVE: the ease's and the turn's rates (the original's are springs whose rates are in the hero's record), the
// camera's swing behind him. Not translated: the wrench swing on the boots (case 5: clip COMBO_1), a weapon fired
// from them (case 9), the hit reaction (case 7), the glow on the boots (0x38728, effect 0x478fe9a), footsteps.
bool Gameplay::magnetic(const RayHit &hit) const { return hit.hit && world_ && (world_->surface_flags(hit.surface) & 4); }
void Gameplay::begin_magne(const RayHit &under) {
    cancel_wrench();
    motion = Motion::Magne;
    position = under.point;
    auto &m = magne_;
    m = {};
    m.normal = under.normal;
    m.up = {0, 1, 0};
    V ahead{std::sin(heading), 0, std::cos(heading)};
    m.forward = m.view = ahead;
    velocity = {};
    grounded = true;
    jumps = 0;
    animation_time = 0;
}
void Gameplay::magne_turn_view(float angle) {
    auto &m = magne_;
    const float c = std::cos(angle), s = std::sin(angle);
    m.view = unit(add(mul(m.view, c), mul(cross(m.up, m.view), s)));
}
void Gameplay::tick_magne(const hig::PadState &pad, float dt) {
    auto &m = magne_;
    constexpr float WALK = .0225f * 60, RUN = .0675f * 60, JUMP = .07f * 60, GRAVITY = .003f * 3600;
    constexpr float TURN = 9, EASE = 10, CHASE = 1.2f; // NATIVE rates: rad/s, 1/s, 1/s
    auto leave = [&](bool on_ground) { // 0x530d0
        if (std::hypot(m.forward[0], m.forward[2]) > .05f)
            heading = std::atan2(m.forward[0], m.forward[2]);
        motion = on_ground ? Motion::Idle : Motion::Fall;
        grounded = on_ground;
        velocity = on_ground ? V{} : add(mul(m.forward, m.speed), m.jump);
        jumps = on_ground ? 0 : 1;
        animation_time = 0;
        m = {};
    };
    if (!health) {
        leave(false);
        return;
    }
    // about his up: Rodrigues, for a vector at right angles to the axis
    auto turned = [](V v, V axis, float angle) { return add(mul(v, std::cos(angle)), mul(cross(axis, v), std::sin(angle))); };
    auto angle_to = [&](V from, V to) { return std::atan2(dot(cross(from, to), m.up), dot(from, to)); };
    auto flat = [&](V v, V otherwise) {
        v = sub(v, mul(m.up, dot(v, m.up)));
        return dot(v, v) > 1e-6f ? unit(v) : otherwise;
    };
    // his up eases to the face's normal; the way he faces and the camera's way stay laid on the surface
    {
        const V eased = add(m.up, mul(sub(m.normal, m.up), std::min(1.f, EASE * dt)));
        if (dot(eased, eased) > 1e-6f)
            m.up = unit(eased);
        V spare = std::fabs(m.up[1]) < .9f ? V{0, 1, 0} : V{0, 0, 1};
        m.forward = flat(m.forward, flat(spare, V{1, 0, 0}));
        m.view = flat(m.view, m.forward);
    }
    m.landing = std::max(0.f, m.landing - dt);
    // the stick, in the camera's frame
    const V right = cross(m.view, m.up);
    V want = add(mul(m.view, -pad.axes[1]), mul(right, pad.axes[0]));
    const float push = std::min(1.f, std::sqrt(dot(want, want)));
    float target = 0;
    if (push >= .1f) {
        const float angle = angle_to(m.forward, unit(want));
        m.forward = unit(turned(m.forward, m.up, std::clamp(angle, -TURN * dt, TURN * dt)));
        if (m.landing <= 0)
            target = push < .9f ? WALK : RUN;
    }
    m.speed += std::clamp(target - m.speed, -20 * dt, 20 * dt);
    m.moving = m.speed > .3f;
    if (m.moving) // NATIVE: the camera swings in behind him as he goes
        m.view = unit(turned(m.view, m.up, angle_to(m.view, m.forward) * std::min(1.f, CHASE * dt)));
    if (!m.air && m.landing <= 0 && (pad.pressed & hig::Cross)) {
        m.air = 1;
        m.jump = mul(m.up, JUMP);
        animation_time = 0;
        audio_events.push_back({0xf4f95945u}); // ratchet-JUMP (0x20cc80; the landing's 0x20cc88 is not loaded with it)
    }
    V step = mul(m.forward, m.speed * dt);
    if (m.air) {
        m.jump = sub(m.jump, mul(m.up, GRAVITY * dt));
        step = add(step, mul(m.jump, dt));
        if (m.air == 1 && dot(m.jump, m.up) < 0)
            m.air = 2;
    }
    // walls: the sphere stops at one and slides along it
    if (dot(step, step) > 1e-10f) {
        const V centre = add(position, mul(m.up, .75f));
        RayHit wall = terrain_.sweep_sphere(centre, add(centre, step), .4f);
        if (wall.hit) {
            const V rest = mul(step, 1 - wall.fraction);
            V slide = sub(rest, mul(wall.normal, dot(rest, wall.normal)));
            const V stopped = add(centre, mul(step, std::max(0.f, wall.fraction - .02f)));
            if (dot(slide, slide) > 1e-10f && terrain_.sweep_sphere(stopped, add(stopped, slide), .4f).hit)
                slide = {};
            step = add(mul(step, std::max(0.f, wall.fraction - .02f)), slide);
        }
        position = add(position, step);
    }
    if (!m.air) {
        const V top = add(position, m.up);
        const RayHit under = terrain_.ray(top, sub(top, mul(m.up, 3)));
        if (!magnetic(under) || under.fraction * 3 - 1 >= .5f) {
            // off the magnetic strip: on plain ground he walks on, else he falls
            const bool ground = under.hit && under.fraction * 3 - 1 < .5f && under.normal[1] > .55f && m.up[1] > .55f;
            if (ground)
                position = under.point;
            leave(ground);
            return;
        }
        position = under.point;
        m.normal = under.normal;
    } else if (m.air == 2) {
        const V top = add(position, mul(m.up, .75f));
        const RayHit under = terrain_.ray(top, sub(top, mul(m.up, 3)));
        if (!magnetic(under)) {
            leave(false);
            return;
        }
        if (under.fraction * 3 - .75f < 0) {
            const float fall = dot(m.jump, m.up);
            position = under.point;
            m.normal = under.normal;
            m.landing = fall <= -.38f * 60 ? .233f : fall <= -.156f * 60 ? .1f : 0;
            m.air = 0;
            m.jump = {};
            animation_time = 0;
        }
    }
    if (std::hypot(m.forward[0], m.forward[2]) > .05f)
        heading = std::atan2(m.forward[0], m.forward[2]);
    velocity = {};
    grounded = !m.air;
    if (getenv("SM_MAGNE_DEBUG"))
        std::printf("MagneTick: p=%.2f,%.2f,%.2f f=%.2f,%.2f,%.2f v=%.2f,%.2f,%.2f up=%.2f,%.2f,%.2f stick=%.2f,%.2f speed=%.2f\n", position[0], position[1],
                    position[2], m.forward[0], m.forward[1], m.forward[2], m.view[0], m.view[1], m.view[2], m.up[0], m.up[1], m.up[2],
                    pad.axes[0], pad.axes[1], m.speed);
}
void Gameplay::tick_slide() {
    auto &s = slide_;
    constexpr int steps = 2;
    // the original's spring: velocity = (goal - value) * gain - velocity * .2, held within .4 a step
    auto ease = [](float &value, float &rate, float goal, float gain) {
        for (int i = 0; i < steps; i++) {
            rate = std::clamp((goal - value) * gain - rate * .2f, -.4f, .4f);
            value += rate;
        }
    };
    // the facing's spring keeps its velocity: velocity += (goal - value) * .1 - velocity * .2
    auto turn = [](float &value, float &rate, float goal) {
        for (int i = 0; i < steps; i++) {
            rate = std::clamp(rate + (goal - value) * .1f - rate * .2f, -.4f, .4f);
            value += rate;
        }
    };
    const V before = position;
    if (s.phase == 1) {
        s.distance += s.speed;
        // the point that far along the path, the segment it is on, and what is left of the path
        float along = s.distance, total = 0;
        for (size_t i = 0; i + 1 < s.path.size(); i++) {
            const V d = sub(s.path[i + 1], s.path[i]);
            total += std::sqrt(dot(d, d));
        }
        V goal = s.path.back();
        size_t segment = s.path.size() - 2;
        bool ended = true;
        for (size_t i = 0; i + 1 < s.path.size(); i++) {
            const V d = sub(s.path[i + 1], s.path[i]);
            const float length = std::sqrt(dot(d, d));
            if (along <= length && length > 1e-6f) {
                goal = add(s.path[i], mul(d, along / length));
                segment = i;
                ended = false;
                break;
            }
            along -= length;
        }
        if (ended || total - s.distance < 6) {
            // off the slide: he eases to a place six metres ahead, at the height of the path's end
            s.phase = 2;
            s.forward_goal[1] = 0;
            s.exit = {position[0] + s.forward[0] * 6, s.path.back()[1], position[2] + s.forward[2] * 6};
        } else {
            for (int k = 0; k < 3; k++)
                ease(position[k], s.spring[k], goal[k], .1f);
            s.forward_goal = unit(sub(s.path[segment + 1], s.path[segment]));
        }
    }
    if (s.phase == 2) {
        for (int k = 0; k < 3; k++)
            ease(position[k], s.spring[k], s.exit[k], .05f);
    }
    for (int k = 0; k < 3; k++)
        turn(s.forward[k], s.forward_spring[k], s.forward_goal[k]);
    s.forward = unit(s.forward);
    heading = std::atan2(s.forward[0], s.forward[2]);
    slide_pitch = -std::asin(std::clamp(s.forward[1], -1.f, 1.f));
    velocity = mul(sub(position, before), 30);
    grounded = true;
    if (s.phase == 2 && std::fabs(s.spring[0]) < .01f && std::fabs(s.spring[1]) < .01f && std::fabs(s.spring[2]) < .01f) {
        // he has come to rest: back to standing (state 0, the idle clip)
        motion = Motion::Idle;
        animation_time = 0;
        velocity = {};
        slide_pitch = 0;
        s = {};
        // the ground under him, should the eased place float above it or sink below
        const auto floor = terrain_.ray(add(position, V{0, 1.5f, 0}), add(position, V{0, -3, 0}));
        if (floor.hit)
            position[1] = floor.point[1];
    }
}
void Gameplay::carry(V delta) { position=add(position,delta); carried=add(carried,delta); }
void Gameplay::relocate(V p) {
    pack_jump_ = Motion::Idle;
    position = previous_ = p;
    velocity = {};
    grounded = false;
    swimming = wading = strafing = false;
    reset_swim();
    heli_state_=heli_clip_=0;heli_time_=0;heli_rate_=1;
    heli_pose_.clear();previous_heli_pose_.clear();heli_transition_.reset();
    motion = Motion::Idle;
    animation_time = 0;
    fire_time_ = attachment_time_ = 0;
    run_rate_ = 1;
    jumps = 0;
    swim_exit_ = hurt_time_ = action_time_ = 0;
    cancel_wrench();
    knockback_ = {};
    reset_pose();
}
void Gameplay::reset_swim() {
    underwater=false;swim_pitch=previous_swim_pitch_=0;
    swim_source_={};swim_remainder_=0;swim_frame_=0;
}
bool Gameplay::camera_underwater(V eye,bool previous) const {
    // Original SM ocean height, host plane adapter. Hysteresis prevents tint
    // flicker while the rendered camera crosses the surface independently.
    return has_water_&&std::isfinite(eye[1])&&eye[1]<water_height+(previous?.04f:-.04f);
}
void Gameplay::move_underwater(const hig::PadState& pad,float analog,float desired_heading,float dt) {
#ifdef SM_PC_STANDALONE // the dive is Up Your Arsenal's (extras.h): he is never under water in this build
    (void)pad, (void)analog, (void)desired_heading, (void)dt;
#else
    auto& h=swim_source_;
    ps2game::PadInput input;input.square=(pad.raw.buttons&hig::Square)!=0;input.cross=(pad.raw.buttons&hig::Cross)!=0;
    input.r1=(pad.raw.buttons&hig::R1)!=0;input.r2=(pad.raw.buttons&hig::R2)!=0;
    h.water.hydro_available=hydro_pack_enabled;
    h.analog_magnitude=std::clamp(analog,0.f,1.f);h.desired_facing=desired_heading;
    h.in_water=true;h.water_surface_z=water_height;h.group=0x11;h.health=int(health);
    // UYA 004f27f8 source dynamics run at 60 Hz. Position is SM Y-up;
    // source velocities are units/tick, host velocities are units/second.
    // The source's 30-marker stroke curve is timed to SM's authored SWIM clip.
    float duration=1.f;
    if(hero_animations_&&hero_animations_->clips.size()>38&&hero_animations_->clips[38].duration>0)
        duration=hero_animations_->clips[38].duration;
    swim_remainder_+=double(dt);
    const int steps=int(std::floor(swim_remainder_*60+1e-6));
    swim_remainder_=std::max(0.,swim_remainder_-double(steps)/60);
    for(int i=0;i<steps;++i){
        h.position={position[2],position[0],position[1]};
        h.animation_clock.frame_a=int(swim_frame_);h.animation_clock.frame_b=(int(swim_frame_)+1)%30;
        h.animation_clock.t=swim_frame_-std::floor(swim_frame_);
        h.animation_clock.inv_duration=30.f/(duration*60.f);
        const auto requested=ps2game::water_select(h,input);
        if(requested==ps2game::HeroMode::Swim||requested==ps2game::HeroMode::SwimCoast||requested==ps2game::HeroMode::SwimFast){
            if(requested!=h.mode){h.previous_mode=h.mode;h.mode=requested;h.state_ticks=0;ps2game::water_enter(h);}
        }
        ps2game::water_move(h,input);
        velocity={h.velocity.y*60,h.velocity.z*60,h.velocity.x*60};
        const V before=position;
        terrain_.move(position, velocity, 1.f/60, grounded, body_radius(), body_height());
        h.displacement={position[2]-before[2],position[0]-before[0],position[1]-before[1]};
        grounded=false;++h.state_ticks;
        const float next=swim_frame_+h.animation.rate*h.animation_clock.inv_duration;
        h.animation_clock.wrapped=next>=30;swim_frame_=std::fmod(next,30.f);
    }
    heading=h.facing;swim_pitch=h.swim_pitch;
    if(!h.water.diving&&!input.square&&velocity[1]>0&&position[1]>water_height-.4f){
        reset_swim();velocity[1]=0; // surface buoyancy resumes; a new Cross press jumps out.
    }
#endif
}
void Gameplay::configure_animations(const hig::AnimationBank &bank, const hig::ActorModel *hero) {
    player_assets_ = nullptr;
    hero_model_ = hero;
    hero_animations_ = &bank;
    for (unsigned i = 0; i < 3; i++)
        if (bank.clips.size() > 12 + i && bank.clips[12 + i].duration > 0)
            wrench_duration_[i] = bank.clips[12 + i].duration;
    if (bank.clips.size() > 55) {
        throw_duration_ = bank.clips[55].duration;
        hyper_duration_ = bank.clips[10].duration;
    }
    reset_pose();
}
void Gameplay::cancel_wrench() {
    wrench_time_ = throw_time_ = -1;
    wrench_queued_ = thrown_ = throw_released_ = false;
    attack_speed_ = attack_derivative_ = attack_turn_derivative_ = 0;
    melee_hits_.clear();
    melee_target_hits_.clear();
    melee_previous_valid_ = false;
    hyper_phase_ = 0;
    throw_hits_.clear();
    throw_target_hits_.clear();
}
void Gameplay::attack_events(unsigned clip, float before, float after) {
    if (!hero_animations_ || clip >= hero_animations_->clips.size())
        return;
    for (const auto &event : hero_animations_->clips[clip].events)
        if (event.sound && (event.time > before || (before == 0 && event.time == 0)) &&
            event.time <= after)
            audio_events.push_back({event.sound});
}
void Gameplay::begin_wrench(unsigned stage) {
    if (wrench_time_ < 0) {
        attack_speed_ = std::hypot(velocity[0], velocity[2]) / 60.f;
        attack_derivative_ = attack_turn_derivative_ = 0;
    }
    wrench_stage_ = stage;
    // 0x3fd78/0x3fdf8/0x3fe44: first swing seeks .4s, later swings start at 0.
    wrench_time_ = stage == 0 ? .4f : 0;
    wrench_queued_ = false;
    melee_hits_.assign(enemies.size(), 0), melee_target_hits_.clear();
    melee_previous_valid_ = false;
    action_time_ = 0;
    wrench_swings++;
    audio_events.push_back({clank ? 0x82061018u : 0x09b077b5u}); // Clank's is level-PUNCH_WHOOSH
}
void Gameplay::melee_contacts() {
    const bool swinging = wrench_time_ >= 0;
    if ((!swinging && hyper_phase_ < 2) || !encounter_assets_ || thrown_)
        return;
    std::array<V, 2> centers;
    float radius = .25f;
    if (player_assets_ && !pose_.empty() && player_assets_->wrench.bones.size() > 9) {
        std::vector<hig::Matrix> joints, skin, weapon_joints;
        hig::pose_matrices(player_assets_->ratchet, pose_, joints, skin);
        if (joints.size() <= 56)
            return;
        auto grip = hig::multiply(joints[56], actor_transform(position, heading));
        float time =
            swinging ? std::max(0.f, wrench_time_ - hig::wrench_swings[wrench_stage_].deform_begin)
                     : hyper_weapon_time_;
        const bool deform = swinging
                                ? wrench_time_ >= hig::wrench_swings[wrench_stage_].deform_begin
                                : hyper_phase_ == 3;
        std::vector<hig::BonePose> weapon_pose;
        hig::sample_animation(player_assets_->wrench,
                              &player_assets_->wrench_animations.clips[deform ? 3 : 0], time, false,
                              weapon_pose);
        hig::pose_matrices(player_assets_->wrench, weapon_pose, weapon_joints, skin);
        // 0x130c90: joints 3 and 9, using joint 3's Y for both endpoints.
        V a{weapon_joints[3][12], weapon_joints[3][13], weapon_joints[3][14]};
        V b{weapon_joints[9][12], a[1], weapon_joints[9][14]};
        a = hig::transform_point(a, grip);
        b = hig::transform_point(b, grip);
        auto span = sub(b, a);
        radius = std::min(.25f, std::sqrt(dot(span, span)) * .25f);
        centers = {add(a, mul(span, .25f)), add(a, mul(span, .75f))};
    } else {
        // Geometry-free controller harnesses have no skeleton. Live play always
        // supplies the original model; this fallback is not a retail volume.
        V forward{std::sin(heading), 0, std::cos(heading)};
        centers = {add(add(position, V{0, .65f, 0}), mul(forward, .5f)),
                   add(add(position, V{0, .65f, 0}), mul(forward, 1.f))};
    }
    std::array<V, 4> contacts{centers[0], centers[1], centers[0], centers[1]};
    if (melee_previous_valid_)
        for (size_t k = 0; k < 2; ++k)
            contacts[k + 2] = mul(add(centers[k], melee_previous_[k]), .5f);
    melee_previous_ = centers;
    melee_previous_valid_ = true;
    if (swinging && !hig::wrench_contact_active(wrench_stage_, wrench_time_))
        return;
    melee_hits_.resize(enemies.size());
    for (size_t i = 0; i < enemies.size(); ++i) {
        auto &e = enemies[i];
        if (e.hidden || e.health <= 0 || melee_hits_[i] ||
            e.model >= encounter_assets_->models.size())
            continue;
        const auto &m = encounter_assets_->models[e.model];
        // Target-class collision primitives remain a native upright capsule.
        const float target_radius = m.radius * e.scale, height = m.height * e.scale;
        V low = add(e.position, V{0, std::min(height * .5f, target_radius), 0});
        V high = add(e.position, V{0, std::max(height * .5f, height - target_radius), 0});
        for (auto contact : contacts) {
            V on_weapon, on_target;
            closest_segments(contact, contact, low, high, on_weapon, on_target);
            V d = sub(contact, on_target);
            if (dot(d, d) > (radius + target_radius) * (radius + target_radius))
                continue;
            if (terrain_.ray(add(position, V{0, .65f, 0}), contact).hit)
                continue;
            // Broad target capsules can overlap a wall even while the target
            // itself is on its far side. Confirm the contact reaches its body.
            V body{e.position[0], std::clamp(contact[1], low[1], high[1]), e.position[2]};
            if (terrain_.ray(contact, body).hit)
                continue;
            melee_hits_[i] = 1;
            if (const float dealt = armor_wrench_hit(i, hig::wrench_base_damage * (hyper_phase_ == 3 ? hig::wrench_hyper_multiplier : 1.f), false);
                dealt > 0)
                hurt_enemy(i, dealt);
            ++impacts;
            audio_events.push_back({0xbe624601});
            break;
        }
    }
    // World targets (crates, breakables): one hit a swing each. Their own solid boxes stand between Ratchet and
    // the contact, so there is no wall test here.
    for (const auto &t : targets) {
        if (std::find(melee_target_hits_.begin(), melee_target_hits_.end(), t.id) != melee_target_hits_.end())
            continue;
        for (auto contact : contacts) {
            V d = sub(contact, t.center);
            const float reach = radius + t.radius;
            if (dot(d, d) > reach * reach)
                continue;
            melee_target_hits_.push_back(t.id);
            const bool hyper = hyper_phase_ == 3;
            target_hits.push_back({t.id, hyper ? HitKind::HyperStrike : HitKind::Wrench,
                                   hig::wrench_base_damage * (hyper ? hig::wrench_hyper_multiplier : 1.f),
                                   position});
            ++impacts;
            break;
        }
    }
}
void Gameplay::tick_thrown_wrench(float dt) {
    if (!thrown_)
        return;
    throw_previous_ = throw_position_;
    throw_previous_spin_ = throw_spin_;
    bool returning = throw_progress_ >= .5f;
    // 0x130290..0x1302f0 increments once per weapon update, independent of present FPS.
    throw_progress_ = std::min(1.f, throw_progress_ + hig::wrench_throw_step * dt * 30);
    throw_spin_ += hig::wrench_throw_spin * dt * 30;
    const auto &pivot = hig::wrench_throw_pivot;
    float c = std::cos(throw_spin_), s = std::sin(throw_spin_);
    // 0x1241b8 applies the 3x3 Y rotation built at 0x1240c0 to this pivot.
    V offset{pivot[0] * c + pivot[2] * s, pivot[1], -pivot[0] * s + pivot[2] * c};
    offset = hig::transform_point(offset, actor_transform({}, heading));
    V end = throw_progress_ >= .5f ? add(position, V{0, .3f, 0}) : add(throw_origin_, offset);
    V next = add(end, mul(sub(throw_target_, end), hig::wrench_throw_curve(throw_progress_)));
    for (const auto &t : targets) { // world targets: once on the way out, once on the way back
        const uint32_t key = t.id * 4 + (returning ? 2u : 1u);
        float f;
        if (std::find(throw_target_hits_.begin(), throw_target_hits_.end(), key) == throw_target_hits_.end() &&
            segment_sphere(throw_position_, next, t.center, t.radius + .3f, f)) {
            throw_target_hits_.push_back(key);
            target_hits.push_back({t.id, HitKind::WrenchThrow, hig::wrench_base_damage, throw_position_});
            impacts++;
        }
    }
    auto wall = terrain_.sweep_sphere(throw_position_, next, .3f);
    if (wall.hit && !returning) {
        next = add(throw_position_,
                   mul(sub(next, throw_position_), std::max(0.f, wall.fraction - .01f)));
        // 0x130514 reflects the outbound phase when the weapon hits an obstruction.
        throw_progress_ = 1 - throw_progress_;
        // Preserve the collision point when the return endpoint changes to the
        // player. The native sphere sweep replaces the retail collision callbacks.
        end = add(position, V{0, .3f, 0});
        throw_target_ =
            add(end, mul(sub(next, end),
                         1 / std::max(.0001f, hig::wrench_throw_curve(throw_progress_))));
        impacts++;
        audio_events.push_back({0xbe624601});
    }
    uint8_t leg = throw_progress_ >= .5f ? 2 : 1;
    if (encounter_assets_) {
        throw_hits_.resize(enemies.size());
        for (size_t i = 0; i < enemies.size(); i++) {
            const auto &e = enemies[i];
            if (!e.health || e.hidden || (throw_hits_[i] & leg))
                continue;
            const auto &m = encounter_assets_->models[e.model];
            float f;
            if (segment_sphere(throw_position_, next,
                               add(e.position, V{0, m.height * e.scale * .5f, 0}),
                               std::max(m.radius * e.scale, m.height * e.scale * .5f) + .25f, f) &&
                !terrain_
                     .ray(throw_position_, add(throw_position_, mul(sub(next, throw_position_), f)))
                     .hit) {
                throw_hits_[i] |= leg;
                if (const float dealt = armor_wrench_hit(i, hig::wrench_base_damage, true); dealt > 0)
                    hurt_enemy(i, dealt);
                impacts++;
                audio_events.push_back({0xbe624601});
            }
        }
    }
    throw_position_ = next;
    armor_throw_tick();
    if (throw_progress_ >= 1 ||
        (throw_progress_ >= .5f && dot(sub(next, end), sub(next, end)) < 1)) {
        thrown_ = false;
        if (throw_time_ >= 0)
            throw_time_ = 2.66666675f; // 0x1c7844: resume the authored catch/recovery
    }
}
void Gameplay::start_enemies(const World &world, const EncounterAssets &assets) {
    encounter_assets_ = &assets;
    world_ = &world;
    enemies.clear();
    auto word = [](const std::vector<uint8_t> &b, size_t p) {
        uint32_t v = 0;
        if (p + 4 <= b.size())
            std::memcpy(&v, b.data() + p, 4);
        return v;
    };
    auto real = [&](const std::vector<uint8_t> &b, size_t p, float fallback) {
        auto bits = word(b, p);
        float v;
        std::memcpy(&v, &bits, 4);
        return std::isfinite(v) && v > 0 ? v : fallback;
    };
    for (const auto &instance : world.level.bog_instances) {
        const auto &def = world.level.bog_defs.at(instance.model_def);
        for (size_t i = 0; i < assets.models.size(); i++)
            if (assets.models[i].hash == def.hash) {
                const auto &m = assets.models[i];
                if (instance.matrix[12] == 0 && instance.matrix[13] == 0 && instance.matrix[14] == 0)
                    break; // a pooled spare at the origin (Pokitaru: ten robot heads), not a placement
                Enemy e;
                e.model = unsigned(i);
                e.health = m.health;
                e.position = e.previous =
                    e.home = {instance.matrix[12], instance.matrix[13], instance.matrix[14]};
                e.heading = e.previous_heading =
                    std::atan2(instance.matrix[8], instance.matrix[10]);
                e.source_offset = instance.source_offset;
                e.scale = std::hypot(instance.matrix[0],
                                     std::hypot(instance.matrix[1], instance.matrix[2]));
                if (m.kind == EnemyKind::Crab)
                    e.scale *= real(instance.properties, 0x44, 1);
                if (m.kind == EnemyKind::Torso) {
                    e.alert_radius = real(instance.properties, 0x44, 10);
                    e.hidden = instance.properties.size() > 0x5b && instance.properties[0x5b] != 0;
                }
                if (m.kind == EnemyKind::Head)
                    e.alert_radius = real(instance.properties, 0x40, 10);
                if (m.kind == EnemyKind::Generic) {
                    const hig::ClassProps props{instance.properties, assets.objects.record(def.hash)};
                    e.detect_area = props.u32("detectArea");
                    e.hovers = props.has("yFloor");
                    if (props.has("junkController")) {
                        e.stationed = true;
                        e.station = props.u32("junkController");
                        e.health = 1; // any hit of his is its end (its event, 0x16dda8)
                    }
                    if (props.f32("scale") > .05f && props.f32("scale") < 20)
                        e.scale *= props.f32("scale");
                    if (props.f32("alertRadius") > .5f)
                        e.alert_radius = props.f32("alertRadius");
                }
                auto ground =
                    terrain_.ray(add(e.position, V{0, 1, 0}), add(e.position, V{0, -4, 0}));
                if (ground.hit && ground.normal[1] > .55f) {
                    e.position[1] = ground.point[1] + .005f;
                    e.previous = e.home = e.position;
                    e.grounded = true;
                }
                enemies.push_back(e);
                break;
            }
    }
}
void Gameplay::hurt_enemy(size_t index, float damage) {
    auto &e = enemies.at(index);
    if (e.state == EnemyState::Dead || e.hidden)
        return;
    const auto &m = encounter_assets_->models[e.model];
    e.health -= std::min(e.health, damage);
    e.time = 0;
    e.action_fired = false;
    if (!e.health) {
        e.state = EnemyState::Dead;
        e.velocity = {};
        defeated++;
        if (drop_bolts)
            drops.push_back({add(e.position, V{0, m.height * e.scale * .5f, 0}), m.bolt_reward});
        else
            bolts += m.bolt_reward;
        award_xp(m.xp_reward);
    } else
        e.state = EnemyState::Hurt;
}
bool Gameplay::segment_sphere_hit(V from, V to, V center, float radius, float &fraction) {
    return segment_sphere(from, to, center, radius, fraction);
}
void Gameplay::configure_weapons(const hig::WeaponTables *tables, unsigned level_number, bool everything) {
    const bool first = !weapon_tables_;
    weapon_tables_ = tables && tables->weapons.size() == hig::WeaponCount ? tables : nullptr;
    level_number_ = level_number;
    if (!weapon_tables_)
        return;
    for (unsigned i = 0; i < hig::WeaponCount; i++) {
        const auto &d = weapon_tables_->weapons[i];
        // a new game holds the Lacerator and the Acid Bomb Glove (the two with first level 1)
        const bool given = everything ? i != hig::WeaponMiniTurretGlove : d.type == 2 && d.first_level == 1;
        if (given && !arsenal.owned[i]) {
            arsenal.owned[i] = true;
            // the Suck Cannon's ammunition is what it has swallowed: it starts empty
            arsenal.ammo[i] = i == hig::WeaponSuckCannon ? 0 : d.tier(arsenal.level[i]).max_ammo;
        }
    }
    if (first || !arsenal.owned[arsenal.equipped]) {
        arsenal.equipped = hig::WeaponBlaster;
        ammo = arsenal.ammo[arsenal.equipped];
    }
    max_ammo = equipped_tier()->max_ammo;
    ammo = std::min(ammo, max_ammo);
}
const hig::WeaponTier *Gameplay::equipped_tier() const {
    if (!weapon_tables_)
        return nullptr;
    return &weapon_tables_->weapons[arsenal.equipped].tier(arsenal.level[arsenal.equipped]);
}
const char *Gameplay::equipped_name() const {
    return weapon_tables_ ? weapon_tables_->weapons[arsenal.equipped].name.c_str() : "blaster";
}
bool Gameplay::grant_weapon(unsigned index, bool equip_it) {
    if (!weapon_tables_ || index >= hig::WeaponCount)
        return false;
    if (!arsenal.owned[index]) {
        arsenal.owned[index] = true;
        arsenal.ammo[index] = weapon_tables_->weapons[index].tier(arsenal.level[index]).max_ammo;
    }
    return !equip_it || equip(index);
}
std::vector<unsigned> Gameplay::vendor_stock() const {
    std::vector<unsigned> out;
    if (!weapon_tables_)
        return out;
    for (unsigned i = 0; i < hig::WeaponCount; i++) {
        const auto &d = weapon_tables_->weapons[i];
        if (arsenal.owned[i] || !d.tier(0).price)
            continue;
        if ((d.type == 2 && d.first_level && d.first_level <= level_number_) || (i == hig::GadgetHypershot && level_number_ >= 2))
            out.push_back(i);
    }
    return out;
}
unsigned Gameplay::price(unsigned index) const {
    return weapon_tables_ && index < hig::WeaponCount ? weapon_tables_->weapons[index].tier(0).price : 0;
}
bool Gameplay::buy(unsigned index) {
    const unsigned cost = price(index);
    if (!weapon_tables_ || index >= hig::WeaponCount || arsenal.owned[index] || bolts < cost)
        return false;
    bolts -= cost;
    return grant_weapon(index, true);
}
void Gameplay::launch(V v, float gravity) {
    launch_ = true;
    launch_gravity_ = gravity;
    launch_velocity_ = v;
    velocity = v;
    grounded = false;
    position[1] += .05f;
    jumps = 2;
    cancel_wrench();
    motion = Motion::Jump;
    animation_time = 0;
}
bool Gameplay::equip(unsigned next) {
    if (!weapon_tables_ || next >= hig::WeaponCount || !arsenal.owned[next])
        return false;
    arsenal.ammo[arsenal.equipped] = ammo;
    arsenal.equipped = next;
    ammo = arsenal.ammo[next];
    max_ammo = equipped_tier()->max_ammo;
    fire_cooldown_ = std::max(fire_cooldown_, .25f);
    return true;
}
void Gameplay::award_xp(float amount) {
    if (!weapon_tables_ || amount <= 0)
        return;
    const unsigned w = arsenal.equipped;
    const auto &d = weapon_tables_->weapons[w];
    const uint32_t cost = d.tier(arsenal.level[w]).xp_next;
    if (!cost || arsenal.level[w] + 1u >= d.tiers.size())
        return; // no further level this way (the top level is bought)
    // 0x250f8: scaled by how far this level is past the weapon's first, capped at a third of the cost
    const int ahead = int(level_number_) - int(d.first_level) - int(arsenal.level[w]);
    amount = std::min(amount * float(d.first_level ? std::max(1, ahead) : 1), std::floor(float(cost) * .333333f));
    arsenal.xp[w] += amount;
    if (arsenal.xp[w] > float(cost)) {
        arsenal.xp[w] -= float(cost);
        arsenal.level[w]++;
        level_ups++;
        if (w == arsenal.equipped) {
            max_ammo = equipped_tier()->max_ammo;
            ammo = max_ammo; // a level-up refills the weapon
        }
    }
}
float Gameplay::shot_damage() const {
    if (!weapon_tables_)
        return 1; // bare harnesses: the first level's value
    // 0x14eba0: the Lacerator's shot carries damage row 0x29 + the weapon's level (1, 3, 5, then 4 a shot of the pair)
    const size_t row = 0x29 + std::min<unsigned>(arsenal.level[hig::WeaponBlaster], 3);
    return row < weapon_tables_->damage.size() ? weapon_tables_->damage[row].amount : 1;
}
void Gameplay::blast(V center, float radius, float damage) {
    if (!encounter_assets_)
        return;
    for (size_t i = 0; i < enemies.size(); i++) {
        const auto &e = enemies[i];
        if (e.hidden || e.state == EnemyState::Dead)
            continue;
        const auto &m = encounter_assets_->models[e.model];
        V d = sub(add(e.position, V{0, m.height * e.scale * .5f, 0}), center);
        if (dot(d, d) <= radius * radius)
            hurt_enemy(i, damage);
    }
}
void Gameplay::hurt_player(unsigned damage, V from, bool absolute) {
    if (invulnerable_ > 0 || !health)
        return;
    if (shield_time_ > 0 && damage < health)
        return; // the Static Barrier takes it (a death from the world still kills)
    // The armour takes its share off (LEVEL_02 0x40330: damage * (1 - protection), on the default Ratchet only).
    // The original's health is a float; here it is whole points, so the fraction is carried to the next hit.
    if (!absolute && (!player_assets_ || (player_assets_->hero_skin == 0 && !player_assets_->ps2)))
        if (const float protection = armor_protection(); protection > 0) {
            const float taken = float(damage) * (1 - protection) + armor_carry_;
            const unsigned whole = std::min(damage, unsigned(taken));
            armor_carry_ = taken - float(whole);
            armor_blocked += damage - whole;
            armor_absorb(float(damage) * protection);
            damage = whole;
        }
    unsigned hit = std::min(health, damage);
    health -= hit;
    damage_taken += hit;
    invulnerable_ = 1.1f;
    action_time_ = 0;
    cancel_wrench();
    end_interaction();
    if (clank) // level-CLANK_DAMAGE, level-DEATH
        audio_events.push_back({health ? 0xd0dd94bdu : 0x0d8df1a1u});
    else
        audio_events.push_back({health ? 0x2b583bc3u : 0xced82c61u});
    if (!health) {
        motion = Motion::Dead;
        animation_time = 0;
        death_time_ = 1.8f;
        return;
    }
    // Small native contact response; retail damage/knockback tuning is still being recovered.
    V away = unit(sub(position, from));
    knockback_ = mul(away, 3);
    hurt_time_ = .3f;
}
void Gameplay::tick_enemies(float dt) {
    if (!encounter_assets_)
        return;
    for (size_t index = 0; index < enemies.size(); index++) {
        auto &e = enemies[index];
        const auto &m = encounter_assets_->models[e.model];
        e.previous = e.position;
        e.previous_heading = e.heading;
        if (e.hidden)
            continue;
        if (e.dormant && e.state != EnemyState::Dead) {
            if (e.state == EnemyState::Idle || e.health >= 1e9f)
                continue;
            e.dormant = false; // hurt awake
        }
        if (e.frozen > 0 && e.state != EnemyState::Dead) { // in a block of ice: it neither moves nor acts
            e.frozen -= dt;
            continue;
        }
        // the Chameleon set's wearer on the move: enemies take him to be where he was a moment ago
        const V seen = armor_ghost();
        V to = sub(seen, e.position);
        float distance = std::hypot(to[0], to[2]);
        if (distance > 70 && e.state != EnemyState::Dead)
            continue;
        auto state = [&](EnemyState s) {
            if (e.state != s) {
                e.state = s;
                e.time = 0;
                e.action_fired = false;
            }
        };
        const auto &old_clip = m.animations.clips[enemy_clip(m, e.state)];
        float old_time = e.time;
        e.time += dt;
        e.cooldown = std::max(0.f, e.cooldown - dt);
        for (const auto &event : old_clip.events)
            if (event.time >= old_time && event.time < e.time && event.sound && distance < 18)
                audio_events.push_back({event.sound});
        if (e.state == EnemyState::Dead) {
            continue;
        }
        if (e.entry_at < e.entry.size()) {
            // Its way in (the classes' `initPath`: a Pygmy's 0x1c3b08 and a StoneIdol's walk it before anything else;
            // NATIVE here: at its running speed, straight from point to point, through whatever stands there).
            if (e.entry_trigger && world_) {
                bool inside = false;
                for (const auto &t : world_->level.triggers)
                    if (t.source_offset == e.entry_trigger)
                        inside = hig::trigger_contains(t, position);
                if (!inside)
                    continue;
                e.entry_trigger = 0;
            }
            const V goal = e.entry[e.entry_at], way = sub(goal, e.position);
            const float left = std::sqrt(dot(way, way)), step = m.speed * dt;
            if (left <= step + .05f) {
                e.position = goal;
                e.entry_at++;
            } else {
                e.position = add(e.position, mul(way, step / left));
                e.heading = std::atan2(way[0], way[2]);
            }
            e.velocity = {};
            state(EnemyState::Chase);
            if (e.entry_at >= e.entry.size())
                e.home = e.position;
            continue;
        }
        if (e.stationed) {
            // Kalidon's FactoryWorker (update 0x16df60). It works (TO_WORK, then WORKING over and over) while the
            // hero is before it - within 10 m of its height, 4 m off and .75 of its facing (0x16e478, 0x14378) - or
            // its conveyor stands still (0x157c60); else it idles. Working, its tools hurt him between .4 and
            // 1.4 s of each WORKING (the rows of its model in 0x23a198 / 0x23a1a8) when he is within .75 m of its
            // height, 2.24 m off and .8 of its facing. NATIVE: WORKING is drawn from the attack clip the
            // model was given; the swing is taken to last 1.6 s where the clip read is shorter than its own window.
            const V facing{std::sin(e.heading), 0, std::cos(e.heading)};
            const V off = sub(position, e.position);
            const float flat = std::hypot(off[0], off[2]);
            auto before = [&](float height, float reach, float cosine) {
                return health && std::fabs(off[1]) <= height && flat * flat <= reach && flat > 1e-3f &&
                       (off[0] * facing[0] + off[2] * facing[2]) / flat >= cosine;
            };
            const bool busy = before(10, 16, .75f) || e.station_busy;
            e.station_busy = false;
            if (e.state == EnemyState::Hurt)
                state(EnemyState::Idle);
            if (e.state != EnemyState::Attack) {
                if (busy)
                    state(EnemyState::Attack);
                else if (e.time >= old_clip.duration)
                    e.time = 0;
                continue;
            }
            const float swing = std::max(old_clip.duration, 1.6f);
            if (!e.action_fired && e.time >= .4f && e.time <= 1.4f && before(.75f, 5, .8f)) {
                e.action_fired = true;
                hurt_player(m.damage, e.position);
            }
            if (e.time >= swing) {
                e.time = 0;
                e.action_fired = false;
                if (!busy)
                    state(EnemyState::Idle);
            }
            continue;
        }
        V eye = add(e.position, V{0, m.height * .7f, 0}), target = add(seen, V{0, .65f, 0});
        bool sees = health && std::abs(to[1]) < 3 && !terrain_.ray(eye, target).hit;
        // A class with a detect area notices the hero inside it, wherever he stands in it.
        bool in_area = false;
        if (e.detect_area && world_ && health)
            for (const auto &t : world_->level.triggers)
                if (t.source_offset == e.detect_area) {
                    in_area = hig::trigger_contains(t, position);
                    break;
                }
        if (e.state == EnemyState::Hurt) {
            if (e.time >= old_clip.duration)
                state(EnemyState::Chase);
            continue;
        }
        if (e.state == EnemyState::Idle) {
            if (e.detect_area ? in_area : sees && distance < e.alert_radius)
                state(EnemyState::Alert);
            else if (e.time >= old_clip.duration)
                e.time = 0;
        } else if (e.state == EnemyState::Alert) {
            e.heading = std::atan2(to[0], to[2]);
            if (e.time >= old_clip.duration)
                state(EnemyState::Chase);
        } else if (e.state == EnemyState::Attack) {
            float attack_end = old_clip.duration;
            // Attack windows come from the authored sound-event markers where available.
            if (m.kind == EnemyKind::Torso) {
                for (const auto &event : old_clip.events)
                    if (event.name == "FIRE") {
                        // The third authored shot is just beyond the last pose frame.
                        attack_end = std::max(attack_end, event.time + dt);
                        if (event.time >= old_time && event.time < e.time && sees) {
                            Projectile p;
                            p.position = eye;
                            p.velocity = mul(unit(sub(target, eye)), 12.f);
                            p.life = 3;
                            p.hostile = true;
                            projectiles.push_back(p);
                        }
                    }
            } else {
                float strike = old_clip.duration * .5f;
                for (const auto &event : old_clip.events)
                    if (event.name == "ATTACK" || event.name == "attack") {
                        strike = event.time;
                        break;
                    }
                if (!e.action_fired && e.time >= strike) {
                    e.action_fired = true;
                    if (sees && distance < m.attack_distance + .25f &&
                        std::hypot(position[0] - e.position[0], position[2] - e.position[2]) < m.attack_distance + .25f)
                        hurt_player(m.damage, e.position);
                }
            }
            if (e.time >= attack_end) {
                e.cooldown = .6f;
                state(EnemyState::Chase);
            }
        } else if (e.state == EnemyState::Chase) {
            if (e.detect_area ? !in_area : !sees || distance > e.alert_radius * 1.5f) {
                state(EnemyState::Idle);
                continue;
            }
            e.heading = std::atan2(to[0], to[2]);
            if (distance <= m.attack_distance && e.cooldown <= 0) {
                state(EnemyState::Attack);
                continue;
            }
            if (distance > m.attack_distance * .85f) {
                float speed = m.speed;
                V wish = mul(unit(V{to[0], 0, to[2]}), speed);
                V next = add(e.position, mul(wish, dt));
                // Through the air at its own height: a class that flies, and one placed where the level has no
                // floor at all (Kalidon's robots wait behind the shaft's doors on nothing) until it is over ground.
                if (e.hovers || !terrain_.ray(add(e.position, V{0, .65f, 0}), add(e.position, V{0, -1.5f, 0})).hit) {
                    e.position = {next[0], e.position[1], next[2]};
                    e.velocity = {};
                    if (e.time >= old_clip.duration)
                        e.time = 0;
                    continue;
                }
                // Keep land enemies off unsupported ledges and deep ocean.
                auto ground = terrain_.ray(add(next, V{0, .65f, 0}), add(next, V{0, -1.5f, 0}));
                if (ground.hit && ground.normal[1] > .55f &&
                    (!has_water_ || ground.point[1] > water_height - .3f)) {
                    e.velocity[0] = wish[0];
                    e.velocity[2] = wish[2];
                    e.velocity[1] -= 22 * dt;
                    terrain_.move(e.position, e.velocity, dt, e.grounded);
                }
            }
            if (e.time >= old_clip.duration)
                e.time = 0;
        }
    }
}
void Gameplay::tick_projectiles(float dt) {
    std::erase_if(projectiles, [](const auto &p) { return p.life <= 0; });
    for (auto &p : projectiles) {
        if(p.age==0)p.origin=p.position;
        p.previous=p.position;
        p.age+=dt;
        V next = add(p.position, mul(p.velocity, dt));
        auto hit = terrain_.ray(p.position, next);
        float closest = hit.hit ? hit.fraction : 1.1f;
        size_t victim = enemies.size();
        bool player_hit = false;
        if (p.hostile) {
            float f;
            if (segment_sphere(p.position, next, add(position, V{0, .65f, 0}), .4f, f) &&
                f < closest) {
                closest = f;
                player_hit = true;
            }
        } else if (encounter_assets_)
            for (size_t i = 0; i < enemies.size(); i++) {
                const auto &e = enemies[i];
                if (e.state == EnemyState::Dead || e.hidden)
                    continue;
                const auto &m = encounter_assets_->models[e.model];
                float f;
                if (segment_sphere(p.position, next,
                                   add(e.position, V{0, m.height * e.scale * .5f, 0}),
                                   std::max(m.radius * e.scale, m.height * e.scale * .5f), f) &&
                    f < closest) {
                    closest = f;
                    victim = i;
                }
            }
        const Target *struck = nullptr;
        if (!p.hostile)
            for (const auto &t : targets) {
                float f;
                if (segment_sphere(p.position, next, t.center, t.radius, f) && f < closest) {
                    closest = f;
                    struck = &t;
                    victim = enemies.size();
                }
            }
        if (struck)
            target_hits.push_back({struck->id, HitKind::Shot, 1, p.origin});
        p.life -= dt;
        if (hit.hit || victim < enemies.size() || player_hit || struck) {
            V from = p.position;
            p.position = add(from, mul(sub(next, from), closest));
            p.life = 0;
            p.end_fraction=std::clamp(closest,0.f,1.f);
            impacts++;
            if(player_assets_) {
                V normal=(victim==enemies.size() && !player_hit && !struck)?hit.normal:mul(unit(p.velocity),-1);
                effects.impact(player_assets_->effects,p.position,normal,p.hostile,dt*(1-p.end_fraction));
            }
            if (victim < enemies.size())
                hurt_enemy(victim, shot_damage());
            if (player_hit)
                hurt_player(1, from);
            audio_events.push_back({0xbe624601});
        } else
            p.position = next;
    }
}
void Gameplay::tick(const hig::PadState &pad, float camera_yaw, float dt, bool defer_weapons) {
    audio_events.clear();
    if (!std::isfinite(dt) || dt <= 0 || dt > .05f)
        return;
    strafing = false;
    previous_ = position;
    previous_swim_pitch_ = swim_pitch;
    previous_slide_pitch_ = slide_pitch;
    effects.tick(dt);
    previous_heading_ = heading;
    previous_motion_ = motion;
    if (motion == Motion::Fire)
        attack_turn_limit_ = .05f; // aiming state 1, 0x4289c
    attachment_time_ += dt;
    invulnerable_ = std::max(0.f, invulnerable_ - dt);
    hurt_time_ = std::max(0.f, hurt_time_ - dt);
    swim_exit_ = std::max(0.f, swim_exit_ - dt);
    if (!health) {
        reset_swim();
        animation_time += dt;
        update_pose(dt);
        death_time_ -= dt;
        if (death_time_ <= 0) {
            relocate(spawn);
            heading = spawn_heading;
            health = max_health;
            ammo = max_ammo;
            respawns++;
            invulnerable_ = 2;
            projectiles.clear();
            effects.clear();
        }
        if (!defer_weapons) tick_weapons(pad,dt);
        return;
    }
    if (motion == Motion::Grapple) {
        animation_time += dt;
        tick_grapple(pad, camera_yaw, dt);
        tick_enemies(dt);
        tick_projectiles(dt);
        tick_shots(dt);
        update_pose(dt);
        if (!defer_weapons) tick_weapons(pad,dt);
        return;
    }
    // Ground of type 3 under him (the probe of 0x47ee8: from .75 m above his feet) puts the Magneboots to work.
    if (motion != Motion::Magne && motion != Motion::Grind && motion != Motion::Dead && motion != Motion::Grapple &&
        motion != Motion::Slide && health && !interacting() && !launch_ && velocity[1] <= .5f) {
        const RayHit under = terrain_.ray(add(position, V{0, .75f, 0}), sub(position, V{0, .35f, 0}));
        if (magnetic(under))
            begin_magne(under);
    }
    if (motion == Motion::Magne) {
        tick_magne(pad, dt);
        animation_time += dt;
        tick_enemies(dt);
        tick_projectiles(dt);
        tick_shots(dt);
        update_pose(dt);
        return;
    }
    if (motion == Motion::Grind) {
        tick_grind(pad);
        animation_time += dt;
        tick_shots(dt);
        update_pose(dt);
        return;
    }
    if (motion == Motion::Slide) {
        tick_slide();
        animation_time += dt;
        tick_enemies(dt);
        tick_projectiles(dt);
        tick_shots(dt);
        update_pose(dt);
        if (!defer_weapons) tick_weapons(pad,dt);
        return;
    }
    if (interacting()) {
        float before=animation_time;
        interaction_time_ += dt;
        animation_time += dt;
        attack_events(interaction_clip_,before,animation_time);
        tick_enemies(dt);
        tick_projectiles(dt);
        tick_shots(dt);
        update_pose(dt);
        if (!defer_weapons) tick_weapons(pad,dt);
        return;
    }
    float x = pad.axes[0], z = -pad.axes[1], len = std::hypot(x, z);
    if (len > 1) {
        x /= len;
        z /= len;
    }
    // Original world is right-handed: screen-right is forward cross up.
    V wish{-x * std::cos(camera_yaw) + z * std::sin(camera_yaw), 0,
           z * std::cos(camera_yaw) + x * std::sin(camera_yaw)};
    bool was_swimming = swimming;
    auto floor = has_water_ ? terrain_.ray(add(position, V{0, .5f, 0}),
                                           V{position[0], std::min(water_height-3,position[1]-3), position[2]})
                            : RayHit{};
    bool deep = has_water_ && (!floor.hit || floor.point[1] < water_height - .75f);
    swimming = deep && swim_exit_ <= 0 && position[1] + (underwater ? -.4f : was_swimming ? -.35f : .6f) < water_height;
    wading = has_water_ && !swimming && position[1] < water_height - .05f;
    if(!swimming)reset_swim();
    if (swimming && !was_swimming) {
        water_entries++;
        audio_events.push_back({0x5b9c5fd5});
        jumps = 0;
        action_time_ = 0;
        cancel_wrench();
    }
#ifndef SM_PC_STANDALONE // the dive is Up Your Arsenal's (extras.h)
    if(swimming&&!underwater&&(pad.raw.buttons&hig::Square)){
        underwater=true;swim_source_={};swim_source_.game=ps2game::HeroGame::Uya;
        swim_source_.mode=ps2game::HeroMode::Swim;swim_source_.previous_mode=ps2game::HeroMode::WaterIdle;
        swim_source_.group=0x11;swim_source_.facing=heading;
        swim_source_.water.hydro_available=hydro_pack_enabled;swim_source_.animation.rate=.6f;
        ps2game::water_enter(swim_source_);swim_frame_=0;swim_remainder_=0;
    }
#endif
    const bool attacking =
        wrench_time_ >= 0 || hyper_phase_ || (throw_time_ >= 0 && throw_time_ < 1.16666663f);
    strafing = strafe_enabled && (pad.raw.buttons & hig::L2) && !swimming && !attacking && hurt_time_ <= 0;
    if (!attacking) {
        if (strafing) heading = camera_yaw;
        else if (len > .05f) heading = std::atan2(wish[0], wish[2]);
    }
    if (!swimming && hurt_time_ <= 0 && (pad.pressed & hig::Square)) {
        if (wrench_time_ >= 0)
            wrench_queued_ |= hig::wrench_chain_input(wrench_stage_, wrench_time_);
        else if (!hyper_phase_ && throw_time_ < 0 && !thrown_) {
            if (!grounded) {
                melee_hits_.assign(enemies.size(), 0), melee_target_hits_.clear();
                melee_previous_valid_ = false;
                hyper_phase_ = 1;
                hyper_time_ = hyper_weapon_time_ = 0;
                velocity[1] = hig::hyper_launch * 60;
                attack_speed_ = std::hypot(velocity[0], velocity[2]) / 60;
                attack_derivative_ = 0;
                action_time_ = 0;
                hyper_strikes++;
            } else if (pad.raw.buttons & hig::L2) {
                throw_time_ = .4f;
                throw_released_ = false;
                action_time_ = 0;
                wrench_throws++;
            } else
                begin_wrench(0);
        }
    }
    bool owns_motion =
        wrench_time_ >= 0 || hyper_phase_ || (throw_time_ >= 0 && throw_time_ < 1.16666663f);
    if (grounded || swimming || owns_motion || hurt_time_ > 0 || !health)
        pack_jump_ = Motion::Idle;
    const bool crouch = (pad.raw.buttons & hig::R1) && !swimming && !wading &&
                        !owns_motion && hurt_time_ <= 0;
    float speed = swimming ? 3.2f : wading ? 3.5f : 6.f;
    if (crouch && grounded) speed = 2.f; // PC input adapter: a controllable crouch walk.
    if (!owns_motion) {
        velocity[0] = wish[0] * speed;
        velocity[2] = wish[2] * speed;
        if (pack_jump_ == Motion::LongJump) {
            velocity[0] = std::sin(pack_jump_heading_) * hig::rocket_long_speed;
            velocity[2] = std::cos(pack_jump_heading_) * hig::rocket_long_speed;
            heading = pack_jump_heading_;
        }
    }
    if (launch_) { // a CannonSprout's throw carries him
        if (launch_steer_ && len > .05f) {
            // Off a swing he is in his jump state in the original (0x40), with the stick's say over his way; how
            // much is not read. NATIVE: the stick turns his way over the ground by up to 60 degrees a second.
            const float speed = std::hypot(launch_velocity_[0], launch_velocity_[2]);
            const float now = std::atan2(launch_velocity_[0], launch_velocity_[2]);
            const float turn = std::clamp(std::remainder(std::atan2(wish[0], wish[2]) - now, 6.2831855f), -1.0471976f * dt, 1.0471976f * dt);
            launch_velocity_[0] = std::sin(now + turn) * speed;
            launch_velocity_[2] = std::cos(now + turn) * speed;
        }
        velocity[0] = launch_velocity_[0];
        velocity[2] = launch_velocity_[2];
    } else
        launch_steer_ = false;
    if (hurt_time_ > 0)
        for (int axis : {0, 2})
            velocity[axis] += knockback_[axis] * (hurt_time_ / .3f);
    if (grounded)
        jumps = 0;
    bool water_jump = swimming && !underwater && (pad.pressed & hig::Cross);
    if (water_jump) {
        swimming = false;
        swim_exit_ = .5f;
        velocity[1] = 7.5f;
        jumps = 1;
        grounded = false;
        audio_events.push_back({0x5b9c5fd5});
    }
    if (!owns_motion && !water_jump && !swimming && !launch_ && pack_jump_ == Motion::Idle && (pad.pressed & hig::Cross) &&
        (grounded || jumps < 2)) {
        audio_events.push_back({clank ? 0xadaa38b8u : jumps ? 0x090979acu : 0xf4f95945u}); // Clank's is level-JUMP
        if (grounded && crouch) {
            // Neutral C+Cross is high jump; a direction requests the long jump.
            // Require a new grounded press, never re-launch from a held button.
            pack_jump_ = len > .25f ? Motion::LongJump : Motion::HighJump;
            pack_jump_heading_ = heading;
            velocity[1] = pack_jump_ == Motion::HighJump ? hig::rocket_high_launch : hig::rocket_long_launch;
            if (pack_jump_ == Motion::LongJump) {
                velocity[0] = std::sin(heading) * hig::rocket_long_speed;
                velocity[2] = std::cos(heading) * hig::rocket_long_speed;
            }
        } else {
            // The second jump is the original's: up at .08 a pass (hero +0x114, defaults LEVEL_01 0x380e8) against
            // the light gravity of .002 a pass (+0x124, states 0xd / 0xe of 0x40338), 1.6 m in two thirds of a
            // second. The first keeps this file's bring-up numbers (the original's .17 a pass under .012 reaches
            // the same 1.2 m in less time).
            double_rise_ = jumps != 0;
            velocity[1] = jumps ? .08f * 60 : 7.2f;
        }
        grounded = false;
        jumps++;
    }
    bool glide = !crouch && !owns_motion && !swimming && !wading && swim_exit_ <= 0 && !grounded &&
                 velocity[1] < 0 && (pad.raw.buttons & hig::Cross);
    if (glide) pack_jump_ = Motion::Idle;
    if (wrench_time_ >= 0 || hyper_phase_) {
        // The retail player performs two 60 Hz movement passes per 30 Hz tick.
        // Attack drive replaces stick velocity. The capsule still resolves walls/floors.
        unsigned passes = std::max(1u, unsigned(std::lround(dt * 60)));
        float step = dt / passes;
        if (wrench_time_ >= 0 && len > .05f &&
            wrench_time_ < hig::wrench_swings[wrench_stage_].drive_begin)
            heading += hig::wrench_turn(std::atan2(wish[0], wish[2]) - heading, std::min(len, 1.f),
                                        passes, attack_turn_derivative_, attack_turn_limit_);
        for (unsigned i = 0; i < passes; i++) {
            float target = wrench_time_ >= 0
                               ? hig::wrench_drive(wrench_stage_, wrench_time_ + i * step * 1.6f)
                               : 0;
            hig::wrench_spring(target, attack_speed_, attack_derivative_);
            velocity[0] = std::sin(heading) * attack_speed_ * 60;
            velocity[2] = std::cos(heading) * attack_speed_ * 60;
            velocity[1] =
                hyper_phase_ && hyper_phase_ < 3
                    ? std::max(-.38f * 60, velocity[1] + hig::hyper_acceleration * 3600 * step)
                    : velocity[1] - 22 * step;
            terrain_.move(position, velocity, step, grounded, body_radius(), body_height());
            if (hyper_phase_ == 2 && grounded) {
                hyper_phase_ = 3;
                hyper_weapon_time_ = 0;
                hyper_impacts++;
                audio_events.push_back({0x09b077b5});
                armor_hyper_impact();
            }
        }
    } else {
        if (owns_motion)
            velocity[0] = velocity[2] = 0;
        // SWIM/TREAD already contain the authored downward root translation.
        const bool moved_underwater=underwater;
        if (moved_underwater) {
            move_underwater(pad,std::min(len,1.f),len>.05f?std::atan2(wish[0],wish[2]):heading,dt);
        } else if (swimming) {
            velocity[1] = std::clamp((water_height - position[1]) * 8.f, -3.f, 3.f);
            grounded = false;
        } else if (pack_jump_ != Motion::Idle) {
            // Retain the original two movement passes at 30 Hz, including
            // collision at each pass, so launch height is cadence independent.
            const unsigned passes = std::max(1u, unsigned(std::lround(dt * 60)));
            const float step = dt / passes;
            const float gravity = pack_jump_ == Motion::HighJump ? hig::rocket_high_gravity : hig::rocket_long_gravity;
            for (unsigned i = 0; i < passes; ++i) {
                velocity[1] = std::max(velocity[1] - gravity * step, -hig::rocket_terminal_speed);
                terrain_.move(position, velocity, step, grounded, body_radius(), body_height());
                if (grounded) break;
            }
        } else
            double_rise_ &= velocity[1] > 0 && !grounded && !launch_;
            velocity[1] -= (launch_ ? launch_gravity_ : glide ? 5.f : double_rise_ ? .002f * 3600 : 22.f) * dt; // a cannon's: 0x6a238, .006944445 a pass
        if (glide)
            velocity[1] = std::max(velocity[1], -2.2f);
        if(!moved_underwater && pack_jump_ == Motion::Idle)
            terrain_.move(position, velocity, dt, grounded, body_radius(), body_height());
    }
    if (grounded) {
        jumps = 0;
        pack_jump_ = Motion::Idle;
        launch_ = false;
    }
    const bool uya_weapon=player_assets_&&player_assets_->ps2_weapons&&player_assets_->ps2_weapons->enabled();
    if (pad.pressed & hig::Triangle) {
        if(uya_weapon) {
            auto& guns=*player_assets_->ps2_weapons;const auto& choices=guns.choices();
            const auto current=std::find_if(choices.begin(),choices.end(),[&](const auto& c){return c.id==guns.equipped();});
            if(!choices.empty()) {
                const size_t next=current==choices.end()?0:(size_t(current-choices.begin())+1)%choices.size();
                std::string error;guns.equip(choices[next].id,error);
            }
        } else if (weapon_tables_) {
            // the next owned weapon (the quick-select ring is not rebuilt yet)
            for (unsigned step = 1; step <= hig::WeaponCount; step++) {
                const unsigned next = (arsenal.equipped + step) % hig::WeaponCount;
                // the weapons, and the gadget that is held like one
                if (arsenal.owned[next] && (weapon_tables_->weapons[next].type == 2 || next == hig::GadgetSproutOMatic ||
                                            next == hig::GadgetHypershot)) {
                    equip(next);
                    break;
                }
            }
        } else weapon = 1 - weapon;
    }
    fire_cooldown_ = std::max(0.f, fire_cooldown_ - dt);
    fire_time_ += dt;
    action_time_ = std::max(0.f, action_time_ - dt);
    const bool lacerator = !weapon_tables_ || arsenal.equipped == hig::WeaponBlaster;
    if (lacerator) {
        fire_clip_ = 26;
        fire_layer_clip_ = 25;
    } else if (!uya_weapon)
        tick_arsenal(pad, dt);
    if (!uya_weapon && lacerator && !swimming && hurt_time_ <= 0 && wrench_time_ < 0 && !hyper_phase_ && throw_time_ < 0 &&
        (pad.raw.buttons & hig::Circle) && fire_cooldown_ <= 0 && ammo) {
        Projectile p;
        p.position = weapon_muzzle(1);
        // Keep the muzzle on the near side if the hand is through a wall.
        V chest=add(position,V{0,.85f,0});
        auto obstruction=terrain_.ray(chest,p.position);
        if(obstruction.hit)p.position=add(obstruction.point,mul(obstruction.normal,.02f));
        V aim{std::sin(heading), 0, std::cos(heading)};
        float nearest = 20.f;
        if (encounter_assets_)
            for (const auto &e : enemies) {
                if (e.hidden || !e.health)
                    continue;
                const auto &m = encounter_assets_->models[e.model];
                V target = add(e.position, V{0, m.height * e.scale * .55f, 0}),
                  delta = sub(target, p.position);
                float range = std::sqrt(dot(delta, delta));
                // Native aim assistance keeps low enemies reachable from the hand.
                if (range < nearest &&
                    dot(unit(V{delta[0], 0, delta[2]}),
                        V{std::sin(heading), 0, std::cos(heading)}) > .97f &&
                    std::abs(delta[1]) < 3 && !terrain_.ray(p.position, target).hit) {
                    nearest = range;
                    aim = unit(delta);
                }
            }
        p.velocity = mul(aim, weapon ? 42.f : 28.f);
        p.life = 2;
        projectiles.push_back(p);
        shots++;
        ammo--;
        audio_events.push_back({0xb78c792f});
        fire_cooldown_ = weapon ? .09f : .22f;
        fire_time_ = 0;
        action_time_ = 1.f / 3;
    }
    if (wrench_time_ >= 0) {
        // 0x3fda0 and 0x3fe68 set the animation controller's speed to 1.6.
        attack_events(12 + wrench_stage_, wrench_time_, wrench_time_ + dt * 1.6f);
        armor_swing_time(wrench_stage_, wrench_time_, wrench_time_ + dt * 1.6f);
        wrench_time_ += dt * 1.6f;
        const auto &swing = hig::wrench_swings[wrench_stage_];
        if (wrench_queued_ && wrench_time_ > swing.chain_after)
            begin_wrench(wrench_stage_ + 1);
        else if (wrench_time_ >= wrench_duration_[wrench_stage_])
            wrench_time_ = -1;
    }

    bool already_thrown = thrown_;
    if (throw_time_ >= 0) {
        float rate = throw_time_ < 1.16666663f ? 2.3f : throw_time_ < 2.33333325f ? .2f : 1.f;
        attack_events(55, throw_time_, throw_time_ + dt * rate);
        throw_time_ += dt * rate;
        if (!throw_released_ && throw_time_ >= 1.1f) {
            thrown_ = throw_released_ = true;
            throw_progress_ = throw_spin_ = throw_previous_spin_ = 0;
            throw_heading_ = heading;
            throw_origin_ = add(position, V{0, .3f, 0});
            if (hero_model_ && hero_animations_ && hero_model_->bones.size() > 56) {
                std::vector<hig::BonePose> pose;
                std::vector<hig::Matrix> joints, skin;
                hig::sample_animation(*hero_model_, &hero_animations_->clips[55], throw_time_,
                                      false, pose);
                hig::pose_matrices(*hero_model_, pose, joints, skin);
                auto grip = hig::multiply(joints[56], actor_transform(position, heading));
                throw_origin_ = {grip[12], grip[13], grip[14]};
            }
            throw_target_ = add(add(throw_origin_, hig::wrench_throw_pivot),
                                V{6 * std::sin(heading), 0, 6 * std::cos(heading)});
            throw_position_ = throw_previous_ = throw_origin_;
            throw_hits_.assign(enemies.size(), 0), throw_target_hits_.clear();
        }
        if (throw_time_ >= throw_duration_ || (throw_time_ >= 1.16666663f && len > .05f))
            throw_time_ = -1;
    }
    if (already_thrown)
        tick_thrown_wrench(dt);
    if (hyper_phase_) {
        attack_events(10, hyper_time_, hyper_time_ + dt * (hyper_phase_ == 2 ? .1f : 1.f));
        hyper_time_ += dt * (hyper_phase_ == 2 ? .1f : 1.f);
        if (hyper_phase_ == 1 && hyper_time_ >= .23333335f)
            hyper_phase_ = 2;
        if (hyper_phase_ == 3) {
            hyper_weapon_time_ += dt;
            if (hyper_time_ >= hyper_duration_)
                hyper_phase_ = 0;
        }
    }
    tick_enemies(dt);
    tick_projectiles(dt);
    tick_shots(dt);
    if (!health)
        motion = Motion::Dead;
    else if (motion == Motion::Grapple && grapple_.phase)
        ; // the Hypershot took a target this tick (tick_arsenal): the grapple state runs from the next
    else if (swimming)
        motion = len > .05f || (underwater&&std::hypot(velocity[0],velocity[1],velocity[2])>.1f) ? Motion::Swim : Motion::SwimIdle;
    else if (hurt_time_ > 0)
        motion = Motion::Hurt;
    else if (swim_exit_ > 0)
        motion = Motion::SwimJump;
    else if (hyper_phase_)
        motion = Motion::HyperStrike;
    else if (throw_time_ >= 0)
        motion = Motion::WrenchThrow;
    else if (wrench_time_ >= 0)
        motion = Motion::Wrench;
    else if (wading && grounded && len > .05f)
        motion = Motion::Wade;
    else if (!grounded)
        motion = glide ? Motion::Glide : pack_jump_ != Motion::Idle ? pack_jump_ : velocity[1] > 0 ? Motion::Jump : Motion::Fall;
    else if (action_time_ > 0)
        motion = len > .05f ? Motion::FireRun : Motion::Fire;
    else if (crouch)
        motion = Motion::Crouch;
    else
        motion = len > .05f ? Motion::Run : Motion::Idle;
    const bool changed_clip = pose_clip_ != clip_index();
    bool same_gait = (motion == Motion::Run && previous_motion_ == Motion::FireRun) ||
                     (motion == Motion::FireRun && previous_motion_ == Motion::Run);
    if (motion == Motion::Run || motion == Motion::FireRun) {
        // 0x37ba0..0x37bec performs two movement passes per 30 Hz update;
        // 0x3eee0 measures each pass's horizontal displacement for gait +0x270.
        // Convert actual native travel to that cadence without changing velocity.
        float distance = std::hypot(position[0] - previous_[0], position[2] - previous_[2]);
        run_rate_ = hig::ratchet_run_playback_rate(distance / (dt * 60.f));
    }
    animation_time = (motion == previous_motion_ && !changed_clip) || same_gait
                         ? animation_time + dt * animation_rate()
                         : 0;
    if (motion != previous_motion_) {
        if (motion == Motion::HighJump) animation_time = hig::rocket_high_start_time;
        if (motion == Motion::LongJump) animation_time = hig::rocket_long_start_time;
    }
    if (motion == Motion::Wrench)
        animation_time = wrench_time_;
    if (motion == Motion::WrenchThrow)
        animation_time = throw_time_;
    if (motion == Motion::HyperStrike)
        animation_time = hyper_time_;
    if ((motion == Motion::Glide) != (previous_motion_ == Motion::Glide)) {
        attachment_time_ = 0;
        audio_events.push_back({0x1e930d2b, motion != Motion::Glide});
    }
    update_pose(dt);
    melee_contacts();
    if (position[1] < floor_limit_) {
        relocate(spawn);
        heading = spawn_heading;
        grounded = true;
        jumps = 0;
        respawns++;
        health = max_health;
        ammo = max_ammo;
        swimming = wading = false;
        audio_events.push_back({0x1e930d2b, true});
        audio_events.push_back({0x1298e61b});
        projectiles.clear();
            effects.clear();
    }
    if (!defer_weapons) tick_weapons(pad,dt);
}
void Gameplay::tick_weapons(const hig::PadState &pad,float dt) {
    if (!player_assets_ || !std::isfinite(dt) || dt<=0 || dt>.05f) return;
    if (player_assets_->ps2_packs) player_assets_->ps2_packs->tick(*this,*player_assets_,dt);
    if (!player_assets_->ps2_weapons) return;
    const bool controls=health&&!interacting()&&!grinding()&&!swimming&&hurt_time_<=0&&wrench_time_<0&&!hyper_phase_&&throw_time_<0&&
        !(player_assets_->ps2&&player_assets_->ps2->hands_free());
    player_assets_->ps2_weapons->tick(*this,*player_assets_,pad,controls,dt);
}
void Gameplay::reset_pose() {
    pose_.clear();
    previous_pose_.clear();
    pose_transition_.reset();
    arm_transition_.reset();
    pose_clip_ = clip_index();
    pose_shots_ = shots;
    pose_time_ = motion == Motion::Fire ? fire_time_ : animation_time;
    pose_fire_layer_ = motion == Motion::FireRun;
    previous_heading_ = heading;
    update_pose(0);
    previous_pose_ = pose_;
}
void Gameplay::update_pose(float dt) {
    tick_native_heli(dt);
    const unsigned clip = clip_index();
    const float time = motion == Motion::Fire ? fire_time_ : animation_time;
    if (!hero_model_ || !hero_animations_ || clip >= hero_animations_->clips.size()) {
        pose_clip_ = clip;
        return;
    }
    previous_pose_ = pose_;
    const bool layered = motion == Motion::FireRun;
    const bool changed = clip != pose_clip_ || time + .0001f < pose_time_;
    if (changed && !pose_.empty()) {
        pose_transition_.begin(pose_, hig::ratchet_blend_duration(clip));
        arm_transition_.reset();
    }
    const bool loop = motion != Motion::Jump && motion != Motion::HighJump && motion != Motion::LongJump && motion != Motion::Wrench &&
                      motion != Motion::WrenchThrow && motion != Motion::HyperStrike &&
                      motion != Motion::Dead && motion != Motion::SwimJump &&
                      motion != Motion::Hurt && (!interacting() || (interaction_clip_!=21 && interaction_clip_!=32));
    hig::sample_animation(*hero_model_, &hero_animations_->clips[clip], time, loop, pose_);
    if (layered && hero_animations_->clips.size() > fire_layer_clip_)
        hig::sample_animation_layer(*hero_model_, hero_animations_->clips[fire_layer_clip_], fire_time_, false,
                                    52, pose_);
    pose_transition_.advance(dt * animation_rate());
    pose_transition_.apply(pose_);
    // Keep RUN's phase and legs completely independent of the firing arm.
    if (clip == 3) {
        if (!changed && (layered != pose_fire_layer_ || (layered && shots != pose_shots_)))
            arm_transition_.begin(previous_pose_, .1f); // 0x6b1d4 / 0x6b038
        auto base = pose_;
        arm_transition_.advance(dt); // the original auxiliary layer has an independent clock
        arm_transition_.apply(pose_);
        for (size_t bone = 0; bone < pose_.size(); ++bone) {
            size_t parent = bone;
            while (parent < hero_model_->bones.size() && parent != 52)
                parent = hero_model_->bones[parent].parent;
            if (parent != 52)
                pose_[bone] = base[bone];
        }
    }
    pose_clip_ = clip;
    pose_time_ = time;
    pose_fire_layer_ = layered;
    pose_shots_ = shots;
    if (previous_pose_.empty())
        previous_pose_ = pose_;
}
void Gameplay::sample_pose(float alpha, std::vector<hig::BonePose> &out) const {
    hig::blend_poses(previous_pose_, pose_, alpha, out);
}
void Gameplay::tick_native_heli(float dt) {
    if(!player_assets_||player_assets_->heli_animations.clips.size()<7)return;
    const auto& asset=*player_assets_;
    auto select=[&](unsigned state,unsigned clip,float rate,float blend){
        heli_transition_.begin(heli_pose_,blend);heli_state_=state;heli_clip_=clip;heli_time_=0;heli_rate_=rate;
    };
    // Original LEVEL_01.REL17bc68/17bcc8 commands and17bb98 completion
    // callback. The SM host supplies active-move eligibility and shutdown.
    const bool active=health&&!swimming&&air_pack==ps2game::HeroPack::Helipack&&
        (motion==Motion::HighJump||motion==Motion::LongJump||motion==Motion::Glide);
    previous_heli_pose_=heli_pose_;
    if(active&&(heli_state_==0||heli_state_==3))select(1,2,2,0);
    if(!active&&(heli_state_==1||heli_state_==2))select(3,3,1,.15f);
    heli_time_+=std::max(0.f,dt)*heli_rate_;
    if(heli_state_==1&&heli_time_>=asset.heli_animations.clips[2].duration)select(2,4,1,.15f);
    else if(heli_state_==3&&heli_time_>=asset.heli_animations.clips[3].duration)select(0,0,1,.15f);
    hig::sample_animation(asset.heli,&asset.heli_animations.clips[heli_clip_],heli_time_,heli_state_==0||heli_state_==2,heli_pose_);
    heli_transition_.advance(std::max(0.f,dt)*heli_rate_);heli_transition_.apply(heli_pose_);
    if(previous_heli_pose_.empty())previous_heli_pose_=heli_pose_;
}
unsigned Gameplay::clip_index() const {
    if(interacting())return interaction_clip_;
    switch (motion) {
    case Motion::Run:
        return 3;
    case Motion::Jump:
        return jumps == 2 ? 7 : 4;
    case Motion::Crouch:
        return 15;
    case Motion::HighJump:
        return 8;
    case Motion::LongJump:
        return 9;
    case Motion::Fall:
        return 5;
    case Motion::Glide:
        return 11;
    case Motion::Fire:
        return fire_clip_;
    case Motion::FireRun:
        return 3; // Original FIRE_BLA is layered onto right-arm branch 52 below.
    case Motion::Wrench:
        return 12 + wrench_stage_;
    case Motion::WrenchThrow:
        return 55;
    case Motion::HyperStrike:
        return 10;
    case Motion::Wade:
        return 35;
    case Motion::SwimIdle:
        return underwater?38:36; // SM has no separately verified underwater coast clip.
    case Motion::SwimJump:
        return 37;
    case Motion::Swim:
        return 38;
    case Motion::Hurt:
        return 18;
    case Motion::Slide:
        return 34; // WATER_SL (0x51808 plays clip 0x22)
    case Motion::Grind:
        return grind_.clip;
    case Motion::Magne: // 0x529a0: MAGNA_WA(LK) 0x29; the jump's JUMP_TAK, FALL_CYC and JUMP_LAN
        return magne_.air ? (animation_time < .2f ? 4 : 5) : magne_.landing > 0 ? 6 : magne_.moving ? 41 : 0;
    case Motion::Grapple:
        return grapple_.phase == 1 ? 57 : 59; // GRPL_FIR (0x6ab98), then GRPL_CYC (0x6b2c8)
    case Motion::Dead:
        return 62;
    default:
        return 0;
    }
}
const char *Gameplay::animation_name(const PlayerAssets &assets) const {
    if (motion == Motion::FireRun)
        return "RUN + FIRE_BLA";
    return assets.animations.clips[clip_index()].name.c_str();
}
V Gameplay::interpolated(float alpha) const {
    return add(previous_, mul(sub(position, previous_), std::clamp(alpha, 0.f, 1.f)));
}
float Gameplay::interpolated_heading(float alpha) const {
    return previous_heading_ + std::remainder(heading - previous_heading_, 6.28318530718f) *
        std::clamp(alpha, 0.f, 1.f);
}
float Gameplay::animation_rate() const {
    if(underwater&&(motion==Motion::Swim||motion==Motion::SwimIdle))return swim_source_.animation.rate;
    if (motion == Motion::WrenchThrow)
        return throw_time_ < 1.16666663f ? 2.3f : throw_time_ < 2.33333325f ? .2f : 1.f;
    if (motion == Motion::HyperStrike)
        return hyper_phase_ == 2 ? .1f : 1.f;
    if (motion == Motion::Wrench)
        return 1.6f;
    if (motion == Motion::Grind) // 0x56378: the jump at .8, the swing at 1.5
        return grind_.clip == 0x45 ? .8f : grind_.clip == 0x46 ? 1.5f : 1.f;
    return motion == Motion::Run || motion == Motion::FireRun ? run_rate_ : 1.f;
}
void Gameplay::render(const PlayerAssets &assets, float alpha, Scene &scene, bool show_player,
                      bool show_enemies) const {
    scene = {};
    scene.lighting=&lighting_;
    V at = interpolated(alpha);
    std::vector<hig::Matrix> joints;
    float fraction = (std::clamp(alpha, 0.f, 1.f) - (hero_model_ ? 1.f : 0.f)) / 30;
    float time = std::max(0.f, animation_time + fraction * animation_rate());
    float fire_time = std::max(0.f, fire_time_ + fraction);
    float attachment_time = std::max(0.f, attachment_time_ + fraction);
    if (motion == Motion::Fire)
        time = fire_time;
    const float rendered_heading = interpolated_heading(alpha);
    auto model = actor_transform(at, hero_model_ ? rendered_heading : heading);
    if (motion == Motion::Magne) { // on the Magneboots he stands along the surface's normal
        const V up = magne_.up, ahead = magne_.forward, side = cross(up, ahead);
        model = {side[0], side[1], side[2], 0, up[0], up[1], up[2], 0, ahead[0], ahead[1], ahead[2], 0, at[0], at[1], at[2], 1};
    }
    if(underwater){
        const float pitch=previous_swim_pitch_+(swim_pitch-previous_swim_pitch_)*std::clamp(alpha,0.f,1.f);
        const float c=std::cos(pitch),s=std::sin(pitch);
        const hig::Matrix tilt{1,0,0,0,0,c,s,0,0,-s,c,0,0,.6f*(1-c),-.6f*s,1};
        model=hig::multiply(tilt,model);
    }
    if(slide_pitch!=0||previous_slide_pitch_!=0){ // the slide leans him along its path, about his feet
        const float pitch=previous_slide_pitch_+(slide_pitch-previous_slide_pitch_)*std::clamp(alpha,0.f,1.f);
        const float c=std::cos(pitch),s=std::sin(pitch);
        model=hig::multiply(hig::Matrix{1,0,0,0,0,c,s,0,0,-s,c,0,0,0,0,1},model);
    }
    const int clank_model = clank ? assets.encounters.objects.class_model(hig::class_hash("ClankPlr")) : -1;
    if (show_player && clank_model >= 0 && assets.encounters.objects.models[size_t(clank_model)].skinned) {
        // Clank himself (ClankMaster_rar): his own clips by what he is doing
        const auto &body = assets.encounters.objects.models[size_t(clank_model)];
        const char *name = "IDLE_DEF";
        bool loop = true;
        switch (motion) {
        case Motion::Run: case Motion::FireRun: case Motion::Wade:
            name = "RUN";
            break;
        case Motion::Jump: case Motion::HighJump: case Motion::LongJump:
            name = "JUMP", loop = false;
            break;
        case Motion::Fall:
            name = "FALL_CYC";
            break;
        case Motion::Glide:
            name = "GLIDE";
            break;
        case Motion::Wrench: case Motion::WrenchThrow: case Motion::HyperStrike:
            name = "PUNCH", loop = false;
            break;
        case Motion::Hurt:
            name = "GET_HIT", loop = false;
            break;
        case Motion::Dead:
            name = "DEATH", loop = false;
            break;
        default:
            break;
        }
        const int clip = body.clip(name);
        if (clip >= 0)
            append_animated_actor(body.actor, body.animations.clips[size_t(clip)], time, loop, model, scene);
    } else if (show_player) {
        std::vector<hig::BonePose> evaluated;
        sample_pose(alpha, evaluated);
        // PS2 Ratchet override (ps2_ratchet.h): his own mesh and rig; `joints` keeps SM's bone numbering.
        if (assets.ps2 && assets.ps2->loaded())
            assets.ps2->append(*this, assets, model, alpha, scene, joints);
        else {
            // Worn armour (LEVEL_02 0x1b83e0): each place's piece is its class's model of the worn set, drawn on the
            // hero's own pose; the part of his body under it is hidden (0x3e908: his mesh's parts by mask bit). The
            // default Ratchet only, as the original (the skins have no parts to hide).
            const auto &objects = assets.encounters.objects;
            const bool armored = armor_tables_ && objects.has_armor && assets.hero_skin == 0;
            const bool body = armored && armor.worn[0], helmet = armored && armor.worn[1], gloves = armored && armor.worn[2],
                       boots = armored && armor.worn[4];
            uint32_t hide = (body ? 1u << 3 : 0) | (helmet ? 1u << 4 : 0) | (gloves ? 1u << 6 | 1u << 1 : 0) |
                            (boots ? 1u << 2 : 0) | (body || boots ? 1u << 5 : 0);
            if (const char *parts = getenv("SM_HIDE_PARTS")) // research: which of his mesh's parts is which
                hide = uint32_t(std::strtoul(parts, nullptr, 0));
            const bool loop = motion != Motion::Jump && motion != Motion::HighJump && motion != Motion::LongJump &&
                              motion != Motion::Wrench && motion != Motion::WrenchThrow && motion != Motion::HyperStrike &&
                              motion != Motion::Dead && motion != Motion::SwimJump && motion != Motion::Hurt;
            const auto *layer = evaluated.empty() && motion == Motion::FireRun ? &assets.animations.clips[fire_layer_clip_] : nullptr;
            append_actor(assets.ratchet, &assets.animations.clips[clip_index()], time, loop, model, scene, hide, &joints, layer,
                         fire_time, evaluated.empty() ? nullptr : &evaluated);
            if (armored) {
                // A piece has a small skeleton of its own, modelled about the hero bone it hangs from (the pelvis,
                // the neck, a forearm, a thigh: its second bone). Each of its bones takes the hero's bone with the
                // same number (0x3e5a0 / 0x92920); here that is the bone's place in his model space, which is what
                // the original's copied poses under the piece's own matrix come to.
                std::vector<hig::BonePose> hero_pose = evaluated;
                if (hero_pose.empty()) {
                    hig::sample_animation(assets.ratchet, &assets.animations.clips[clip_index()], time, loop, hero_pose);
                    if (layer)
                        hig::sample_animation_layer(assets.ratchet, *layer, fire_time, false, 52, hero_pose);
                }
                std::vector<hig::Matrix> hero_world, hero_skin;
                hig::pose_matrices(assets.ratchet, hero_pose, hero_world, hero_skin);
                std::array<int, 256> hero_bone;
                hero_bone.fill(-1);
                for (size_t b = 0; b < assets.ratchet.bones.size(); b++)
                    if (hero_bone[assets.ratchet.bones[b].id] < 0)
                        hero_bone[assets.ratchet.bones[b].id] = int(b);
                for (unsigned place = 0; place < 6; place++) {
                    const int index = armor.worn[place] < 8 ? objects.armor_models[place][armor.worn[place]] : -1;
                    if (!armor.worn[place] || index < 0 || !objects.models[size_t(index)].skinned)
                        continue;
                    const auto &piece = objects.models[size_t(index)].actor;
                    if (piece.bones.size() < 2)
                        continue;
                    std::vector<hig::Matrix> places(piece.bones.size());
                    // a piece's own root has no bone of his: it stands with the bone the piece hangs from
                    const int hung = hero_bone[piece.bones[1].id];
                    for (size_t b = 0; b < piece.bones.size(); b++) {
                        const int from = hero_bone[piece.bones[b].id] >= 0 ? hero_bone[piece.bones[b].id] : hung;
                        if (from < 0 || size_t(from) >= hero_world.size()) {
                            places.clear();
                            break;
                        }
                        places[b] = hero_world[size_t(from)];
                    }
                    if (places.empty())
                        continue;
                    // the body armor's own lower part gives way to boots
                    append_actor(piece, nullptr, 0, false, model, scene, place == 0 && boots ? 1u << 1 : 0, nullptr, nullptr, 0,
                                 nullptr, &places);
                }
            }
        }
        // LEVEL_01.REL 0x38a74 selects bone 5 for the back attachment.
        // 0x39264 selects bone 56 for the normal weapon grip. The composition
        // helper at 0x125128 multiplies this bone matrix by Ratchet's world matrix.
        // These world-space joints already contain the skin-space scale conversion.
        bool using_wrench = motion == Motion::Wrench || motion == Motion::WrenchThrow ||
                            motion == Motion::HyperStrike || motion == Motion::Grind || interacting();
        // The PS2 Ratchet's hoverboots and charge-boot dash (ps2_ratchet.h) leave both hands free.
        const bool hands_free = assets.ps2 && assets.ps2->hands_free();
        if (!swimming && motion != Motion::Dead && (!thrown_ || !using_wrench) && !hands_free) {
            if (using_wrench) {
                float wt = 0;
                unsigned clip = 0;
                if (motion == Motion::Wrench) {
                    float start = hig::wrench_swings[wrench_stage_].deform_begin;
                    if (time >= start) {
                        clip = 3;
                        wt = time - start;
                    }
                } else if (motion == Motion::HyperStrike && hyper_phase_ == 3) {
                    clip = 3;
                    wt = hyper_weapon_time_ + fraction;
                }
                std::vector<hig::BonePose> tool_pose;
                if(motion==Motion::Crank) {
                    // 0x3ff30..0x3ff54 selects ANIM_OPE (4) with a .3-second
                    // blend. Its independent clock survives SCREW_ENTER/RUN/IDLE.
                    clip=4;wt=std::max(0.f,interaction_time_+fraction);
                    std::vector<hig::BonePose> closed;
                    hig::sample_animation(assets.wrench,&assets.wrench_animations.clips[0],0,false,closed);
                    hig::sample_animation(assets.wrench,&assets.wrench_animations.clips[4],wt,false,tool_pose);
                    hig::blend_poses(closed,tool_pose,std::clamp(wt/.3f,0.f,1.f),tool_pose);
                }
                if (assets.ps2_weapons && assets.ps2_weapons->wrench()) // an imported OmniWrench in his hand
                    assets.ps2_weapons->append_wrench(joints[56], scene);
                else
                append_actor(assets.wrench, &assets.wrench_animations.clips[clip], wt, false,
                             joints[56], scene,0,nullptr,nullptr,0,tool_pose.empty()?nullptr:&tool_pose);
            } else if(assets.ps2_weapons&&assets.ps2_weapons->enabled())
                assets.ps2_weapons->append_held(joints[56],scene);
            else if (const int held = weapon_tables_ && arsenal.equipped != hig::WeaponBlaster
                                          ? assets.encounters.objects.class_model(equipped_tier()->class_hash)
                                          : -1;
                     held >= 0 && assets.encounters.objects.models[held].skinned &&
                     !assets.encounters.objects.models[held].animations.clips.empty()) {
                // the equipped weapon's own model in the hand: its SHOOT (a glove's toss) clip while firing
                const auto &gun = assets.encounters.objects.models[held];
                const bool firing = motion == Motion::Fire || motion == Motion::FireRun;
                int clip = -1;
                if (firing)
                    for (auto name : {"SHOOT", "FIRE", motion == Motion::Fire ? "STANDTOS" : "RUNTOSS"})
                        if (clip < 0)
                            clip = gun.clip(name);
                if (clip < 0)
                    clip = std::max(0, gun.clip("IDLE"));
                append_actor(gun.actor, &gun.animations.clips[size_t(clip)], firing ? fire_time : attachment_time,
                             !firing, joints[56], scene);
            } else
                append_actor(
                    assets.blaster,
                    &assets.blaster_animations
                         .clips[(motion == Motion::Fire || motion == Motion::FireRun) ? 3 : 0],
                    (motion == Motion::Fire || motion == Motion::FireRun) ? fire_time
                                                                          : attachment_time,
                    true, joints[56], scene);
        }
        if (thrown_) {
            V p = add(throw_previous_,
                      mul(sub(throw_position_, throw_previous_), std::clamp(alpha, 0.f, 1.f)));
            // The original flight basis lays the wrench in a horizontal spinning plane.
            float spin_angle = throw_previous_spin_ +
                               (throw_spin_ - throw_previous_spin_) * std::clamp(alpha, 0.f, 1.f);
            float c = std::cos(spin_angle), s = std::sin(spin_angle);
            hig::Matrix spin{c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            float ch = std::cos(throw_heading_), sh = std::sin(throw_heading_);
            hig::Matrix flight{ch, 0, -sh, 0, -sh, 0, -ch, 0, 0, 1, 0, 0, p[0], p[1], p[2], 1};
            if (assets.ps2_weapons && assets.ps2_weapons->wrench()) // an imported OmniWrench in flight
                assets.ps2_weapons->append_wrench(hig::multiply(spin, flight), scene);
            else
            append_actor(assets.wrench, &assets.wrench_animations.clips[0], 0, false,
                         hig::multiply(spin, flight), scene);
        }
        // Clank's authored root track carries his mounting rotation and offset;
        // the unanimated bind pose is not his mounted pose. Keep him visible when
        // the separate helipack deploys, using the original glide clips.
        if(assets.ps2_packs&&assets.ps2_packs->replaces_native())assets.ps2_packs->append(joints[5],scene);
        else {
            append_actor(assets.clank, &assets.clank_animations.clips[motion == Motion::Glide ? 1 : 0],
                         attachment_time, true, joints[5], scene);
            std::vector<hig::BonePose> heli_pose;
            hig::blend_poses(previous_heli_pose_,heli_pose_,std::clamp(alpha,0.f,1.f),heli_pose);
            append_actor(assets.heli,&assets.heli_animations.clips[heli_clip_],heli_time_,heli_state_==0||heli_state_==2,
                         joints[5],scene,0,nullptr,nullptr,0,heli_pose.empty()?nullptr:&heli_pose);
        }
    } // player and attachments
    if (show_enemies && encounter_assets_)
        for (const auto &e : enemies) {
            const auto &m = encounter_assets_->models[e.model];
            const auto &clip = m.animations.clips[enemy_clip(m, e.state)];
            if (e.hidden || (e.state == EnemyState::Dead && (m.vanish || e.time >= clip.duration + 1)) ||
                dot(sub(e.position, at), sub(e.position, at)) > 70 * 70)
                continue;
            V p = add(e.previous, mul(sub(e.position, e.previous), std::clamp(alpha, 0.f, 1.f)));
            auto transform = actor_transform(p, e.heading);
            for (unsigned i = 0; i < 12; i++)
                transform[i] *= e.scale;
            bool loop = e.state == EnemyState::Idle || e.state == EnemyState::Chase;
            append_actor(m.model, &clip, e.time + std::clamp(alpha, 0.f, 1.f) / 30, loop, transform,
                         scene);
        }

}
V Gameplay::weapon_muzzle(float alpha) const {
    if(player_assets_&&player_assets_->ps2_weapons&&player_assets_->ps2_weapons->enabled()) {
        const float angle=previous_heading_+std::remainder(heading-previous_heading_,6.28318530718f)*std::clamp(alpha,0.f,1.f);
        const auto model=actor_transform(interpolated(alpha),angle);
        std::vector<hig::Matrix> joints,skin;
        if(player_assets_->ps2&&player_assets_->ps2->loaded())player_assets_->ps2->sample_joints(model,alpha,joints);
        else if(hero_model_) {
            std::vector<hig::BonePose> pose;sample_pose(alpha,pose);
            hig::pose_matrices(*hero_model_,pose,joints,skin);
            for(auto& joint:joints)joint=hig::multiply(joint,model);
        }
        return joints.size()>56?player_assets_->ps2_weapons->muzzle(joints[56]):player_assets_->ps2_weapons->muzzle();
    }
    if(!player_assets_ || !hero_model_ || hero_model_->bones.size()<=56)
        return add(interpolated(alpha),V{std::sin(heading)*.6f,.85f,std::cos(heading)*.6f});
    std::vector<hig::BonePose> pose;std::vector<hig::Matrix> joints,skin;
    sample_pose(alpha,pose);hig::pose_matrices(*hero_model_,pose,joints,skin);
    float angle=previous_heading_+std::remainder(heading-previous_heading_,6.28318530718f)*std::clamp(alpha,0.f,1.f);
    return hig::transform_point(player_assets_->effects.source.blaster.muzzle,
        hig::multiply(joints[56],actor_transform(interpolated(alpha),angle)));
}
void Gameplay::render_effects(const PlayerAssets &assets,float alpha,V eye,Scene &scene,bool show_player) const {
    effects.render(assets.effects,projectiles,alpha,eye,scene);
    render_shots(assets,alpha,eye,scene);
    // the Hookshot targets' glow (0x191ce8: green, or yellow for one to swing from, pulsing), and the Hypershot's line
    for (const auto &h : hooks)
        effects.glow(assets.effects, h.position, eye, .55f + .1f * std::sin(animation_time * 6), h.swing ? 0xc000ffff : 0xc000ff00, scene);
    if (motion == Motion::Grapple && grapple_.phase)
        effects.beam(assets.effects, weapon_muzzle(alpha), grapple_.anchor, eye, .035f, 0xffe0e0e0, 0, scene);
    if(show_player&&assets.ps2_packs&&assets.ps2_packs->loaded())assets.ps2_packs->append_effects(eye,scene);
    const bool uya_weapon=assets.ps2_weapons&&assets.ps2_weapons->enabled();
    if(uya_weapon)assets.ps2_weapons->append_effects(eye,scene);
    if(!uya_weapon && show_player && shots && (motion==Motion::Fire || motion==Motion::FireRun))
        effects.muzzle(assets.effects,weapon_muzzle(alpha),fire_time_+(std::clamp(alpha,0.f,1.f)-1)/30,eye,scene);
}
} // namespace sm
