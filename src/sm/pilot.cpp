#include "pilot.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace sm {
namespace {
constexpr float pi = 3.14159265358979f, tau = 6.28318530717959f;
constexpr float spacing = 1.25f, nav_radius = 90, step_up = .45f, body = .3f, walkable = .65f;
constexpr size_t nav_cap = 24000;
using V = hig::Vec3;
V add(V a, V b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
float flat(V a, V b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
float dist(V a, V b) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
// Gameplay convention: heading h faces {sin h, 0, cos h}.
float bearing(V from, V to) { return std::atan2(to[0] - from[0], to[2] - from[2]); }
float wrap(float a) {
    while (a > pi) a -= tau;
    while (a < -pi) a += tau;
    return a;
}
int64_t key(int x, int z) { return (int64_t(x) << 32) ^ int64_t(uint32_t(z)); }
bool alive(const Enemy &e) { return e.state != EnemyState::Dead && !e.hidden && e.health > 0; }
} // namespace

Pilot::Pilot(const std::string &plan) {
    std::stringstream in(plan);
    std::string item;
    while (std::getline(in, item, ',')) {
        if (item.empty())
            continue;
        const auto colon = item.find(':');
        Step s{item.substr(0, colon), colon == std::string::npos ? 30.f : std::stof(item.substr(colon + 1))};
        if (s.action == "seed") {
            rng_ = uint32_t(s.seconds) * 2654435761u + 1;
            continue;
        }
        if (s.action != "combat" && s.action != "tour" && s.action != "idle")
            throw std::runtime_error("Size Matters pilot actions are combat, tour and idle");
        if (!std::isfinite(s.seconds) || s.seconds <= 0 || s.seconds > 3600)
            throw std::runtime_error("Pilot step needs 0-3600 seconds");
        steps_.push_back(s);
    }
    if (steps_.empty())
        throw std::runtime_error("Empty pilot plan");
}

float Pilot::random(float lo, float hi) {
    rng_ = rng_ * 1664525u + 1013904223u;
    return lo + (hi - lo) * float(rng_ >> 8) / float(1u << 24);
}

std::string Pilot::status() const { return (finished() ? std::string("finished") : steps_[step_].action) + ": " + doing_; }

void Pilot::build_nav(const Gameplay &g) {
    built_ = true;
    nav_.clear();
    cells_.clear();
    const auto &q = g.terrain();
    auto floor = [&](float x, float z, float top, float bottom) -> std::optional<V> {
        const auto hit = q.ray({x, top, z}, {x, bottom, z});
        if (!hit.hit || hit.normal[1] < walkable)
            return {};
        return V{hit.point[0], hit.point[1] + .02f, hit.point[2]};
    };
    auto insert = [&](V p) -> std::pair<uint32_t, bool> {
        const int x = int(std::lround(p[0] / spacing)), z = int(std::lround(p[2] / spacing));
        auto &column = cells_[key(x, z)];
        for (auto id : column)
            if (std::abs(nav_[id].p[1] - p[1]) < .5f)
                return {id, false};
        nav_.push_back({p, {}});
        column.push_back(uint32_t(nav_.size() - 1));
        return {uint32_t(nav_.size() - 1), true};
    };
    const V at = g.position;
    auto start = floor(at[0], at[2], at[1] + 1, at[1] - 2);
    if (!start)
        return;
    std::queue<uint32_t> open;
    open.push(insert(*start).first);
    const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty() && nav_.size() < nav_cap) {
        const uint32_t id = open.front();
        open.pop();
        const V p = nav_[id].p;
        for (int k = 0; k < 8; ++k) {
            const float x = std::round(p[0] / spacing) * spacing + dx[k] * spacing, z = std::round(p[2] / spacing) * spacing + dz[k] * spacing;
            if (std::hypot(x - at[0], z - at[2]) > nav_radius)
                continue;
            const auto n = floor(x, z, p[1] + step_up + .8f, p[1] - 1.5f);
            if (!n)
                continue;
            const float rise = (*n)[1] - p[1];
            if (rise > step_up + .1f || rise < -1.5f)
                continue;
            // Two body spheres (knee and chest) must pass between the floors.
            bool blocked = false;
            for (float h : {step_up + body, 1.1f}) {
                const auto hit = q.sweep_sphere({p[0], p[1] + h, p[2]}, {(*n)[0], (*n)[1] + h, (*n)[2]}, body);
                if (hit.hit && hit.fraction < .999f)
                    blocked = true;
            }
            if (blocked)
                continue;
            const auto [other, fresh] = insert(*n);
            if (other == id)
                continue;
            auto &e = nav_[id].edges;
            if (std::find(e.begin(), e.end(), other) == e.end())
                e.push_back(other);
            if (rise > -step_up - .1f) {
                auto &back = nav_[other].edges;
                if (std::find(back.begin(), back.end(), id) == back.end())
                    back.push_back(id);
            }
            if (fresh)
                open.push(other);
        }
    }
    stats_.nav_nodes = nav_.size();
}

std::optional<uint32_t> Pilot::nearest(V p, float within) const {
    std::optional<uint32_t> best;
    float best_d = within;
    const int r = int(std::ceil(within / spacing)), cx = int(std::lround(p[0] / spacing)), cz = int(std::lround(p[2] / spacing));
    for (int x = cx - r; x <= cx + r; ++x)
        for (int z = cz - r; z <= cz + r; ++z) {
            const auto it = cells_.find(key(x, z));
            if (it == cells_.end())
                continue;
            for (auto id : it->second) {
                const float d = flat(nav_[id].p, p) + std::abs(nav_[id].p[1] - p[1]) * .5f;
                if (d < best_d) {
                    best_d = d;
                    best = id;
                }
            }
        }
    return best;
}

bool Pilot::route_to(V from, V to) {
    route_.clear();
    route_ids_.clear();
    next_ = 0;
    const auto a = nearest(from, 4), b = nearest(to, 7);
    if (!a || !b)
        return false;
    std::vector<float> g(nav_.size(), 1e30f);
    std::vector<uint32_t> parent(nav_.size(), UINT32_MAX);
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    g[*a] = 0;
    open.push({dist(nav_[*a].p, nav_[*b].p), *a});
    for (size_t expanded = 0; !open.empty() && expanded < 60000; ++expanded) {
        const auto [f, id] = open.top();
        open.pop();
        if (id == *b)
            break;
        for (auto n : nav_[id].edges) {
            if (n != *b && blocked_.contains(n))
                continue;
            const float cost = g[id] + dist(nav_[id].p, nav_[n].p);
            if (cost < g[n]) {
                g[n] = cost;
                parent[n] = id;
                open.push({cost + dist(nav_[n].p, nav_[*b].p), n});
            }
        }
    }
    if (g[*b] >= 1e29f)
        return false;
    for (uint32_t id = *b; id != UINT32_MAX; id = parent[id]) {
        route_.push_back(nav_[id].p);
        route_ids_.push_back(id);
    }
    std::reverse(route_.begin(), route_.end());
    std::reverse(route_ids_.begin(), route_ids_.end());
    ++stats_.routes;
    return true;
}

V Pilot::waypoint(const Gameplay &g) {
    while (next_ < route_.size() && flat(route_[next_], g.position) < .9f)
        ++next_;
    for (int k = 0; k < 2 && next_ + 1 < route_.size(); ++k) { // shortcut only where the body fits
        const V b = route_[next_ + 1];
        if (std::abs(b[1] - g.position[1]) > .5f || flat(b, g.position) > 5)
            break;
        const auto hit = g.terrain().sweep_sphere({g.position[0], g.position[1] + step_up + body, g.position[2]},
                                                  {b[0], b[1] + step_up + body, b[2]}, body);
        if (hit.hit && hit.fraction < .999f)
            break;
        ++next_;
    }
    return next_ < route_.size() ? route_[next_] : route_.empty() ? g.position : route_.back();
}

bool Pilot::clear(const Gameplay &g, V a, V b) const {
    const auto hit = g.terrain().ray(a, b);
    return !hit.hit || hit.fraction > .97f;
}

bool Pilot::ground_ahead(const Gameplay &g, float heading, float distance) const {
    const V p{g.position[0] + std::sin(heading) * distance, g.position[1], g.position[2] + std::cos(heading) * distance};
    return g.terrain().ray({p[0], p[1] + 1, p[2]}, {p[0], p[1] - 1.8f, p[2]}).hit;
}

// Stick for a world heading: the game maps (x, z) through the camera yaw, right-handed.
void Pilot::move(hig::PadSample &s, float heading, float camera_yaw, float speed) const {
    const float r = heading - camera_yaw, x = -std::sin(r) * speed, z = std::cos(r) * speed;
    s.sticks[0] = uint8_t(std::clamp(128.f + x * 127.f, 0.f, 255.f));
    s.sticks[1] = uint8_t(std::clamp(128.f - z * 127.f, 0.f, 255.f));
}

hig::PadSample Pilot::sample(const Gameplay &g, float camera_yaw, float dt) {
    hig::PadSample s;
    if (finished())
        return s;
    if (!built_)
        build_nav(g);
    if (!nearest(g.position, 7) && g.grounded) {
        if ((lost_ += dt) > 1.5f) {
            build_nav(g);
            route_.clear();
            lost_ = 0;
        }
    } else
        lost_ = 0;
    for (auto it = blocked_.begin(); it != blocked_.end();)
        if ((it->second -= dt) <= 0) it = blocked_.erase(it); else ++it;
    for (auto it = ignored_.begin(); it != ignored_.end();)
        if ((it->second -= dt) <= 0) it = ignored_.erase(it); else ++it;
    age_ += dt;
    if (age_ >= steps_[step_].seconds) {
        age_ = 0;
        ++step_;
        route_.clear();
        target_.reset();
        goal_.reset();
        if (finished())
            return s;
    }
    const auto &action = steps_[step_].action;
    if (action == "idle") {
        doing_ = "idle";
        return s;
    }
    const V chest{g.position[0], g.position[1] + .9f, g.position[2]};
    bool moving = false;
    // Combat: nearest living enemy on the grid; walk in, then fire from a slow walk at it.
    const Enemy *target = nullptr;
    if (action == "combat") {
        if (target_ && *target_ < g.enemies.size() && alive(g.enemies[*target_]))
            target = &g.enemies[*target_];
        if (!target) {
            float best = 1e9f;
            for (size_t i = 0; i < g.enemies.size(); ++i) {
                const auto &e = g.enemies[i];
                if (!alive(e) || ignored_.contains(i))
                    continue;
                // Enemies already attacking or chasing come first.
                const bool active = e.state == EnemyState::Attack || e.state == EnemyState::Chase || e.state == EnemyState::Alert;
                const float d = dist(e.position, g.position) * (active ? .4f : 1.f);
                if (d < best && d < 70 && nearest(e.position, 4)) {
                    best = d;
                    target = &e;
                    target_ = i;
                }
            }
            if (target) {
                target_age_ = fight_age_ = 0;
                target_health_ = target->health;
                route_.clear();
                ++stats_.engagements;
            } else
                target_.reset();
        }
    }
    if (target) {
        target_age_ += dt;
        const V aim{target->position[0], target->position[1] + .6f, target->position[2]};
        const float d = flat(target->position, g.position), to = bearing(g.position, target->position);
        const bool seen = clear(g, chest, aim);
        if (target_age_ > 25 && d > 10) {
            ignored_[*target_] = 40;
            target_.reset();
            doing_ = "giving up on an unreachable enemy";
        } else if (d > 12 || !seen) {
            doing_ = "closing on an enemy";
            if (route_.empty() || flat(route_.back(), target->position) > 5)
                if (!route_to(g.position, target->position)) {
                    ignored_[*target_] = 30;
                    target_.reset();
                }
            move(s, bearing(g.position, waypoint(g)), camera_yaw, 1);
            moving = true;
        } else if ((fight_age_ += dt) > 6 && target->health >= target_health_) {
            ignored_[*target_] = 45; // no damage after six seconds: probably invulnerable or out of reach
            target_.reset();
            doing_ = "switching to a target that takes damage";
        } else {
            if (target->health < target_health_) {
                target_health_ = target->health;
                fight_age_ = 0;
            }
            doing_ = "fighting";
            route_.clear();
            burst_ += dt;
            const float cycle = std::fmod(burst_, 2.8f);
            if (d < 2.2f) { // wrench combo up close
                move(s, to, camera_yaw, .12f);
                if ((melee_age_ += dt) > .3f) {
                    melee_age_ = 0;
                    s.buttons |= hig::Square;
                    ++stats_.melee;
                }
            } else if (cycle < 2.f) { // face it from a slow walk and fire the Blaster
                move(s, to, camera_yaw, d > 9 ? .5f : .12f);
                // The Blaster fires per press: pulse the button (three frames down, three up).
                if (std::abs(wrap(to - g.heading)) < .35f && seen && (++pulse_ % 6) < 3) {
                    s.buttons |= hig::Circle;
                    ++stats_.fire_frames;
                }
            } else { // reposition sideways
                if ((side_age_ += dt) > .8f) {
                    side_age_ = 0;
                    if (random(0, 1) < .5f)
                        side_ = -side_;
                }
                float heading = to + side_ * pi * .5f;
                if (!ground_ahead(g, heading, 1.4f)) {
                    side_ = -side_;
                    heading = to + side_ * pi * .5f;
                }
                move(s, heading, camera_yaw, .9f);
                moving = true;
            }
            dodge_age_ += dt;
            for (const auto &p : g.projectiles)
                if (p.hostile && dist(p.position, chest) < 3.5f && dodge_age_ > 1.2f && g.grounded) {
                    s.buttons |= hig::Cross;
                    dodge_age_ = 0;
                    ++stats_.dodges;
                    ++stats_.jumps;
                    break;
                }
        }
    } else { // tour: walk between distant points of the grid
        if (pause_ > 0) {
            pause_ -= dt;
            doing_ = "taking in the view";
        } else {
            if (!goal_ || route_.empty() || next_ >= route_.size() || flat(*goal_, g.position) < 1.5f) {
                if (goal_ && flat(*goal_, g.position) < 1.5f)
                    pause_ = random(.6f, 1.4f);
                goal_.reset();
                for (int attempt = 0; attempt < 12 && nav_.size() > 8; ++attempt) {
                    const auto &n = nav_[size_t(random(0, float(nav_.size() - 1)))];
                    const float d = flat(n.p, g.position);
                    if (d > 15 && d < 55 && route_to(g.position, n.p)) {
                        goal_ = n.p;
                        break;
                    }
                }
            }
            if (goal_) {
                doing_ = action == "combat" ? "looking for enemies" : "touring";
                move(s, bearing(g.position, waypoint(g)), camera_yaw, .85f);
                moving = true;
            } else
                doing_ = "no route found";
        }
    }
    // Stuck: jump, then route around the spot.
    if (!have_last_) {
        last_ = window_start_ = g.position;
        have_last_ = true;
    }
    if (dist(g.position, last_) > 10) { // respawn
        route_.clear();
        target_.reset();
        goal_.reset();
    }
    stats_.travelled += std::min(2.f, flat(g.position, last_));
    last_ = g.position;
    if ((window_ += dt) >= 1.2f) {
        if (moving && flat(g.position, window_start_) < .4f) {
            stuck_age_ += window_;
            if (g.grounded) {
                s.buttons |= hig::Cross;
                ++stats_.jumps;
            }
            if (stuck_age_ > 3) {
                ++stats_.stuck;
                if (next_ < route_ids_.size()) {
                    const V at = nav_[route_ids_[next_]].p;
                    for (uint32_t id = 0; id < nav_.size(); ++id)
                        if (dist(nav_[id].p, at) < 2)
                            blocked_[id] = 30;
                }
                route_.clear();
                goal_.reset();
                if (target_) {
                    ignored_[*target_] = 20;
                    target_.reset();
                }
                stuck_age_ = 0;
            }
        } else
            stuck_age_ = 0;
        window_ = 0;
        window_start_ = g.position;
    }
    return s;
}

void Pilot::steer_camera(float &yaw, float &pitch, const Gameplay &g, float dt) const {
    // Trail behind Ratchet; the lag keeps turns readable on video.
    yaw += wrap(g.heading - yaw) * std::min(1.f, dt * 2.2f);
    pitch += (-.28f - pitch) * std::min(1.f, dt * 2.f);
}

} // namespace sm
