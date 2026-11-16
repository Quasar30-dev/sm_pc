#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hig {
// Read-only oracle metadata. These APIs neither relocate nor execute guest code.
struct ProgramSection {
    std::string name;
    uint32_t address = 0, offset = 0, size = 0;
    bool executable = false, zero_fill = false;
};
struct ProgramSymbol {
    std::string name;
    uint32_t value = 0, record_offset = 0, file_offset = UINT32_MAX;
    uint8_t kind = 0; // SNR2: nil, import, relative, weak, absolute
};
struct ProgramReference {
    uint32_t file_offset = 0, symbol = 0, addend = 0, instruction = 0;
    uint8_t relocation = 0; // ELF MIPS relocation number
    bool direct_call = false;
};
struct Program {
    bool resident_elf = false;
    uint32_t entry = 0, load_function = 0, unload_function = 0;
    uint32_t file_size = 0, symbol_table_offset = 0;
    std::string source_elf;
    std::vector<ProgramSection> sections;
    std::vector<ProgramSymbol> symbols;
    std::vector<ProgramReference> references;
    std::vector<std::string> warnings;
    uint32_t offset_for_address(uint32_t address) const;
};
bool decode_program(std::span<const uint8_t> bytes, Program &out, std::string *error = nullptr);
const char *program_system(const std::string &name);
} // namespace hig
