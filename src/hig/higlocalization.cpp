#include "higlocalization.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
namespace hig {
const LocalizedText *Localization::find(uint32_t id, size_t language) const {
    if (language >= languages.size())
        return nullptr;
    for (const auto &e : languages[language].entries)
        if (e.id == id)
            return &e;
    return nullptr;
}
bool decode_localization(std::span<const uint8_t> b, Localization &out, std::string *error) {
    out = {};
    auto fail = [&](const char *s) {
        if (error)
            *error = s;
        return false;
    };
    if (b.size() < 56 || std::memcmp(b.data(), "LOC_DATA", 8) ||
        std::memcmp(b.data() + 16, "SCOL", 4))
        return fail("Not a supported LOC_DATA container");
    auto u32 = [&](size_t o) {
        uint32_t v;
        std::memcpy(&v, b.data() + o, 4);
        return v;
    };
    size_t languages = b[12], count = size_t(b[13]) | (size_t(b[14]) << 8);
    if (!languages || languages > 32 || !count || count > 65535)
        return fail("Invalid localization counts");
    size_t toc = 20;
    while (toc + 4 <= b.size() && toc < 1044 && std::memcmp(b.data() + toc, "LTOC", 4))
        toc += 4;
    if (toc + 4 + languages * 12 > b.size() || toc >= 1044)
        return fail("Localization LTOC is missing or truncated");
    Localization result;
    std::copy_n(b.begin() + 8, 4, result.version.begin());
    for (size_t i = 20; i < toc; i += 4)
        result.colors.push_back(u32(i));
    uint64_t previous_end = toc + 4 + languages * 12;
    for (size_t l = 0; l < languages; l++) {
        size_t record = toc + 4 + l * 12;
        LanguageData lang;
        lang.file_offset = u32(record);
        lang.byte_size = u32(record + 4);
        lang.auxiliary_offset = u32(record + 8);
        size_t base = lang.file_offset, size = lang.byte_size, aux = lang.auxiliary_offset;
        if (base < previous_end || base > b.size() || size > b.size() - base || aux > size ||
            count * 12 > aux)
            return fail("Localization language extent/table is out of bounds");
        std::set<uint32_t> ids;
        for (size_t i = 0; i < count; i++) {
            size_t p = base + i * 12;
            if (std::memcmp(b.data() + p, "TDEF", 4))
                return fail("Localization text definition marker is invalid");
            LocalizedText text;
            text.id = u32(p + 4);
            size_t offset = u32(p + 8);
            if (offset < count * 12 + 4 || offset >= aux ||
                std::memcmp(b.data() + base + offset - 4, "ENTR", 4) || !ids.insert(text.id).second)
                return fail("Localization text reference is invalid or duplicated");
            const char *start = (const char *)b.data() + base + offset;
            const char *end = (const char *)std::memchr(start, 0, aux - offset);
            if (!end || size_t(end - start) > 65536)
                return fail("Localization text is unterminated or excessive");
            text.file_offset = uint32_t(base + offset);
            text.encoded.assign(start, end);
            lang.entries.push_back(std::move(text));
        }
        previous_end = base + size;
        result.languages.push_back(std::move(lang));
    }
    out = std::move(result);
    if (error)
        error->clear();
    return true;
}
std::string inspect_localized_text(const LocalizedText &text) {
    std::string out;
    for (size_t i = 0; i < text.encoded.size(); i++) {
        uint8_t c = uint8_t(text.encoded[i]);
        if (c == 0x90 && i + 1 < text.encoded.size()) {
            i++;
            continue;
        } // observed inline colour control
        if (c >= 0x80 && c < 0xa0) {
            char glyph[20];
            sprintf_s(glyph, "[glyph %02X]", c);
            out += glyph;
        } else if (c >= 0xa0) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 63));
        } else
            out += char(c);
    }
    return out;
}
} // namespace hig
