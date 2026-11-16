#include "world.h"
#include <algorithm>
namespace sm {
bool Runtime::mount(const std::wstring &path, std::string &error) {
    world_ = {};
    if (!disc_.open(path, &error))
        return false;
    if (!disc_.find("/SCUS_976.15")) {
        disc_.close();
        error = "Initial native target is Size Matters PS2 USA (SCUS-97615)";
        return false;
    }
    return true;
}
bool Runtime::load(const std::string &wad, std::string &error) {
    World next;
    auto file = disc_.find(wad);
    if (!file) {
        error = "Level WAD is absent from ISO: " + wad;
        return false;
    }
    next.disc_path = file->path;
    auto bytes = disc_.read(*file);
    hig::HigWad header;
    std::vector<uint8_t> image;
    if (!hig::unpack_hig_wad(bytes, header, image, &error))
        return false;
    if (hig::wad_checksum(image) != header.crc) {
        error = "WAD checksum mismatch";
        return false;
    }
    if (!hig::build_level(image, header, next.level, &error))
        return false;
    for (const auto &a : next.level.collision_assets)
        if (a.offset == header.sections[2]) {
            if (a.offset > image.size() || a.size > image.size() - a.offset ||
                !hig::decode_collision(std::span<const uint8_t>(image).subspan(a.offset, a.size),
                                       next.terrain_collision, &error))
                return false;
            break;
        }
    if (next.terrain_collision.faces.empty())
        next.pending.push_back("No decoded world terrain COL");
    // The surface types' table: "STD ", version 3, a count, then 16 bytes a type {hash, flags, sound, -}. It is
    // asset 0x41 of type 0xb, looked for in the level's WAD and then in the global ones (0x129b70, 0x128720).
    auto surfaces = [&](std::span<const uint8_t> data) {
        for (size_t at = 0; at + 12 <= data.size(); at += 4) {
            if (std::memcmp(data.data() + at, "STD ", 4))
                continue;
            uint32_t version, count;
            std::memcpy(&version, data.data() + at + 4, 4);
            std::memcpy(&count, data.data() + at + 8, 4);
            if (version != 3 || !count || count > 4096 || at + 12 + size_t(count) * 16 > data.size())
                continue;
            for (uint32_t k = 0; k < count; k++) {
                World::SurfaceType s;
                const uint8_t *row = data.data() + at + 12 + size_t(k) * 16;
                std::memcpy(&s.hash, row, 4);
                std::memcpy(&s.flags, row + 4, 2);
                std::memcpy(&s.sound, row + 8, 4);
                next.surface_types.push_back(s);
            }
            return true;
        }
        return false;
    };
    if (!surfaces(image))
        for (const char *shared : {"/SP.WAD", "/GLOBAL.WAD"}) {
            auto other = disc_.find(shared);
            if (!other)
                continue;
            hig::HigWad other_header;
            std::vector<uint8_t> other_image;
            std::string ignored;
            if (hig::unpack_hig_wad(disc_.read(*other), other_header, other_image, &ignored) && surfaces(other_image))
                break;
        }
    next.pending.insert(next.pending.end(), next.level.warnings.begin(), next.level.warnings.end());
    next.pending.push_back("Original enemies use native combat; retail wave triggers, scripted "
                           "objectives and combat tuning remain under review");
    next.pending.push_back("Native player controller is a bring-up implementation; retail movement "
                           "constants and full material passes remain under review");
    next.pending.push_back(
        "Progress saves, full sound sequencing and SPU reverb/envelopes remain pending");
    world_ = std::move(next);
    error.clear();
    return true;
}
uint32_t FixedClock::advance(std::chrono::nanoseconds elapsed) {
    uint64_t ns = uint64_t(std::max<int64_t>(0, elapsed.count()));
    constexpr uint64_t maximum = 250000000;
    if (ns > maximum) {
        dropped_ += ns - maximum;
        ns = maximum;
    }
    remainder_ += ns * hz_;
    uint32_t count = uint32_t(remainder_ / 1000000000);
    remainder_ %= 1000000000;
    ticks_ += count;
    return count;
}
} // namespace sm
