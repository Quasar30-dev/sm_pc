#pragma once
#include "gameplay.h"
#include "hig/higcatalog.h"

namespace sm {
struct PhotoAsset {
    hig::ActorModel model;
    hig::AnimationBank animations;
    std::vector<hig::LevelTexture> textures;
    size_t catalog = 0;
    bool ratchet = false;
};
struct PhotoActor {
    uint64_t id = 0;
    size_t asset = 0;
    std::string name;
    hig::Vec3 position{}, rotation{}; // pitch, yaw, roll in radians
    float scale = 1, time = 0, speed = 1;
    unsigned clip = 0;
    bool visible = true, playing = false, loop = true, clank = true, auto_attachments = true;
    // 0 = none, 1 = wrench, 2 = blaster; attachment clocks can be posed separately.
    unsigned weapon = 0, weapon_clip = 0, clank_clip = 0;
    float attachment_time = 0;
};
// Photo actors own transforms and playback state; no references into gameplay
// actors, save data, collision state or enemy AI. All methods are CPU/headless.
class PhotoSession {
  public:
    std::vector<hig::ActorCatalogEntry> catalog;
    std::vector<PhotoAsset> assets;
    std::vector<PhotoActor> actors;
    uint64_t selected = 0;
    bool show_player = true, show_enemies = true;
    void seed(const PlayerAssets &);
    bool load(const hig::IsoFs &, size_t catalog_index, size_t &asset, std::string &error);
    uint64_t spawn(size_t asset, hig::Vec3 position, float heading = 0);
    uint64_t duplicate(uint64_t id);
    bool remove(uint64_t id);
    PhotoActor *find(uint64_t id);
    const hig::AnimationClip &clip(const PhotoActor &) const;
    std::pair<hig::Vec3, hig::Vec3> bounds(const PhotoActor &) const;
    void tick(float seconds);
    void render(const PlayerAssets &, Scene &, int texture_base) const;
    void append_textures(std::vector<hig::LevelTexture> &) const;

  private:
    uint64_t next_id_ = 1;
};
bool read_photo_catalog(const hig::IsoFs &, const std::string &wad,
                        std::vector<hig::ActorCatalogEntry> &, std::string &error);
} // namespace sm
