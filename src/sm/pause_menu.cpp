#include "pause_menu.h"
#include "extras.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace sm {
namespace {
constexpr std::pair<unsigned, unsigned> resolutions[] = {{1280, 720},  {1600, 900},  {1920, 1080},
                                                         {2560, 1440}, {3440, 1440}, {3840, 2160}};
constexpr unsigned rates[] = {30, 60, 120, 144, 240};
// HUD templates of the USA disc's MAINHUD.W3D: a button plate, its selected fill, a value box.
constexpr unsigned PLATE = 764, PLATE_SELECTED = 765, VALUE_BOX = 773;
// Rows of our own (not an item of the module's tables) carry ids from here.
enum : uint32_t {
    ROW_GRAPHICS = 100000, ROW_RESOLUTION, ROW_FRAME_CAP, ROW_VSYNC, ROW_APPLY, ROW_NO, ROW_YES,
    ROW_ITEM = 110000,  // + a weapon's table index
    ROW_CHEAT = 120000, // + one of the Cheat values
    ROW_SKIN = 130000,  // + a PauseSkin::skin
    ROW_SKIN_GAMEPLAY = 140000,
    ROW_ARMOR_SET = 150000, // + a set, 1 to 13
};
enum Cheat : uint32_t { CheatCamera, CheatStrafe, CheatAirPack, CheatHydroPack, CheatWeaponSet, CheatWrench, CheatLook, CheatChargeBoots, CheatAllWeapons, CheatAllArmor, CheatCount };
// The ARMOR page's rows: the text of each kind of piece and the place of the body it is worn at.
constexpr std::pair<uint32_t, unsigned> armor_rows[4] = {{549, 1}, {550, 0}, {551, 2}, {552, 4}};
constexpr const char *wrench_names[4] = {"Size Matters", "UYA Silver", "UYA Blue", "UYA Orange"};
struct CheatText {
    const char *label, *off, *on, *note;
};
// "off" / "on" empty: the localised Off / On
constexpr CheatText cheat_text[CheatCount] = {
    {"UYA Follow Camera", "", "", "Up Your Arsenal's follow camera in place of Size Matters' own."},
    {"Strafe", "", "", "Hold LT / Z to keep facing the camera's way while moving sideways or backwards."},
    {"Air Pack", "Heli-Pack", "Thruster-Pack",
     "The pack on Ratchet's back. The Thruster-Pack is read from your Up Your Arsenal disc. Hold RB / C and jump: a "
     "high jump standing, a long jump with a direction."},
    {"Hydro-Pack", "", "", "Up Your Arsenal's Hydro-Pack underwater: hold RB / C to boost. Read from your Up Your Arsenal disc."},
    {"Weapon Set", "Size Matters", "Up Your Arsenal",
     "Size Matters' own weapons, or the Shock Blaster and Nitro Launcher read from your Up Your Arsenal disc. Each set "
     "keeps its own ammunition."},
    {"Wrench", "", "",
     "The wrench in Ratchet's hand: Size Matters' own, or one of Up Your Arsenal's OmniWrenches read from your Up Your "
     "Arsenal disc. The swings, the throw and their damage stay Size Matters'."},
    {"Presentation", "Size Matters", "Up Your Arsenal",
     "The scene's colours and filtering: Size Matters' own, or a look after Up Your Arsenal. The HUD and the movies stay "
     "as they are."},
    {"Charge Boots", "", "",
     "With a Ratchet imported from Up Your Arsenal or Going Commando (SPECIAL, SKINS): double-tap RB / Shift to dash."},
    {"All Weapons", "Give", "Given", "Every weapon and gadget of the game, with full ammunition."},
    {"All Armor", "Give", "Given", "Every piece of the seven armor sets. Wear them on the pause menu's ARMOR page."},
};
// A build without the borrowed play options (extras.h) offers only the rows that are Size Matters' own or the PC's.
constexpr bool cheat_shown(uint32_t k) {
    return extras_built || k == CheatStrafe || k == CheatLook || k == CheatAllWeapons || k == CheatAllArmor;
}
// The cheat of the page's row `selection`, CheatCount past the last.
constexpr uint32_t cheat_at(size_t selection) {
    for (uint32_t k = 0; k < CheatCount; k++)
        if (cheat_shown(k) && selection-- == 0)
            return k;
    return CheatCount;
}
// by PauseSkin::skin: the name's text id (the original's SKINS page table, LEVEL_02 0x229190) and a fallback
constexpr std::pair<uint32_t, const char *> skin_names[PauseSkin::Count] = {
    {4018, "Wear Current Armor"}, {4024, "Kanga-Ratchet"},     {4023, "Hiro Ratchet"},
    {4022, "Tropical Vacation"},  {4020, "Ratchetzilla"},      {4019, "Plundering Pirate Captain"},
    {4021, "Super Incognito"},    {0, "Up Your Arsenal Ratchet"}, {0, "Going Commando Ratchet"},
    {0, "Ratchet & Clank Ratchet"}};
// The page's rows: the original's six in its own order, then the skin it leaves out, then the imported Ratchets.
constexpr unsigned skin_order[PauseSkin::Count] = {0, 1, 2, 3, 5, 4, 6, 7, 8, 9};
std::string on_off(const hig::Localization &text, bool on) { return pause_text(text, on ? 471 : 472, on ? "On" : "Off"); }
std::string normal_inverted(const hig::Localization &text, bool inverted) {
    return pause_text(text, inverted ? 481 : 480, inverted ? "Inverted" : "Normal");
}
} // namespace

