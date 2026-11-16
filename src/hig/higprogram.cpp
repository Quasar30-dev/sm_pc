#include "higprogram.h"
#include <algorithm>
#include <cstring>

namespace hig {
namespace {
bool fits(std::span<const uint8_t> b, size_t p, size_t n) { return p <= b.size() && n <= b.size() - p; }
uint16_t u16(std::span<const uint8_t> b, size_t p) {
    uint16_t v;
    std::memcpy(&v, b.data() + p, 2);
    return v;
}
uint32_t u32(std::span<const uint8_t> b, size_t p) {
    uint32_t v;
    std::memcpy(&v, b.data() + p, 4);
    return v;
}
bool fail(std::string *error, const char *text) {
    if (error)
        *error = text;
    return false;
}
bool read_string(std::span<const uint8_t> b, size_t p, std::string &s) {
    if (p >= b.size())
        return false;
    size_t end = p;
    while (end < b.size() && end - p < 4096 && b[end])
        ++end;
    if (end == b.size() || b[end])
        return false;
    s.assign(reinterpret_cast<const char *>(b.data() + p), end - p);
    return true;
}
} // namespace
uint32_t Program::offset_for_address(uint32_t address) const {
    if (!resident_elf)
        return address < file_size ? address : UINT32_MAX;
    for (const auto &s : sections)
        if (!s.zero_fill && address >= s.address && uint64_t(address) < uint64_t(s.address) + s.size)
            return s.offset + address - s.address;
    return UINT32_MAX;
}
bool decode_program(std::span<const uint8_t> b, Program &out, std::string *error) {
    out = {};
    Program p;
    if (b.size() > UINT32_MAX || !fits(b, 0, 60))
        return fail(error, "Program header is truncated");
    p.file_size = uint32_t(b.size());
    size_t sn_offset = 0, sn_size = b.size();
    uint32_t sn_address = 0;
    if (!std::memcmp(b.data(),
                     "\x7f"
                     "ELF",
                     4)) {
        p.resident_elf = true;
        if (b[4] != 1 || b[5] != 1 || u16(b, 18) != 8)
            return fail(error, "Oracle expects a little-endian ELF32 MIPS image");
        p.entry = u32(b, 24);
        uint32_t table = u32(b, 32);
        uint16_t stride = u16(b, 46), count = u16(b, 48), strings = u16(b, 50);
        if (stride < 40 || !count || strings >= count || !fits(b, table, size_t(stride) * count))
            return fail(error, "Invalid ELF section table");
        size_t names_header = table + size_t(strings) * stride;
        uint32_t names_offset = u32(b, names_header + 16), names_size = u32(b, names_header + 20);
        if (!fits(b, names_offset, names_size))
            return fail(error, "Invalid ELF section names");
        auto names = b.subspan(names_offset, names_size);
        bool found = false;
        for (uint16_t i = 0; i < count; ++i) {
            size_t h = table + size_t(i) * stride;
            ProgramSection s;
            if (!read_string(names, u32(b, h), s.name))
                return fail(error, "Invalid ELF section name");
            s.address = u32(b, h + 12);
            s.offset = u32(b, h + 16);
            s.size = u32(b, h + 20);
            s.executable = (u32(b, h + 8) & 4) != 0;
            s.zero_fill = u32(b, h + 4) == 8;
            if (!s.zero_fill && !fits(b, s.offset, s.size))
                return fail(error, "ELF section exceeds file");
            if ((u32(b, h + 8) & 2) != 0)
                p.sections.push_back(s);
            if (s.name == ".sndata") {
                if (found || s.zero_fill)
                    return fail(error, "Ambiguous ELF SNR2 metadata");
                sn_offset = s.offset;
                sn_size = s.size;
                sn_address = s.address;
                found = true;
            }
        }
        if (!found)
            return fail(error, "ELF has no SNR2 oracle symbols");
    }
    if (!fits(b, sn_offset, sn_size) || sn_size < 60 || std::memcmp(b.data() + sn_offset, "SNR2", 4))
        return fail(error, "Not an SNR2 module or resident symbol section");
    auto sn = b.subspan(sn_offset, sn_size);
    auto pointer = [&](uint32_t value, size_t length, size_t &offset) {
        if (value < sn_address)
            return false;
        offset = size_t(value - sn_address);
        return fits(sn, offset, length);
    };
    uint32_t declared = u32(sn, 44);
    if ((!p.resident_elf && declared != b.size()) || declared > sn_size || declared < 60)
        return fail(error, "SNR2 declared file size is invalid");
    uint32_t nr = u32(sn, 8), ns = u32(sn, 16);
    size_t relocations, symbols;
    if (nr > 1000000 || ns > 1000000 || !pointer(u32(sn, 4), size_t(nr) * 12, relocations) ||
        !pointer(u32(sn, 12), size_t(ns) * 12, symbols))
        return fail(error, "SNR2 tables exceed their container");
    p.symbol_table_offset = uint32_t(sn_offset + symbols);
    p.load_function = u32(sn, 24);
    p.unload_function = u32(sn, 28);
    if (u32(sn, 20)) {
        size_t offset;
        if (!pointer(u32(sn, 20), 1, offset) || !read_string(sn, offset, p.source_elf))
            return fail(error, "Invalid SNR2 source path");
    }
    p.symbols.reserve(ns);
    for (uint32_t i = 0; i < ns; ++i) {
        size_t r = symbols + size_t(i) * 12;
        ProgramSymbol s;
        s.record_offset = uint32_t(sn_offset + r);
        s.value = u32(sn, r + 4);
        s.kind = sn[r + 10];
        if (s.kind > 4)
            return fail(error, "Unknown SNR2 symbol kind");
        if (u32(sn, r)) {
            size_t name;
            if (!pointer(u32(sn, r), 1, name) || !read_string(sn, name, s.name))
                return fail(error, "SNR2 symbol name is out of range");
        }
        if (s.kind >= 2)
            s.file_offset = p.offset_for_address(s.value);
        if (!p.resident_elf && s.name.starts_with("lib_main") && s.kind == 2)
            p.entry = s.value;
        p.symbols.push_back(std::move(s));
    }
    for (uint32_t i = 0; i < nr; ++i) {
        size_t r = relocations + size_t(i) * 12;
        ProgramReference ref;
        ref.file_offset = p.offset_for_address(u32(sn, r));
        uint32_t info = u32(sn, r + 4);
        ref.symbol = info >> 8;
        ref.relocation = uint8_t(info);
        ref.addend = u32(sn, r + 8);
        if (ref.symbol >= p.symbols.size() || !fits(b, ref.file_offset, 4))
            return fail(error, "SNR2 reference site or symbol is out of range");
        ref.instruction = u32(b, ref.file_offset);
        ref.direct_call = ref.relocation == 4 && (ref.instruction >> 26) == 3;
        p.references.push_back(ref);
    }
    if (!p.resident_elf)
        p.warnings.push_back(
            "Internal relocation tail is retained as oracle data; it is not executed or applied.");
    out = std::move(p);
    if (error)
        error->clear();
    return true;
}
const char *program_system(const std::string &s) {
    if (s.starts_with("MEMCARD") || s.starts_with("sceMc"))
        return "save";
    if (s.starts_with("scePad"))
        return "input";
    if (s.starts_with("SND_") || s.starts_with("snd_") || s.starts_with("sceSd"))
        return "audio";
    if (s.find("vu_") != std::string::npos || s.find("microcode") != std::string::npos)
        return "vu";
    if (s.find("WAD") != std::string::npos || s.starts_with("LOADER_") || s.starts_with("MODULE_"))
        return "assets";
    if (s.find("Timer") != std::string::npos || s.find("VSYNC") != std::string::npos ||
        s.starts_with("SYSTEM_Swap"))
        return "timing";
    if (s.starts_with("Bink"))
        return "video";
    if (s.starts_with("dlib_") || s.starts_with("RENDER") || s.starts_with("FOG_") ||
        s.starts_with("TEXMGR") || s.find("scee3Psp") != std::string::npos)
        return "render";
    return "other";
}
} // namespace hig
