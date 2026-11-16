#pragma once
// Engine-aware demo pilot for Size Matters footage: produces the pad a player would, each frame,
// from live state (enemies, hostile projectiles, collision). PC tooling, not retail behaviour.
#include "gameplay.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sm {

class Pilot {
  public:
    struct Stats {
        unsigned engagements = 0, switches = 0, jumps = 0, melee = 0, dodges = 0, stuck = 0, routes = 0;
        unsigned fire_frames = 0;
        size_t nav_nodes = 0;
        float travelled = 0;
    };
    // "combat:40,tour:20" (also seed:N)
    explicit Pilot(const std::string &plan);
    // The pad for this frame; camera_yaw is the yaw the game will use for movement.
    hig::PadSample sample(const Gameplay &, float camera_yaw, float dt);
    // Footage camera: eases in behind Ratchet's heading.
    void steer_camera(float &yaw, float &pitch, const Gameplay &, float dt) const;
    bool finished() const { return step_ >= steps_.size(); }
    std::string status() const;
    const Stats &stats() const { return stats_; }

  private:
    struct Step {
        std::string action;
        float seconds = 30;
    };
    struct Node {
        hig::Vec3 p{};
        std::vector<uint32_t> edges;
    };
    void build_nav(const Gameplay &);
    std::optional<uint32_t> nearest(hig::Vec3 p, float within) const;
    bool route_to(hig::Vec3 from, hig::Vec3 to);
    hig::Vec3 waypoint(const Gameplay &);
    bool clear(const Gameplay &, hig::Vec3 a, hig::Vec3 b) const;
    bool ground_ahead(const Gameplay &, float heading, float distance) const;
    void move(hig::PadSample &, float heading, float camera_yaw, float speed) const;
    float random(float lo, float hi);

    std::vector<Step> steps_;
    size_t step_ = 0;
    float age_ = 0, burst_ = 0, melee_age_ = 0, dodge_age_ = 0, side_ = 1, side_age_ = 0, pause_ = 0, lost_ = 0;
    uint32_t rng_ = 7, pulse_ = 0;
    std::vector<Node> nav_;
    std::unordered_map<int64_t, std::vector<uint32_t>> cells_;
    bool built_ = false;
    std::vector<hig::Vec3> route_;
    std::vector<uint32_t> route_ids_;
    size_t next_ = 0;
    std::unordered_map<uint32_t, float> blocked_;
    std::optional<size_t> target_;
    float target_age_ = 0, fight_age_ = 0, target_health_ = 0;
    std::unordered_map<size_t, float> ignored_;
    std::optional<hig::Vec3> goal_;
    hig::Vec3 last_{}, window_start_{};
    bool have_last_ = false;
    float window_ = 0, stuck_age_ = 0;
    Stats stats_;
    std::string doing_ = "starting";
};

} // namespace sm