std::string pause_text(const hig::Localization &text, uint32_t id, const char *fallback) {
    const auto *entry = text.find(id);
    if (!entry)
        return fallback;
    std::string out;
    for (size_t i = 0; i < entry->encoded.size(); i++) {
        const uint8_t c = uint8_t(entry->encoded[i]);
        if (c == 0x90 && i + 1 < entry->encoded.size()) { // inline colour control and its argument
            i++;
            continue;
        }
        out += char(c);
    }
    return out;
}

void PauseMenu::set_retail(const hig::HudLayout *layout, std::vector<hig::HudMenu> menus) {
    layout_ = layout;
    menus_ = std::move(menus);
}
const hig::HudMenu *PauseMenu::menu(uint32_t first_text) const {
    if (layout_)
        for (const auto &m : menus_)
            if (!m.items.empty() && m.items[0].text == first_text)
                return &m;
    return nullptr;
}
void PauseMenu::place(PauseRow &row, const hig::HudMenuItem &item, bool with_value) const {
    const auto &elements = layout_->elements;
    row.plate.element = item.plate;
    row.selected.element = item.selected_plate;
    row.label_box = elements[item.bounds].bounds;
    if (!with_value)
        return;
    // its value box: the bounds element level with the label's box that starts 22 to 24 units right of it
    // (Subtitles: label 82..286, value 309..405; a slider's is two units lower and eight tall)
    float best = 1e9f;
    for (const auto &e : elements) {
        const float gap = e.bounds[0] - row.label_box[2], rise = std::abs(e.bounds[1] - row.label_box[1]);
        if (e.kind == 2 && gap >= 20 && gap <= 26 && rise <= 2 && e.bounds[2] - e.bounds[0] > 60 && rise < best) {
            best = rise;
            row.value_box = e.bounds;
        }
    }
    // and the value plate level with the label's plate
    const auto &plate = elements[item.plate];
    if (plate.kind == 3)
        for (size_t i = 0; i < elements.size(); i++)
            if (elements[i].kind == 3 && elements[i].reference == VALUE_BOX && std::abs(elements[i].y - plate.y) <= 1 &&
                elements[i].x > plate.x)
                row.value_plate.element = unsigned(i);
    // or a box drawn for this row alone on the item's screen (Sound's wider one, element 1326 of screen 42)
    if (!row.value_plate.set())
        for (const auto &screen : layout_->screens)
            if (std::find(screen.begin(), screen.end(), item.plate) != screen.end())
                for (unsigned i : screen)
                    if (elements[i].kind == 0 && !elements[i].triangles.empty() && std::abs(elements[i].y - plate.y) <= 1 &&
                        elements[i].x > plate.x)
                        row.value_plate.element = i;
}

void PauseMenu::open() {
    page_ = PausePage::Main;
    selection_ = 0;
    stack_.clear();
    notice.clear();
}
void PauseMenu::go(PausePage next) {
    stack_.push_back({page_, selection_});
    page_ = next;
    selection_ = 0;
    notice.clear();
}
bool PauseMenu::back() {
    if (stack_.empty())
        return false;
    page_ = stack_.back().first;
    selection_ = stack_.back().second;
    stack_.pop_back();
    notice.clear();
    return true;
}
std::vector<unsigned> PauseMenu::listed(const Gameplay &game) const {
    std::vector<unsigned> out;
    if (!game.weapon_tables() || (page_ != PausePage::Weapons && page_ != PausePage::Gadgets))
        return out;
    const auto &weapons = game.weapon_tables()->weapons;
    for (unsigned i = 0; i < weapons.size(); i++)
        if (game.arsenal.owned[i] && (weapons[i].type == 2) == (page_ == PausePage::Weapons))
            out.push_back(i);
    return out;
}

