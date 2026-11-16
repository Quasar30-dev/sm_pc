#pragma once
#include "gameplay.h"
#include "hig/highud.h"
#include "hig/higlocalization.h"
#include <filesystem>

namespace sm {
// The pause menu, rebuilt natively on the original's own data: the menus' item tables of the level module (text,
// button plate, text box, selected plate, action: hig::decode_hud_menus) and the plates and boxes of
// /HUD/MAINHUD.W3D they name, so every button stands where the PS2 game draws it. The PC's own settings are a page
// under OPTIONS in the same plates. The pause mode's screens and what is not rebuilt yet:
// docs/SIZE_MATTERS_CAMPAIGN.md, "The pause mode's screens".
// This is the state machine: headless, driven by button pulses; pause_ui.inl draws it.
enum class PausePage : uint8_t {
    Main,           // WEAPONS, GADGETS, QUICK SELECT, ARMOR, HELP, OPTIONS, SPECIAL (HUD screen 36)
    Weapons,
    Gadgets,
    Armor,
    Help,           // HELP MESSAGE LOG, CONTROLS HELP, MOVES HELP (screen 53)
    ControlsHelp,
    Options,        // SAVE, LOAD, CAMERA & CONTROLS, AUDIO VISUAL SETTINGS, QUIT (screen 37) and the PC's GRAPHICS
    AudioVisual,    // screen 42
    CameraControls, // screen 44
    Graphics,       // PC: resolution, frame cap, v-sync
    Special,        // CHEATS, SKINS, SKILL POINTS, CINEMATICS, ARMOR SETS (screen 66)
    Cheats,         // screen 45; here the PC's own play options (the original's cheats are not rebuilt yet)
    Skins,          // screen 65: the original's seven skins and the Ratchets imported from the PS2 games
    ArmorSets,      // screen 41: the sets whose pieces are all owned; one is put on whole
    Quit,
};
struct PauseOptions {
    bool subtitles = true, helpdesk_text = true, helpdesk_voice = true, widescreen = true;
    int music = 7, effects = 9; // 0..10
    bool invert_y = false, invert_x = false, passive_camera = false, strafe = true, vibration = true;
    bool first_person = false, quick_select_pause = true, invert_flight = false;
    bool operator==(const PauseOptions &) const = default;
};
// The PC's own play options, which were only in the F1 window: shown under SPECIAL > CHEATS. The host fills them
// when the menu opens and applies what a pulse changes (PauseAction::ApplyGoodies).
struct PauseGoodies {
    bool uya_camera = false;    // Up Your Arsenal's follow camera
    bool thruster_pack = false; // UYA's Thruster-Pack in place of the Heli-Pack
    bool hydro_pack = true;     // UYA's Hydro-Pack underwater
    bool uya_weapons = false;   // UYA's weapon set in place of Size Matters'
    bool uya_look = false;      // the UYA-inspired colours and filtering
    bool charge_boots = true;   // with an imported Ratchet: double-tap R1
    bool all_weapons = false;   // every weapon and gadget; given once, kept
    int wrench = 0;             // 0 Size Matters' wrench, 1..3 UYA's OmniWrenches (silver, blue, dark / orange)
    bool all_armor = false;     // every piece of every armour set; given once, kept
    bool operator==(const PauseGoodies &) const = default;
};
// The hero's skin: 0..6 Size Matters' own (sm::load_hero_skin's numbers), then the imported PS2 Ratchets.
struct PauseSkin {
    enum : unsigned { UpYourArsenal = 7, GoingCommando, RatchetAndClank, Count };
    unsigned skin = 0;
    int armor = 0; // the imported Ratchet's armour (UYA, GC)
    // An imported Ratchet moves by his own game's controller (its run, jumps, flips, pack moves, charge boots,
    // camera) with Size Matters' weapons and gadgets: Ps2RatchetSettings::own_controller.
    bool own_gameplay = false;
    bool operator==(const PauseSkin &) const = default;
};
// A shape of the HUD file: an element as the file places it, or a template (a plate) at a place of ours.
struct PauseShape {
    unsigned element = 0, tmpl = 0;
    float x = 0, y = 0;
    bool set() const { return element || tmpl; }
};
struct PauseRow {
    uint32_t id = 0;          // the item's text id in the module's tables, or one of ours
    std::string label, value; // in the retail font's encoding
    bool enabled = true;
    bool slider = false; // `fraction` drawn as a bar in the value box (volumes)
    float fraction = 0;
    unsigned icon = 0;   // a HUD icon id (a weapon's)
    bool marked = false; // the equipped weapon
    // Where the original draws it, in the HUD's 480 x 272 space. Rows without a plate use the list layout.
    PauseShape plate, selected, value_plate;
    std::array<float, 4> label_box{}, value_box{}; // left, top, right, bottom
};
enum class PauseAction : uint8_t { None, Resume, QuitToTitle, RestartLevel, ApplyGraphics, ApplyGoodies, ApplySkin };
class PauseMenu {
  public:
    // The HUD layout and the module's menu tables; without them the pages fall back to plain lists.
    void set_retail(const hig::HudLayout *layout, std::vector<hig::HudMenu> menus);
    void open();
    // One tick of menu input: hig button bits, edges and repeats. `game` for equipping.
    PauseAction pulse(uint32_t buttons, Gameplay &game);
    // Moves the cursor off a row that cannot be chosen (a page's first row may be one not built yet). Each pulse
    // does it; call it before drawing a page that has had no pulse yet.
    void settle(const Gameplay &game);
    PausePage page() const { return page_; }
    unsigned selection() const { return selection_; }
    // The page's title, empty for the pages the original draws without one.
    std::string title(const hig::Localization &) const;
    std::vector<PauseRow> rows(const Gameplay &, const hig::Localization &) const;
    // The selected row's longer text (a weapon's description, a page's note); empty when it has none.
    std::string detail(const Gameplay &, const hig::Localization &) const;
    // Pages that show a column of buttons beside a text panel.
    bool has_detail() const {
        return page_ == PausePage::Weapons || page_ == PausePage::Gadgets || page_ == PausePage::Armor ||
               page_ == PausePage::ControlsHelp;
    }
    // Pages of settings: a label plate and a value box a row, the title bar and the bar of hints (screens 42, 44).
    bool settings_page() const {
        return page_ == PausePage::AudioVisual || page_ == PausePage::CameraControls || page_ == PausePage::Graphics;
    }
    // Pages drawn as a list in the panels of the original's screen, with a panel of text (CHEATS, SKINS).
    bool panel_page() const { return page_ == PausePage::Cheats || page_ == PausePage::Skins || page_ == PausePage::ArmorSets; }
    const hig::HudLayout *layout() const { return layout_; }
    PauseOptions options;
    PauseGoodies goodies;
    PauseSkin skin;         // the row the cursor's choice last asked for (ApplySkin)
    PauseSkin wearing;      // what the hero wears now; the host keeps it current
    // The Graphics page's pending values; main applies and saves them on ApplyGraphics.
    struct Graphics {
        unsigned width = 1920, height = 1080, fps = 60;
        bool vsync = false;
    } graphics;
    std::string notice; // a line of feedback ("Graphics settings saved.")

  private:
    PausePage page_ = PausePage::Main;
    unsigned selection_ = 0;
    std::vector<std::pair<PausePage, unsigned>> stack_;
    const hig::HudLayout *layout_ = nullptr;
    std::vector<hig::HudMenu> menus_;
    void go(PausePage);
    bool back(); // false at the top page
    std::vector<unsigned> listed(const Gameplay &) const; // weapon table indices shown on the current page
    const hig::HudMenu *menu(uint32_t first_text) const;  // a module menu by its first item's text id
    void place(PauseRow &, const hig::HudMenuItem &, bool with_value) const;
};
// Text of a localisation entry in the font's encoding (colour controls dropped); `fallback` when it is absent.
std::string pause_text(const hig::Localization &, uint32_t id, const char *fallback = "");
PauseOptions read_pause_options(const std::filesystem::path &);
bool save_pause_options(const std::filesystem::path &, const PauseOptions &);
} // namespace sm
