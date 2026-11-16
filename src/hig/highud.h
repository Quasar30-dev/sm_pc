#pragma once
#include "higlevel.h"
#include <array>

namespace hig {
// Shared, renderer-independent recovery of the retail HW3D artwork.
struct HudVertex {
    float x = 0, y = 0;
    uint32_t rgba = 0xffffffff;
};
struct HudElement {
    unsigned kind = 0, reference = 0;
    float x = 0, y = 0;
    std::array<float, 4> bounds{}; // left, top, right, bottom
    std::vector<HudVertex> triangles;
};
struct HudLayout {
    std::vector<HudElement> elements;
    std::vector<std::vector<unsigned>> screens;
};
bool decode_hud(std::span<const uint8_t>, HudLayout &, std::string *error = nullptr);
bool hud_triangles(const HudLayout &, unsigned element, std::vector<HudVertex> &,
                   std::string *error = nullptr);
// A template element's triangles about its own origin (the pause menu's plates: 764 a button, 765 the selected
// button, 773 a value box): add the place wanted.
bool hud_template(const HudLayout &, unsigned element, std::vector<HudVertex> &);

// The pause mode's menus in a level code module, read as DATA. An item is 0xe8 bytes (LEVEL_02 0x2153a0: the pause
// menu's WEAPONS): +0x00 text id, +0x04 its button plate (a HUD reference element), +0x08 its text box (a HUD
// bounds element), +0xc0 two floats 1.0, +0xc8 the plate drawn when it is selected, +0xcc / +0xcd enabled and
// focusable, +0xd0 its action (the pause screen it opens, or a command). Items of one menu follow each other.
struct HudMenuItem {
    uint32_t text = 0, plate = 0, bounds = 0, selected_plate = 0, action = 0;
    bool enabled = true;
};
struct HudMenu {
    std::vector<HudMenuItem> items;
};
bool decode_hud_menus(std::span<const uint8_t> module, const HudLayout &, std::vector<HudMenu> &out);

// The linked asset headers are used by WAD and BWD alike. Offsets remain
// relative to the supplied decompressed image; source paths are labels only.
struct MediaAsset {
    uint32_t hash = 0, offset = 0, size = 0, kind = 0;
    std::string name;
};
bool media_assets(std::span<const uint8_t>, const HigWad &, std::vector<MediaAsset> &,
                  std::string *error = nullptr);
struct HudAssets {
    HudLayout layout;
    GimImage font;
    std::array<std::array<uint8_t, 4>, 256> metrics{};
    std::vector<GimImage> icons;
    unsigned weapon_icon = 94, max_ammo = 40;
};
bool load_hud(const IsoFs &, HudAssets &, std::string *error = nullptr);
} // namespace hig