std::string PauseMenu::title(const hig::Localization &text) const {
    switch (page_) {
    case PausePage::Weapons:
        return pause_text(text, 447, "WEAPONS");
    case PausePage::Gadgets:
        return pause_text(text, 448, "GADGETS");
    case PausePage::Armor:
        return pause_text(text, 449, "ARMOR");
    case PausePage::ControlsHelp:
        return pause_text(text, 712, "CONTROLS HELP");
    case PausePage::AudioVisual:
        return pause_text(text, 476, "AUDIO VISUAL SETTINGS");
    case PausePage::CameraControls:
        return pause_text(text, 475, "CAMERA & CONTROLS");
    case PausePage::Graphics:
        return "GRAPHICS";
    case PausePage::Cheats:
        return pause_text(text, 699, "CHEATS");
    case PausePage::Skins:
        return pause_text(text, 717, "SKINS");
    case PausePage::ArmorSets:
        return pause_text(text, 708, "ARMOR SETS");
    default:
        return {}; // the menus of buttons stand alone, as in the original
    }
}

std::vector<PauseRow> PauseMenu::rows(const Gameplay &game, const hig::Localization &text) const {
    std::vector<PauseRow> out;
    auto row = [&](uint32_t id, std::string label, std::string value = {}, bool enabled = true) -> PauseRow & {
        PauseRow r;
        r.id = id;
        r.label = std::move(label);
        r.value = std::move(value);
        r.enabled = enabled;
        out.push_back(std::move(r));
        return out.back();
    };
    // a menu of the module: its items where the HUD file puts them; `enabled`: which actions are built here
    auto retail = [&](uint32_t first, std::initializer_list<uint32_t> enabled, bool with_value) {
        const auto *m = menu(first);
        if (!m)
            return false;
        for (const auto &item : m->items) {
            auto &r = row(item.text, pause_text(text, item.text),
                          {}, std::find(enabled.begin(), enabled.end(), item.text) != enabled.end());
            place(r, item, with_value);
        }
        // in the order they stand on the screen (the tables list a few out of it)
        std::stable_sort(out.begin(), out.end(), [](const PauseRow &a, const PauseRow &b) { return a.label_box[1] < b.label_box[1]; });
        return true;
    };
    // a row of ours in the original's plates: a label plate, and a value box when it has a value
    auto native_row = [&](PauseRow &r, float y, bool centred) {
        const float x = centred ? 239.f : 181.f;
        r.plate = {0, PLATE, x, y};
        r.selected = {0, PLATE_SELECTED, x + 1, y};
        r.label_box = {x - 102, y - 6, x + 102, y + 6};
        if (!centred && !r.value.empty()) {
            r.value_plate = {0, VALUE_BOX, 356, y};
            r.value_box = {309, y - 6, 405, y + 6};
        }
    };
    switch (page_) {
    case PausePage::Main:
        // QUICK SELECT (450) is not rebuilt yet
        if (!retail(447, {447, 448, 449, 451, 452, 453}, false))
            for (auto [id, fallback] : {std::pair{447u, "WEAPONS"}, {448u, "GADGETS"}, {449u, "ARMOR"}, {451u, "HELP"},
                                        {452u, "OPTIONS"}, {453u, "SPECIAL"}})
                row(id, pause_text(text, id, fallback));
        break;
    case PausePage::Options: {
        // SAVE (473) and LOAD (474) wait for progress saving
        const bool placed = retail(473, {475, 476, 477}, false);
        if (!placed)
            for (auto [id, fallback] : {std::pair{475u, "CAMERA & CONTROLS"}, {476u, "AUDIO VISUAL SETTINGS"}, {477u, "QUIT"}})
                row(id, pause_text(text, id, fallback));
        auto &graphics = row(ROW_GRAPHICS, "GRAPHICS");
        if (placed && layout_->elements.size() > 904 && layout_->elements[898].kind == 3) {
            // the sixth plate of the options screen (HUD screen 37), which the original leaves empty
            graphics.plate.element = 898;
            graphics.selected.element = 904;
            const float y = layout_->elements[898].y;
            graphics.label_box = {137, y - 6, 341, y + 6};
        }
        break;
    }
    case PausePage::Help:
        // HELP MESSAGE LOG (711) and MOVES HELP (713) are not rebuilt yet
        if (!retail(711, {712}, false))
            row(712, pause_text(text, 712, "CONTROLS HELP"));
        break;
    case PausePage::Special:
        // SKILL POINTS and CINEMATICS are not rebuilt yet
        if (!retail(699, {699, 717, 708}, false))
            for (auto [id, fallback] : {std::pair{699u, "CHEATS"}, {717u, "SKINS"}, {708u, "ARMOR SETS"}})
                row(id, pause_text(text, id, fallback));
        break;
    case PausePage::Cheats: {
        const bool values[CheatCount] = {goodies.uya_camera, options.strafe, goodies.thruster_pack, goodies.hydro_pack,
                                         goodies.uya_weapons, false, goodies.uya_look, goodies.charge_boots, goodies.all_weapons,
                                         goodies.all_armor};
        for (uint32_t k = 0; k < CheatCount; k++) {
            if (!cheat_shown(k))
                continue;
            const auto &c = cheat_text[k];
            row(ROW_CHEAT + k, c.label,
                k == CheatWrench ? std::string(wrench_names[std::clamp(goodies.wrench, 0, 3)])
                                 : *c.off ? std::string(values[k] ? c.on : c.off) : on_off(text, values[k]));
        }
        break;
    }
    case PausePage::Skins:
        for (uint32_t k : skin_order) {
            if (!extras_built && k >= PauseSkin::UpYourArsenal)
                continue; // the Ratchets of the other games' discs
            auto &r = row(ROW_SKIN + k, skin_names[k].first ? pause_text(text, skin_names[k].first, skin_names[k].second)
                                                            : std::string(skin_names[k].second));
            r.marked = wearing.skin == k;
            if (k == PauseSkin::UpYourArsenal || k == PauseSkin::GoingCommando)
                r.value = std::to_string((r.marked ? wearing.armor : skin.armor) + 1);
        }
        if (extras_built)
            row(ROW_SKIN_GAMEPLAY, "Imported: Own Gameplay", on_off(text, skin.own_gameplay));
        break;
    case PausePage::AudioVisual: {
        // Sound (800) and Progressive Scan (785) have nothing to switch on a PC
        if (!retail(466, {466, 467, 468, 469, 470, 788}, true))
            for (auto [id, fallback] : {std::pair{466u, "Subtitles"}, {467u, "Music Volume"}, {468u, "Effects Volume"},
                                        {469u, "Helpdesk Text"}, {470u, "Helpdesk Voice"}, {788u, "16:9 (WIDESCREEN)"}})
                row(id, pause_text(text, id, fallback));
        for (auto &r : out)
            switch (r.id) {
            case 466:
                r.value = on_off(text, options.subtitles);
                break;
            case 467:
                r.slider = true;
                r.fraction = float(options.music) / 10;
                break;
            case 468:
                r.slider = true;
                r.fraction = float(options.effects) / 10;
                break;
            case 469:
                r.value = on_off(text, options.helpdesk_text);
                break;
            case 470:
                r.value = on_off(text, options.helpdesk_voice);
                break;
            case 788:
                r.value = on_off(text, options.widescreen);
                break;
            case 800:
                r.value = "Stereo";
                break;
            case 785:
                r.value = on_off(text, true);
                break;
            }
        break;
    }
    case PausePage::CameraControls: {
        // View Mode (546: first person) is not built
        if (!retail(546, {482, 483, 484, 639, 545}, true))
            for (auto [id, fallback] : {std::pair{483u, "Camera Left/Right"}, {482u, "Camera Up/Down"}, {545u, "Camera Mode"},
                                        {484u, "Quick Select Pause"}, {639u, "Fly Controls Up/Down"}})
                row(id, pause_text(text, id, fallback));
        for (auto &r : out)
            switch (r.id) {
            case 546:
                r.value = pause_text(text, options.first_person ? 490 : 489, "3rd Person");
                break;
            case 482:
                r.value = normal_inverted(text, options.invert_y);
                break;
            case 483:
                r.value = normal_inverted(text, options.invert_x);
                break;
            case 545:
                r.value = pause_text(text, options.passive_camera ? 488 : 487, options.passive_camera ? "Passive" : "Follow");
                break;
            case 484:
                r.value = on_off(text, options.quick_select_pause);
                break;
            case 639:
                r.value = normal_inverted(text, options.invert_flight);
                break;
            }
        break;
    }
    case PausePage::Graphics: {
        row(ROW_RESOLUTION, "Resolution", std::to_string(graphics.width) + " x " + std::to_string(graphics.height));
        row(ROW_FRAME_CAP, "Frame cap", std::to_string(graphics.fps));
        row(ROW_VSYNC, "V-sync", on_off(text, graphics.vsync));
        row(ROW_APPLY, "Apply and save");
        if (layout_)
            for (size_t i = 0; i < out.size(); i++)
                native_row(out[i], 53 + 31 * float(i), false);
        break;
    }
    case PausePage::Quit: {
        row(ROW_NO, pause_text(text, 479, "No"));
        row(ROW_YES, pause_text(text, 478, "Yes"));
        if (layout_)
            for (size_t i = 0; i < out.size(); i++)
                native_row(out[i], 134 + 31 * float(i), true);
        break;
    }
    case PausePage::Weapons:
    case PausePage::Gadgets: {
        const auto shown = listed(game);
        for (unsigned index : shown) {
            const auto &tier = game.weapon_tables()->weapons[index].tier(game.arsenal.level[index]);
            auto &r = row(ROW_ITEM + index, pause_text(text, tier.name_text, game.weapon_tables()->weapons[index].name.c_str()));
            r.icon = tier.icon;
            r.marked = index == game.arsenal.equipped;
        }
        if (shown.empty())
            row(553, pause_text(text, 553, "none"), {}, false);
        break;
    }
    case PausePage::ArmorSets: {
        // the sets he can put on whole (0xc5398's list), each with what it would take off
        const auto *tables = game.armor_tables();
        for (unsigned set = 1; tables && set < hig::ArmorTables::set_count; set++) {
            if (!game.armor_set_owned(set))
                continue;
            auto &r = row(ROW_ARMOR_SET + set, pause_text(text, tables->sets[set].name, "Armor set"),
                          std::to_string(int(game.armor_set_protection(set) * 100 + .5f)) + "%");
            r.marked = game.armor_set() == set;
        }
        if (out.empty())
            row(553, pause_text(text, 553, "none"), {}, false);
        break;
    }
    case PausePage::Armor:
        // a kind of piece a row; left / right goes through the sets owned for it
        for (unsigned k = 0; k < 4; k++) {
            static constexpr const char *fallback[4] = {"HELMET", "BODY ARMOR", "GLOVES", "BOOTS"};
            const unsigned set = game.armor.worn[armor_rows[k].second];
            const auto *tables = game.armor_tables();
            row(armor_rows[k].first, pause_text(text, armor_rows[k].first, fallback[k]),
                set && tables ? pause_text(text, tables->sets[set].name, "worn") : pause_text(text, 553, "none"));
        }
        break;
    case PausePage::ControlsHelp:
        break;
    }
    return out;
}

