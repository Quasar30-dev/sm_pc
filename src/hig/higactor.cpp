#include "higactor.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace hig {
namespace {
bool range(std::span<const uint8_t> b, size_t p, size_t n) {
    return p <= b.size() && n <= b.size() - p;
}
uint16_t u16(std::span<const uint8_t> b, size_t p) { return b[p] | uint16_t(b[p + 1]) << 8; }
uint32_t u32(std::span<const uint8_t> b, size_t p) {
    return u16(b, p) | uint32_t(u16(b, p + 2)) << 16;
}
float f32(std::span<const uint8_t> b, size_t p) {
    float f;
    uint32_t u = u32(b, p);
    std::memcpy(&f, &u, 4);
    return f;
}
std::string label(std::span<const uint8_t> b, size_t p, size_t n) {
    size_t end = p;
    while (end < p + n && b[end])
        ++end;
    return {reinterpret_cast<const char *>(b.data() + p), end - p};
}
void normalize(std::array<float, 4> &q) {
    float n = 0;
    for (auto x : q)
        n += x * x;
    if (n < 1e-12f) {
        q = {0, 0, 0, 1};
        return;
    }
    for (auto &x : q)
        x /= std::sqrt(n);
}
BonePose blend(const BonePose &a, const BonePose &b, float t) {
    BonePose r;
    float dot = 0;
    for (int k = 0; k < 4; k++)
        dot += a.rotation[k] * b.rotation[k];
    // Shortest-arc spherical interpolation of the original sparse keyframes.
    float sign = dot < 0 ? -1.f : 1.f;
    dot = std::clamp(std::abs(dot), 0.f, 1.f);
    float wa = 1 - t, wb = t;
    if (dot < .9995f) {
        float theta = std::acos(dot), s = std::sin(theta);
        wa = std::sin((1 - t) * theta) / s;
        wb = std::sin(t * theta) / s;
    }
    for (int k = 0; k < 4; k++)
        r.rotation[k] = a.rotation[k] * wa + b.rotation[k] * wb * sign;
    normalize(r.rotation);
    for (int k = 0; k < 3; k++) {
        r.scale[k] = a.scale[k] + t * (b.scale[k] - a.scale[k]);
        r.translation[k] = a.translation[k] + t * (b.translation[k] - a.translation[k]);
    }
    return r;
}
Matrix local_matrix(BonePose p) {
    normalize(p.rotation);
    auto [x, y, z, w] = p.rotation;
    Matrix m{1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),     0,
             2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),     0,
             2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y), 0,
             p.translation[0],        p.translation[1],        p.translation[2],        1};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i * 4 + j] *= p.scale[i];
    return m;
}
} // namespace
Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix c{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                c[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
    return c;
}
Vec3 transform_point(const Vec3 &p, const Matrix &m) {
    Vec3 r;
    for (int k = 0; k < 3; k++)
        r[k] = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] + m[12 + k];
    return r;
}
std::vector<ActorAsset> actor_assets(std::span<const uint8_t> b) {
    std::vector<ActorAsset> out;
    for (size_t p = 0; range(b, p, 192); p += 16) {
        if (b[p + 4] != 'C' || b[p + 5] != ':' || b[p + 6] != '\\')
            continue;
        auto n = u32(b, p + 104);
        if (!n || !range(b, p + 192, n))
            continue;
        out.push_back({u32(b, p), label(b, p + 4, 96), b.subspan(p + 192, n)});
        p += ((192 + n + 15) & ~size_t(15)) - 16;
    }
    return out;
}
bool decode_animation(std::span<const uint8_t> b, AnimationBank &out, std::string *error) {
    out = {};
    auto fail = [&](const char *s) {
        if (error)
            *error = s;
        return false;
    };
    if (!range(b, 0, 12) || u32(b, 0) != 12)
        return fail("Unsupported animation header");
    uint32_t count = b[8];
    if (!count || !range(b, 12, count * 64))
        return fail("Invalid animation clip table");
    AnimationBank bank;
    for (uint32_t i = 0; i < count; i++) {
        size_t c = 12 + i * 64;
        AnimationClip clip;
        clip.name = label(b, c, 8);
        clip.duration = f32(b, c + 40);
        if (!std::isfinite(clip.duration) || clip.duration < 0 || clip.duration > 3600)
            return fail("Invalid animation duration");
        uint32_t event_offset = u32(b, c + 8), event_count = b[c + 53];
        if (!range(b, event_offset, event_count * 32))
            return fail("Animation events exceed asset");
        for (uint32_t e = 0; e < event_count; e++) {
            size_t p = event_offset + e * 32;
            AnimationClip::Event event;
            event.name = label(b, p, 8);
            event.time = f32(b, p + 8);
            event.flags = u32(b, p + 12);
            event.sound = u32(b, p + 20);
            if (!std::isfinite(event.time) || event.time < 0 || event.time > 3600)
                return fail("Invalid animation event time");
            clip.events.push_back(std::move(event));
        }
        uint32_t p = u32(b, c + 12), tracks = b[c + 47];
        if (!range(b, p, tracks * 16))
            return fail("Animation tracks exceed asset");
        for (uint32_t j = 0; j < tracks; j++) {
            size_t tr = p + j * 16;
            AnimationTrack track;
            track.bone = b[tr + 4];
            track.channels = b[tr + 5];
            if (u32(b, tr) != 0xaaaaaaaa || !track.channels || (track.channels & ~7))
                return fail("Unsupported animation track");
            uint32_t n = u32(b, tr + 8), key = u32(b, tr + 12);
            size_t stride = 2 + ((track.channels & 1) ? 5 : 0) + ((track.channels & 2) ? 6 : 0) +
                            ((track.channels & 4) ? 4 : 0);
            if (!n || n > 65536 || !range(b, key, n * stride))
                return fail("Animation keys exceed asset");
            for (uint32_t k = 0; k < n; k++) {
                size_t q = key + k * stride;
                AnimationKey v;
                v.time = u16(b, q) / 256.f;
                q += 2;
                if (k && v.time < track.keys.back().time)
                    return fail("Unordered animation keys");
                // LEVEL_01.REL 0x8a148: four big-endian 10-bit UNORM fields.
                if (track.channels & 1) {
                    uint64_t packed = 0;
                    for (int x = 0; x < 5; x++)
                        packed = (packed << 8) | b[q + x];
                    q += 5;
                    for (int x = 0; x < 4; x++)
                        v.pose.rotation[x] =
                            float((packed >> (30 - 10 * x)) & 1023) * (2.f / 1023.f) - 1;
                    normalize(v.pose.rotation);
                }
                // 0x8a35c: signed fixed-point tenths of a source centimetre.
                if (track.channels & 2) {
                    for (int x = 0; x < 3; x++)
                        v.pose.translation[x] = int16_t(u16(b, q + 2 * x)) * .001f;
                    q += 6;
                }
                // 0x8a470: little-endian X/Y/Z 10-bit fields, 1/64 units.
                if (track.channels & 4) {
                    uint32_t packed = u32(b, q);
                    for (int x = 0; x < 3; x++)
                        v.pose.scale[x] = float((packed >> (20 - 10 * x)) & 1023) / 64.f;
                }
                track.keys.push_back(v);
            }
            clip.tracks.push_back(std::move(track));
        }
        bank.clips.push_back(std::move(clip));
    }
    out = std::move(bank);
    if (error)
        error->clear();
    return true;
}
float ratchet_run_playback_rate(float distance) {
    if (!std::isfinite(distance) || distance <= 0)
        return 0;
    // LEVEL_01.REL gait table 0x1f4638, 28-byte rows: WALK2 +0x1c,
    // RUN +0x38. Preserve the authored float precision rather than 1/12.
    constexpr float previous = .008333001285791397f;
    constexpr float reference = .08332999795675278f;
    constexpr float maximum = .12000000476837158f;
    distance = std::min(distance, maximum);
    // 0x4a394..0x4a3d0 and 0x4a3d4..0x4a458 write controller +8.
    if (distance > reference)
        return 1.f + (maximum / reference - 1.f) * ((distance - reference) / (maximum - reference));
    return std::max(0.f, (distance - previous) / (reference - previous));
}
void sample_animation(const ActorModel &model, const AnimationClip *clip, float t, bool loop,
                      std::vector<BonePose> &out) {
    out.clear();
    for (const auto &b : model.bones)
        out.push_back(b.bind);
    if (!clip || !std::isfinite(t))
        return;
    t = std::max(t, 0.f);
    if (clip->duration > 0)
        t = loop ? std::fmod(t, clip->duration) : std::min(t, clip->duration);
    for (const auto &track : clip->tracks) {
        if (track.bone >= out.size() || track.keys.empty())
            continue;
        auto next = std::upper_bound(track.keys.begin(), track.keys.end(), t,
                                     [](float t, const AnimationKey &k) { return t < k.time; });
        BonePose p;
        if (next == track.keys.begin())
            p = next->pose;
        else if (next == track.keys.end())
            p = track.keys.back().pose;
        else {
            const auto &a = *(next - 1);
            p = blend(a.pose, next->pose, (t - a.time) / (next->time - a.time));
        }
        auto &dest = out[track.bone];
        if (track.channels & 1)
            dest.rotation = p.rotation;
        if (track.channels & 2)
            dest.translation = p.translation;
        if (track.channels & 4)
            dest.scale = p.scale;
    }
}
void sample_animation_layer(const ActorModel &model, const AnimationClip &clip, float seconds,
                            bool loop, uint8_t branch, std::vector<BonePose> &pose) {
    if (pose.size() != model.bones.size() || branch >= pose.size())
        return;
    std::vector<BonePose> layer;
    sample_animation(model, &clip, seconds, loop, layer);
    std::vector<bool> selected(model.bones.size());
    for (size_t i = 0; i < selected.size(); i++)
        selected[i] = i == branch || (model.bones[i].parent < i && selected[model.bones[i].parent]);
    for (const auto &track : clip.tracks) {
        auto bone = track.bone;
        if (bone >= pose.size() || !selected[bone])
            continue;
        if (track.channels & 1)
            pose[bone].rotation = layer[bone].rotation;
        if (track.channels & 2)
            pose[bone].translation = layer[bone].translation;
        if (track.channels & 4)
            pose[bone].scale = layer[bone].scale;
    }
}
void blend_poses(std::span<const BonePose> from, std::span<const BonePose> to, float weight,
                 std::vector<BonePose> &out) {
    const float t = std::isfinite(weight) ? std::clamp(weight, 0.f, 1.f) : 1.f;
    if (from.size() != to.size()) {
        out.assign(to.begin(), to.end());
        return;
    }
    out.resize(to.size());
    for (size_t i = 0; i < to.size(); ++i)
        out[i] = t == 0 ? from[i] : t == 1 ? to[i] : blend(from[i], to[i], t);
}
void pose_matrices(const ActorModel &model, std::span<const BonePose> pose,
                   std::vector<Matrix> &world, std::vector<Matrix> &skin) {
    world.resize(model.bones.size());
    skin.resize(model.bones.size());
    for (size_t i = 0; i < model.bones.size(); i++) {
        const auto &b = model.bones[i];
        auto local = local_matrix(i < pose.size() ? pose[i] : b.bind);
        world[i] = b.parent == 255 ? local : multiply(local, world[b.parent]);
        skin[i] = multiply(b.inverse_bind, world[i]);
    }
}
bool decode_actor(std::span<const uint8_t> b, ActorModel &out, std::string *error) {
    out = {};
    auto fail = [&](const char *s) {
        if (error)
            *error = s;
        return false;
    };
    ActorModel m;
    if (!range(b, 0, 60) || u32(b, 0) != 0x10001 || u32(b, 4) != 0x10000001)
        return fail("Unsupported skinned GDE");
    uint32_t r = u32(b, 28), s = u32(b, 32), nm = u32(b, 8), mp = u32(b, 12), nt = u32(b, 16),
             tp = u32(b, 20);
    if (!nm || nm > 64 || nt > 256 || !range(b, mp, nm * 248) || !range(b, tp, nt * 20) ||
        !range(b, r, 56) || u32(b, r) != 2 || !range(b, s, 32) || u32(b, s) != 0x44444444)
        return fail("Invalid skinned tables");
    uint32_t nb = u32(b, s + 4), bp = u32(b, s + 8), cp = u32(b, s + 12);
    if (!nb || nb > 255 || !range(b, bp, nb * 80) || !range(b, cp, nb * 48))
        return fail("Invalid skeleton");
    const uint32_t ids = u32(b, s + 16); // a byte a bone: its number in the shared bone list
    const bool has_ids = ids && range(b, ids, nb);
    for (uint32_t i = 0; i < nb; i++) {
        SkinBone bone;
        bone.parent = b[bp + i * 80 + 68];
        bone.id = has_ids ? b[ids + i] : uint8_t(i);
        if (bone.parent != 255 && bone.parent >= i)
            return fail("Invalid skeleton hierarchy");
        for (int k = 0; k < 16; k++) {
            bone.inverse_bind[k] = f32(b, bp + i * 80 + k * 4);
            if (!std::isfinite(bone.inverse_bind[k]))
                return fail("Invalid bind matrix");
        }
        // Retail skin space is 1/8 of game metres; convert both sides together.
        for (int k = 0; k < 3; k++)
            bone.inverse_bind[12 + k] *= 8;
        for (int k = 0; k < 4; k++)
            bone.bind.rotation[k] = f32(b, cp + i * 48 + k * 4);
        for (int k = 0; k < 3; k++) {
            bone.bind.scale[k] = f32(b, cp + i * 48 + 16 + k * 4);
            bone.bind.translation[k] = f32(b, cp + i * 48 + 32 + k * 4) * .01f;
        }
        for (auto v : bone.bind.rotation)
            if (!std::isfinite(v))
                return fail("Invalid bone rotation");
        for (int k = 0; k < 3; k++)
            if (!std::isfinite(bone.bind.scale[k]) || !std::isfinite(bone.bind.translation[k]))
                return fail("Invalid bone components");
        m.bones.push_back(bone);
    }
    std::vector<LevelMaterial> materials(nm);
    for (uint32_t i = 0; i < nm; i++) {
        size_t p = mp + i * 248;
        auto &mat = materials[i];
        mat.name = label(b, p, 32);
        if (u32(b, p + 108) > 2)
            return fail("Unsupported skin material passes");
        if (u32(b, p + 108) && u32(b, p + 112)) {
            uint32_t index = u16(b, p + 116);
            if (index >= nt)
                return fail("Skin texture index out of range");
            mat.tex_hash = u32(b, tp + index * 20);
        }
    }
    uint32_t dl = u32(b, r + 24), packets = u32(b, r + 4), table = u32(b, r + 20);
    if (!packets || packets > 100000 || !range(b, table, packets * 16))
        return fail("Invalid skin packet table");
    std::array<uint8_t, 6> palette{};
    uint32_t material = 0;
    uint8_t part = 0;
    bool ended = false;
    for (size_t step = 0; step < 100000 && range(b, dl, 4); step++, dl += 4) {
        uint32_t cmd = u32(b, dl), op = cmd >> 24;
        if (op == 0xfe) {
            ended = true;
            break;
        }
        if (op == 0xfa || op == 0xfb) {
            uint32_t slot = (cmd >> 16) & 15, bone = cmd & 65535;
            if (slot >= 6 || bone >= nb)
                return fail("Invalid skin palette");
            palette[slot] = uint8_t(bone);
        } else if (op == 0xfc) {
            material = cmd & 15;
            if (material >= nm)
                return fail("Invalid skin material");
        } else if (op == 0xf9)
            part = uint8_t(cmd & 15);
        else if (op == 1) {
            uint32_t row = cmd & 0xffffff;
            if (row < table || (row - table) % 16 || (row - table) / 16 >= packets)
                return fail("Invalid display packet reference");
            // SKIN_DrawMesh (SCUS_976.15 +0x282b0) submits the DMA extent;
            // it does not use row[0] as a strip count. TrampSproutLG batches
            // several strips in one packet. Each GIF tag below owns its NLOOP.
            uint32_t size = u32(b, row + 8), p = u32(b, row + 12), n = 0;
            if (!size || !range(b, p, size))
                return fail("Invalid skin VIF extent");
            auto v = b.subspan(p, size);
            size_t weights = 0, normals = 0, uv = 0, pos = 0;
            uint32_t primitive = 0;
            auto emit = [&]() -> bool {
                if (!weights || !normals || !uv || !pos || primitive != 4)
                    return false;
                SkinMesh mesh;
                mesh.material = materials[material];
                mesh.part = part;
                for (uint32_t i = 0; i < n; i++) {
                    SkinVertex vert;
                    vert.bones = palette;
                    float total = 0;
                    for (int k = 0; k < 6; k++) {
                        float w = float(k < 4 ? v[weights + i * 4 + k]
                                              : u16(v, uv + i * 8 + 4 + (k - 4) * 2));
                        if (w > 127)
                            return false;
                        vert.weights[k] = w;
                        total += w;
                    }
                    if (total < 1)
                        return false;
                    for (auto &w : vert.weights)
                        w /= total;
                    for (int k = 0; k < 3; k++) {
                        vert.position[k] =
                            (int16_t(u16(v, pos + i * 6 + 2 * k)) / 16384.f - 1.f) * 8;
                        vert.normal[k] = int8_t(v[normals + i * 4 + k]) / 127.f;
                    }
                    // SCUS_976.15 skinnedmesh_vu_renderer_fast: immediate
                    // 0x3a800000, ITOF0 UV conversion and MUL.xy give 1/1024.
                    // Static GDE texture-coordinate scaling is a different path.
                    for (int k = 0; k < 2; k++)
                        vert.uv[k] = int16_t(u16(v, uv + i * 8 + 2 * k)) / 1024.f;
                    mesh.vertices.push_back(vert);
                    if (i >= 2) {
                        mesh.indices.push_back(i - 2);
                        mesh.indices.push_back(i & 1 ? i : i - 1);
                        mesh.indices.push_back(i & 1 ? i - 1 : i);
                    }
                }
                m.meshes.push_back(std::move(mesh));
                weights = normals = uv = pos = 0;
                return true;
            };
            for (size_t o = 0; o < v.size();) {
                if (!range(v, o, 4))
                    return fail("Truncated skin VIF");
                uint16_t imm = u16(v, o);
                uint32_t count = v[o + 2] ? v[o + 2] : 256;
                uint8_t code = v[o + 3] & 127;
                o += 4;
                size_t bytes = 0;
                if (code >= 0x60) {
                    if (code & 0x10)
                        return fail("Masked skin VIF unsupported");
                    bytes = (size_t(count) * (((code >> 2) & 3) + 1) * (32 >> (code & 3)) / 8 + 3) &
                            ~size_t(3);
                    if (!range(v, o, bytes))
                        return fail("Truncated skin attributes");
                    unsigned dest = imm & 1023;
                    if (code == 0x6c && dest == 0 && count == 1) {
                        primitive =
                            uint32_t((uint64_t(u32(v, o)) | uint64_t(u32(v, o + 4)) << 32) >> 47) &
                            7;
                        n = u32(v, o) & 32767;
                        if (!n || n > 256)
                            return fail("Invalid skin strip count");
                    } else if (count != n)
                        return fail("Inconsistent skin vertex count");
                    else if (code == 0x6e && dest == 1)
                        weights = o;
                    else if (code == 0x6e && dest == 2)
                        normals = o;
                    else if (code == 0x6d && dest == 3)
                        uv = o;
                    else if (code == 0x69 && dest == 5)
                        pos = o;
                    else
                        return fail("Unsupported skin attributes");
                } else if (code == 0x01) {
                    if ((imm & 255) < (imm >> 8))
                        return fail("Filling skin VIF unsupported");
                } else if (code == 0x17 || code == 0x14) {
                    if (!emit())
                        return fail("Incomplete or malformed skin strip");
                } else if (code != 0 && code != 0x10 && code != 0x05)
                    return fail("Unsupported skin VIF command");
                o += bytes;
            }
            if (pos && !emit())
                return fail("Incomplete final skin strip");

        // 0x9b000000/1 appears on weapons, skyboarders and Dozer. The PS2
        // SKIN_DrawMesh dispatcher (+0x28028 -> +0x283d8) consumes it without
        // changing geometry, material or palette state.
        } else if (op != 0x12 && op != 0x1d && op != 0 && op != 0xfd && op != 0x9b)
            return fail("Unsupported skin display command");
    }
    if (!ended || m.meshes.empty())
        return fail("Unterminated or empty skin display list");
    out = std::move(m);
    if (error)
        error->clear();
    return true;
}
} // namespace hig
