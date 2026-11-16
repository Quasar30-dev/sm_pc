#include "photo.h"
#include "hig/higwrench.h"
#include <cmath>
#include <unordered_map>

namespace sm {
namespace {
bool archive(const hig::IsoFs &disc, const std::string &path, std::vector<uint8_t> &b,
             std::string &error) {
    hig::HigWad header;
    if (!hig::unpack_hig_wad(disc.read(path), header, b, &error))
        return false;
    if (hig::wad_checksum(b) != header.crc) {
        error = path + ": WAD checksum mismatch";
        return false;
    }
    return true;
}
hig::Matrix transform(const PhotoActor &actor) {
    float x = actor.rotation[0], y = actor.rotation[1], z = actor.rotation[2];
    hig::Matrix pitch{1, 0, 0, 0, 0, std::cos(x), std::sin(x), 0, 0, -std::sin(x), std::cos(x),
                      0, 0, 0, 0, 1};
    hig::Matrix yaw{std::cos(y), 0, -std::sin(y), 0, 0, 1, 0, 0,
                    std::sin(y), 0, std::cos(y),  0, 0, 0, 0, 1};
    hig::Matrix roll{std::cos(z), std::sin(z), 0, 0, -std::sin(z), std::cos(z), 0, 0, 0, 0, 1, 0,
                     0,           0,           0, 1};
    auto m = hig::multiply(hig::multiply(pitch, yaw), roll);
    for (unsigned i = 0; i < 12; i++)
        m[i] *= actor.scale;
    std::copy(actor.position.begin(), actor.position.end(), m.begin() + 12);
    return m;
}
} // namespace
bool read_photo_catalog(const hig::IsoFs &disc, const std::string &wad,
                        std::vector<hig::ActorCatalogEntry> &out, std::string &error) {
    std::vector<uint8_t> b;
    if (!archive(disc, wad, b, error))
        return false;
    out = hig::catalog_actors(b, wad);
    return true;
}
void PhotoSession::seed(const PlayerAssets &player) {
    *this = {};
    catalog.push_back({"Ratchet (default)", "/ART/HEROSKIN.PAK", "default skin 29",
                       "RatchetStdMaster.mb.anim", 0, unsigned(player.animations.clips.size()),
                       unsigned(player.ratchet.bones.size())});
    assets.push_back({player.ratchet, player.animations, player.textures, 0, true});
}
bool PhotoSession::load(const hig::IsoFs &disc, size_t index, size_t &asset_index,
                        std::string &error) {
    if (index >= catalog.size()) {
        error = "Character is no longer in the catalog";
        return false;
    }
    for (size_t i = 0; i < assets.size(); i++)
        if (assets[i].catalog == index) {
            asset_index = i;
            return true;
        }
    const auto &entry = catalog[index];
    std::vector<std::vector<uint8_t>> archives;
    std::vector<hig::ActorAsset> entries;
    for (const auto &path : {entry.wad, std::string("/GLOBAL.WAD"), std::string("/SP.WAD")}) {
        // Repeated shared archive entries are harmless and source-local textures take precedence.
        archives.emplace_back();
        if (!archive(disc, path, archives.back(), error))
            return false;
        auto added = hig::actor_assets(archives.back());
        entries.insert(entries.end(), added.begin(), added.end());
    }
    auto bytes = [&](const std::string &name) -> std::span<const uint8_t> {
        for (const auto &e : entries)
            if (e.path == name)
                return e.bytes;
        return {};
    };
    PhotoAsset asset;
    asset.catalog = index;
    if (!hig::decode_actor(bytes(entry.model_path), asset.model, &error) ||
        !hig::decode_animation(bytes(entry.animation_path), asset.animations, &error))
        return false;
    if (asset.animations.clips.empty()) {
        error = "Character has no animation clips";
        return false;
    }
    for (const auto &clip : asset.animations.clips)
        for (const auto &track : clip.tracks)
            if (track.bone >= asset.model.bones.size()) {
                error = "Animation bank does not match this skeleton";
                return false;
            }
    std::unordered_map<uint32_t, int> bound;
    for (auto &mesh : asset.model.meshes) {
        uint32_t hash = mesh.material.tex_hash;
        if (!hash)
            continue;
        auto found = bound.find(hash);
        if (found == bound.end()) {
            auto e = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto &e) { return e.hash == hash; });
            if (e == entries.end()) {
                error = "Missing character texture " + std::to_string(hash);
                return false;
            }
            hig::LevelTexture texture;
            texture.hash = hash;
            texture.name = e->path;
            if (!hig::decode_gim(e->bytes, texture.image, &error))
                return false;
            found = bound.emplace(hash, int(asset.textures.size())).first;
            asset.textures.push_back(std::move(texture));
        }
        mesh.material.texture = found->second;
    }
    asset_index = assets.size();
    assets.push_back(std::move(asset));
    error.clear();
    return true;
}
uint64_t PhotoSession::spawn(size_t asset, hig::Vec3 position, float heading) {
    if (asset >= assets.size() || actors.size() >= 64)
        return 0;
    PhotoActor a;
    a.id = next_id_++;
    a.asset = asset;
    a.name = catalog[assets[asset].catalog].name;
    a.position = position;
    a.rotation[1] = heading;
    actors.push_back(std::move(a));
    return selected = actors.back().id;
}
PhotoActor *PhotoSession::find(uint64_t id) {
    for (auto &a : actors)
        if (a.id == id)
            return &a;
    return nullptr;
}
uint64_t PhotoSession::duplicate(uint64_t id) {
    auto *actor = find(id);
    if (!actor || actors.size() >= 64)
        return 0;
    auto copy = *actor;
    copy.id = next_id_++;
    copy.position[0] += .75f;
    actors.push_back(std::move(copy));
    return selected = actors.back().id;
}
bool PhotoSession::remove(uint64_t id) {
    size_t count = std::erase_if(actors, [&](const auto &a) { return a.id == id; });
    if (selected == id)
        selected = actors.empty() ? 0 : actors.back().id;
    return count != 0;
}
const hig::AnimationClip &PhotoSession::clip(const PhotoActor &a) const {
    const auto &bank = assets.at(a.asset).animations;
    return bank.clips.at(std::min<size_t>(a.clip, bank.clips.size() - 1));
}
std::pair<hig::Vec3, hig::Vec3> PhotoSession::bounds(const PhotoActor &a) const {
    Scene scene;
    append_animated_actor(assets.at(a.asset).model, clip(a), a.time, false, transform(a), scene);
    hig::Vec3 lo = a.position, hi = a.position;
    if (!scene.vertices.empty())
        for (int k = 0; k < 3; k++)
            lo[k] = hi[k] = scene.vertices.front().position[k];
    for (const auto &v : scene.vertices)
        for (int k = 0; k < 3; k++) {
            lo[k] = std::min(lo[k], v.position[k]);
            hi[k] = std::max(hi[k], v.position[k]);
        }
    return {lo, hi};
}
void PhotoSession::tick(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    for (auto &a : actors) {
        if (!a.playing)
            continue;
        float duration = clip(a).duration;
        a.time += seconds * std::clamp(a.speed, .05f, 4.f);
        if (a.loop && duration > 0)
            a.time = std::fmod(a.time, duration);
        else if (a.time >= duration) {
            a.time = duration;
            a.playing = false;
        }
    }
}
void PhotoSession::append_textures(std::vector<hig::LevelTexture> &out) const {
    for (const auto &a : assets)
        out.insert(out.end(), a.textures.begin(), a.textures.end());
}
void PhotoSession::render(const PlayerAssets &player, Scene &scene, int texture_base) const {
    std::vector<int> offsets;
    for (const auto &asset : assets) {
        offsets.push_back(texture_base);
        texture_base += int(asset.textures.size());
    }
    for (const auto &a : actors) {
        if (!a.visible)
            continue;
        const auto &asset = assets.at(a.asset);
        size_t first = scene.batches.size();
        auto matrix = transform(a);
        // Scrubbing the final frame must not wrap back to the first pose.
        append_animated_actor(asset.model, clip(a), a.time, false, matrix, scene);
        if (asset.ratchet && (a.clank || a.weapon)) {
            std::vector<hig::BonePose> pose;
            std::vector<hig::Matrix> joints, skin;
            hig::sample_animation(asset.model, &clip(a), a.time, false, pose);
            hig::pose_matrices(asset.model, pose, joints, skin);
            unsigned weapon = a.weapon, weapon_clip = a.weapon_clip, clank_clip = a.clank_clip;
            float weapon_time = a.attachment_time, clank_time = a.attachment_time;
            if (a.auto_attachments) {
                clank_clip = a.clip == 11 ? 1 : 0;
                clank_time = a.time;
                if (weapon && ((a.clip >= 12 && a.clip <= 14) || a.clip == 10 || a.clip == 55)) {
                    weapon = 1;
                    weapon_clip = 0;
                    weapon_time = 0;
                    if (a.clip >= 12 && a.clip <= 14 &&
                        a.time >= hig::wrench_swings[a.clip - 12].deform_begin) {
                        weapon_clip = 3;
                        weapon_time = a.time - hig::wrench_swings[a.clip - 12].deform_begin;
                    }
                }
            }
            if (a.clank) {
                append_animated_actor(player.clank, player.clank_animations.clips.at(clank_clip),
                                      clank_time, false, hig::multiply(joints[5], matrix), scene);
                if (a.auto_attachments && a.clip == 11)
                    append_animated_actor(player.heli, player.heli_animations.clips[4], a.time,
                                          false, hig::multiply(joints[5], matrix), scene);
            }
            if (a.weapon) {
                const auto &bank =
                    weapon == 1 ? player.wrench_animations : player.blaster_animations;
                append_animated_actor(
                    weapon == 1 ? player.wrench : player.blaster,
                    bank.clips.at(std::min<size_t>(weapon_clip, bank.clips.size() - 1)),
                    weapon_time, false, hig::multiply(joints[56], matrix), scene);
            }
        }
        for (size_t i = first; i < scene.batches.size(); i++)
            if (scene.batches[i].texture >= 0)
                scene.batches[i].texture += offsets[a.asset];
    }
}
} // namespace sm
