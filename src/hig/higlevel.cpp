#include "higlevel.h"
#include "higenvironment.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace hig {
namespace {

uint16_t rd16(std::span<const uint8_t> b, size_t o) {
    return o + 2 <= b.size() ? uint16_t(b[o] | (b[o + 1] << 8)) : 0;
}
uint32_t rd32(std::span<const uint8_t> b, size_t o) {
    if (o + 4 > b.size()) return 0;
    return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) |
           (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
}
float rdf(std::span<const uint8_t> b, size_t o) {
    uint32_t v = rd32(b, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
int16_t rds16(std::span<const uint8_t> b, size_t o) {
    return int16_t(rd16(b, o));
}

// ---- GIM ---------------------------------------------------------------------

void px5650(uint16_t v, uint8_t* o) {
    uint8_t r = uint8_t((v & 0x1F) << 3), g = uint8_t(((v >> 5) & 0x3F) << 2),
            b = uint8_t(((v >> 11) & 0x1F) << 3);
    o[0] = uint8_t(r | (r >> 5));
    o[1] = uint8_t(g | (g >> 6));
    o[2] = uint8_t(b | (b >> 5));
    o[3] = 255;
}
void px5551(uint16_t v, uint8_t* o) {
    uint8_t r = uint8_t((v & 0x1F) << 3), g = uint8_t(((v >> 5) & 0x1F) << 3),
            b = uint8_t(((v >> 10) & 0x1F) << 3);
    o[0] = uint8_t(r | (r >> 5));
    o[1] = uint8_t(g | (g >> 5));
    o[2] = uint8_t(b | (b >> 5));
    o[3] = (v & 0x8000) ? 255 : 0;
}
void px4444(uint16_t v, uint8_t* o) {
    o[0] = uint8_t((v & 0xF) * 17);
    o[1] = uint8_t(((v >> 4) & 0xF) * 17);
    o[2] = uint8_t(((v >> 8) & 0xF) * 17);
    o[3] = uint8_t(((v >> 12) & 0xF) * 17);
}

// direct-colour pixel fetch (fmt 0..3) into RGBA
bool fetch_px(std::span<const uint8_t> d, size_t idx, uint32_t fmt,
              uint8_t* o) {
    if (fmt == 3) {
        if (idx * 4 + 4 > d.size()) return false;
        std::memcpy(o, d.data() + idx * 4, 4);
        return true;
    }
    if (idx * 2 + 2 > d.size()) return false;
    uint16_t v = uint16_t(d[idx * 2] | (d[idx * 2 + 1] << 8));
    if (fmt == 0)
        px5650(v, o);
    else if (fmt == 1)
        px5551(v, o);
    else
        px4444(v, o);
    return true;
}

// PSP swizzle: 16-byte x 8-row blocks
std::vector<uint8_t> unswizzle(std::span<const uint8_t> data,
                               uint32_t width_bytes, uint32_t height) {
    std::vector<uint8_t> out(data.size());
    uint32_t bw = width_bytes / 16;
    size_t src = 0;
    for (uint32_t by = 0; by < height / 8; by++)
        for (uint32_t bx = 0; bx < bw; bx++)
            for (uint32_t y = 0; y < 8; y++) {
                size_t dst = size_t(by * 8 + y) * width_bytes + bx * 16;
                if (src + 16 > data.size() || dst + 16 > out.size()) break;
                std::memcpy(out.data() + dst, data.data() + src, 16);
                src += 16;
            }
    return out;
}

struct GimBlockHdr {
    uint32_t hdr_size = 0, fmt = 0, order = 0, w = 0, h = 0;
    uint32_t bpp = 0, pitch_align = 0;
};
GimBlockHdr gim_img_header(std::span<const uint8_t> b, size_t o) {
    GimBlockHdr h;
    h.hdr_size = rd16(b, o);
    h.fmt = rd16(b, o + 4);
    h.order = rd16(b, o + 6);
    h.w = rd16(b, o + 8);
    h.h = rd16(b, o + 10);
    h.bpp = rd16(b, o + 12);
    h.pitch_align = rd16(b, o + 14);
    return h;
}

struct GimSurface {
    GimBlockHdr header;
    size_t pitch = 0;
    std::span<const uint8_t> pixels;
    std::vector<uint8_t> linear;
};

// sceGimPictureGetImagePixels (SCUS file 0x1f1a0) indexes the frame table
// at header +0x18. Its first entry selects mip 0, frame 0. Row padding is
// separate from the visible width (e.g. r_wallTrim: 35 pixels, 48 bytes).
bool gim_surface(std::span<const uint8_t> b, size_t block, GimSurface& s, uint32_t palette_limit = 0) {
    if (block > b.size() || b.size() - block < 16) return false;
    size_t size = rd32(b, block + 4), data = rd32(b, block + 12);
    if (size > b.size() - block || data < 16 || data > size || size - data < 0x30) return false;
    auto h = b.subspan(block + data, size - data);
    s.header = gim_img_header(h, 0);
    // Retail INDEX4 tube_01 retains a 256-colour palette width in its
    // header but stores only the 16 addressable entries. Never read the
    // missing entries from the following chunk or asset.
    if (palette_limit && s.header.h == 1)
        s.header.w = std::min(s.header.w, palette_limit);
    const auto& ih = s.header;
    if (ih.hdr_size < 0x30 || ih.hdr_size > h.size() || ih.fmt > 5 || ih.order > 1 ||
        !ih.w || !ih.h || ih.w > 4096 || ih.h > 4096 || ih.pitch_align > 4096) return false;
    uint32_t bits = ih.fmt == 4 ? 4 : ih.fmt == 5 ? 8 : ih.fmt == 3 ? 32 : 16;
    if (ih.bpp != bits || !rd16(h, 0x2a) || !rd16(h, 0x2e)) return false;
    size_t table = rd32(h, 0x18), end = rd32(h, 0x20);
    if (table < ih.hdr_size || table > h.size() || h.size() - table < 4) return false;
    size_t start = rd32(h, table);
    if (start < table + 4 || end > h.size() || start > end) return false;
    size_t row = (size_t(ih.w) * bits + 7) / 8;
    size_t align = std::max(1u, ih.pitch_align);
    s.pitch = (row + align - 1) / align * align;
    // Palettes can omit padding after their last row (239 RGBA entries
    // in r_wallTrim). Padding between rows must still be present.
    if (size_t(ih.h - 1) * s.pitch + row > end - start) return false;
    s.pixels = h.subspan(start, end - start);
    if (ih.order == 1) {
        uint32_t rows = (ih.h + 7) & ~7u;
        if (s.pitch % 16 || s.pitch * rows > s.pixels.size()) return false;
        s.linear = unswizzle(s.pixels, uint32_t(s.pitch), rows);
        s.pixels = s.linear;
    }
    return true;
}

}  // namespace

bool decode_gim(std::span<const uint8_t> gim, GimImage& out,
                std::string* err) {
    if (gim.size() < 0x30 || std::memcmp(gim.data(), "MIG.00.1PSP", 11)) {
        if (err) *err = "not a GIM";
        return false;
    }
    // block walk: {u16 id, u16, u32 size, u32 next, u32 data_ofs}
    size_t img_blk = 0, pal_blk = 0;
    struct Frame { size_t off, end; };
    std::vector<Frame> stack{{0x10, gim.size()}};
    while (!stack.empty()) {
        Frame f = stack.back();
        stack.pop_back();
        size_t o = f.off;
        while (o + 16 <= f.end) {
            uint32_t id = rd16(gim, o);
            uint32_t size = rd32(gim, o + 4);
            uint32_t dofs = rd32(gim, o + 12);
            if (!id || size < 16 || size > f.end - o || dofs < 16 || dofs > size) break;
            if (id == 4 && !img_blk) img_blk = o;
            if (id == 5 && !pal_blk) pal_blk = o;
            if (id == 2 || id == 3) {
                stack.push_back({o + dofs, std::min<size_t>(o + size, f.end)});
                o += size;
            } else {
                o += size;
            }
        }
    }
    if (!img_blk) {
        if (err) *err = "GIM: no image block";
        return false;
    }
    GimSurface source;
    if (!gim_surface(gim, img_blk, source)) {
        if (err) *err = "GIM: invalid image frame or row pitch";
        return false;
    }
    const auto& ih = source.header;
    uint32_t w = ih.w, h = ih.h;

    std::vector<std::array<uint8_t, 4>> pal;
    if (pal_blk) {
        GimSurface palette;
        if (!gim_surface(gim, pal_blk, palette, ih.fmt == 4 ? 16 : 256) || palette.header.fmt > 3 ||
            size_t(palette.header.w) * palette.header.h > 65536) {
            if (err) *err = "GIM: invalid palette frame";
            return false;
        }
        const auto& ph = palette.header;
        uint32_t n = ph.w * ph.h;
        pal.resize(n);
        for (uint32_t y = 0; y < ph.h; ++y)
            for (uint32_t x = 0; x < ph.w; ++x)
                if (!fetch_px(palette.pixels.subspan(y * palette.pitch), x, ph.fmt, pal[y * ph.w + x].data()))
                    return false;
    }

    out.width = w;
    out.height = h;
    out.rgba.assign(size_t(w) * h * 4, 0);
    auto emit = [&](size_t i, const uint8_t* px) {
        if (i * 4 + 4 <= out.rgba.size())
            std::memcpy(out.rgba.data() + i * 4, px, 4);
    };
    if (ih.fmt <= 3) {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t px4[4];
                if (fetch_px(source.pixels.subspan(y * source.pitch), x, ih.fmt, px4)) emit(size_t(y) * w + x, px4);
            }
    } else if (ih.fmt == 5 || ih.fmt == 4) {   // INDEX8 / INDEX4
        bool idx4 = ih.fmt == 4;
        if (pal.empty()) {
            if (err) *err = "GIM: indexed without palette";
            return false;
        }
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t v = source.pixels[y * source.pitch + (idx4 ? x / 2 : x)];
                uint32_t index = idx4 ? ((x & 1) ? v >> 4 : v & 15) : v;
                emit(size_t(y) * w + x, pal[index < pal.size() ? index : 0].data());
            }
    } else {
        if (err) *err = "GIM: unsupported format " + std::to_string(ih.fmt);
        return false;
    }
    // PS2 alpha convention: 0x80 = opaque; expand to full range
    for (size_t i = 3; i < out.rgba.size(); i += 4) {
        uint32_t a = out.rgba[i] * 2;
        out.rgba[i] = uint8_t(a > 255 ? 255 : a);
    }
    return true;
}

