#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace hig {
struct LocalizedText {
    uint32_t id = 0, file_offset = 0;
    std::string encoded;
};
struct LanguageData {
    uint32_t file_offset = 0, byte_size = 0, auxiliary_offset = 0;
    std::vector<LocalizedText> entries;
};
struct Localization {
    std::array<uint8_t, 4> version{};
    std::vector<uint32_t> colors;
    std::vector<LanguageData> languages;
    const LocalizedText *find(uint32_t id, size_t language = 0) const;
};
bool decode_localization(std::span<const uint8_t>, Localization &, std::string *error = nullptr);
// Inspection text: retain raw encoded bytes separately. Inline glyph codes
// are printed as markers until their font/input meanings are established.
std::string inspect_localized_text(const LocalizedText &);
} // namespace hig
