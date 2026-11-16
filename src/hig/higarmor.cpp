#include "higarmor.h"
#include "higclass.h"
#include <cmath>
#include <cstring>

namespace hig {
bool decode_armor_tables(std::span<const uint8_t> b, ArmorTables &out) {
    out = {};
    auto u32 = [&](size_t p) {
        uint32_t v = 0;
        if (p + 4 <= b.size())
            std::memcpy(&v, b.data() + p, 4);
        return v;
    };
    auto f32 = [&](size_t p) {
        float v = 0;
        if (p + 4 <= b.size())
            std::memcpy(&v, b.data() + p, 4);
        return v;
    };
    // The piece table is found by its shape: 28 rows of {signature, place 0 / 1 / 2 / 4, partner 6 / 6 / 3 / 5,
    // set 1..7, text} in set order. The other tables stand at fixed places about it (the same source file's data).
    constexpr uint32_t places[4] = {0, 1, 2, 4}, partners[4] = {6, 6, 3, 5};
    size_t at = 0;
    for (size_t p = 0x90; p + ArmorTables::piece_count * 20 + 0x400 <= b.size() && !at; p += 4) {
        bool match = true;
        for (unsigned row = 0; row < ArmorTables::piece_count && match; row++) {
            const size_t r = p + size_t(row) * 20;
            match = u32(r) != 0 && u32(r + 4) == places[row & 3] && u32(r + 8) == partners[row & 3] &&
                    u32(r + 12) == 1 + row / 4 && u32(r + 16) - 0x90 < 0x40;
        }
        if (match)
            at = p;
    }
    if (!at)
        return false;
    for (unsigned row = 0; row < ArmorTables::piece_count; row++) {
        const size_t r = at + size_t(row) * 20;
        out.pieces[row] = {u32(r), uint8_t(u32(r + 4)), uint8_t(u32(r + 8)), uint8_t(u32(r + 12)), u32(r + 16)};
    }
    for (unsigned k = 0; k < 8; k++) {
        out.protection[k] = f32(at - 0x50 + k * 4);
        out.variant[k] = u32(at - 0x88 + k * 4);
    }
    for (unsigned k = 0; k < 6; k++)
        out.place_class[k] = u32(at - 0x68 + k * 4);
    for (unsigned k = 0; k < ArmorTables::set_count; k++) {
        auto &set = out.sets[k];
        const size_t recipe = at + 0x310 + size_t(k) * 20, text = at + 0x268 + size_t(k) * 12;
        for (unsigned j = 0; j < 4; j++)
            set.recipe[j] = uint8_t(u32(recipe + j * 4));
        set.bonus = f32(recipe + 16);
        set.name = u32(text);
        set.description = u32(text + 4);
    }
    // what the neighbours must be for this to be the armour data
    bool valid = out.protection[0] == 0 && out.place_class[0] == class_hash("ArmorTorso") &&
                 out.place_class[1] == class_hash("ArmorHelmet") && out.variant[0] == 0;
    for (unsigned k = 1; k < 8 && valid; k++)
        valid = std::isfinite(out.protection[k]) && out.protection[k] > out.protection[k - 1] && out.protection[k] < 1 &&
                out.variant[k] != 0 && out.sets[k].recipe == std::array<uint8_t, 4>{uint8_t(k), uint8_t(k), uint8_t(k), uint8_t(k)};
    for (unsigned k = 0; k < ArmorTables::set_count && valid; k++)
        valid = std::isfinite(out.sets[k].bonus) && out.sets[k].bonus >= 0 && out.sets[k].bonus < 1;
    if (!valid)
        out = {};
    return valid;
}
} // namespace hig
