#include "effects.h"
#include <algorithm>
#include <cmath>

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
V mul(V a, float b) {
    for (auto &x : a)
        x *= b;
    return a;
}
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
V unit(V a) {
    float n = std::sqrt(dot(a, a));
    return n > 1e-6f ? mul(a, 1 / n) : V{0, 1, 0};
}
V side(V direction, V view) {
    V v = cross(direction, view);
    if (dot(v, v) < 1e-8f)
        v = cross(direction, std::abs(direction[1]) < .9f ? V{0, 1, 0} : V{1, 0, 0});
    return unit(v);
}
uint32_t fade(uint32_t c, float a) {
    return (c & 0xffffff) | (uint32_t(std::clamp(a, 0.f, 1.f) * float(c >> 24)) << 24);
}
void quad(Scene &s, int texture, V center, std::array<V, 4> p, uint32_t color, uint32_t tail,
          float scroll = 0, bool wrap = false) {
    Batch b;
    b.first = uint32_t(s.indices.size());
    b.count = 6;
    b.texture = texture;
    b.blend = true;
    b.additive = true;
    b.effect = true;
    b.center = center;
    b.effect_wrap = wrap;
    b.uv_offset={0,scroll};
    uint32_t base = uint32_t(s.vertices.size());
    constexpr float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (unsigned i = 0; i < 4; i++) {
        Vertex v{};
        std::copy(p[i].begin(), p[i].end(), v.position);
        std::copy(uv[i], uv[i] + 2, v.uv);
        uint32_t c = i < 2 ? color : tail;
        for (unsigned k = 0; k < 4; k++)
            v.color[k] = uint8_t(c >> (8 * k));
        s.vertices.push_back(v);
    }
    for (unsigned i : {0, 1, 2, 0, 2, 3})
        s.indices.push_back(base + i);
    s.batches.push_back(b);
}
void sprite(Scene &s, int texture, V at, V eye, float radius, uint32_t color, float angle = 0) {
    V view = unit(sub(eye, at)), r = side(V{0, 1, 0}, view), up = unit(cross(view, r));
    V x = mul(add(mul(r, std::cos(angle)), mul(up, std::sin(angle))), radius);
    V y = mul(sub(mul(up, std::cos(angle)), mul(r, std::sin(angle))), radius);
    quad(s, texture, at,
         {sub(sub(at, x), y), add(sub(at, y), x), add(add(at, x), y), add(sub(at, x), y)}, color,
         color);
}
void ribbon(Scene &s, int texture, V head, V tail, V eye, float radius, uint32_t color,
            float scroll = 0, bool wrap = false) {
    if (dot(sub(head, tail), sub(head, tail)) < 1e-10f)
        return;
    V r = mul(side(unit(sub(head, tail)), unit(sub(eye, head))), radius);
    quad(s, texture, mul(add(head, tail), .5f),
         {sub(head, r), add(head, r), add(tail, r), sub(tail, r)}, color, color & 0xffffff, scroll,
         wrap);
}
} // namespace
bool load_effect_assets(const hig::IsoFs &disc, std::span<const hig::ActorAsset> entries,
                        EffectAssets &out, std::vector<hig::LevelTexture> &textures,
                        std::string &error) {
    EffectAssets a;
    auto rel = disc.read("/LVL/LEVEL_01.REL");
    if (!hig::read_weapon_fx(rel, a.source, error))
        return false;
    constexpr unsigned slots[] = {31, 12, 83, 30, 60, 15};
    for (unsigned i = 0; i < 6; i++) {
        uint32_t hash = a.source.texture_hashes[slots[i]];
        auto old = std::find_if(textures.begin(), textures.end(),
                                [&](const auto &t) { return t.hash == hash; });
        if (old != textures.end()) {
            a.textures[i] = int(old - textures.begin());
            continue;
        }
        auto entry = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto &e) { return e.hash == hash; });
        if (entry == entries.end()) {
            error = "Missing original weapon effect texture " + std::to_string(hash);
            return false;
        }
        hig::LevelTexture t;
        t.hash = hash;
        t.name = entry->path;
        if (!hig::decode_gim(entry->bytes, t.image, &error))
            return false;
        a.textures[i] = int(textures.size());
        textures.push_back(std::move(t));
    }
    out = std::move(a);
    return true;
}
float WeaponEffects::random() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return float(random_ >> 8) / 16777216.f;
}
void WeaponEffects::tick(float dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > .05f)
        return;
    for (auto &p : particles_)
        p.age += dt;
    // Keep the last simulation interval for interpolated rendering.
    std::erase_if(particles_, [](const auto &p) { return p.age > p.life + 1.f / 30; });
}
void WeaponEffects::impact(const EffectAssets &assets, V position, V normal, bool hostile,
                           float age) {
    normal = unit(normal);
    V right = side(normal, V{0, 1, 0}), up = cross(normal, right);
    auto &b = assets.source.blaster;
    if (particles_.size() + 28 > 2048)
        return;
    // Surface-facing hemisphere and deterministic RNG are native policies;
    // the source emitter uses its own random generator/cone transform.
    for (const auto &spray : b.sparks)
        for (unsigned i = 0; i < spray.count; i++) {
            float c = .15f + .85f * random(), a = random() * 6.28318530718f,
                  s = std::sqrt(1 - c * c);
            V direction =
                add(mul(normal, c), add(mul(right, std::cos(a) * s), mul(up, std::sin(a) * s)));
            Particle p;
            p.age = age;
            p.position = add(position, mul(normal, .025f));
            p.velocity = mul(direction,
                             (spray.step_min + (spray.step_max - spray.step_min) * random()) * 30);
            p.life = (spray.frames_min + (spray.frames_max - spray.frames_min) * random()) / 30;
            p.size = spray.size_min + (spray.size_max - spray.size_min) * random();
            p.gravity = spray.gravity * 900;
            p.color = hostile ? 0xff5040ff : spray.color;
            particles_.push_back(p);
        }
    // Brief soft contact flare uses the original splotch/tint; its envelope is
    // an enhancement, independent of combat damage and collision response.
    Particle flash;
    flash.age = age;
    flash.position = add(position, mul(normal, .04f));
    flash.life = .12f;
    flash.size = b.glow_radius;
    flash.color = hostile ? 0xc85040ff : b.glow;
    flash.flare = true;
    particles_.push_back(flash);
}
void WeaponEffects::burst(V position, float radius, uint32_t color, unsigned sparks, float age) {
    if (particles_.size() + sparks + 2 > 2048)
        return;
    Particle flash;
    flash.age = age;
    flash.position = position;
    flash.life = .28f;
    flash.size = radius;
    flash.color = color;
    flash.flare = true;
    particles_.push_back(flash);
    flash.life = .12f;
    flash.size = radius * .55f;
    flash.color = 0xffffffff;
    particles_.push_back(flash);
    for (unsigned i = 0; i < sparks; i++) {
        const float y = random() * 2 - 1, a = random() * 6.28318530718f, s = std::sqrt(std::max(0.f, 1 - y * y));
        Particle p;
        p.age = age;
        p.position = position;
        p.velocity = mul(V{std::cos(a) * s, std::abs(y) * .8f + .2f, std::sin(a) * s}, radius * (2.5f + 3.5f * random()));
        p.life = .25f + .35f * random();
        p.size = .5f + .6f * random();
        p.gravity = 9;
        p.color = color;
        particles_.push_back(p);
    }
}
void WeaponEffects::puff(V position, V velocity, float size, float life, uint32_t color) {
    if (particles_.size() >= 2048)
        return;
    Particle p;
    p.position = position;
    p.velocity = velocity;
    p.life = life;
    p.size = size;
    p.color = color;
    p.flare = true;
    particles_.push_back(p);
}
void WeaponEffects::beam(const EffectAssets &assets, V from, V to, V eye, float radius, uint32_t color, float phase,
                         Scene &scene) const {
    ribbon(scene, assets.textures[3], to, from, eye, radius, fade(color, .7f), std::fmod(phase, 1.f), true);
    ribbon(scene, assets.textures[1], to, from, eye, radius * .3f, 0xffffffff);
    sprite(scene, assets.textures[0], to, eye, radius * 2.2f, color);
}
void WeaponEffects::disc(const EffectAssets &assets, V c, float r, uint32_t color, Scene &scene) const {
    quad(scene, assets.textures[0], c,
         {V{c[0] - r, c[1], c[2] - r}, V{c[0] + r, c[1], c[2] - r}, V{c[0] + r, c[1], c[2] + r}, V{c[0] - r, c[1], c[2] + r}},
         color, color);
}
void WeaponEffects::glow(const EffectAssets &assets, V at, V eye, float radius, uint32_t color, Scene &scene) const {
    sprite(scene, assets.textures[0], at, eye, radius, color);
}
void WeaponEffects::render(const EffectAssets &assets, std::span<const Projectile> projectiles,
                           float alpha, V eye, Scene &scene) const {
    alpha = std::clamp(alpha, 0.f, 1.f);
    float offset = (alpha - 1) / 30;
    const auto &b = assets.source.blaster;
    for (const auto &p : projectiles) {
        if (p.life <= 0 && alpha >= p.end_fraction)
            continue;
        float age = std::max(0.f, p.age + offset);
        float fraction = p.end_fraction > 0 ? std::min(1.f, alpha / p.end_fraction) : 1;
        V head = add(p.previous, mul(sub(p.position, p.previous), fraction));
        V direction = unit(p.velocity);
        // Retail scrolling phase is +.2/tick, spin +1 rad/tick (0x14de20).
        float phase = age * 6, spin = age * 30;
        float travelled = std::sqrt(dot(sub(head, p.origin), sub(head, p.origin)));
        V tail;
        // Native cap prevents a two-second bring-up projectile becoming a
        // hundred-unit trail; the original shot length grows by 2/tick.
        tail = sub(head, mul(direction, std::min(travelled, 3.f)));
        uint32_t tint = p.hostile ? 0xff5040ff : b.trail;
        ribbon(scene, assets.textures[3], head, tail, eye, b.trail_radius, fade(tint, .65f),
               std::fmod(phase, 1.f), true);
        ribbon(scene, assets.textures[1], head, tail, eye, b.trail_radius * .22f, tint);
        sprite(scene, assets.textures[0], head, eye, b.glow_radius, p.hostile ? 0xc85040ff : b.glow,
               spin);
        sprite(scene, assets.textures[2], head, eye, b.glow_radius * .4f, fade(0xffffffff, .85f),
               spin);
    }
    for (const auto &p : particles_) {
        float t = p.age + offset;
        if (t < 0 || t >= p.life)
            continue;
        V at = add(p.position, mul(p.velocity, t));
        at[1] -= .5f * p.gravity * t * t;
        float fade_out = 1 - t / p.life;
        fade_out *= fade_out;
        if (p.flare)
            sprite(scene, assets.textures[0], at, eye, p.size * (1 + t / p.life),
                   fade(p.color, fade_out));
        else {
            V tail = add(p.position, mul(p.velocity, std::max(0.f, t - .025f)));
            float tt = std::max(0.f, t - .025f);
            tail[1] -= .5f * p.gravity * tt * tt;
            // Subpixel streaks retain a small bright core and fade continuously.
            ribbon(scene, assets.textures[4], at, tail, eye, p.size * .07f,
                   fade(p.color, fade_out));
            sprite(scene, assets.textures[4], at, eye, p.size * .1f, fade(p.color, fade_out));
        }
    }
}
void WeaponEffects::muzzle(const EffectAssets &assets, V at, float age, V eye, Scene &scene) const {
    // Enhancement envelope; the firing point comes from the original weapon.
    if (age < 0 || age >= .09f)
        return;
    float f = 1 - age / .09f;
    sprite(scene, assets.textures[0], at, eye, .24f + .1f * f, fade(assets.source.blaster.glow, f));
    sprite(scene, assets.textures[2], at, eye, .16f, fade(0xffffffff, f));
}
} // namespace sm
