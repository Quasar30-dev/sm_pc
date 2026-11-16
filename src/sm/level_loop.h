#pragma once
#include "frontend.h"
#include "gameplay.h"
#include "hig/higlocalization.h"
#include "mechanisms.h"
#include "objects.h"
#include <unordered_set>

namespace sm {
enum class LevelPanel { None, Vendor, Help, Planets };
struct VendorPlacement {
    hig::Matrix matrix{};
    unsigned clip = 1;
    float time = .4f;
    bool open = false, sells_weapons = false;
};
class LevelLoop {
  public:
    bool start(const hig::IsoFs &, const World &, std::string &error,
               const EncounterAssets *assets = nullptr);
    // Returns true only when the pad may reach gameplay. Opening, closing and
    // held dismissal buttons are consumed here, including between fixed ticks.
    bool input(const hig::PadState &, Gameplay &);
    void tick(Gameplay &, float dt = 1.f / 30);
    void render(const EncounterAssets &, float alpha, Scene &) const;
    void activate(unsigned row, Gameplay &); // Same path for mouse/controller.
    void close();
    void open_planets();
    struct Destination {
        unsigned level;
        std::string wad, name;
    };
    std::vector<Destination> destinations;
    Mechanisms mechanisms;
    // Every placed class the systems above do not run. Start after the enemies and this loop have started.
    ObjectWorld objects;
    void start_objects(const Gameplay &, const ObjectAssets &);
    LevelPanel panel = LevelPanel::None;
    unsigned selection = 0, checkpoint_activations = 0, purchases = 0;
    int nearby_vendor = -1, checkpoint = -1;
    int checkpoint_slot = -1; // which ContinuePoint instance is the current one (Kalidon's all carry index 0)
    std::vector<VendorPlacement> vendors;
    hig::Localization help;
    std::string notice, movie_request, travel_request;
    float notice_time = 0;
    unsigned next_level = 0;
    unsigned refill_cost(const Gameplay &) const;
    // The vendor's rows: the refill, what it has for sale (Gameplay::vendor_stock), and leaving.
    unsigned vendor_rows(const Gameplay &game) const { return unsigned(game.vendor_stock().size()) + 2; }
    std::array<std::string, 32> item_names; // by weapon table index, in the game's own words (set by the host)
    std::string help_text(unsigned page, bool controller) const;
    unsigned help_count() const;
    unsigned ammo_price = 1;

  private:
    const World *world_ = nullptr;
    struct Checkpoint {
        uint32_t trigger;
        hig::Vec3 position;
        float heading;
        unsigned index;
        bool inside = false;
    };
    struct Hint {
        uint32_t trigger;
        int id;
    };
    struct Exit {
        uint32_t trigger;
        unsigned movie, level;
        bool fired = false;
    };
    std::vector<Checkpoint> checkpoints_;
    std::vector<Hint> hints_;
    std::vector<Exit> exits_;
    std::unordered_set<int> shown_;
    bool release_ = false;
    unsigned level_ = 0;
    unsigned help_page_ = 0;
    MenuRepeat repeat_;
    bool contains(uint32_t trigger, hig::Vec3) const;
};
} // namespace sm
