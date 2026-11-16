#include "higcatalog.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace hig {
namespace {
std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
uint32_t word(std::span<const uint8_t> b, size_t p) {
    uint32_t n = 0;
    if (p <= b.size() && b.size() - p >= 4)
        std::memcpy(&n, b.data() + p, 4);
    return n;
}
} // namespace
std::vector<ActorCatalogEntry> catalog_actors(std::span<const uint8_t> wad,
                                              const std::string &source) {
    auto entries = actor_assets(wad);
    std::unordered_map<std::string, size_t> animations;
    for (size_t i = 0; i < entries.size(); i++)
        if (lower(entries[i].path).ends_with(".anim"))
            animations.emplace(lower(entries[i].path), i);
    std::vector<ActorCatalogEntry> out;
    for (const auto &e : entries) {
        auto path = lower(e.path);
        if (!path.ends_with(".ps2.gde") || word(e.bytes, 0) != 0x10001 ||
            word(e.bytes, 4) != 0x10000001)
            continue;
        auto found = animations.find(path.substr(0, path.size() - 8) + ".anim");
        if (found == animations.end())
            continue;
        const auto &bank = entries[found->second];
        if (bank.bytes.size() < 12 || word(bank.bytes, 0) != 12 || !bank.bytes[8])
            continue;
        uint32_t skeleton = word(e.bytes, 32), bones = word(e.bytes, size_t(skeleton) + 4);
        if (!skeleton || word(e.bytes, skeleton) != 0x44444444 || !bones || bones > 255)
            continue;
        std::string name = e.path.substr(e.path.find_last_of("\\/") + 1);
        name.resize(name.size() - 8);
        if (name.ends_with(".mb"))
            name.resize(name.size() - 3);
        out.push_back({name, source, e.path, bank.path, e.hash, bank.bytes[8], bones});
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.name < b.name; });
    return out;
}
} // namespace hig