// ---- GDE mesh extraction ------------------------------------------------------

namespace {

// All offsets below are relative to one bounded GDE asset, not its WAD section.
bool range(std::span<const uint8_t> b, size_t off, size_t count, size_t stride = 1) {
    return !count || (off <= b.size() && count <= (b.size() - off) / stride);
}
std::string ascii_name(std::span<const uint8_t> b, size_t off, size_t len) {
    if (!range(b, off, len)) return {};
    std::string s;
    for (size_t i = 0; i < len && b[off + i]; ++i) {
        uint8_t c = b[off + i];
        if (c < 32 || c >= 127) return {};
        s.push_back(char(c));
    }
    return s;
}

struct Unpack {
    uint8_t cmd = 0;
    uint16_t imm = 0;
    uint32_t num = 0;
    size_t off = 0;
};

// Consume a complete packet before emitting any geometry. In particular an
// UNPACK may never borrow vertices from the next DMA packet or asset.
bool parse_vif(std::span<const uint8_t> b, std::vector<Unpack>& out) {
    uint32_t cl = 1, wl = 1;
    for (size_t o = 0; o < b.size();) {
        if (!range(b, o, 4)) return false;
        uint16_t imm = rd16(b, o);
        uint32_t n = b[o + 2] ? b[o + 2] : 256;
        uint8_t cmd = b[o + 3] & 0x7f;
        o += 4;
        size_t bytes = 0;
        if (cmd >= 0x60) {
            uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
            uint32_t consumed = wl <= cl ? n : cl * (n / wl) + std::min(n % wl, cl);
            bytes = vl == 3 ? size_t(consumed) * 2
                           : size_t(consumed) * (vn + 1) * (32 >> vl) / 8;
            bytes = (bytes + 3) & ~size_t(3);
            if (!range(b, o, bytes)) return false;
            // Mesh streams use skip cycles. Filling and masked UNPACKs need
            // VU memory emulation; do not misinterpret them as dense arrays.
            if (wl > cl || (cmd & 0x10)) return false;
            out.push_back({cmd, imm, n, o});
        } else {
            switch (cmd) {
                case 0x01: cl = imm & 255; wl = imm >> 8; if (!wl) wl = 256; break;
                case 0x00: case 0x02: case 0x03: case 0x04:
                case 0x05: case 0x06: case 0x07: case 0x10:
                case 0x11: case 0x13: case 0x14: case 0x15: case 0x17: break;
                case 0x20: bytes = 4; break;
                case 0x30: case 0x31: bytes = 16; break;
                case 0x4a: bytes = size_t(n) * 8; break;
                case 0x50: case 0x51: bytes = size_t(imm) * 16; break;
                default: return false;
            }
        }
        if (!range(b, o, bytes)) return false;
        o += bytes;
    }
    return true;
}

bool stream_tris(std::span<const uint8_t> b, LevelMaterial& m,
                 float pos_scale, float uv_units) {
    std::vector<Unpack> ups;
    if (!parse_vif(b, ups)) return false;
    struct Batch {
        const Unpack *pos = nullptr, *uv = nullptr, *color = nullptr, *normal = nullptr;
        uint32_t count = 0;
        uint32_t primitive = 0;
    } cur;
    std::vector<Batch> batches;
    for (const auto& u : ups) {
        uint32_t format = u.cmd & 15;
        if (format == 12 && (u.imm & 0x3ff) == 0 && u.num == 1) {
            uint64_t tag = uint64_t(rd32(b, u.off)) | (uint64_t(rd32(b, u.off + 4)) << 32);
            cur = {};
            cur.count = uint32_t(tag & 0x7fff);
            cur.primitive = uint32_t((tag >> 47) & 0x7ff);
        } else if (format == 5) {
            cur.uv = &u;
        } else if (format == 14) {
            if (u.imm & 0x4000) cur.color = &u;
            else cur.normal = &u;
        } else if (format == 13 || format == 9) {
            // Both static and rigid/shrub programs emit independent strips.
            if (!cur.count || cur.count != u.num || (cur.primitive & 7) != 4 ||
                (cur.uv && cur.uv->num != u.num) ||
                (cur.color && cur.color->num != u.num) ||
                (cur.normal && cur.normal->num != u.num)) return false;
            cur.pos = &u;
            batches.push_back(cur);
            cur = {};
        }
    }
    for (const auto& bt : batches) {
        const size_t stride = (bt.pos->cmd & 15) == 9 ? 6 : 8;
        const uint32_t first = uint32_t(m.positions.size() / 3);
        m.alpha_blend |= (bt.primitive & 0x40) != 0;
        int16_t previous[2][3]{};
        for (uint32_t i = 0; i < bt.count; ++i) {
            int16_t p[3];
            for (int k = 0; k < 3; ++k) {
                p[k] = rds16(b, bt.pos->off + i * stride + k * 2);
                m.positions.push_back(p[k] * pos_scale);
            }
            for (int k = 0; k < 2; ++k)
                m.uvs.push_back(bt.uv ? rds16(b, bt.uv->off + i * 4 + k * 2) / uv_units : 0.0f);
            for (int k = 0; k < 4; ++k)
                m.rgba.push_back(bt.color ? uint8_t(std::min(255, int(b[bt.color->off + i * 4 + k]) * 2)) : 255);
            float normal[3]{};
            if (bt.normal) {
                float len = 0;
                for (int k = 0; k < 3; ++k) {
                    normal[k] = int8_t(b[bt.normal->off + i * 4 + k]) / 127.0f;
                    len += normal[k] * normal[k];
                }
                if (len > 0) for (float& v : normal) v /= std::sqrt(len);
            }
            m.normals.insert(m.normals.end(), normal, normal + 3);
            if (i >= 2) {
                // Repeated vertices stitch separate strips. Full int16 extrema
                // are valid positions, not clip markers (e.g. shrub tips).
                auto same = [](const int16_t* a, const int16_t* c) {
                    return a[0] == c[0] && a[1] == c[1] && a[2] == c[2];
                };
                if (!same(p, previous[0]) && !same(p, previous[1]) && !same(previous[0], previous[1])) {
                    uint32_t v = first + i;
                    // Preserve the strip's counter-clockwise front face. The
                    // renderer explicitly opts these meshes into that convention.
                    if (i & 1) m.indices.insert(m.indices.end(), {v - 2, v, v - 1});
                    else m.indices.insert(m.indices.end(), {v - 2, v - 1, v});
                }
            }
            std::copy(previous[1], previous[1] + 3, previous[0]);
            std::copy(p, p + 3, previous[1]);
        }
    }
    return !batches.empty();
}

struct GdeHeader {
    uint32_t type, nmat, mats, ntex, textures, render;
};
bool gde_header(std::span<const uint8_t> b, GdeHeader& g) {
    if (!range(b, 0, 0x3c) || rd32(b, 0) != 0x10001) return false;
    g = {rd32(b, 4), rd32(b, 8), rd32(b, 12), rd32(b, 16), rd32(b, 20), rd32(b, 28)};
    return (g.type & 0xfffffff0) == 0x10000000 &&
           range(b, g.mats, g.nmat, 0xf8) && range(b, g.textures, g.ntex, 0x14) &&
           range(b, g.render, 0x2c);
}

// These are independent tables. A material pass stores texture *indices*,
// which LOADER_LoadGdeFile resolves through the asset's texture table.
bool material_info(std::span<const uint8_t> b, const GdeHeader& g,
                   uint32_t index, LevelMaterial& m) {
    if (index >= g.nmat) return false;
    size_t r = g.mats + size_t(index) * 0xf8;
    m.name = ascii_name(b, r, 32);
    if (m.name.empty()) m.name = "mat" + std::to_string(index);
    uint32_t passes = rd32(b, r + 0x6c);
    if (passes > 2) return false;
    if (passes && rd16(b, r + 0x70)) {
        uint32_t texture = rd16(b, r + 0x74);
        if (texture >= g.ntex) return false;
        m.tex_hash = rd32(b, g.textures + size_t(texture) * 0x14);
    }
    if (passes) {
        uint8_t test = b[r + 0xa5];
        // PS2 alpha reference 0x40 / opaque 0x80; mode 0 rejects alpha 0.
        m.alpha_test = test == 1 ? 0.5f : test == 0 ? 1.0f / 255.0f : 0.0f;
    }
    return true;
}

bool decode_model(std::span<const uint8_t> b, const GdeHeader& g,
                  LevelModel& model, std::string& err, unsigned lod = 0) {
    auto fail = [&](const char* why) { err = why; return false; };
    size_t r = g.render;
    float scale = 0;
    // The resident tie/shrub VU renderers use I=1/1024 (file offsets
    // 0xa4df8 / 0xa4548). Static terrain uses I=1/256 at 0xa0d00.
    // Do not divide by the render header's +8 (shrub +0xc) float. The
    // PS2 programs use these fixed units; e.g. stones/grass encode a full
    // texture tile as 4096..5120 even though that header field is 2.
    // Dividing again selects a quarter of the atlas and cuts alpha foliage.
    const float uv_units = g.type == 0x10000000 ? 256.f : 1024.f;
    std::vector<LevelMaterial> mats(g.nmat);
    for (uint32_t i = 0; i < g.nmat; ++i)
        if (!material_info(b, g, i, mats[i])) return fail("invalid material table");
    auto emit = [&](uint32_t m, size_t off, size_t len) {
        return m < mats.size() && range(b, off, len) &&
               stream_tris(b.subspan(off, len), mats[m], scale, uv_units);
    };
    auto valid_scale = [&] { return std::isfinite(scale) && scale > 0; };
    if (g.type == 0x10000000) {
        if (rd32(b, r) != 3) return fail("unsupported static render version");
        scale = rdf(b, r + 4) * 0.01f;  // quantization unit is centimetres
        uint32_t count = rd32(b, r + 0x10), list = rd32(b, r + 0x14);
        if (!valid_scale() || !range(b, list, count, 8)) return fail("invalid static group table");
        for (uint32_t i = 0; i < count; ++i) {
            size_t row = list + size_t(i) * 8;
            uint32_t material = rd16(b, row), clusters = rd16(b, row + 2), cp = rd32(b, row + 4);
            if (material >= mats.size() || !range(b, cp, clusters, 16)) return fail("invalid static cluster table");
            for (uint32_t j = 0; j < clusters; ++j) {
                size_t c = cp + size_t(j) * 16;
                uint32_t chunks = rd16(b, c + 2), p = rd32(b, c + 4);
                if (!range(b, p, chunks, 24)) return fail("invalid static packet table");
                for (uint32_t k = 0; k < chunks; ++k) {
                    size_t packet = p + size_t(k) * 24;
                    uint32_t qwc = rd32(b, packet) >> 20, off = rd32(b, packet + 4);
                    if (!emit(material, off, size_t(qwc) * 16)) return fail("invalid static VIF packet");
                }
            }
        }
    } else if (g.type == 0x10000004) {
        uint32_t lods = rd32(b, r + 4);
        if (rd32(b, r) != 3 || !lods || !range(b, r + 8, lods, 0x24)) return fail("invalid rigid LOD table");
        if(lod>=lods)return fail("rigid LOD index out of range");
        r += 8 + size_t(lod)*0x24; // each LOD has its own material streams
        scale = rdf(b, r + 4) * (0.01f / 32768.0f);
        uint32_t count = rd32(b, r + 12), list = rd32(b, r + 16);
        if (!valid_scale() || !range(b, list, count, 12)) return fail("invalid rigid material streams");
        for (uint32_t i = 0; i < count; ++i) {
            size_t row = list + size_t(i) * 12;
            // Rigid streams carry a part selector in byte 3. Bit 4 selects
            // separately transformed break pieces (umbrella parts 0x11..0x1a).
            // The editor/native intact model must not overlay these fragments.
            if (b[row + 3] & 0x10) continue;
            if (!emit(b[row + 2], rd32(b, row + 8), rd32(b, row + 4))) return fail("invalid rigid VIF packet");
        }
    } else if (g.type == 0x10000003) {
        if (rd32(b, r) != 2 || !g.nmat) return fail("unsupported shrub render header");
        scale = rdf(b, r + 8) * (0.01f / 32768.0f);
        if (!valid_scale() || !emit(0, rd32(b, r + 0x28), rd32(b, r + 0x24))) return fail("invalid shrub VIF packet");
    } else return fail("unsupported GDE model type");
    for (auto& m : mats) if (!m.indices.empty()) model.materials.push_back(std::move(m));
    return true;
}

// BOG has separate 24-byte model definitions for rigid scenery and shrubs.
// Unlike gameplay class definitions these contain the exact GDE asset hash.
// Instance records have a fixed prefix followed by n eight-byte light entries.
bool place_static_models(std::span<const uint8_t> b, LevelData& level) {
    if (!range(b, 0, 0x80) || rd32(b, 0) != 0x20474f42) return false;
    size_t total = rd32(b, 8);
    if (total < 0x80 || total > b.size()) return false;
    b = b.first(total);
    // LEVEL_01.REL 11013c..110198 binds these original BOG fields to the
    // scene light environment subsequently used by actor lighting model 2.
    LevelLighting environment;
    const uint32_t point_off=rd32(b,0x14),point_count=rd32(b,0x18);
    const uint32_t dir_off=rd32(b,0x1c),dir_count=rd32(b,0x20);
    if(point_count>100000||dir_count>10000||!range(b,point_off,point_count,24)||
       !range(b,dir_off,dir_count,20))return false;
    std::copy_n(b.data()+0x10,4,environment.ambient.begin());
    for(uint32_t i=0;i<dir_count;++i) {
        const size_t at=dir_off+size_t(i)*20;LevelLighting::Directional light;
        for(int k=0;k<3;++k){light.direction[k]=rdf(b,at+k*4);if(!std::isfinite(light.direction[k]))return false;}
        std::copy_n(b.data()+at+16,4,light.rgba.begin());environment.directional.push_back(light);
    }
    for(uint32_t i=0;i<point_count;++i) {
        const size_t at=point_off+size_t(i)*24;LevelLighting::Point light;
        for(int k=0;k<3;++k){light.position[k]=rdf(b,at+k*4);if(!std::isfinite(light.position[k]))return false;}
        light.radius=rdf(b,at+20);if(!std::isfinite(light.radius)||light.radius<0)return false;
        std::copy_n(b.data()+at+16,4,light.rgba.begin());environment.points.push_back(light);
    }
    struct Placement { size_t model; std::array<float, 16> matrix; LevelModel::InstanceLighting lighting; };
    std::vector<Placement> placements;
    uint32_t unresolved = 0;
    for (bool shrub : {false, true}) {
        const size_t directory = shrub ? 0x64 : 0x54;
        uint32_t defs = rd32(b, directory), ndefs = rd32(b, directory + 4);
        uint32_t instances = rd32(b, directory + 8), ninstances = rd32(b, directory + 12);
        if (!range(b, defs, ndefs, 24)) return false;
        size_t cursor = instances;
        uint32_t walked = 0;
        for (uint32_t d = 0; d < ndefs; ++d) {
            size_t def = defs + size_t(d) * 24;
            uint32_t hash = rd32(b, def + 4), off = rd32(b, def + 16), count = rd32(b, def + 20);
            if (!count) continue;
            if (off != cursor || count > ninstances - walked) return false;
            size_t mi = 0;
            while (mi < level.models.size() && level.models[mi].hash != hash) ++mi;
            for (uint32_t j = 0; j < count; ++j) {
                const size_t prefix = shrub ? 84 : 100;
                if (!range(b, cursor, prefix)) return false;
                size_t length = prefix + size_t(b[cursor + (shrub ? 81 : 98)]) * 8;
                if (!range(b, cursor, length)) return false;
                std::array<float, 16> matrix;
                for (int k = 0; k < 16; ++k) {
                    matrix[k] = rdf(b, cursor + k * 4);
                    if (!std::isfinite(matrix[k])) return false;
                }
                if (matrix[3] != 0 || matrix[7] != 0 || matrix[11] != 0 || matrix[15] != 1) return false;
                if (mi == level.models.size()) ++unresolved;
                else {
                    // LEVEL_01.REL 833a4..833d4 / 83fd8..83ff0: both
                    // receivers pass these original fields to LIGHT model 1.
                    LevelModel::InstanceLighting lighting;
                    lighting.source_offset=uint32_t(cursor);
                    std::copy_n(b.data()+cursor+(shrub?68:92),4,lighting.ambient.begin());
                    for(size_t at=cursor+prefix;at<cursor+length;at+=8) {
                        LevelModel::NodeLight light;
                        for(int k=0;k<3;++k)light.direction[k]=int8_t(b[at+k]);
                        std::copy_n(b.data()+at+4,4,light.rgba.begin());
                        lighting.lights.push_back(light);
                    }
                    placements.push_back({mi,matrix,std::move(lighting)});
                }
                cursor += length;
                ++walked;
            }
        }
        if (walked != ninstances) return false;
    }
    // Publish only after both tables validate; malformed transforms cannot
    // leave a half-populated level. Rows already match the renderer's p * M.
    for (auto& p : placements) {
        auto& model=level.models[p.model];
        model.instances.push_back(p.matrix);
        model.instance_lighting.push_back(std::move(p.lighting));
    }
    level.static_instances = uint32_t(placements.size());
    level.unresolved_static_instances = unresolved;
    environment.valid=true;level.lighting=std::move(environment);
    return true;
}

}  // namespace

