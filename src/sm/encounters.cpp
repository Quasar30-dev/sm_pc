#include "encounters.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <unordered_map>

namespace sm {
unsigned enemy_clip(const EnemyModel &m, EnemyState s) {
    switch (s) {
    case EnemyState::Alert:
        return m.alert;
    case EnemyState::Chase:
        return m.run;
    case EnemyState::Attack:
        return m.attack;
    case EnemyState::Hurt:
        return m.hurt;
    case EnemyState::Dead:
        return m.death;
    default:
        return m.idle;
    }
}
const char *enemy_state_name(EnemyState s) {
    switch (s) {
    case EnemyState::Alert:
        return "alert";
    case EnemyState::Chase:
        return "chase";
    case EnemyState::Attack:
        return "attack";
    case EnemyState::Hurt:
        return "hurt";
    case EnemyState::Dead:
        return "dead";
    default:
        return "idle";
    }
}
bool load_encounter_assets(const hig::IsoFs &disc, const std::string &level, EncounterAssets &out,
                           std::vector<hig::LevelTexture> &textures, std::string &error) {
    EncounterAssets next;
    hig::HigWad h;
    std::vector<uint8_t> b;
    if (!hig::unpack_hig_wad(disc.read(level), h, b, &error))
        return false;
    if (hig::wad_checksum(b) != h.crc) {
        error = "Encounter WAD checksum mismatch";
        return false;
    }
    auto entries = hig::actor_assets(b);
    std::vector<uint8_t> sp;
    if (!hig::unpack_hig_wad(disc.read("/SP.WAD"), h, sp, &error) ||
        hig::wad_checksum(sp) != h.crc) {
        error = "Vendor SP.WAD checksum/decompression failure";
        return false;
    }
    auto shared = hig::actor_assets(sp);
    entries.insert(entries.end(), shared.begin(), shared.end());
    auto asset = [&](const std::string &name) -> std::span<const uint8_t> {
        for (const auto &e : entries)
            if (e.path.ends_with(name))
                return e.bytes;
        return {};
    };
    struct Definition {
        EnemyKind kind;
        uint32_t hash;
        const char *name, *file;
        unsigned idle, alert, run, attack, hurt, death;
        float radius, height, speed, range;
        unsigned hp, bolts;
    };
    // Class/model and animation identities are retail. Combat/navigation tuning
    // is native bring-up policy until the full class state machines are translated.
    const Definition definitions[] = {
        {EnemyKind::Crab, 0xb66d8ce9, "Crab", "CrabMaster", 6, 11, 15, 0, 5, 3, .42f, .65f, 2.8f,
         1.3f, 2, 3},
        {EnemyKind::TrainingBot, 0x54a0cb18, "TrainingBot", "TrainingRobotMaster", 10, 13, 14, 0, 8,
         1, .5f, 1.35f, 2.4f, 1.6f, 4, 5},
        {EnemyKind::Torso, 0x5dda0218, "TMRobotTorsoB", "TMRobotTorsoMaster", 17, 14, 5, 1, 12, 10,
         .45f, 1.1f, 2.2f, 8.f, 3, 5},
        {EnemyKind::Head, 0x128c157b, "TMRobotHeadB", "TMRobotHeadMaster", 0, 10, 2, 3, 16, 4, .3f,
         .65f, 3.5f, 1.2f, 2, 3}};
    std::unordered_map<uint32_t, int> indices;
    for (size_t i = 0; i < textures.size(); i++)
        indices.emplace(textures[i].hash, int(i));
    auto next_textures = textures;
    auto bind_textures = [&](hig::ActorModel &model) {
        for (auto &mesh : model.meshes) {
            auto it = indices.find(mesh.material.tex_hash);
            if (it == indices.end()) {
                auto e = std::find_if(entries.begin(), entries.end(), [&](const auto &e) {
                    return e.hash == mesh.material.tex_hash;
                });
                if (e == entries.end()) {
                    error = "Missing actor texture " + std::to_string(mesh.material.tex_hash);
                    return false;
                }
                hig::LevelTexture t;
                t.hash = e->hash;
                t.name = e->path;
                if (!hig::decode_gim(e->bytes, t.image, &error))
                    return false;
                it = indices.emplace(t.hash, int(next_textures.size())).first;
                next_textures.push_back(std::move(t));
            }
            mesh.material.texture = it->second;
        }
        return true;
    };
    for (const auto &d : definitions) {
        auto gde = asset(std::string(d.file) + ".mb.ps2.gde");
        if (gde.empty())
            continue;
        EnemyModel m;
        m.kind = d.kind;
        m.hash = d.hash;
        m.name = d.name;
        m.idle = d.idle;
        m.alert = d.alert;
        m.run = d.run;
        m.attack = d.attack;
        m.hurt = d.hurt;
        m.death = d.death;
        m.radius = d.radius;
        m.height = d.height;
        m.speed = d.speed;
        m.attack_distance = d.range;
        m.health = d.hp;
        m.bolt_reward = d.bolts;
        if (!hig::decode_actor(gde, m.model, &error) ||
            !hig::decode_animation(asset(std::string(d.file) + ".mb.anim"), m.animations, &error)) {
            error = m.name + ": " + error;
            return false;
        }
        for (unsigned clip : {m.idle, m.alert, m.run, m.attack, m.hurt, m.death})
            if (clip >= m.animations.clips.size()) {
                error = "Unsupported enemy animation revision";
                return false;
            }
        if (!bind_textures(m.model))
            return false;
        next.models.push_back(std::move(m));
    }
    if (!hig::decode_actor(asset("VendorMaster.mb.ps2.gde"), next.vendor, &error) ||
        !hig::decode_animation(asset("VendorMaster.mb.anim"), next.vendor_animations, &error) ||
        next.vendor_animations.clips.size() != 3 || !bind_textures(next.vendor)) {
        error = "Vendor: " + error;
        return false;
    }
    out = std::move(next);
    textures = std::move(next_textures);
    error.clear();
    return true;
}
namespace {
struct EnemyTier {
    unsigned level;
    const char *name;
    int health, attack;
};
const EnemyTier enemy_tiers[] = {
#include "enemy_tiers.inl"
};
} // namespace
void add_generic_enemies(const World &world, EncounterAssets &assets) {
    const auto &level = world.level;
    const auto &objects = assets.objects;
    // BoltController (LEVEL_02 0x15ba78 / 0x15be48): 70% of the level's bolts are paid by its enemies, each class
    // its share divided by the number of its instances.
    float enemy_bolts = 0;
    for (const auto &i : level.bog_instances)
        if (level.bog_defs[i.model_def].hash == hig::class_hash("BoltController") && i.properties.size() >= 4) {
            uint32_t total;
            std::memcpy(&total, i.properties.data(), 4);
            enemy_bolts = float(total) * .7f;
        }
    const auto &tuning = objects.level_tuning();
    const unsigned row = std::min(objects.level, 10u); // the share tables are indexed by level number (0x831a8)
    const float level_xp = objects.has_weapons ? float(objects.weapons.level_xp[std::min(objects.level, 15u)]) : 0;
    // hit points and damage from the level's table, by the tier the class's own code reads; bolts and experience
    // by the class's shares
    auto tune = [&](EnemyModel &m, const hig::ClassRecord *record, unsigned count) {
        int health = 0, attack = 0;
        for (const auto &t : enemy_tiers)
            if (t.level == objects.level && hig::class_hash(t.name) == m.hash) {
                health = std::max(0, t.health);
                attack = std::max(0, t.attack);
            }
        if (objects.has_tuning) {
            m.health = unsigned(std::max(1.f, std::round(tuning.health[0][health])));
            m.damage = unsigned(std::max(1.f, std::round(tuning.attack[0][attack])));
        }
        if (record && record->rewards && count) {
            m.bolt_reward = unsigned(std::round(enemy_bolts * record->bolt_share[row] / float(count)));
            m.xp_reward = level_xp * record->xp_share[row] / float(count);
        }
    };
    for (auto &m : assets.models) // the classes with their own native definitions
        for (const auto &def : level.bog_defs)
            if (def.hash == m.hash)
                tune(m, objects.record(def.hash), def.count);
    for (const auto &def : level.bog_defs) {
        const auto *record = objects.record(def.hash);
        if (!record || !record->rewards || !def.count)
            continue;
        if (std::any_of(assets.models.begin(), assets.models.end(), [&](const auto &m) { return m.hash == def.hash; }))
            continue; // a class with its own native definition
        int source = -1;
        unsigned placed = 0;
        for (uint32_t k = 0; k < def.count && def.first_instance + k < level.bog_instances.size(); k++) {
            const auto &i = level.bog_instances[def.first_instance + k];
            if (i.matrix[12] == 0 && i.matrix[13] == 0 && i.matrix[14] == 0)
                continue; // pooled
            placed++;
            if (source < 0)
                source = objects.model(i.model_hash, i.animation_hash);
        }
        if (source < 0 || !objects.models[source].skinned || objects.models[source].animations.clips.empty())
            continue;
        const auto &asset = objects.models[source];
        EnemyModel m;
        m.kind = EnemyKind::Generic;
        m.hash = def.hash;
        m.name = record->name;
        m.model = asset.actor;
        m.animations = asset.animations;
        auto first = [&](std::initializer_list<const char *> names, int fallback) {
            for (auto name : names) {
                int c = asset.clip(name);
                if (c >= 0)
                    return c;
            }
            return fallback;
        };
        const int idle = first({"IDLE"}, 0);
        const int move = first({"RUN", "WALK", "JOG", "FLY", "HOP", "CRAWL", "ROLL"}, -1);
        const int death = first({"DEATH", "DIE", "DEAD"}, -1);
        m.idle = unsigned(idle);
        m.alert = unsigned(first({"NOTICE", "ALERT", "SPOT", "ROAR"}, idle));
        m.run = unsigned(move < 0 ? idle : move);
        m.attack = unsigned(first({"ATTACK", "ATK", "SWING", "BITE", "MELEE", "SHOOT", "FIRE", "GOO_SHOO"}, idle));
        m.hurt = unsigned(first({"GET_HIT", "GETHIT", "HIT", "DAMAGE", "FLINCH", "STUN"}, idle));
        m.death = unsigned(death < 0 ? idle : death);
        m.vanish = death < 0;
        m.radius = std::clamp(asset.radius() * .6f, .3f, 1.6f);
        m.height = std::clamp(asset.height(), .5f, 4.f);
        // rooted classes (no travelling clip) strike from where they stand
        m.speed = move < 0 ? 0 : asset.clip("RUN") >= 0 ? 3.f : 1.7f;
        m.attack_distance = m.radius + (move < 0 ? 1.8f : 1.f);
        tune(m, record, def.count);
        (void)placed;
        assets.models.push_back(std::move(m));
    }
}
} // namespace sm