std::string PauseMenu::detail(const Gameplay &game, const hig::Localization &text) const {
    if (page_ == PausePage::Weapons || page_ == PausePage::Gadgets) {
        const auto shown = listed(game);
        if (selection_ >= shown.size())
            return {};
        const unsigned index = shown[selection_];
        const auto &tier = game.weapon_tables()->weapons[index].tier(game.arsenal.level[index]);
        std::string out = pause_text(text, tier.description_text);
        if (page_ == PausePage::Weapons) {
            out += "\n\n" + pause_text(text, 463, "Level") + " " + std::to_string(unsigned(game.arsenal.level[index]) + 1);
            if (tier.max_ammo) {
                const unsigned ammo = index == game.arsenal.equipped ? game.ammo : game.arsenal.ammo[index];
                out += "\n" + pause_text(text, 461, "Ammo:") + " " + std::to_string(ammo) + " / " + std::to_string(tier.max_ammo);
            }
            if (tier.xp_next)
                out += "\n" + pause_text(text, 462, "Next level:") + " " + std::to_string(unsigned(game.arsenal.xp[index])) +
                       " / " + std::to_string(tier.xp_next);
        }
        return out;
    }
    if (page_ == PausePage::Armor) {
        const auto *tables = game.armor_tables();
        if (!tables)
            return "No armor found yet.";
        // the set worn, what it does, and the damage the armour takes off (the original's "Damage reduction")
        const unsigned set = game.armor_set();
        std::string out = pause_text(text, 691, "Damage reduction: %d"); // the original's own line
        const std::string percent = std::to_string(int(game.armor_protection() * 100 + .5f)) + "%";
        if (const auto at = out.find("%d"); at != std::string::npos)
            out.replace(at, 2, percent);
        else
            out += " " + percent;
        if (set)
            out += "\n\n" + pause_text(text, tables->sets[set].name, "Armor set") + "\n" +
                   pause_text(text, tables->sets[set].description);
        else {
            bool any = false;
            for (unsigned k = 1; k < 8; k++)
                any |= game.armor.owned[k] != 0;
            out += any ? "\n\nLeft / right: the pieces you own. A whole set, or one of the mixed sets, gives the wrench its power."
                       : "\n\nNo armor found yet.";
        }
        return out;
    }
    if (page_ == PausePage::ArmorSets) {
        const auto *tables = game.armor_tables();
        const auto list = rows(game, text);
        if (!tables || selection_ >= list.size() || list[selection_].id < ROW_ARMOR_SET)
            return "No whole armor set found yet. Its pieces are found one by one; a set is whole with its helmet, body "
                   "armor, gloves and boots.";
        const unsigned set = list[selection_].id - ROW_ARMOR_SET;
        std::string out = pause_text(text, 691, "Damage reduction: %d");
        const std::string percent = std::to_string(int(game.armor_set_protection(set) * 100 + .5f)) + "%";
        if (const auto at = out.find("%d"); at != std::string::npos)
            out.replace(at, 2, percent);
        else
            out += " " + percent;
        if (list[selection_].marked)
            out += "  (worn)";
        return out + "\n\n" + pause_text(text, tables->sets[set].description);
    }
    if (page_ == PausePage::ControlsHelp)
        return "Move: left stick / WASD\nCamera: right stick / mouse\nJump, glide: A / Space\nFire: B / E\n"
               "Wrench: X / F\nNext weapon, interact: Y / R\nStrafe: LT / Z\nCrouch: RB / C\nHelp desk: Back / Tab\n"
               "Pause: Start / Esc";
    if (page_ == PausePage::Quit)
        return pause_text(text, 532, "Are you sure you want to quit?");
    if (page_ == PausePage::Cheats && cheat_at(selection_) < CheatCount)
        return cheat_text[cheat_at(selection_)].note;
    if (extras_built && page_ == PausePage::Skins && selection_ == PauseSkin::Count)
        return "An imported Ratchet moves as in his own game: its running, jumps, flips, Heli-Pack and Thruster-Pack "
               "moves, charge boots and camera, with Size Matters' weapons, wrench and gadgets.";
    if (page_ == PausePage::Skins && selection_ < PauseSkin::Count) {
        const unsigned which = skin_order[selection_];
        if (which == 0)
            return "Ratchet as Size Matters has him, in the armor he has found.";
        if (which == 6)
            return "A skin on Size Matters' disc that its own SKINS page leaves out.";
        if (which < 7)
            return "One of Size Matters' own skins, from the game's disc.";
        if (which == PauseSkin::RatchetAndClank)
            return "Ratchet as the first game has him, with its animations. Read from your Ratchet & Clank disc.";
        return std::string("Ratchet as ") + (which == PauseSkin::UpYourArsenal ? "Up Your Arsenal" : "Going Commando") +
               " has him, with that game's animations, hoverboots and charge boots. Read from your disc of that game. "
               "Left / right: which of its armors he wears.";
    }
    return {};
}

