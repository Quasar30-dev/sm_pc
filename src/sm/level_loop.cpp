#include "level_loop.h"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace sm {
namespace {
uint32_t word(const std::vector<uint8_t> &b, size_t p) {
    uint32_t n = 0;
    if (p + 4 <= b.size())
        std::memcpy(&n, b.data() + p, 4);
    return n;
}
float distance2(hig::Vec3 a, hig::Vec3 b) {
    float n = 0;
    for (int k = 0; k < 3; k++)
        n += (a[k] - b[k]) * (a[k] - b[k]);
    return n;
}
std::string display_help(hig::LocalizedText text, bool controller) {
    // Keep the source text untouched; replace established face-button glyphs
    // only in the presentation copy. Other retail controls stay named as authored.
    std::string decoded;
    const char *buttons[] = {controller ? "A" : "Space", controller ? "X" : "F",
                             controller ? "Y" : "Tab", controller ? "B" : "E"};
    for (unsigned char c : text.encoded) {
        if (c >= 0x80 && c <= 0x83)
            decoded += buttons[c - 0x80];
        else
            decoded += char(c);
    }
    text.encoded = std::move(decoded);
    return hig::inspect_localized_text(text);
}
} // namespace
bool LevelLoop::start(const hig::IsoFs &disc, const World &world, std::string &error,
                      const EncounterAssets *assets) {
    *this = LevelLoop{};
    world_ = &world;
    if ((world.disc_path.size() != 11 && world.disc_path.size() != 12) ||
        !world.disc_path.starts_with("/WAD/") || !world.disc_path.ends_with(".WAD") ||
        std::from_chars(world.disc_path.data() + 5, world.disc_path.data() + 7, level_).ec !=
            std::errc{}) {
        error = "Level loop requires an indexed WAD";
        return false;
    }
    if (!hig::decode_localization(disc.read("/HUD/HELPDAT.BIN"), help, &error))
        return false;
    if (help.languages.empty() || help.languages[0].entries.size() < 2) {
        error = "Original help desk entries are missing";
        return false;
    }
    hig::Localization labels;
    if (!hig::decode_localization(disc.read("/HUD/LOCALDAT.BIN"), labels, &error))
        return false;
    for (const auto &f : disc.files()) {
        if (!f.path.starts_with("/WAD/") || !f.path.ends_with(".WAD"))
            continue;
        unsigned number = 0;
        auto result = std::from_chars(f.path.data() + 5, f.path.data() + f.path.size() - 4, number);
        if (result.ec != std::errc{} || !number)
            continue;
        std::string title = f.path.substr(5, f.path.size() - 9);
        if (number <= 10) {
            if (auto label = labels.find(185 + number))
                title = hig::inspect_localized_text(*label);
        } else {
            switch (number) {
            case 15:
                title = "Metalis - Giant Clank";
                break;
            case 21:
                title = "Challax - Giant Clank";
                break;
            case 22:
                title = "Kalidon - Skyboard race";
                break;
            case 23:
                title = "Medical Outpost - Skyboard race";
                break;
            case 24:
                title = "High Impact Treehouse";
                break;
            case 20:
                title = "Multiplayer lobby";
                break;
            default:
                title = "Multiplayer arena " + std::to_string(number - 15);
                break;
            }
            if (f.path.size() == 12)
                title += " (co-op)";
        }
        destinations.push_back({number, f.path, title});
    }
    std::sort(destinations.begin(), destinations.end(),
              [](const auto &a, const auto &b) { return a.wad < b.wad; });
    if (destinations.empty()) {
        error = "No level destinations on disc";
        return false;
    }
    if (!mechanisms.start(world, assets, error))
        return false;
    // Template 0x1f0ff0, ammo unit cost +0x34, capacity +0x38. Vendor
    // 0xd0c18..0xd0c40 uses these fields; challenge-mode multipliers are separate.
    auto definition = disc.read("/LVL/LEVEL_01.REL");
    ammo_price = word(definition, 0x1f1024);
    if (!ammo_price || ammo_price > 10000) {
        error = "Unsupported vendor ammo definition";
        return false;
    }
    for (const auto &b : world.level.bog_instances) {
        auto hash = world.level.bog_defs.at(b.model_def).hash;
        auto &p = b.properties;
        if (hash == 0x05daf470 && p.size() >= 16) {
            VendorPlacement v;
            std::copy_n(b.matrix, 16, v.matrix.begin());
            v.sells_weapons = p[8] != 0;
            vendors.push_back(v);
        } else if (hash == 0x9f887978 && p.size() >= 8) {
            checkpoints_.push_back({word(p, 0),
                                    {b.matrix[12], b.matrix[13], b.matrix[14]},
                                    std::atan2(b.matrix[8], b.matrix[10]),
                                    p[4]});
        } else if (hash == 0xc81fe9f7 && p.size() >= 0x28) {
            // LEVEL_01.REL 0x18b114..0x18b5ac: original property links and help IDs.
            for (auto [offset, id] : {std::pair{0u, -4},
                                      {8u, -6},
                                      {12u, -7},
                                      {16u, -8},
                                      {20u, -9},
                                      {24u, -10},
                                      {32u, -11},
                                      {36u, -159}})
                hints_.push_back({word(p, offset), id});
        } else if (hash == 0xa88a927c && p.size() >= 32) {
            auto destination = word(p, 4), movie = (b.flags >> 8) & 255;
            if (destination > 0 && destination <= 24 && movie < 26)
                exits_.push_back({word(p, 0), movie, destination});
        }
    }
    error.clear();
    return true;
}
bool LevelLoop::contains(uint32_t link, hig::Vec3 point) const {
    if (!link || !world_)
        return false;
    for (const auto &t : world_->level.triggers)
        if (t.source_offset == link)
            return hig::trigger_contains(t, point);
    return false;
}
unsigned LevelLoop::refill_cost(const Gameplay &game) const {
    uint64_t cost = uint64_t(game.max_ammo - std::min(game.ammo, game.max_ammo)) * ammo_price;
    return unsigned(std::min(cost, uint64_t(std::numeric_limits<unsigned>::max())));
}
void LevelLoop::open_planets() {
    if (destinations.empty())
        return;
    if (panel == LevelPanel::Help)
        help_page_ = std::min(selection, help_count() - 1);
    panel = LevelPanel::Planets;
    selection = 0;
    repeat_.clear();
    for (unsigned i = 0; i < destinations.size(); i++)
        if (destinations[i].wad == world_->disc_path)
            selection = i;
}
void LevelLoop::activate(unsigned row, Gameplay &game) {
    if (panel == LevelPanel::Planets) {
        if (row < destinations.size()) {
            travel_request = destinations[row].wad;
            game.end_interaction();
            close();
        }
        return;
    }
    if (panel == LevelPanel::Help) {
        if (row == help_count()) {
            open_planets();
            return;
        }
        selection = std::min(row, help_count() - 1);
        return;
    }
    if (panel != LevelPanel::Vendor)
        return;
    const auto stock = game.vendor_stock();
    selection = std::min(row, unsigned(stock.size()) + 1);
    if (selection == stock.size() + 1) {
        close();
        return;
    }
    if (selection >= 1) { // one of the things for sale
        const unsigned item = stock[selection - 1];
        const std::string name = item < item_names.size() && !item_names[item].empty() ? item_names[item] : "it";
        if (game.buy(item)) {
            purchases++;
            notice = "You have acquired the " + name + "!";
            selection = 0;
        } else
            notice = "Not enough bolts.";
        notice_time = 5;
        return;
    }
    const unsigned cost = refill_cost(game);
    if (!cost)
        notice = "Ammo is already full.";
    else if (game.bolts < cost)
        notice = "Not enough bolts.";
    else {
        game.bolts -= cost;
        game.ammo = game.max_ammo;
        purchases++;
        notice = "Ammo refilled.";
    }
    notice_time = 5;
}
void LevelLoop::close() {
    if (panel == LevelPanel::Help)
        help_page_ = std::min(selection, help_count() - 1);
    panel = LevelPanel::None;
    release_ = true;
    repeat_.clear();
}
bool LevelLoop::input(const hig::PadState &pad, Gameplay &game) {
    if (release_) {
        if (!pad.raw.buttons)
            release_ = false;
        return false;
    }
    auto pulse = repeat_.tick(pad);
    if (panel != LevelPanel::None) {
        // The vendor finishes opening while its menu owns the gameplay pad.
        if (panel == LevelPanel::Vendor)
            for (auto &v : vendors) {
                v.time += 1.f / 30;
                if (v.open && v.clip == 0 && v.time >= .4f) {
                    v.clip = 2;
                    v.time = 0;
                }
            }
        if (pulse & (hig::Circle | hig::Start | hig::Select))
            close();
        else if (panel == LevelPanel::Help && (pulse & hig::Triangle))
            open_planets();
        else {
            unsigned count = panel == LevelPanel::Help      ? help_count() + 1
                             : panel == LevelPanel::Planets ? unsigned(destinations.size())
                                                            : vendor_rows(game);
            if (pulse & (hig::Up | hig::Left))
                selection = (selection + count - 1) % count;
            if (pulse & (hig::Down | hig::Right))
                selection = (selection + 1) % count;
            if (pulse & hig::Cross)
                activate(selection, game);
        }
        return false;
    }
    if (!game.health || game.swimming)
        return true;
    if (pulse & hig::Select) {
        panel = LevelPanel::Help;
        selection = help_page_;
        repeat_.clear();
        return false;
    }
    objects.input(pad);
    mechanisms.input(pad, game);
    if (mechanisms.planet_request) {
        open_planets();
        return false;
    }
    if (game.interacting())
        return true;
    if ((pulse & hig::Triangle) && nearby_vendor >= 0) {
        panel = LevelPanel::Vendor;
        selection = 0;
        notice.clear();
        repeat_.clear();
        return false;
    }
    return movie_request.empty() && travel_request.empty();
}
void LevelLoop::tick(Gameplay &game, float dt) {
    if (!world_ || !std::isfinite(dt) || dt <= 0 || panel != LevelPanel::None)
        return;
    notice_time = std::max(0.f, notice_time - dt);
    nearby_vendor = -1;
    float nearest = 6.25f; // Original vendor update 0x1c6354; radius 2.5 world units.
    for (size_t i = 0; i < vendors.size(); i++) {
        auto &v = vendors[i];
        float d = distance2(game.position, {v.matrix[12], v.matrix[13], v.matrix[14]});
        bool open = d <= 64;
        if (open != v.open) {
            v.open = open;
            v.clip = open ? 0 : 1;
            v.time = 0;
        } else
            v.time += dt;
        if (v.open && v.clip == 0 && v.time >= .4f) {
            v.clip = 2;
            v.time = 0;
        }
        if (d <= nearest && v.sells_weapons) {
            nearest = d;
            nearby_vendor = int(i);
        }
    }
    for (size_t k = 0; k < mechanisms.objects.size(); k++)
        if (const auto &m = mechanisms.objects[k]; m.kind == MechanismKind::Crank)
            objects.cranks[m.source] = {m.progress, mechanisms.active == int(k) && game.motion == Motion::Crank};
    objects.tick(game, dt);
    mechanisms.extra_colliders.assign(objects.colliders().begin(), objects.colliders().end());
    if (objects.travel_level) { // a behaviour's level change (a teleporter to another level)
        if (travel_request.empty())
            for (const auto &d : destinations)
                if (d.level == objects.travel_level)
                    travel_request = d.wad;
        objects.travel_level = 0;
    }
    if (!objects.notice_request.empty()) {
        notice = std::move(objects.notice_request);
        objects.notice_request.clear();
        notice_time = 4;
    }
    if (!objects.movie_request.empty() && movie_request.empty()) {
        movie_request = std::move(objects.movie_request);
        objects.movie_request.clear();
        next_level = 0;
    }
    mechanisms.tick(game, dt);
    if (!mechanisms.movie_request.empty() && movie_request.empty()) {
        movie_request = std::move(mechanisms.movie_request);
        mechanisms.movie_request.clear();
        next_level = 0;
    }
    if (!game.health)
        return;
    // ContinuePoint (LEVEL_03 0x1569d0): the hero in its trigger makes it the current one unless it already is -
    // by instance, not by its `index`.
    for (auto &c : checkpoints_) {
        bool inside = contains(c.trigger, game.position);
        const int slot = int(&c - checkpoints_.data());
        if (inside && !c.inside && checkpoint_slot != slot) {
            auto from = c.position, to = c.position;
            from[1] += 3;
            to[1] -= 10;
            auto hit = game.terrain().ray(from, to);
            if (hit.hit && hit.normal[1] > .55f) {
                game.spawn = hit.point;
                game.spawn[1] += .005f;
                game.spawn_heading = c.heading;
                checkpoint = int(c.index);
                checkpoint_slot = slot;
                checkpoint_activations++;
                notice = "Checkpoint reached";
                notice_time = 4;
            }
        }
        c.inside = inside;
    }
    for (auto &h : hints_)
        if (!shown_.contains(h.id) && contains(h.trigger, game.position)) {
            // Preserve all original tutorials in the help desk. Contextual popups
            // currently point there without claiming unrecovered gadgets work.
            shown_.insert(h.id);
            notice = "New tutorial available in Help Desk";
            notice_time = 6;
            for (unsigned i = 0; i < help_count(); i++)
                if (help.languages[0].entries[i + 1].id == uint32_t(h.id)) {
                    help_page_ = i;
                    break;
                }
            break;
        }
    for (auto &e : exits_)
        if (!e.fired && contains(e.trigger, game.position)) {
            char path[64];
            std::snprintf(path, sizeof(path), "/VID/L%02u/%02u%c.BIK", level_, level_,
                          'A' + e.movie);
            movie_request = path;
            next_level = e.level;
            e.fired = true;
            break;
        }
}
unsigned LevelLoop::help_count() const {
    return help.languages.empty() ? 1 : unsigned(help.languages[0].entries.size() - 1);
}
std::string LevelLoop::help_text(unsigned page, bool controller) const {
    if (help.languages.empty() || page >= help_count())
        return {};
    return display_help(help.languages[0].entries[page + 1], controller);
}
void LevelLoop::start_objects(const Gameplay &game, const ObjectAssets &assets) {
    if (!world_)
        return;
    std::vector<uint32_t> owned;
    for (const auto &e : game.enemies)
        owned.push_back(e.source_offset);
    for (const auto &m : mechanisms.objects)
        owned.push_back(m.source);
    for (const auto &i : world_->level.bog_instances) {
        const uint32_t hash = world_->level.bog_defs[i.model_def].hash;
        if (hash == 0x05daf470 || hash == 0xfb84ad53) // vendor; the Pokitaru photo camera prop
            owned.push_back(i.source_offset);
    }
    objects.start(*world_, assets, owned);
}
void LevelLoop::render(const EncounterAssets &assets, float alpha, Scene &scene) const {
    mechanisms.render(alpha, scene);
    if (assets.vendor_animations.clips.size() != 3)
        return;
    for (const auto &v : vendors)
        append_animated_actor(assets.vendor, assets.vendor_animations.clips[v.clip],
                              v.time + std::clamp(alpha, 0.f, 1.f) / 30, v.clip == 2, v.matrix,
                              scene);
}
} // namespace sm
