#pragma once
#include "gameplay.h"
#include "hig/hignavigation.h"
#include <unordered_set>

namespace sm {
enum class MechanismKind { Crank, Cranked, Elevator, Ship, Luna, Camera };
struct Mechanism {
    MechanismKind kind{};
    uint32_t source = 0, model = 0, link = 0, variant = 0;
    hig::Matrix base{}, matrix{}, previous{};
    hig::Vec3 from{}, to{};
    float progress = 0, time = 0, duration = 1, amount = 0;
    float heading = 0, previous_heading = 0;
    unsigned clip = 0, stage = 0;
    unsigned advance_stage = 0; // highest sequential escort trigger reached by Ratchet
    bool complete = false, moving = false, reverse = false, aboard = false;
    std::vector<hig::Vec3> route;
    std::vector<uint32_t> triggers, targets, controllers;
    hig::PoseTransition transition;
    std::vector<hig::BonePose> pose, previous_pose;
    unsigned pose_clip = ~0u;
    unsigned nav_point=0, jumps=0;
    hig::LunaJump jump;
    float jump_time=-1, landing_time=0;
};
// Retail placement/path decoding is shared with Theia. Native state machines
// translate the recovered behavior without running guest code.
class Mechanisms {
  public:
    bool start(const World &, const EncounterAssets *, std::string &error);
    void input(const hig::PadState &, Gameplay &);
    void tick(Gameplay &, float dt);
    void render(float alpha, Scene &) const;
    void movie_finished();
    std::string prompt(bool controller) const;
    std::vector<Mechanism> objects;
    struct Zipline {
        uint32_t source = 0;
        hig::Vec3 from{}, to{};
        float length = 0;
    };
    std::vector<Zipline> ziplines;
    int nearby = -1, nearby_zip = -1, active = -1, active_zip = -1;
    bool planet_request = false;
    unsigned photos = 0, zip_rides = 0, elevators_used = 0, cranks_completed = 0;
    std::string movie_request;
    // Solid bodies of other systems (the object world) for this tick, added to the mechanisms' own.
    std::vector<TerrainQuery::Object> extra_colliders;

  private:
    const World *world_ = nullptr;
    const EncounterAssets *assets_ = nullptr;
    float zip_time_ = 0, zip_distance_ = 0, zip_speed_ = 0, zip_cooldown_ = 0;
    hig::Vec3 zip_origin_{};
    float turn_input_ = 0;
    unsigned respawns_ = 0;
    uint32_t movie_first_ = 0, movie_second_ = 0;
    bool movie_armed_ = false, movie_fired_ = false;
    uint32_t camera_model_ = 0;
    std::unordered_set<uint32_t> photo_taken_;
    const hig::BogInstance *instance(uint32_t source) const;
    const hig::BogCurve *curve(uint32_t source) const;
    bool contains(uint32_t source, hig::Vec3) const;
    bool completed(uint32_t source) const;
    void update_collision(Gameplay &);
    void move_luna(Mechanism &,Gameplay &,float dt);
};
} // namespace sm