void PauseMenu::settle(const Gameplay &game) {
    hig::Localization none;
    const auto list = rows(game, none);
    if (list.empty()) {
        selection_ = 0;
        return;
    }
    selection_ = std::min(selection_, unsigned(list.size()) - 1);
    for (unsigned tries = 0; tries < list.size() && !list[selection_].enabled; tries++)
        selection_ = (selection_ + 1) % unsigned(list.size());
}
PauseAction PauseMenu::pulse(uint32_t buttons, Gameplay &game) {
    settle(game);
    hig::Localization none;
    const auto list = rows(game, none); // ids, counts and enabled flags are what is used here
    const unsigned count = unsigned(list.size());
    if (buttons & hig::Start)
        return PauseAction::Resume;
    if (buttons & (hig::Circle | hig::Triangle))
        return back() ? PauseAction::None : PauseAction::Resume;
    if (!count)
        return PauseAction::None;
    selection_ = std::min(selection_, count - 1);
    auto step = [&](int direction) {
        for (unsigned tries = 0; tries < count; tries++) {
            selection_ = (selection_ + count + unsigned(direction)) % count;
            if (list[selection_].enabled)
                return;
        }
    };
    if (buttons & hig::Up)
        step(-1);
    else if (buttons & hig::Down)
        step(1);
    if (!list[selection_].enabled)
        step(1);
    if (!list[selection_].enabled)
        return PauseAction::None; // a page with nothing built on it yet
    const int turn = (buttons & hig::Right) ? 1 : (buttons & hig::Left) ? -1 : 0;
    const bool accept = (buttons & hig::Cross) != 0;
    auto toggle = [&](bool &value) {
        if (turn || accept)
            value = !value;
    };
    auto slide = [&](int &value) {
        if (turn)
            value = std::clamp(value + turn, 0, 10);
    };
    const uint32_t id = list[selection_].id;
    if (id >= ROW_CHEAT && id < ROW_CHEAT + CheatCount) {
        if (!turn && !accept)
            return PauseAction::None;
        switch (id - ROW_CHEAT) {
        case CheatCamera:
            goodies.uya_camera = !goodies.uya_camera;
            break;
        case CheatStrafe:
            options.strafe = !options.strafe;
            return PauseAction::None; // an option of the menu's own; the host reads it every frame
        case CheatAirPack:
            goodies.thruster_pack = !goodies.thruster_pack;
            break;
        case CheatHydroPack:
            goodies.hydro_pack = !goodies.hydro_pack;
            break;
        case CheatWeaponSet:
            goodies.uya_weapons = !goodies.uya_weapons;
            break;
        case CheatWrench:
            goodies.wrench = (goodies.wrench + (turn < 0 ? 3 : 1)) % 4;
            break;
        case CheatLook:
            goodies.uya_look = !goodies.uya_look;
            break;
        case CheatChargeBoots:
            goodies.charge_boots = !goodies.charge_boots;
            break;
        case CheatAllWeapons:
            if (goodies.all_weapons)
                return PauseAction::None;
            goodies.all_weapons = true;
            break;
        case CheatAllArmor:
            if (goodies.all_armor)
                return PauseAction::None;
            goodies.all_armor = true;
            break;
        }
        return PauseAction::ApplyGoodies;
    }
    if (id == ROW_SKIN_GAMEPLAY) {
        if (!turn && !accept)
            return PauseAction::None;
        skin.own_gameplay = !skin.own_gameplay;
        if (wearing.skin < PauseSkin::UpYourArsenal) { // kept for the imported Ratchet he may wear next
            wearing.own_gameplay = skin.own_gameplay;
            return PauseAction::None;
        }
        skin.skin = wearing.skin;
        skin.armor = wearing.armor;
        return PauseAction::ApplySkin;
    }
    if (id >= ROW_SKIN && id < ROW_SKIN + PauseSkin::Count) {
        const unsigned which = id - ROW_SKIN;
        const bool armored = which == PauseSkin::UpYourArsenal || which == PauseSkin::GoingCommando;
        if (turn && armored) {
            if (wearing.skin == which)
                skin.armor = wearing.armor;
            skin.armor = std::clamp(skin.armor + turn, 0, 15);
            if (wearing.skin != which)
                return PauseAction::None; // chosen with Cross
        } else if (!accept)
            return PauseAction::None;
        skin.skin = which;
        if (!armored)
            skin.armor = 0;
        return PauseAction::ApplySkin;
    }
    if (id > ROW_ARMOR_SET && id < ROW_ARMOR_SET + hig::ArmorTables::set_count) {
        if (accept)
            game.wear_armor_set(id - ROW_ARMOR_SET);
        return PauseAction::None;
    }
    switch (id) {
    // the pause menu
    case 447:
        if (accept)
            go(PausePage::Weapons);
        break;
    case 448:
        if (accept)
            go(PausePage::Gadgets);
        break;
    case 449:
        if (accept)
            go(PausePage::Armor);
        break;
    case 451:
        if (accept)
            go(PausePage::Help);
        break;
    case 452:
        if (accept)
            go(PausePage::Options);
        break;
    case 453:
        if (accept)
            go(PausePage::Special);
        break;
    // special
    case 699:
        if (accept)
            go(PausePage::Cheats);
        break;
    case 708:
        if (accept) {
            go(PausePage::ArmorSets);
            // on the set he wears
            unsigned at = 0;
            for (unsigned set = 1; set < hig::ArmorTables::set_count; set++)
                if (game.armor_set_owned(set)) {
                    if (set == game.armor_set())
                        selection_ = at;
                    at++;
                }
        }
        break;
    case 717:
        if (accept) {
            go(PausePage::Skins);
            skin = wearing;
            selection_ = unsigned(std::find(std::begin(skin_order), std::end(skin_order), wearing.skin) - std::begin(skin_order)) %
                         unsigned(PauseSkin::Count);
        }
        break;
    // options
    case 475:
        if (accept)
            go(PausePage::CameraControls);
        break;
    case 476:
        if (accept)
            go(PausePage::AudioVisual);
        break;
    case 477:
        if (accept)
            go(PausePage::Quit);
        break;
    case ROW_GRAPHICS:
        if (accept)
            go(PausePage::Graphics);
        break;
    case 712:
        if (accept)
            go(PausePage::ControlsHelp);
        break;
    // audio visual settings
    case 466:
        toggle(options.subtitles);
        break;
    case 467:
        slide(options.music);
        break;
    case 468:
        slide(options.effects);
        break;
    case 469:
        toggle(options.helpdesk_text);
        break;
    case 470:
        toggle(options.helpdesk_voice);
        break;
    case 788:
        toggle(options.widescreen);
        break;
    // camera and controls
    case 482:
        toggle(options.invert_y);
        break;
    case 483:
        toggle(options.invert_x);
        break;
    case 545:
        toggle(options.passive_camera);
        break;
    case 484:
        toggle(options.quick_select_pause);
        break;
    case 639:
        toggle(options.invert_flight);
        break;
    // graphics
    case ROW_RESOLUTION:
        if (turn || accept) {
            size_t at = 0;
            for (size_t i = 0; i < std::size(resolutions); i++)
                if (resolutions[i].first == graphics.width && resolutions[i].second == graphics.height)
                    at = i;
            at = (at + std::size(resolutions) + size_t(turn ? turn : 1)) % std::size(resolutions);
            graphics.width = resolutions[at].first;
            graphics.height = resolutions[at].second;
        }
        break;
    case ROW_FRAME_CAP:
        if (turn || accept) {
            size_t at = 1;
            for (size_t i = 0; i < std::size(rates); i++)
                if (rates[i] == graphics.fps)
                    at = i;
            graphics.fps = rates[(at + std::size(rates) + size_t(turn ? turn : 1)) % std::size(rates)];
        }
        break;
    case ROW_VSYNC:
        toggle(graphics.vsync);
        break;
    case ROW_APPLY:
        if (accept)
            return PauseAction::ApplyGraphics;
        break;
    case ROW_YES:
        if (accept)
            return PauseAction::QuitToTitle;
        break;
    case ROW_NO:
        if (accept)
            back();
        break;
    case 549:
    case 550:
    case 551:
    case 552:
        if (page_ == PausePage::Armor && (turn || accept)) {
            // the next set owned for this kind of piece, bare among them (0x4f8, 0x580)
            unsigned place = 0;
            for (const auto &[text_id, where] : armor_rows)
                if (text_id == id)
                    place = where;
            unsigned set = game.armor.worn[place];
            for (unsigned tries = 0; tries < 8; tries++) {
                set = (set + (turn < 0 ? 7u : 1u)) % 8;
                if (game.armor_owned(set, place))
                    break;
            }
            game.wear_armor(set, place);
        }
        break;
    default:
        if (id >= ROW_ITEM && accept && page_ == PausePage::Weapons)
            game.equip(id - ROW_ITEM);
        break;
    }
    return PauseAction::None;
}

