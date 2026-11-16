#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hig {
// Gameplay class records of a level code module (/LVL/LEVEL_nn.REL), read as DATA; nothing is executed.
//
// A record is sixteen words in the module's data, one per class the level's BOG can place:
//   +0x00 pointer to the class name          (the BOG class hash is CRC32 of the upper-cased name)
//   +0x04 four callbacks (the first is never 0; working names: init, cb1, update, event)
//   +0x14 pointer to the property declaration text, or 0
//   +0x18 property byte size                 (equals the BOG class definition's property size)
//   +0x1c per-instance data size
//   +0x20 eight further words (callbacks, data pointers, flags)
// Pointers are file offsets: the module is linked at 0.
//
// The declaration is the designers' C struct of the instance's BOG properties, e.g. the Crab's
//   "Navigate navigateBehavior; TriggerLink detectArea; ... f32 scale; TriggerLink noDetectArea; Toggle pathNotice;
//    u8 pad[ 3 ];"
// laid out here with C rules (natural alignment, at most 4). All 940 declarations on the USA disc end exactly at
// their record's property size with the type table in higclass.cpp (tools/sm/sm_class_schema.py checks this).
// TriggerLink / MobyLink / CurveLink / TieLink / ShrubLink fields hold the BOG offset of the trigger, instance or
// curve they name (0 = none).
struct ClassField {
    std::string type, name;
    uint32_t offset = 0, size = 0, count = 1; // element size and array length
    bool pointer = false;                     // run-time pointer field: no authored meaning
};
struct ClassRecord {
    uint32_t offset = 0, hash = 0;
    std::string name, declaration;
    std::array<uint32_t, 4> callbacks{};
    uint32_t props_size = 0, instance_size = 0;
    std::array<uint32_t, 8> extra{};
    std::vector<ClassField> fields;
    bool layout_exact = false; // every type known and the last field ends at props_size (padding aside)
    // Enemy classes carry two float tables indexed by level (extra words 5 and 6): the class's share of the level's
    // enemy bolts (BoltController, LEVEL_02 0x15be48) and of its experience. `rewards` = the bolt table is present.
    bool rewards = false;
    std::array<float, 11> bolt_share{}, xp_share{};
    const ClassField *field(std::string_view name) const;
};
uint32_t class_hash(std::string_view name);
bool decode_class_records(std::span<const uint8_t> module, std::vector<ClassRecord> &out,
                          std::string *error = nullptr);

// The module's per-level combat table (LEVEL_02 0x22ed80; 0x40 bytes a level, levels 1..10): the hit points of three
// enemy strength tiers in the first playthrough and in challenge mode (Pygmy init 0x1b33b0 reads tier 0 of its
// level), then a second set of three with its challenge values, read by the enemies' attacks.
struct LevelTuning {
    float health[2][4]{}, attack[2][4]{};
};
bool find_level_tuning(std::span<const uint8_t> module, std::array<LevelTuning, 10> &out);

// One instance's properties read through its class's declaration. Absent fields and short data read as 0.
struct ClassProps {
    std::span<const uint8_t> bytes;
    const ClassRecord *record = nullptr;
    bool has(std::string_view field) const { return record && record->field(field); }
    uint32_t u32(std::string_view field, unsigned index = 0) const;
    int32_t s32(std::string_view field, unsigned index = 0) const { return int32_t(u32(field, index)); }
    float f32(std::string_view field, unsigned index = 0) const;
    uint8_t u8(std::string_view field, unsigned index = 0) const;
    bool toggle(std::string_view field, unsigned index = 0) const { return u8(field, index) != 0; }
    std::array<float, 3> vec3(std::string_view field, unsigned index = 0) const;
    // raw accessors by byte offset, for fields inside engine structs (Navigate, ...)
    uint32_t word(size_t offset) const;
    float real(size_t offset) const;
};
} // namespace hig
