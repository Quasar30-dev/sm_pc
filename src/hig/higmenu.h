#pragma once
#include "higinput.h"
#include <string>
#include <vector>
namespace hig {
struct MenuItem {
    uint32_t file_offset = 0, text_id = 0, normal_anchor = 0, selected_anchor = 0, action = 0;
    bool enabled = false, focusable = false;
};
struct MenuData {
    std::vector<MenuItem> items;
    uint32_t selection = 0;
};
// Verified USA frontend data profile, not a scan for plausible UI strings.
bool decode_frontend_menu(std::span<const uint8_t>, MenuData &, std::string *error = nullptr);
struct NewGameBootstrap {
    uint32_t level = 0, context_size = 0;
};
// Read verified reset constants. This is not a complete progress-save schema.
bool decode_new_game_bootstrap(std::span<const uint8_t>, NewGameBootstrap &,
                               std::string *error = nullptr);
// Recovered FRONTEND.REL 0x12200..0x122d4. The argument is the caller's
// navigation pulse mask; input repeat timing is a separate caller policy.
bool navigate_menu(MenuData &, uint32_t pulse);
// Title state checks Cross before navigation and requires enabled (0x1a084).
// Zero means no dispatched action; does not invent downstream gameplay.
uint32_t advance_menu(MenuData &, uint32_t pulse);
} // namespace hig