PauseOptions read_pause_options(const std::filesystem::path &path) {
    PauseOptions o;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        int value = 0;
        std::istringstream(line.substr(eq + 1)) >> value;
        const bool on = value != 0;
        if (key == "subtitles")
            o.subtitles = on;
        else if (key == "helpdesk_text")
            o.helpdesk_text = on;
        else if (key == "helpdesk_voice")
            o.helpdesk_voice = on;
        else if (key == "widescreen")
            o.widescreen = on;
        else if (key == "music")
            o.music = std::clamp(value, 0, 10);
        else if (key == "effects")
            o.effects = std::clamp(value, 0, 10);
        else if (key == "invert_y")
            o.invert_y = on;
        else if (key == "invert_x")
            o.invert_x = on;
        else if (key == "passive_camera")
            o.passive_camera = on;
        else if (key == "strafe")
            o.strafe = on;
        else if (key == "vibration")
            o.vibration = on;
        else if (key == "quick_select_pause")
            o.quick_select_pause = on;
        else if (key == "invert_flight")
            o.invert_flight = on;
    }
    return o;
}
bool save_pause_options(const std::filesystem::path &path, const PauseOptions &o) {
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out)
        return false;
    out << "subtitles=" << o.subtitles << "\nhelpdesk_text=" << o.helpdesk_text << "\nhelpdesk_voice=" << o.helpdesk_voice
        << "\nwidescreen=" << o.widescreen << "\nmusic=" << o.music << "\neffects=" << o.effects << "\ninvert_y="
        << o.invert_y << "\ninvert_x=" << o.invert_x << "\npassive_camera=" << o.passive_camera << "\nstrafe=" << o.strafe
        << "\nvibration=" << o.vibration << "\nquick_select_pause=" << o.quick_select_pause << "\ninvert_flight="
        << o.invert_flight << "\n";
    return bool(out);
}
} // namespace sm
