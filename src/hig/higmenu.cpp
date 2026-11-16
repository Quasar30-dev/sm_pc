#include "higmenu.h"
#include <cstring>
namespace hig {
bool decode_new_game_bootstrap(std::span<const uint8_t> b, NewGameBootstrap &out,
                               std::string *error) {
    out = {};
    auto u32 = [&](size_t o) {
        uint32_t v;
        std::memcpy(&v, b.data() + o, 4);
        return v;
    };
    if (b.size() < 0x3034 || std::memcmp(b.data(), "SNR2", 4) || u32(0x2fc8) != 0x24100001 ||
        u32(0x2fd8) != 0x3c060001 || u32(0x2fe4) != 0x34c63f60 || u32(0x3030) != 0xac701c6c) {
        if (error)
            *error = "New Game reset profile does not match this frontend";
        return false;
    }
    out.level = u32(0x2fc8) & 0xffff;
    out.context_size = ((u32(0x2fd8) & 0xffff) << 16) | (u32(0x2fe4) & 0xffff);
    if (error)
        error->clear();
    return true;
}
bool decode_frontend_menu(std::span<const uint8_t> b, MenuData &out, std::string *error) {
    out = {};
    auto fail = [&](const char *s) {
        if (error)
            *error = s;
        return false;
    };
    if (b.size() < 0x30a00 || std::memcmp(b.data(), "SNR2", 4))
        return fail("USA frontend menu data is unavailable");
    auto u32 = [&](size_t o) {
        uint32_t v;
        std::memcpy(&v, b.data() + o, 4);
        return v;
    };
    // These original instructions identify the title state's reference to
    // this data structure. Reject other builds rather than use wrong offsets.
    if (u32(0x19f9c) != 0x244509f0 || u32(0x1a350) != 0x0c004880)
        return fail("Frontend menu profile does not match this program");
    constexpr size_t menu = 0x309f0;
    uint32_t table = u32(menu), count = u32(menu + 4), selected = u32(menu + 8);
    if (!count || count > 64 || selected >= count || table > b.size() ||
        uint64_t(count) * 4 > b.size() - table)
        return fail("Frontend menu pointer table is invalid");
    MenuData result;
    result.selection = selected;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t p = u32(table + i * 4);
        if (p > b.size() || 0xd4 > b.size() - p)
            return fail("Frontend item exceeds its module");
        MenuItem item;
        item.file_offset = p;
        item.text_id = u32(p);
        item.normal_anchor = u32(p + 4);
        item.selected_anchor = u32(p + 8);
        item.enabled = b[p + 0xcc] != 0;
        item.focusable = b[p + 0xcd] != 0;
        item.action = u32(p + 0xd0);
        if (!item.text_id || !item.action)
            return fail("Frontend item has no label/action");
        result.items.push_back(item);
    }
    out = std::move(result);
    if (error)
        error->clear();
    return true;
}
bool navigate_menu(MenuData &menu, uint32_t pulse) {
    size_t count = menu.items.size();
    if (!count || menu.selection >= count)
        return false;
    size_t original = menu.selection;
    if (!(pulse & (Up | Down)) && menu.items[original].enabled)
        return false;
    int step = (pulse & Up) ? -1 : 1;
    size_t selected = original;
    do {
        selected = step < 0 ? (selected ? selected - 1 : count - 1) : (selected + 1) % count;
        if (menu.items[selected].enabled && menu.items[selected].focusable) {
            menu.selection = uint32_t(selected);
            return true;
        }
    } while (selected != original);
    return false;
}
uint32_t advance_menu(MenuData &menu, uint32_t pulse) {
    if (menu.selection >= menu.items.size())
        return 0;
    if (pulse & Cross)
        return menu.items[menu.selection].enabled ? menu.items[menu.selection].action : 0;
    navigate_menu(menu, pulse);
    return 0;
}
} // namespace hig
