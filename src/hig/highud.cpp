#include "highud.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <stdexcept>

namespace hig {
namespace {
struct Reader {
    std::span<const uint8_t> b;
    uint32_t u32(size_t p) const {
        if (p > b.size() || b.size() - p < 4)
            throw std::runtime_error("HUD/media read exceeds asset");
        uint32_t v;
        std::memcpy(&v, b.data() + p, 4);
        return v;
    }
};
float signed12(unsigned v) { return float(int(v & 2047) - int(v & 2048)); }
bool fail(std::string *e, const std::exception &x) {
    if (e)
        *e = x.what();
    return false;
}
} // namespace
bool media_assets(std::span<const uint8_t> bytes, const HigWad &hdr, std::vector<MediaAsset> &out,
                  std::string *error) {
    out.clear();
    try {
        Reader r{bytes};
        std::set<uint32_t> seen;
        for (uint32_t first : hdr.sections) {
            uint32_t p = first;
            while (p) {
                if (p < 0xc0 || p > bytes.size() || !seen.insert(p).second)
                    throw std::runtime_error("Invalid/cyclic media asset chain");
                size_t h = p - 0xc0;
                MediaAsset a;
                a.hash = r.u32(h);
                a.offset = p;
                a.kind = r.u32(h + 0x64);
                a.size = r.u32(h + 0x68);
                if (a.size > bytes.size() - p)
                    throw std::runtime_error("Media asset exceeds WAD");
                const char *s = reinterpret_cast<const char *>(bytes.data() + h + 4);
                const char *end = static_cast<const char *>(std::memchr(s, 0, 0x60));
                a.name.assign(s, end ? size_t(end - s) : 0x60);
                out.push_back(std::move(a));
                p = r.u32(h + 0x70);
            }
        }
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &x) {
        out.clear();
        return fail(error, x);
    }
}
bool decode_hud(std::span<const uint8_t> bytes, HudLayout &out, std::string *error) {
    out = {};
    try {
        Reader r{bytes};
        if (r.u32(0) != 0x44335748 || r.u32(4) != 0x08060100 || r.u32(8) != bytes.size() - 16)
            throw std::runtime_error("Unsupported/truncated HW3D layout");
        unsigned groups = r.u32(16);
        if (groups > 4096)
            throw std::runtime_error("Excessive HUD screen count");
        size_t et = 20 + size_t(groups) * 4;
        unsigned count = r.u32(et);
        if (count > 65536)
            throw std::runtime_error("Excessive HUD element count");
        size_t shared = et + 4 + size_t(count) * 4;
        if (r.u32(shared))
            throw std::runtime_error("HW3D shared index sets not yet supported");
        out.elements.resize(count);
        for (unsigned i = 0; i < count; i++) {
            size_t p = r.u32(et + 4 + size_t(i) * 4);
            if (p < shared + 4)
                throw std::runtime_error("HUD element overlaps tables");
            uint32_t h = r.u32(p);
            p += 4;
            auto &e = out.elements[i];
            e.kind = h >> 29;
            uint32_t xy = r.u32(p);
            p += 4;
            e.x = signed12(xy >> 16);
            e.y = signed12(xy);
            if (e.kind == 2) {
                uint32_t br = r.u32(p);
                e.bounds = {e.x, e.y, signed12(br >> 16), signed12(br)};
            } else if (e.kind == 3) {
                e.reference = r.u32(p);
                if (e.reference >= count)
                    throw std::runtime_error("HUD reference out of range");
            } else if (e.kind == 0) {
                unsigned parts = h & 0x0fffffff;
                if (parts > 4096)
                    throw std::runtime_error("Excessive HUD part count");
                for (unsigned part = 0; part < parts; part++) {
                    size_t size = r.u32(p);
                    unsigned flags = r.u32(p + 4);
                    p += 8;
                    unsigned n = flags & 0xffffff;
                    bool colors = (flags & 0x80000000) != 0;
                    if ((flags & 0x7e000000) || n > 65536 || size != size_t(n) * (colors ? 8 : 4))
                        throw std::runtime_error("Unsupported HUD vertex encoding");
                    uint32_t color = 0xffffffff;
                    if (!colors) {
                        color = r.u32(p);
                        p += 4;
                    }
                    std::vector<HudVertex> strip;
                    for (unsigned k = 0; k < n; k++) {
                        if (colors) {
                            color = r.u32(p);
                            p += 4;
                        }
                        uint32_t v = r.u32(p);
                        p += 4;
                        strip.push_back({e.x + signed12(v >> 16), e.y + signed12(v), color});
                    }
                    for (unsigned k = 2; k < n; k++)
                        for (unsigned j : {k - 2, k - 1, k})
                            e.triangles.push_back(strip[j]);
                }
            } else
                throw std::runtime_error("Unknown HW3D element type");
        }
        out.screens.resize(groups);
        for (unsigned i = 0; i < groups; i++) {
            size_t p = r.u32(20 + size_t(i) * 4);
            unsigned n = r.u32(p);
            if (n > count)
                throw std::runtime_error("Excessive HUD screen elements");
            for (unsigned j = 0; j < n; j++) {
                unsigned e = r.u32(p + 4 + size_t(j) * 4);
                if (e >= count)
                    throw std::runtime_error("HUD screen reference out of range");
                out.screens[i].push_back(e);
            }
        }
        std::vector<HudVertex> tmp;
        for (unsigned i = 0; i < count; i++)
            if (!hud_triangles(out, i, tmp, error))
                throw std::runtime_error("Cyclic HUD reference");
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &x) {
        out = {};
        return fail(error, x);
    }
}
bool hud_triangles(const HudLayout &hud, unsigned element, std::vector<HudVertex> &out,
                   std::string *error) {
    out.clear();
    float x = 0, y = 0;
    for (size_t depth = 0; depth <= hud.elements.size(); depth++) {
        if (element >= hud.elements.size())
            break;
        const auto &e = hud.elements[element];
        if (e.kind == 3) {
            x += e.x;
            y += e.y;
            element = e.reference;
            continue;
        }
        out = e.triangles;
        // A reference places its template at the reference's own position: the template's origin is only where it
        // sits in the authoring sheet (the pause menu's button plate 764 is kept at -178,462 and placed seven times
        // down the middle of screen 36). Elements drawn directly keep their own origin.
        if (depth) {
            x -= e.x;
            y -= e.y;
        }
        for (auto &v : out) {
            v.x += x;
            v.y += y;
        }
        return true;
    }
    if (error)
        *error = "Invalid/cyclic HUD reference";
    return false;
}
bool hud_template(const HudLayout &hud, unsigned element, std::vector<HudVertex> &out) {
    out.clear();
    if (element >= hud.elements.size() || hud.elements[element].kind != 0)
        return false;
    const auto &e = hud.elements[element];
    out = e.triangles;
    for (auto &v : out) {
        v.x -= e.x;
        v.y -= e.y;
    }
    return true;
}
bool decode_hud_menus(std::span<const uint8_t> b, const HudLayout &hud, std::vector<HudMenu> &out) {
    out.clear();
    auto u32 = [&](size_t p) {
        uint32_t v = 0;
        if (p + 4 <= b.size())
            std::memcpy(&v, b.data() + p, 4);
        return v;
    };
    const uint32_t one = 0x3f800000;
    const size_t count = hud.elements.size();
    size_t previous = 0;
    for (size_t at = 0; at + 0xe8 <= b.size(); at += 4) {
        const uint32_t text = u32(at), plate = u32(at + 4), bounds = u32(at + 8), selected = u32(at + 0xc8);
        if (!text || text > 6000 || !plate || plate >= count || !bounds || bounds >= count || selected >= count)
            continue;
        if (u32(at + 0xc0) != one || u32(at + 0xc4) != one || b[at + 0xcc] > 1 || b[at + 0xcd] > 1)
            continue;
        if (hud.elements[bounds].kind != 2 || hud.elements[plate].kind == 2)
            continue;
        if (out.empty() || at != previous + 0xe8)
            out.emplace_back();
        out.back().items.push_back({text, plate, bounds, selected, u32(at + 0xd0), b[at + 0xcc] != 0});
        previous = at;
        at += 0xe8 - 4;
    }
    return !out.empty();
}
bool load_hud(const IsoFs &disc, HudAssets &out, std::string *error) {
    out = {};
    if (!decode_hud(disc.read("/HUD/MAINHUD.W3D"), out.layout, error))
        return false;
    HigWad hdr;
    std::vector<uint8_t> bytes;
    std::vector<MediaAsset> assets;
    if (!unpack_hig_wad(disc.read("/GLOBAL.WAD"), hdr, bytes, error) ||
        !media_assets(bytes, hdr, assets, error))
        return false;
    bool font = false;
    for (const auto &a : assets) {
        auto data = std::span<const uint8_t>(bytes).subspan(a.offset, a.size);
        if (a.name.ends_with("HUD_ICONS.PAK")) {
            std::vector<PakEntry> entries;
            if (!parse_pak(data, entries, error))
                return false;
            for (const auto &entry : entries) {
                GimImage icon;
                if (!decode_gim(data.subspan(entry.offset, entry.size), icon, error))
                    return false;
                out.icons.push_back(std::move(icon));
            }
        }
        if (a.name.ends_with("hud_typeface01_16col.gim")) {
            if (!decode_gim(data, out.font, error))
                return false;
            font = true;
        }
    }
    // FontTableNormal is immutable data in the USA resident; no code executes.
    auto resident = disc.read("/SCUS_976.15");
    if (!font || out.icons.size() < 94 || resident.size() < 0x7baa0) {
        if (error)
            *error = "Retail HUD font/icons/USA font metrics are missing";
        return false;
    }
    std::memcpy(out.metrics.data(), resident.data() + 0x7b6a0, 1024);
    auto level = disc.read("/LVL/LEVEL_01.REL");
    if (level.size() >= 0x1f1034) {
        Reader r{level};
        out.max_ammo = r.u32(0x1f1028);
        out.weapon_icon = r.u32(0x1f1030);
        if (!out.max_ammo || out.max_ammo > 10000 || !out.weapon_icon ||
            out.weapon_icon > out.icons.size()) {
            if (error)
                *error = "Unsupported retail weapon HUD data";
            return false;
        }
    }
    // The HUD's GIM conversion retains its magenta transparency key in the
    // palette. This is a HUD-only rule, never applied to world/actor textures.
    auto key = [](GimImage &im) {
        for (size_t p = 0; p + 3 < im.rgba.size(); p += 4)
            if (im.rgba[p] == 255 && im.rgba[p + 1] == 0 && im.rgba[p + 2] == 255)
                std::fill_n(im.rgba.begin() + p, 4, uint8_t(0));
    };
    key(out.font);
    for (auto &icon : out.icons)
        key(icon);
    return true;
}
} // namespace hig
