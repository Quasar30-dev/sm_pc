#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace hig {
// Size Matters' armour, read from a level module (/LVL/LEVEL_nn.REL; every campaign level carries the tables).
// LEVEL_02 offsets: the pieces 0x220480, a set's protection 0x220430, the worn pieces' classes 0x220418 and model
// variants 0x2203f8, the sets' texts 0x2206e8 and recipes 0x220790. docs/SIZE_MATTERS_CAMPAIGN.md, "Armour".
//
// A body has six places: 0 body armor, 1 helmet, 2 and 3 the gloves, 4 and 5 the boots. A piece is one of four
// kinds (its place: 0, 1, 2, 4; gloves and boots fill their second place too) of one of seven sets.
struct ArmorPiece {
    uint32_t pickup_model = 0; // the pickup's model variant signature
    uint8_t place = 0, partner = 6; // partner: the second place it fills (3, 5), 6 for none
    uint8_t set = 0;           // 1..7
    uint32_t text = 0;         // "Body Armor", "Helmet", "Gloves", "Boots"
};
struct ArmorSet {
    // What must be worn for it: the sets of the helmet, the body armor, the gloves and the boots (0x220790; the
    // check 0xaa4 reads places 1, 0, 3, 5 in that order).
    std::array<uint8_t, 4> recipe{};
    float bonus = 0;                    // added to the pieces' protection while the set is worn
    uint32_t name = 0, description = 0; // texts
};
struct ArmorTables {
    static constexpr unsigned piece_count = 28, set_count = 14; // sets: 0 none, 1..7 whole sets, 8..13 mixed
    std::array<ArmorPiece, piece_count> pieces{};
    std::array<float, 8> protection{}; // by a piece's set: the share of damage it takes off (0x220430)
    std::array<ArmorSet, set_count> sets{};
    std::array<uint32_t, 6> place_class{};  // class signatures of the worn pieces, by place (ArmorTorso, ...)
    std::array<uint32_t, 8> variant{};      // by set: the model variant signature of its worn pieces
};
bool decode_armor_tables(std::span<const uint8_t> module, ArmorTables &out);
} // namespace hig