uint32_t gde_type(std::span<const uint8_t> gde) {
    GdeHeader g{};
    return gde_header(gde, g) ? g.type : 0;
}
bool decode_rigid_model(std::span<const uint8_t> gde, LevelModel& out, std::string* err, unsigned lod) {
    GdeHeader g{};
    std::string why;
    LevelModel model;
    if (!gde_header(gde, g))
        why = "invalid GDE tables";
    else if (g.type != 0x10000000 && g.type != 0x10000003 && g.type != 0x10000004)
        why = "not a static, rigid or shrub GDE";
    else if (decode_model(gde, g, model, why) && model.materials.empty())
        why = "no drawable material streams";
    if (!why.empty()) {
        if (err) *err = why;
        return false;
    }
    model.terrain = g.type == 0x10000000;
    model.kind = g.type == 0x10000003 ? "shrub" : "prop";
    (void)lod;
    out = std::move(model);
    if (err) err->clear();
    return true;
}

// Standard (reflected, poly 0xEDB88320) CRC32 — the engine's asset
// "signature" is CRC32 of the UPPERCASED dev path from its root component
// (e.g. "LEVELS\LEVELMP02\DATA\TEXTURES\DIRT01.GIM"). Also used to hash
// object class names for BOG resolution.
uint32_t crc32_upper(const char* s, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        uint8_t ch = uint8_t(s[i]);
        if (ch >= 'a' && ch <= 'z') ch = uint8_t(ch - 32);
        crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Resolve BOG model-def hashes to object class names by harvesting ASCII
// identifier tokens from the WAD image and CRC-matching. Turns opaque
// hashes into readable classes (HomingBeacon, JumpPad, HoverMine, ...).
void resolve_bog_names(std::span<const uint8_t> image,
                       std::vector<BogModelDef>& defs) {
    std::unordered_map<uint32_t, std::string> byhash;
    for (auto& d : defs) byhash.emplace(d.hash, std::string());
    auto consider = [&](const char* p, size_t n) {
        if (n < 3 || n > 48) return;
        uint32_t h = crc32_upper(p, n);
        auto it = byhash.find(h);
        if (it != byhash.end() &&
            (it->second.empty() || n < it->second.size()))
            it->second.assign(p, n);
    };
    size_t i = 0, N = image.size();
    while (i < N) {
        uint8_t c = image[i];
        bool idc = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '_';
        if (!idc) { i++; continue; }
        size_t j = i;
        while (j < N) {
            uint8_t d2 = image[j];
            if (!((d2 >= 'A' && d2 <= 'Z') || (d2 >= 'a' && d2 <= 'z') ||
                  (d2 >= '0' && d2 <= '9') || d2 == '_'))
                break;
            j++;
        }
        const char* tok = reinterpret_cast<const char*>(image.data() + i);
        size_t len = j - i;
        consider(tok, len);
        // digit-stripped base + underscore/dash fragments
        size_t base = len;
        while (base && tok[base - 1] >= '0' && tok[base - 1] <= '9') base--;
        if (base != len) consider(tok, base);
        size_t fs = 0;
        for (size_t k = 0; k <= len; k++)
            if (k == len || tok[k] == '_') {
                if (k - fs >= 3) consider(tok + fs, k - fs);
                fs = k + 1;
            }
        i = j;
    }
    for (auto& d : defs) {
        auto it = byhash.find(d.hash);
        if (it != byhash.end()) d.name = it->second;
    }
}

// ---- BOG scene graph ----------------------------------------------------------
//
// Section magic "BOG ". Header: {magic, u32 0x3C, u32 total, u32 crc, ...}.
// A directory of {u32 offset, u32 count} pairs begins at +0x1C (terminated
// by the 0x80 region). One category is the instance-transform table (128-byte
// records) and another the model-def table (68-byte records). Instances of a
// model-def are contiguous; the model-def's +0x0C points at its first
// instance and +0x30 holds the count.
//   instance record (128 bytes):
//     +0x00  float[16]  4x4 world matrix (row-major; row3 = translation)
//     +0x40  u32        model-def record offset (BOG-relative)
//   model-def record (68 bytes):
//     +0x0C  u32        first-instance offset (BOG-relative)
//     +0x24  u32        asset signature hash
//     +0x30  u32        instance count
//     +0x3C  u32        flags
bool parse_bog(std::span<const uint8_t> bog, std::vector<BogInstance>& insts,
               std::vector<BogModelDef>& defs, std::string* err) {
    insts.clear();
    defs.clear();
    if (bog.size() < 0x80 || std::memcmp(bog.data(), "BOG ", 4)) {
        if (err) *err = "not a BOG section";
        return false;
    }
    // BOG v0x3c has separate gameplay and static-instance directories. The
    // previous heuristic could select the later shrub table as gameplay.
    if(rd32(bog,4)!=0x3c || rd32(bog,8)>bog.size() || rd32(bog,8)<0x80) {
        if(err)*err="Unsupported or truncated BOG";return false;
    }
    bog=bog.first(rd32(bog,8));
    uint32_t def_off=rd32(bog,0x2c),def_cnt=rd32(bog,0x30);
    uint32_t inst_off=rd32(bog,0x34),inst_cnt=rd32(bog,0x38);
    if(!range(bog,def_off,def_cnt,68)||!range(bog,inst_off,inst_cnt,128)||def_cnt>4096||inst_cnt>100000) {
        if(err)*err="BOG gameplay directories exceed section";return false;
    }
    for(uint32_t d=0;d<def_cnt;d++) {
        size_t row=def_off+size_t(d)*68;
        uint32_t first=rd32(bog,row+12),count=rd32(bog,row+48);
        if(!count)continue;
        if(first<inst_off||(first-inst_off)%128||uint64_t(first)+uint64_t(count)*128>uint64_t(inst_off)+uint64_t(inst_cnt)*128) {
            if(err)*err="BOG gameplay instance run exceeds table";return false;
        }
        for(uint32_t n=0;n<count;n++) {
            size_t at=first+size_t(n)*128;
            if(rd32(bog,at+64)!=row) {
                if(err)*err="BOG gameplay instance back-reference mismatch";return false;
            }
            for(int k=0;k<16;k++)if(!std::isfinite(rdf(bog,at+4*k))) {
                if(err)*err="BOG gameplay transform is not finite";return false;
            }
        }
    }

    insts.resize(inst_cnt);
    for (uint32_t i = 0; i < inst_cnt; i++) {
        size_t b = inst_off + size_t(i) * 128;
        for (int k = 0; k < 16; k++)
            insts[i].matrix[k] = rdf(bog, b + k * 4);
        insts[i].model_def = 0;   // filled below from model-def runs
        insts[i].source_offset = uint32_t(b);
        insts[i].flags = rd32(bog, b + 0x44);
    }
    defs.resize(def_cnt);
    for (uint32_t di = 0; di < def_cnt; di++) {
        size_t b = def_off + size_t(di) * 68;
        uint32_t fi = rd32(bog, b + 0x0C);
        BogModelDef& d = defs[di];
        d.hash = rd32(bog, b + 0x24);
        d.count = rd32(bog, b + 0x30);
        d.flags = rd32(bog, b + 0x3C);
        d.first_instance = fi >= inst_off
                               ? (fi - inst_off) / 128
                               : 0;
        bool placed = false;
        for (uint32_t k = 0; k < d.count &&
                             d.first_instance + k < inst_cnt; k++) {
            BogInstance& in = insts[d.first_instance + k];
            in.model_def = di;
            size_t variants=rd32(bog,b+4), variant=variants+size_t(in.flags&255)*52;
            if(variants && range(bog,variant,52)) {
                in.model_hash=rd32(bog,variant+4);
                in.animation_hash=rd32(bog,variant+8);
                in.collision_hash=rd32(bog,variant+20);
                in.variant_hash=rd32(bog,variant+32);
            }
            size_t bytes = rd32(bog,b+0x2c);
            uint32_t prop = rd32(bog, in.source_offset + 0x54);
            if (bytes && prop) {
                if (bytes>65536 || !range(bog, prop, bytes)) { if(err)*err="BOG properties exceed section";return false; }
                in.properties.assign(bog.begin()+prop,bog.begin()+prop+bytes);
            }
            const float* m = in.matrix;
            if (std::fabs(m[12]) > 0.5f || std::fabs(m[13]) > 0.5f ||
                std::fabs(m[14]) > 0.5f)
                placed = true;
        }
        d.placed = placed;
    }
    return true;
}

std::string level_name(const HigWad& hdr) {
    // dev conf path: "...\\LEVELS\\levelMP02\\levelMP02.mb.wad.conf"
    const std::string& p = hdr.source_path;
    size_t e = p.find_last_not_of(" \t");
    if (e == std::string::npos) return "level";
    size_t s = p.find_last_of("\\/", e);
    std::string base = p.substr(s == std::string::npos ? 0 : s + 1,
                                e - (s == std::string::npos ? 0 : s + 1) + 1);
    size_t dot = base.find('.');
    if (dot != std::string::npos) base.resize(dot);
    // strip any non-identifier residue
    std::string out;
    for (char c : base)
        if (isalnum(uint8_t(c)) || c == '_' || c == '-') out.push_back(c);
    return out.empty() ? "level" : out;
}

bool build_level(std::span<const uint8_t> image, const HigWad& hdr,
                 LevelData& out, std::string* err) {
    out = {};
    out.name = level_name(hdr);
    std::unordered_map<uint32_t,std::pair<size_t,size_t>> model_ranges;

    // ---- GIM inventory: every "MIG.00.1PSP" blob in the image ------------
    struct GimRange { size_t start, end; };
    std::vector<GimRange> gims;
    static const uint8_t kMig[11] = {'M', 'I', 'G', '.', '0', '0',
                                     '.', '1', 'P', 'S', 'P'};
    for (size_t o = 0; o + 11 <= image.size(); o++) {
        if (!std::memcmp(image.data() + o, kMig, 11)) {
            if (!gims.empty()) gims.back().end = o;
            gims.push_back({o, image.size()});
            o += 10;
        }
    }
    // Each texture asset is a 0xC0 header {hash, ..., dev path} followed by
    // the GIM data — so a hash hit binds to the GIM that STARTS within 0xC0
    // after it (the header precedes its picture).
    std::unordered_map<uint32_t, int> gim_by_hash;
    for (size_t gi = 0; gi < gims.size(); ++gi)
        if (gims[gi].start >= 0xc0)
            gim_by_hash.emplace(rd32(image, gims[gi].start - 0xc0), int(gi));
    auto hash_to_gim = [&](uint32_t hash) -> int {
        auto it = gim_by_hash.find(hash);
        return it == gim_by_hash.end() ? -1 : it->second;
    };
    // decode lazily: gim index -> LevelData texture index
    std::vector<int> gim_tex(gims.size(), -2);
    auto texture_for = [&](uint32_t hash) -> int {
        int gi = hash_to_gim(hash);
        if (gi < 0) return -1;
        if (gim_tex[gi] != -2) return gim_tex[gi];
        LevelTexture t;
        t.hash = hash;
        std::span<const uint8_t> r = image.subspan(
            gims[gi].start, gims[gi].end - gims[gi].start);
        std::string derr;
        if (!decode_gim(r, t.image, &derr)) {
            gim_tex[gi] = -1;
            return -1;
        }
        // dev path in the 0xC0 asset header preceding the picture
        size_t hs = gims[gi].start >= 0xC0 ? gims[gi].start - 0xC0 : 0;
        for (size_t o = hs; o + 4 < gims[gi].start; o++) {
            if (!std::memcmp(image.data() + o, ".gim", 4)) {
                size_t s = o;
                while (s > hs && image[s - 1] != '\\' &&
                       image[s - 1] != '/' && image[s - 1] >= 32 &&
                       image[s - 1] < 127)
                    s--;
                t.name.assign(image.begin() + s, image.begin() + o + 4);
                break;
            }
        }
        gim_tex[gi] = int(out.textures.size());
        out.textures.push_back(std::move(t));
        return gim_tex[gi];
    };

    // Asset metadata is 0xc0 bytes immediately before each GDE. Its size
    // bounds every table and VIF read, including later assets in a section.
    std::vector<uint32_t> ends;
    for (uint32_t s : hdr.sections)
        if (s && s < image.size()) ends.push_back(s);
    ends.push_back(uint32_t(image.size()));
    std::sort(ends.begin(), ends.end());
    for (int slot = 0; slot < kHigSectionSlots; ++slot) {
        size_t start = hdr.sections[slot];
        if (!start || start >= image.size()) continue;
        auto it = std::upper_bound(ends.begin(), ends.end(), start);
        size_t end = it != ends.end() ? *it : image.size();
        for (size_t off = start; range(image, off, 0x3c) && off < end; off += 4) {
            if (off >= 0xc0 && !std::memcmp(image.data() + off, "COL ", 4)) {
                std::string path = ascii_name(image, off - 0xbc, 96);
                size_t size = rd32(image, off - 0x58);
                if (path.size() >= 4 && path.ends_with(".col") &&
                    path.find('\\') != std::string::npos && size >= 8 &&
                    range(image, off, size) && size <= end - off) {
                    out.collision_assets.push_back({path, rd32(image, off - 0xc0),
                        rd32(image, off + 4), uint32_t(off), uint32_t(size)});
                    off += (size + 3) / 4 * 4 - 4;
                    continue;
                }
            }
            if (off < 0xc0 || rd32(image, off) != 0x10001) continue;
            uint32_t type = rd32(image, off + 4);
            if ((type & 0xfffffff0) != 0x10000000) continue;
            std::string path = ascii_name(image, off - 0xbc, 96);
            if (path.find("\\") == std::string::npos || path.find(".gde") == std::string::npos) continue;
            size_t size = rd32(image, off - 0xc0 + 0x68);
            if (!range(image, off, size) || size > end - off) {
                out.warnings.push_back(path + ": invalid asset length");
                continue;
            }
            auto asset = image.subspan(off, size);
            GdeHeader g{};
            if (!gde_header(asset, g)) {
                out.warnings.push_back(path + ": invalid GDE tables");
                continue;
            }
            if (g.type != 0x10000000 && g.type != 0x10000003 && g.type != 0x10000004) {
                ++out.unsupported_models;
                off += (size + 3) / 4 * 4 - 4;
                continue;
            }
            LevelModel model;
            model.source_path = path;
            model.hash = rd32(image, off - 0xc0);
            model.name = path.substr(path.find_last_of("\\/") + 1);
            size_t suffix = model.name.find(".mb.");
            if (suffix != std::string::npos) model.name.resize(suffix);
            model.section = slot;
            model.terrain = g.type == 0x10000000;
            model.kind = model.terrain ? (slot == 1 ? "shell" : "terrain") :
                         g.type == 0x10000003 ? "shrub" : "prop";
            std::string why;
            if (!decode_model(asset, g, model, why)) {
                out.warnings.push_back(path + ": " + why);
            } else if (!model.materials.empty()) {
                for (auto& m : model.materials) m.texture = texture_for(m.tex_hash);
                model_ranges[model.hash]={off,size};
                out.models.push_back(std::move(model));
            }
            off += (size + 3) / 4 * 4 - 4;
        }
    }
    if (out.models.empty()) {
        if (err) *err = "no renderable GDE models found";
        return false;
    }

    // BOG scene graph: real object placements (transforms reversed; the
    // instance->mesh link needs the global object registry, so these are
    // exposed for inspection rather than auto-applied to props).
    for (int slot = 0; slot < kHigSectionSlots; slot++) {
        uint32_t s = hdr.sections[slot];
        if (!s || s + 4 > image.size()) continue;
        if (std::memcmp(image.data() + s, "BOG ", 4)) continue;
        auto it = std::upper_bound(ends.begin(), ends.end(), s);
        uint32_t e = it != ends.end() ? *it : uint32_t(image.size());
        std::string bog_error;
        auto bog=image.subspan(s,e-s);
        if(!parse_bog(bog, out.bog_instances, out.bog_defs, &bog_error))
            out.warnings.push_back(bog_error);
        resolve_bog_names(image, out.bog_defs);
        std::string trigger_error;
        if(!parse_bog_triggers(bog,out.triggers,trigger_error))out.warnings.push_back(trigger_error);
        if(!parse_bog_curves(bog,out.curves,trigger_error))out.warnings.push_back(trigger_error);
        if (!place_static_models(image.subspan(s, e - s), out))
            out.warnings.push_back("BOG: invalid rigid/shrub placement tables");
        // Recovered Pokitaru controllers. Resolve the water mesh through its
        // actual BOG variant descriptor, not a class-name/filename guess.
        if(out.name=="pokitaru" && bog_error.empty()) {
            bog=bog.first(rd32(bog,8));
            auto &env=out.environment;
            for(size_t di=0;di<out.bog_defs.size();di++) {
                const auto &def=out.bog_defs[di];
                // ShakeyBush is a gameplay-controlled rigid model, absent from
                // the ordinary prop table. Its BOG variant directly names the GDE.
                if(def.hash==0xafb59a6f) {
                    size_t d=rd32(bog,0x2c)+di*68, variants=rd32(bog,d+4);
                    for(uint32_t ii=0;ii<def.count;ii++) {
                        const auto &instance=out.bog_instances[def.first_instance+ii];
                        size_t variant=variants+size_t(instance.flags&255)*52;
                        if(!variants||!range(bog,variant,52))continue;
                        uint32_t hash=rd32(bog,variant+4);
                        for(auto &model:out.models)if(model.hash==hash) {
                            std::array<float,16> matrix;
                            std::copy_n(instance.matrix,16,matrix.begin());
                            model.instances.push_back(matrix);out.static_instances++;break;
                        }
                    }
                }
                if(def.hash==0x88a4e775 && def.count) {
                    env.sky_follow_xz=true;
                    unsigned sky=0;
                    for(size_t mi=0;mi<out.models.size();mi++)if(out.models[mi].kind=="shell") {
                        if(sky++==1)env.rotating_sky=int(mi);
                    }
                    // 0x18bdc4: IEEE754 0x3a11a373 radians per 30 Hz update.
                    env.sky_rotation_speed=.0005555666866712272f*30.f;
                }
                if(def.hash!=0x49742992 || !def.count)continue;
                size_t d=rd32(bog,0x2c)+di*68;
                size_t inst=rd32(bog,d+12),variants=rd32(bog,d+4);
                if(!range(bog,inst,128)||!variants)continue;
                size_t variant=variants+size_t(bog[inst+0x44])*52;
                size_t props=rd32(bog,inst+0x54);
                if(!range(bog,variant,52)||!props||rd32(bog,d+0x2c)<8||!range(bog,props,8)) {
                    out.warnings.push_back("WaterWaves: invalid BOG model/properties");continue;
                }
                uint32_t hash=rd32(bog,variant+4);
                for(size_t mi=0;mi<out.models.size();mi++)if(out.models[mi].hash==hash) {
                    float height=rdf(bog,inst+0x34),amplitude=rdf(bog,props),speed=rdf(bog,props+4);
                    if(!std::isfinite(height)||!std::isfinite(amplitude)||!std::isfinite(speed)||amplitude<0||amplitude>100||std::abs(speed)>100) {
                        out.warnings.push_back("WaterWaves: invalid wave parameters");break;
                    }
                    auto extent=model_ranges.at(hash);
                    auto asset=image.subspan(extent.first,extent.second);
                    GdeHeader g{};LevelModel distant;std::string why;
                    if(!gde_header(asset,g)||!decode_model(asset,g,distant,why,1)) {
                        out.warnings.push_back("WaterWaves distant LOD: "+why);break;
                    }
                    auto &model=out.models[mi];
                    model.distant_materials=std::move(distant.materials);
                    for(auto &m:model.distant_materials)m.texture=texture_for(m.tex_hash);
                    env.ocean_model=int(mi);env.ocean_height=height;env.wave_scale=amplitude;env.wave_speed=speed;
                    model.kind="ocean";
                    // The editor gets a static, correctly placed preview. The
                    // native host follows the player and animates these tiles.
                    Vec3 anchor{};
                    for(const auto &player:out.bog_defs)if(player.hash==0x36919224&&player.count)
                        std::copy_n(out.bog_instances[player.first_instance].matrix+12,3,anchor.begin());
                    for(const auto &tile:ocean_tiles(env,anchor,0))model.instances.push_back(tile.transform);
                    break;
                }
            }
        }
        break;
    }
    return true;
}

}  // namespace hig
