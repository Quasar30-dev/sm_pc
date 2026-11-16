#pragma once
#include "hig/higvfx.h"
#include "scene.h"

namespace sm {
struct EffectAssets {
    hig::WeaponFxData source;
    // Original registry slots 31,12,83,30,60,15 (0x14d660).
    std::array<int, 6> textures{-1, -1, -1, -1, -1, -1};
};
bool load_effect_assets(const hig::IsoFs &, std::span<const hig::ActorAsset>, EffectAssets &,
                        std::vector<hig::LevelTexture> &, std::string &);
struct Projectile {
    hig::Vec3 position{}, velocity{};
    float life = 0;
    bool hostile = false;
    hig::Vec3 previous{}, origin{};
    float age = 0;
    float end_fraction = 1;
};
class WeaponEffects {
  public:
    void tick(float dt);
    void impact(const EffectAssets &, hig::Vec3 position, hig::Vec3 normal, bool hostile = false,
                float age = 0);
    // An explosion: a flare of the given radius and colour (0xRRGGBBAA) and sparks thrown outwards.
    void burst(hig::Vec3 position, float radius, uint32_t color, unsigned sparks = 24, float age = 0);
    // One soft glow drifting with a velocity (flame, smoke, a trail's breath).
    void puff(hig::Vec3 position, hig::Vec3 velocity, float size, float life, uint32_t color);
    // A straight beam between two points, drawn this frame (laser, a tether).
    void beam(const EffectAssets &, hig::Vec3 from, hig::Vec3 to, hig::Vec3 eye, float radius, uint32_t color,
              float phase, Scene &) const;
    // A glow lying flat on the ground (a pool, a ring on the floor), drawn this frame.
    void disc(const EffectAssets &, hig::Vec3 center, float radius, uint32_t color, Scene &) const;
    // A glow at a point, drawn this frame.
    void glow(const EffectAssets &, hig::Vec3 at, hig::Vec3 eye, float radius, uint32_t color, Scene &) const;
    void clear() { particles_.clear(); }
    size_t particles() const { return particles_.size(); }
    void render(const EffectAssets &, std::span<const Projectile>, float alpha, hig::Vec3 eye,
                Scene &) const;
    void muzzle(const EffectAssets &, hig::Vec3 at, float age, hig::Vec3 eye, Scene &) const;

  private:
    struct Particle {
        hig::Vec3 position{}, velocity{};
        float age = 0, life = 0, size = 0, gravity = 0;
        uint32_t color = 0;
        bool flare = false;
    };
    std::vector<Particle> particles_;
    uint32_t random_ = 0x53a917c1;
    float random();
};
} // namespace sm
