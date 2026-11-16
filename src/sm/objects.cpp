#include "objects.h"
#include "hig/higlighting.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace sm {
namespace {
using V = hig::Vec3;
V add(V a, V b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V mul(V a, float f) { return {a[0] * f, a[1] * f, a[2] * f}; }
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(V a) { return std::sqrt(dot(a, a)); }
V position_of(const hig::Matrix &m) { return {m[12], m[13], m[14]}; }
std::string basename(const std::string &path) {
    auto slash = path.find_last_of("\\/");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    auto dot_mb = name.find(".mb.");
    if (dot_mb != std::string::npos)
        name.resize(dot_mb);
    return name;
}
// The bounds as twelve outward-facing triangles; only vertices and face indices are used by the player's solver.
hig::Collision box_collision(V lo, V hi) {
    hig::Collision c;
    for (int i = 0; i < 8; i++)
        c.vertices.push_back({i & 1 ? hi[0] : lo[0], i & 2 ? hi[1] : lo[1], i & 4 ? hi[2] : lo[2]});
    static const uint16_t quads[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4},
                                         {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
    for (const auto &q : quads) {
        hig::CollisionFace a, b;
        a.vertices = {q[0], q[1], q[2]};
        b.vertices = {q[0], q[2], q[3]};
        c.faces.push_back(a);
        c.faces.push_back(b);
    }
    c.minimum = lo;
    c.maximum = hi;
    return c;
}
hig::Matrix rotate_y(const hig::Matrix &m, float angle) { // spin about the object's own up axis, in place
    const float c = std::cos(angle), s = std::sin(angle);
    const hig::Matrix r{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
    return hig::multiply(r, m);
}
bool is_class(const Object &o, std::string_view name) { return o.class_hash == hig::class_hash(name); }
} // namespace

int ObjectModel::clip(std::string_view prefix) const {
    for (size_t i = 0; i < animations.clips.size(); i++) {
        const auto &name = animations.clips[i].name;
        if (name.size() < prefix.size())
            continue;
        bool same = true;
        for (size_t k = 0; k < prefix.size() && same; k++)
            same = std::toupper(uint8_t(name[k])) == std::toupper(uint8_t(prefix[k]));
        if (same)
            return int(i);
    }
    return -1;
}
float ObjectModel::radius() const {
    return .5f * std::max(maximum[0] - minimum[0], maximum[2] - minimum[2]);
}
int ObjectAssets::model(uint32_t model_hash, uint32_t animation_hash) const {
    for (size_t i = 0; i < models.size(); i++)
        if (models[i].model == model_hash && models[i].animation == animation_hash)
            return int(i);
    return -1;
}
const hig::Collision *ObjectAssets::collision(uint32_t hash) const {
    if (hash)
        for (const auto &c : collisions)
            if (c.hash == hash)
                return &c.collision;
    return nullptr;
}
const hig::ClassRecord *ObjectAssets::record(uint32_t class_hash) const {
    for (const auto &r : classes)
        if (r.hash == class_hash)
            return &r;
    return nullptr;
}

bool load_object_assets(const hig::IsoFs &disc, const World &world, ObjectAssets &out,
                        std::vector<hig::LevelTexture> &textures, std::string &error) {
    ObjectAssets next;
    // the level's code module: /WAD/16C.WAD -> /LVL/LEVEL_16.REL (property schemas; data only)
    if (world.disc_path.size() >= 7) {
        auto rel = disc.read("/LVL/LEVEL_" + world.disc_path.substr(5, 2) + ".REL");
        std::string why;
        if (rel.empty() || !hig::decode_class_records(rel, next.classes, &why))
            next.warnings.push_back("class records: " + (rel.empty() ? std::string("module absent") : why));
        next.has_tuning = hig::find_level_tuning(rel, next.tuning);
        if (!rel.empty() && !(next.has_weapons = hig::decode_weapon_tables(rel, next.weapons, &why)))
            next.warnings.push_back("weapon tables: " + why);
        next.level = unsigned(std::atoi(world.disc_path.substr(5, 2).c_str()));
        next.has_armor = !rel.empty() && hig::decode_armor_tables(rel, next.armor);
    }
    for (auto &place : next.armor_models)
        place.fill(-1);
    std::vector<uint8_t> images[3];
    std::unordered_map<uint32_t, hig::ActorAsset> entries;
    const char *wads[3] = {world.disc_path.c_str(), "/SP.WAD", "/GLOBAL.WAD"};
    for (int k = 0; k < 3; k++) {
        hig::HigWad h;
        if (!hig::unpack_hig_wad(disc.read(wads[k]), h, images[k], &error) || hig::wad_checksum(images[k]) != h.crc) {
            error = std::string("Object asset WAD checksum/decompression failure: ") + wads[k];
            return false;
        }
        for (auto &e : hig::actor_assets(images[k]))
            entries.emplace(e.hash, e); // the level's own asset wins
    }
    auto bytes = [&](uint32_t hash) -> std::span<const uint8_t> {
        auto it = entries.find(hash);
        return it == entries.end() ? std::span<const uint8_t>{} : it->second.bytes;
    };
    std::unordered_map<uint32_t, int> indices;
    auto next_textures = textures;
    for (size_t i = 0; i < textures.size(); i++)
        indices.emplace(textures[i].hash, int(i));
    auto bind = [&](hig::LevelMaterial &m) {
        m.texture = -1;
        if (!m.tex_hash)
            return;
        auto it = indices.find(m.tex_hash);
        if (it == indices.end()) {
            hig::LevelTexture t;
            t.hash = m.tex_hash;
            auto e = entries.find(t.hash);
            std::string why;
            if (e == entries.end() || !hig::decode_gim(e->second.bytes, t.image, &why))
                return; // untextured rather than absent
            t.name = e->second.path;
            it = indices.emplace(t.hash, int(next_textures.size())).first;
            next_textures.push_back(std::move(t));
        }
        m.texture = it->second;
    };
    for (const auto &i : world.level.bog_instances) {
        if (i.collision_hash && !next.collision(i.collision_hash)) {
            ObjectAssets::Col c;
            c.hash = i.collision_hash;
            std::string why;
            auto b = bytes(i.collision_hash);
            if (!b.empty() && hig::decode_collision(b, c.collision, &why))
                next.collisions.push_back(std::move(c));
        }
        if (!i.model_hash || next.model(i.model_hash, i.animation_hash) >= 0)
            continue;
        auto gde = bytes(i.model_hash);
        if (gde.empty())
            continue; // another archive's (the hero's skins, multiplayer-only classes)
        ObjectModel m;
        m.model = i.model_hash;
        m.animation = i.animation_hash;
        m.name = basename(entries[i.model_hash].path);
        std::string why;
        V lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        auto grow = [&](const float *p) {
            for (int k = 0; k < 3; k++) {
                lo[k] = std::min(lo[k], p[k]);
                hi[k] = std::max(hi[k], p[k]);
            }
        };
        const uint32_t type = hig::gde_type(gde);
        const bool rigid = type == 0x10000000 || type == 0x10000003 || type == 0x10000004;
        if (!rigid && hig::decode_actor(gde, m.actor, &why)) {
            m.skinned = true;
            if (i.animation_hash && !hig::decode_animation(bytes(i.animation_hash), m.animations, &why))
                m.animations = {};
            for (auto &mesh : m.actor.meshes) {
                bind(mesh.material);
                for (const auto &v : mesh.vertices)
                    grow(v.position.data());
            }
        } else if (rigid && hig::decode_rigid_model(gde, m.rigid, &why)) {
            for (auto &material : m.rigid.materials) {
                bind(material);
                for (size_t k = 0; k + 2 < material.positions.size(); k += 3)
                    grow(&material.positions[k]);
            }
        } else {
            next.warnings.push_back(m.name + ": " + why);
            continue;
        }
        if (lo[0] > hi[0])
            lo = hi = {};
        m.minimum = lo;
        m.maximum = hi;
        m.box = box_collision(lo, hi);
        next.models.push_back(std::move(m));
    }
    for (const auto &def : world.level.bog_defs)
        for (uint32_t k = 0; k < def.count && def.first_instance + k < world.level.bog_instances.size(); k++) {
            const auto &i = world.level.bog_instances[def.first_instance + k];
            const int index = i.model_hash ? next.model(i.model_hash, i.animation_hash) : -1;
            if (index >= 0) {
                next.class_models.push_back({def.hash, index});
                break;
            }
        }
    // the worn armour pieces: the piece classes' pooled instances, one a set, by their variant
    if (next.has_armor)
        for (const auto &def : world.level.bog_defs)
            for (unsigned place = 0; place < 6; place++) {
                if (def.hash != next.armor.place_class[place])
                    continue;
                for (uint32_t k = 0; k < def.count && def.first_instance + k < world.level.bog_instances.size(); k++) {
                    const auto &i = world.level.bog_instances[def.first_instance + k];
                    for (unsigned set = 1; set < 8; set++)
                        if (i.variant_hash == next.armor.variant[set] && i.model_hash)
                            next.armor_models[place][set] = next.model(i.model_hash, i.animation_hash);
                }
            }
    out = std::move(next);
    textures = std::move(next_textures);
    error.clear();
    return true;
}

void append_rigid_model(const hig::LevelModel &model, const hig::Matrix &matrix, Scene &scene, uint8_t alpha) {
    const bool lit = scene.lighting && scene.lighting->valid;
    const auto lighting = lit ? hig::actor_lighting(*scene.lighting, {matrix[12], matrix[13], matrix[14]})
                              : hig::ActorLighting{};
    for (const auto &m : model.materials) {
        const size_t count = m.positions.size() / 3;
        if (!count || m.uvs.size() != count * 2 || m.rgba.size() != count * 4)
            continue;
        Batch b;
        b.first = uint32_t(scene.indices.size());
        b.texture = m.texture;
        b.alpha_test = m.alpha_test;
        b.blend = m.alpha_blend || alpha < 255;
        const uint32_t base = uint32_t(scene.vertices.size());
        const bool normals = lit && m.normals.size() == count * 3;
        for (size_t i = 0; i < count; i++) {
            Vertex v{};
            V p = hig::transform_point({m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]}, matrix);
            std::copy(p.begin(), p.end(), v.position);
            std::copy_n(m.uvs.data() + i * 2, 2, v.uv);
            std::copy_n(m.rgba.data() + i * 4, 4, v.color);
            if (normals) {
                const auto n = hig::lighting_normal({m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]},
                                                    matrix);
                if (n[0] != 0 || n[1] != 0 || n[2] != 0) {
                    const auto light = hig::actor_light_rgb(lighting, n);
                    for (int k = 0; k < 3; k++)
                        v.color[k] = uint8_t(std::clamp(float(v.color[k]) * light[k], 0.f, 255.f));
                }
            }
            v.color[3] = uint8_t(unsigned(v.color[3]) * alpha / 255);
            scene.vertices.push_back(v);
            b.center = add(b.center, p);
        }
        for (auto i : m.indices)
            scene.indices.push_back(base + i);
        b.count = uint32_t(scene.indices.size()) - b.first;
        b.center = mul(b.center, 1.f / float(count));
        scene.batches.push_back(b);
    }
}

float ObjectWorld::random() {
    random_ = random_ * 1664525u + 1013904223u;
    return float(random_ >> 8) * (1.f / 16777216.f);
}
const Object *ObjectWorld::find(uint32_t source) const {
    for (const auto &o : objects)
        if (o.source == source)
            return &o;
    return nullptr;
}
V ObjectWorld::center(const Object &o) const {
    if (o.model < 0)
        return position_of(o.matrix);
    const auto &m = assets_->models[o.model];
    return hig::transform_point(mul(add(m.minimum, m.maximum), .5f), o.matrix);
}

void ObjectWorld::start(const World &world, const ObjectAssets &assets, std::span<const uint32_t> owned) {
    *this = {};
    world_ = &world;
    assets_ = &assets;
    const auto &level = world.level;
    unsigned bolt_total = 0, crates = 0;
    for (const auto &inst : level.bog_instances)
        instances_.emplace(inst.source_offset, &inst);
    std::vector<const ObjectClass *> behaviours;
    for (const auto &c : shared_object_classes())
        behaviours.push_back(&c);
    for (const auto &c : sprout_object_classes())
        behaviours.push_back(&c);
    for (const auto &c : clankbot_object_classes())
        behaviours.push_back(&c);
    for (const auto &c : grind_object_classes())
        behaviours.push_back(&c);
    if (assets.level == 2)
        for (const auto &c : level02_object_classes())
            behaviours.push_back(&c);
    if (assets.level == 3)
        for (const auto &c : level03_object_classes())
            behaviours.push_back(&c);
    if (assets.level == 4)
        for (const auto &c : level04_object_classes())
            behaviours.push_back(&c);
    if (assets.level == 5)
        for (const auto &c : level05_object_classes())
            behaviours.push_back(&c);
    for (const auto &def : level.bog_defs) {
        const ObjectClass *behaviour = nullptr;
        for (auto c : behaviours)
            if (hig::class_hash(c->name) == def.hash)
                behaviour = c; // a level's own definition, listed later, replaces a shared one
        const auto *record = assets.record(def.hash);
        const std::string name = !def.name.empty() ? def.name : record ? record->name : std::string();
        const uint32_t category = def.flags >> 24;
        for (uint32_t k = 0; k < def.count && def.first_instance + k < level.bog_instances.size(); k++) {
            const auto &inst = level.bog_instances[def.first_instance + k];
            Object o;
            o.source = inst.source_offset;
            o.class_hash = def.hash;
            o.name = name;
            o.props = {inst.properties, record};
            o.variant = int(inst.flags & 0xff);
            o.variant_hash = inst.variant_hash;
            o.flags = inst.flags;
            std::copy_n(inst.matrix, 16, o.base.begin());
            o.matrix = o.previous = o.base;
            o.model = assets.model(inst.model_hash, inst.animation_hash);
            o.collision = assets.collision(inst.collision_hash);
            const bool pooled = inst.matrix[12] == 0 && inst.matrix[13] == 0 && inst.matrix[14] == 0;
            const bool taken = std::find(owned.begin(), owned.end(), o.source) != owned.end();
            if (is_class(o, "BoltController"))
                bolt_total = o.props.u32("boltAmnt");
            // templates for what this world spawns
            if (is_class(o, "Bolt") && o.variant < 3)
                bolt_models_[o.variant] = o.model;
            if (is_class(o, "PowerupAmmo") && o.variant == 0)
                ammo_model_ = o.model;
            if (pooled || taken || category == 3) // 3: the player's own actors
                o.kind = ObjectKind::Logic;
            else if (is_class(o, "Crate"))
                o.kind = ObjectKind::Crate;
            else if (is_class(o, "CrateAmmo") || is_class(o, "CrateAmmoSpawned"))
                o.kind = ObjectKind::CrateAmmo;
            else if (is_class(o, "CrateHealth") || is_class(o, "CrateHealthSpawned"))
                o.kind = ObjectKind::CrateHealth;
            else if (is_class(o, "CrateExplosive"))
                o.kind = ObjectKind::CrateExplosive;
            else if (is_class(o, "TitaniumBolt"))
                o.kind = ObjectKind::TitaniumBolt;
            else if (is_class(o, "ArmorPickup"))
                o.kind = ObjectKind::ArmorPickup;
            if (!pooled && !taken)
                o.behaviour = behaviour;
            o.pooled = pooled && !taken;
            o.pool_behaviour = behaviour;
            if (o.kind == ObjectKind::Logic || (o.behaviour && !o.behaviour->body)) {
                o.visible = o.solid = false;
            } else {
                o.visible = o.model >= 0;
                const bool crate = o.kind == ObjectKind::Crate || o.kind == ObjectKind::CrateAmmo ||
                                   o.kind == ObjectKind::CrateHealth || o.kind == ObjectKind::CrateExplosive;
                o.solid = o.collision || (crate && o.model >= 0);
                if (o.kind == ObjectKind::Crate || o.kind == ObjectKind::CrateExplosive)
                    crates++;
                if (o.model >= 0 && assets.models[o.model].skinned) {
                    const auto &m = assets.models[o.model];
                    o.clip = m.clip("IDLE");
                    if (o.clip < 0 && !m.animations.clips.empty())
                        o.clip = 0;
                }
            }
            objects.push_back(std::move(o));
        }
    }
    // BoltController init (LEVEL_02 0x15ba78): 30% of the level's bolts are shared equally among its Crate and
    // CrateExplosive instances; each pays its share less a fifth, plus a random amount up to two fifths.
    if (bolt_total && crates) {
        const unsigned share = unsigned(float(bolt_total) * .3f) / crates;
        const unsigned spread = unsigned(float(share) * .2f);
        crate_base_ = share - spread;
        crate_range_ = spread * 2;
    }
}

void ObjectWorld::spawn_bolts(V at, unsigned amount) {
    // 0x15b678: the payout rounded up to four, paid in bolts of 500, 100, 20, 5 and 1 (table 0x232670, 0x2c a row:
    // model variant, value, radius .12, pickup sound, tint).
    static const struct {
        unsigned value, variant;
        uint32_t sound, tint;
    } rows[5] = {{500, 2, 0xc76fbbf0, 0xff00ff00}, {100, 2, 0xc76fbbf0, 0xffff8800}, {20, 2, 0xc76fbbf0, 0xffffff00},
                 {5, 1, 0xf56562be, 0xffffffff},   {1, 0, 0xf56562be, 0xffffffff}};
    amount = (amount + 3) & ~3u;
    for (const auto &row : rows) {
        unsigned count = amount / row.value;
        amount -= count * row.value;
        for (unsigned k = 0; k < count && pickups.size() < 512; k++) {
            Pickup p;
            p.kind = Pickup::Kind::Bolt;
            p.model = bolt_models_[row.variant];
            p.value = row.value;
            p.scale = bolt_scale_;
            p.sound = row.sound;
            p.tint = row.tint;
            const float angle = random() * 6.2831853f, speed = 1.2f + random() * 2.2f;
            p.position = p.previous = add(at, V{std::sin(angle) * .15f, .2f, std::cos(angle) * .15f});
            p.velocity = {std::sin(angle) * speed, 3.2f + random() * 2.3f, std::cos(angle) * speed};
            p.spin = p.previous_spin = random() * 6.2831853f;
            pickups.push_back(p);
        }
    }
}

void ObjectWorld::break_crate(Object &o, Gameplay &game, bool silent) {
    if (!o.alive)
        return;
    const V at = center(o);
    o.alive = o.visible = o.solid = false;
    crates_broken++;
    if (!silent)
        game.audio_events.push_back({0x9a914c5a}); // the crate's break sound, a GLOBAL.WAD sound (0x1dfb40)
    if (o.kind == ObjectKind::Crate || o.kind == ObjectKind::CrateExplosive)
        spawn_bolts(at, crate_base_ + (crate_range_ ? unsigned(random() * float(crate_range_)) : 0));
    else if (o.kind == ObjectKind::CrateAmmo) {
        Pickup p;
        p.kind = Pickup::Kind::Ammo;
        p.model = ammo_model_;
        p.value = 10;
        p.position = p.previous = at;
        p.velocity = {0, 3.5f, 0};
        pickups.push_back(p);
    } else if (o.kind == ObjectKind::CrateHealth) {
        Pickup p;
        p.kind = Pickup::Kind::Health;
        p.value = 2;
        p.position = p.previous = at;
        p.velocity = {0, 3.f, 0};
        pickups.push_back(p);
    }
    // the crates resting on it fall
    if (o.model >= 0) {
        const auto &m = assets_->models[o.model];
        const float top = o.matrix[13] + m.maximum[1];
        for (auto &other : objects) {
            if (!other.alive || !other.solid || other.model < 0 || &other == &o || other.kind == ObjectKind::Prop)
                continue;
            const auto &om = assets_->models[other.model];
            const float reach = (m.radius() + om.radius()) * .9f;
            if (std::abs(other.matrix[12] - o.matrix[12]) < reach && std::abs(other.matrix[14] - o.matrix[14]) < reach &&
                other.matrix[13] + om.minimum[1] > top - .25f)
                other.unsettled = true;
        }
    }
}

void ObjectWorld::explode(Object &o, Gameplay &game) {
    const V at = center(o);
    explosions++;
    break_crate(o, game, true);
    game.audio_events.push_back({0x9a914c5a});
    if (game.alive() && length(sub(add(game.position, V{0, .6f, 0}), at)) < 3.2f)
        game.damage(2, at);
    game.blast(at, 3.5f, 4);
    for (auto &other : objects) {
        if (!other.alive || &other == &o)
            continue;
        const bool crate = other.kind == ObjectKind::Crate || other.kind == ObjectKind::CrateAmmo ||
                           other.kind == ObjectKind::CrateHealth || other.kind == ObjectKind::CrateExplosive;
        if (!crate || length(sub(center(other), at)) > 3.5f)
            continue;
        if (other.kind == ObjectKind::CrateExplosive) {
            if (other.timer < 0)
                other.timer = .12f; // a chain goes off crate by crate
        } else
            break_crate(other, game, true);
    }
}

void ObjectWorld::settle(Object &o, Gameplay &game, float dt) {
    const auto &m = assets_->models[o.model];
    const float bottom = o.matrix[13] + m.minimum[1];
    // what it can come to rest on: the highest living crate under it, else the ground
    float support = -1e30f;
    for (const auto &other : objects) {
        if (!other.alive || !other.solid || other.model < 0 || &other == &o)
            continue;
        const auto &om = assets_->models[other.model];
        const float reach = (m.radius() + om.radius()) * .9f;
        const float top = other.matrix[13] + om.maximum[1];
        if (std::abs(other.matrix[12] - o.matrix[12]) < reach && std::abs(other.matrix[14] - o.matrix[14]) < reach &&
            top <= bottom + .05f)
            support = std::max(support, top);
    }
    // below its own underside, so the ray does not start inside its own box
    auto ground = game.terrain().ray({o.matrix[12], bottom - .02f, o.matrix[14]}, {o.matrix[12], bottom - 60, o.matrix[14]});
    if (ground.hit)
        support = std::max(support, ground.point[1]);
    if (support < -1e29f || bottom - support < .01f) {
        o.unsettled = false;
        o.fall_speed = 0;
        return;
    }
    o.fall_speed += 22 * dt;
    float drop = std::min(o.fall_speed * dt, bottom - support);
    o.matrix[13] -= drop;
    if (bottom - drop - support < .005f) {
        o.unsettled = false;
        o.fall_speed = 0;
    }
    // whatever stood on it follows
    const float top = o.matrix[13] + m.maximum[1];
    for (auto &other : objects) {
        if (!other.alive || !other.solid || other.model < 0 || &other == &o || other.unsettled ||
            other.kind == ObjectKind::Prop)
            continue;
        const auto &om = assets_->models[other.model];
        const float reach = (m.radius() + om.radius()) * .9f;
        const float gap = other.matrix[13] + om.minimum[1] - top;
        if (std::abs(other.matrix[12] - o.matrix[12]) < reach && std::abs(other.matrix[14] - o.matrix[14]) < reach &&
            gap > .01f && gap < 1.f)
            other.unsettled = true;
    }
}

void ObjectWorld::tick_pickups(Gameplay &game, float dt) {
    const V hero = add(game.position, V{0, .6f, 0});
    for (auto &p : pickups) {
        p.previous = p.position;
        p.previous_spin = p.spin;
        p.age += dt;
        p.spin += dt * 5.5f;
        const V to = sub(hero, p.position);
        const float d = length(to);
        const bool wanted = p.kind == Pickup::Kind::Bolt || (p.kind == Pickup::Kind::Ammo && game.ammo < game.max_ammo) ||
                            (p.kind == Pickup::Kind::Health && game.health < game.max_health);
        if (game.alive() && wanted && p.age > .45f && d < 4.5f) {
            // drawn to the hero, faster the closer it is (native flight; the retail manager is not translated)
            const float speed = 7 + 16 * (1 - d / 4.5f);
            p.velocity = mul(to, speed / std::max(d, .001f));
            p.position = add(p.position, mul(p.velocity, dt));
            p.grounded = false;
        } else if (!p.grounded) {
            p.velocity[1] -= 20 * dt;
            V next = add(p.position, mul(p.velocity, dt));
            auto hit = game.terrain().ray(add(p.position, V{0, .05f, 0}), add(next, V{0, -.12f, 0}));
            if (hit.hit && p.velocity[1] <= 0) {
                next[1] = hit.point[1] + .12f;
                if (std::abs(p.velocity[1]) < 1.5f) {
                    p.velocity = {};
                    p.grounded = true;
                } else
                    p.velocity = {p.velocity[0] * .55f, -p.velocity[1] * .38f, p.velocity[2] * .55f};
            }
            p.position = next;
        }
        if (game.alive() && wanted && p.age > .3f && length(sub(hero, p.position)) < .75f) {
            if (p.kind == Pickup::Kind::Bolt) {
                game.bolts += p.value;
                bolts_collected += p.value;
            } else if (p.kind == Pickup::Kind::Ammo)
                game.ammo = std::min(game.max_ammo, game.ammo + p.value);
            else
                game.health = std::min(game.max_health, game.health + p.value);
            if (p.sound)
                game.audio_events.push_back({p.sound});
            p.age = 1e9f;
        }
    }
    std::erase_if(pickups, [](const Pickup &p) { return p.age > 30; });
}

void ObjectWorld::tick(Gameplay &game, float dt) {
    if (!world_ || !assets_)
        return;
    player_ = game.position;
    // the hits of the player's last attacks
    hits_ = std::move(game.target_hits);
    game.target_hits.clear();
    blasts = std::move(new_blasts_);
    new_blasts_.clear();
    game.hooks.clear();
    marks.clear();
    aim_camera = false;
    const auto &hits = hits_;
    for (const auto &hit : hits) {
        for (auto &o : objects) {
            if (o.source != hit.id || !o.alive)
                continue;
            if (o.kind == ObjectKind::CrateExplosive) {
                if (o.timer < 0)
                    o.timer = .08f;
            } else if (o.kind == ObjectKind::Crate || o.kind == ObjectKind::CrateAmmo ||
                       o.kind == ObjectKind::CrateHealth)
                break_crate(o, game);
            break;
        }
    }
    prompt.clear();
    if (!started_) {
        started_ = true;
        for (const auto &e : game.enemies)
            if (e.hidden)
                holds_[e.source_offset].initial = true;
        // the enemies' ways in (`initPath`, and for a class that has one the trigger that starts it)
        for (auto &e : game.enemies) {
            const auto props = instance_props(e.source_offset);
            if (!props.has("initPath"))
                continue;
            const auto *way = curve(props.u32("initPath"));
            if (!way || way->points.size() < 2)
                continue;
            e.entry.assign(way->points.begin(), way->points.end());
            e.entry_at = 0;
            e.entry_trigger = props.has("initPathTrigger") ? props.u32("initPathTrigger") : 0;
        }
        for (size_t i = 0; i < objects.size(); i++) // by index: a start may not add objects, but keep it safe
            if (objects[i].behaviour && objects[i].behaviour->start)
                objects[i].behaviour->start(*this, objects[i], game);
    }
    // what the enemies defeated since the last tick leave behind
    game.drop_bolts = bolt_models_[0] >= 0;
    for (const auto &drop : game.drops)
        spawn_bolts(drop.at, drop.bolts);
    game.drops.clear();
    const V hero = add(game.position, V{0, .6f, 0});
    for (auto &o : objects) {
        o.previous = o.matrix;
        if (!o.alive)
            continue;
        o.time += dt;
        switch (o.kind) {
        case ObjectKind::CrateExplosive:
            if (o.timer >= 0 && (o.timer -= dt) < 0) {
                explode(o, game);
                break;
            }
            [[fallthrough]];
        case ObjectKind::Crate:
        case ObjectKind::CrateAmmo:
        case ObjectKind::CrateHealth:
            if (o.unsettled && o.model >= 0)
                settle(o, game, dt);
            break;
        case ObjectKind::TitaniumBolt:
        case ObjectKind::ArmorPickup:
            o.matrix = rotate_y(o.base, o.time * 2.2f);
            if (game.alive() && length(sub(hero, center(o))) < 1.3f) {
                o.alive = o.visible = false;
                (o.kind == ObjectKind::TitaniumBolt ? titanium_bolts : armor_pickups)++;
                game.audio_events.push_back({0xc76fbbf0});
                // the piece it is: the pickup's model variant names its row of the armour table (0x870)
                if (o.kind == ObjectKind::ArmorPickup && assets_->has_armor)
                    if (auto found = instances_.find(o.source); found != instances_.end())
                        for (unsigned row = 0; row < hig::ArmorTables::piece_count; row++)
                            if (assets_->armor.pieces[row].pickup_model == found->second->variant_hash)
                                game.grant_armor(row);
            }
            break;
        default:
            break;
        }
        if (o.alive && o.behaviour && o.behaviour->tick)
            o.behaviour->tick(*this, o, game, dt);
    }
    interact_ = false;
    pressed_ = 0;
    for (auto &e : game.enemies) {
        auto hold = holds_.find(e.source_offset);
        if (hold != holds_.end())
            e.hidden = hold->second.wave || hold->second.area || hold->second.initial;
    }
    tick_pickups(game, dt);
    // what the player collides with and can hit, near him
    colliders_.clear();
    game.targets.clear();
    for (const auto &o : objects) {
        if (!o.alive || !o.solid)
            continue;
        const V at = position_of(o.matrix);
        if (std::abs(at[0] - player_[0]) > 90 || std::abs(at[2] - player_[2]) > 90)
            continue;
        const bool crate = o.kind != ObjectKind::Prop || o.target;
        const hig::Collision *shape = o.collision ? o.collision : o.model >= 0 ? &assets_->models[o.model].box : nullptr;
        if (shape && o.steps && o.collision) {
            // only the level faces at or below his feet
            auto &held = stepped_[o.source];
            if (held.vertices.size() != o.collision->vertices.size())
                held.vertices = o.collision->vertices;
            held.faces.clear();
            for (const auto &face : o.collision->faces) {
                if (face.planes[1] < .7f)
                    continue;
                float top = -1e9f;
                for (int k = 0; k < 3; k++) {
                    const auto &v = o.collision->vertices[face.vertices[k]];
                    top = std::max(top, o.matrix[1] * v[0] + o.matrix[5] * v[1] + o.matrix[9] * v[2] + o.matrix[13]);
                }
                if (top <= game.position[1] + .1f)
                    held.faces.push_back(face);
            }
            colliders_.push_back({&held, o.matrix});
        } else if (shape && !o.ghost)
            colliders_.push_back({shape, o.matrix});
        if (crate && o.model >= 0) {
            const auto &m = assets_->models[o.model];
            game.targets.push_back({o.source, center(o), .55f * length(sub(m.maximum, m.minimum))});
        }
    }
}

void ObjectWorld::input(const hig::PadState &pad) {
    if (pad.pressed & hig::Triangle)
        interact_ = true;
    pressed_ |= pad.pressed;
    stick_ = {pad.axes[0], -pad.axes[1]};
}
Object *ObjectWorld::object(uint32_t source) {
    if (source)
        for (auto &o : objects)
            if (o.source == source)
                return &o;
    return nullptr;
}
bool ObjectWorld::inside(uint32_t trigger, V point) const {
    if (!trigger || !world_)
        return false;
    for (const auto &t : world_->level.triggers)
        if (t.source_offset == trigger)
            return hig::trigger_contains(t, point);
    return false;
}
const hig::BogTrigger *ObjectWorld::trigger(uint32_t source) const {
    if (source && world_)
        for (const auto &t : world_->level.triggers)
            if (t.source_offset == source)
                return &t;
    return nullptr;
}
bool ObjectWorld::inside_collection(uint32_t collection, V point) const {
    auto props = instance_props(collection);
    if (props.bytes.empty())
        return false;
    // 0x1db570 with the count 0x15540 gives the collection: its links up to the first empty one, fourteen at most.
    for (size_t at = 0; at + 4 <= props.bytes.size() && at < 56 && props.word(at); at += 4)
        if (inside(props.word(at), point))
            return true;
    return false;
}
const hig::BogCurve *ObjectWorld::curve(uint32_t source) const {
    if (source && world_)
        for (const auto &c : world_->level.curves)
            if (c.source_offset == source)
                return &c;
    return nullptr;
}
hig::ClassProps ObjectWorld::instance_props(uint32_t source) const {
    auto it = instances_.find(source);
    if (!source || it == instances_.end())
        return {};
    const auto &def = world_->level.bog_defs[it->second->model_def];
    return {it->second->properties, assets_->record(def.hash)};
}
bool ObjectWorld::links_to(uint32_t from, uint32_t to) const {
    auto props = instance_props(from);
    if (!props.record || !to)
        return false;
    for (const auto &field : props.record->fields)
        if (field.type == "MobyLink" && !field.pointer)
            for (uint32_t k = 0; k < field.count; k++)
                if (props.word(field.offset + k * 4) == to)
                    return true;
    return false;
}
Enemy *ObjectWorld::enemy(Gameplay &game, uint32_t source) const {
    if (source)
        for (auto &e : game.enemies)
            if (e.source_offset == source)
                return &e;
    return nullptr;
}
bool ObjectWorld::gone(uint32_t source, const Gameplay &game) const {
    if (!source)
        return true;
    for (const auto &e : game.enemies)
        if (e.source_offset == source)
            return e.state == EnemyState::Dead;
    if (auto o = find(source))
        return !o->alive;
    return true;
}
void ObjectWorld::destroy(Object &o) {
    o.alive = o.visible = o.solid = false;
    if (o.pooled) { // back to the pool: a template again
        o.behaviour = nullptr;
        o.kind = ObjectKind::Logic;
        o.alive = true;
        o.target = false;
        o.steps = false;
        o.owner = 0;
    }
}
Object *ObjectWorld::spawn(uint32_t class_hash, const hig::Matrix &at, Gameplay &game) {
    for (auto &o : objects) {
        if (o.class_hash != class_hash || !o.pooled || o.kind != ObjectKind::Logic)
            continue;
        o.kind = ObjectKind::Prop;
        o.behaviour = o.pool_behaviour;
        o.base = o.matrix = o.previous = at;
        o.alive = true;
        o.visible = o.model >= 0;
        o.solid = false;
        o.state = 0;
        o.time = 0;
        o.timer = -1;
        o.f = {};
        o.n = {};
        o.clip = o.model >= 0 && assets_->models[o.model].skinned && !assets_->models[o.model].animations.clips.empty() ? 0 : -1;
        o.loop = false;
        if (o.behaviour && o.behaviour->start)
            o.behaviour->start(*this, o, game);
        return &o;
    }
    return nullptr;
}
const TargetHit *ObjectWorld::hit(const Object &o) const {
    for (const auto &h : hits_)
        if (h.id == o.source)
            return &h;
    return nullptr;
}
void ObjectWorld::hold_enemy(uint32_t source, bool by_wave, bool held) {
    auto &hold = holds_[source];
    (by_wave ? hold.wave : hold.area) = held;
}
void ObjectWorld::play(Object &o, std::string_view prefix, bool loop) {
    if (o.model < 0)
        return;
    const int clip = assets_->models[o.model].clip(prefix);
    if (clip >= 0 && (clip != o.clip || !loop)) {
        o.clip = clip;
        o.time = 0;
    }
    o.loop = loop;
}
void ObjectWorld::render(const PlayerAssets &assets, float alpha, V eye, Scene &scene) const {
    if (!assets_)
        return;
    const float a = std::clamp(alpha, 0.f, 1.f);
    unsigned count = 0;
    for (const auto &o : objects) {
        if (!o.visible || o.model < 0)
            continue;
        hig::Matrix m = o.matrix;
        for (int k = 12; k < 15; k++)
            m[k] = o.previous[k] + (o.matrix[k] - o.previous[k]) * a;
        const V at = position_of(m);
        if (length(sub(at, eye)) > 140)
            continue;
        const auto &model = assets_->models[o.model];
        if (model.skinned) {
            if (o.clip >= 0 && size_t(o.clip) < model.animations.clips.size())
                append_animated_actor(model.actor, model.animations.clips[o.clip], o.time + a / 30, o.loop, m, scene);
            else {
                std::vector<hig::BonePose> rest;
                hig::sample_animation(model.actor, nullptr, 0, false, rest);
                static const hig::AnimationClip none;
                append_animated_actor(model.actor, none, 0, false, m, scene, &rest);
            }
        } else
            append_rigid_model(model.rigid, m, scene);
        count++;
    }
    for (const auto &p : pickups) {
        if (p.model < 0 || p.age > 30)
            continue;
        const V at = add(p.previous, mul(sub(p.position, p.previous), a));
        if (length(sub(at, eye)) > 90)
            continue;
        const float spin = p.previous_spin + (p.spin - p.previous_spin) * a;
        const float c = std::cos(spin), s = std::sin(spin);
        const float k = p.scale;
        const hig::Matrix m{c * k, 0, -s * k, 0, 0, k, 0, 0, s * k, 0, c * k, 0, at[0], at[1], at[2], 1};
        const auto &model = assets_->models[p.model];
        if (!model.skinned)
            append_rigid_model(model.rigid, m, scene);
        count++;
    }
    static const WeaponEffects draw;
    for (const auto &m : marks)
        if (m.kind == Mark::Kind::Glow)
            draw.glow(assets.effects, m.a, eye, m.radius, m.color, scene);
        else if (m.kind == Mark::Kind::Disc)
            draw.disc(assets.effects, m.a, m.radius, m.color, scene);
        else
            draw.beam(assets.effects, m.a, m.b, eye, m.radius, m.color, 0, scene);
    const_cast<ObjectWorld *>(this)->drawn = count;
}
} // namespace sm
