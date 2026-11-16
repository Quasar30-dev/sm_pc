#include "higdemaster.h"
#include "meshoptimizer.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_map>

namespace hig {
namespace {
using Bytes = std::vector<uint8_t>;
using V = std::array<float, 3>;
bool fail(std::string *e, const std::string &s) {
    if (e)
        *e = s;
    return false;
}
uint16_t u16(std::span<const uint8_t> b, size_t o) {
    uint16_t v;
    std::memcpy(&v, b.data() + o, 2);
    return v;
}
uint32_t u32(std::span<const uint8_t> b, size_t o) {
    uint32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
float flt(std::span<const uint8_t> b, size_t o) {
    float v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
template <class T> void put(Bytes &b, size_t o, const T &v) { std::memcpy(b.data() + o, &v, sizeof v); }
bool range(std::span<const uint8_t> b, size_t o, size_t n) { return o <= b.size() && n <= b.size() - o; }
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float len(V a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
struct Batch {
    size_t pos = 0, uv = 0, color = 0;
    uint32_t count = 0, stride = 8;
};
struct Packet {
    size_t record = 0;
    std::vector<Batch> batches;
};
struct Page {
    size_t gim = 0, size = 0;
    uint32_t width = 0, height = 0;
    std::vector<Packet> packets;
    size_t capacity = 0;
};
// Retain the donor's exact VIF commands, double buffering and DMA structure.
// Only replace the vertex attributes already allocated by those commands.
bool batches(std::span<const uint8_t> b, size_t start, size_t size, Packet &packet) {
    if (!range(b, start, size))
        return false;
    size_t o = start, end = start + size;
    Batch bt;
    uint32_t cl = 1, wl = 1;
    while (o + 4 <= end) {
        uint16_t imm = u16(b, o);
        uint32_t n = b[o + 2] ? b[o + 2] : 256;
        uint8_t cmd = b[o + 3] & 127;
        o += 4;
        size_t bytes = 0;
        if (cmd >= 0x60) {
            uint32_t format = cmd & 15;
            if ((cmd & 16) || wl > cl)
                return false;
            uint32_t consumed = wl <= cl ? n : cl * (n / wl) + std::min(n % wl, cl);
            bytes = ((cmd & 3) == 3 ? size_t(consumed) * 2
                                    : size_t(consumed) * (1 + ((cmd >> 2) & 3)) * (32 >> (cmd & 3)) / 8);
            bytes = (bytes + 3) & ~size_t(3);
            if (!range(b, o, bytes) || o + bytes > end)
                return false;
            if (format == 12 && (imm & 1023) == 0 && n == 1)
                bt = {};
            else if (format == 5)
                bt.uv = o;
            else if (format == 14 && (imm & 0x4000))
                bt.color = o;
            else if (format == 13 || format == 9) {
                bt.pos = o;
                bt.count = n;
                bt.stride = format == 13 ? 8 : 6;
                if (!bt.uv || !bt.color)
                    return false;
                packet.batches.push_back(bt);
                bt = {};
            }
        } else
            switch (cmd) {
            case 1:
                cl = imm & 255;
                wl = imm >> 8;
                if (!wl)
                    wl = 256;
                break;
            case 0x20:
                bytes = 4;
                break;
            case 0x30:
            case 0x31:
                bytes = 16;
                break;
            case 0x4a:
                bytes = n * 8;
                break;
            case 0x50:
            case 0x51:
                bytes = size_t(imm) * 16;
                break;
            case 0:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 0x10:
            case 0x11:
            case 0x13:
            case 0x14:
            case 0x15:
            case 0x17:
                break;
            default:
                return false;
            }
        if (o + bytes > end)
            return false;
        o += bytes;
    }
    return o == end && !packet.batches.empty();
}
struct ImageBlock {
    size_t pixels = 0, end = 0;
    uint32_t fmt = 0, order = 0, w = 0, h = 0;
};
bool image_blocks(std::span<const uint8_t> b, ImageBlock &img, ImageBlock &pal) {
    if (b.size() < 0x30 || std::memcmp(b.data(), "MIG.00.1PSP", 11))
        return false;
    struct R {
        size_t o, end;
    };
    std::vector<R> stack{{0x10, b.size()}};
    while (!stack.empty()) {
        auto r = stack.back();
        stack.pop_back();
        for (size_t o = r.o; o + 16 <= r.end;) {
            uint32_t id = u16(b, o), size = u32(b, o + 4), off = u32(b, o + 12);
            if (size < 16 || !range(b, o, size) || o + size > r.end || off > size)
                return false;
            if ((id == 2 || id == 3) && off < 16)
                return false;
            if (id == 2 || id == 3)
                stack.push_back({o + off, o + size});
            if (id == 4 || id == 5) {
                if (!range(b, o + 16, 16))
                    return false;
                ImageBlock x;
                x.fmt = u16(b, o + 20);
                x.order = u16(b, o + 22);
                x.w = u16(b, o + 24);
                x.h = u16(b, o + 26);
                x.pixels = o + 16 + u16(b, o + 16) + 16;
                x.end = o + size;
                if ((id == 4 && img.w) || (id == 5 && pal.w))
                    return false;
                if (id == 4)
                    img = x;
                else
                    pal = x;
            }
            o += size;
        }
    }
    // This writer replaces every texel in a single-level allocation. Reject
    // mip/frame chains instead of leaving old retail pixels in later levels.
    return img.w && img.h && img.w <= 1024 && img.h <= 1024 && img.fmt >= 4 && img.fmt <= 5 &&
           img.pixels + size_t(img.w) * img.h / (img.fmt == 4 ? 2 : 1) == img.end;
}
// Weighted median-cut palette: quantisation is deterministic and includes
// every atlas colour, rather than a fixed RGB cube that wastes palette slots.
std::vector<std::array<uint8_t, 4>> palette(const Bytes &rgba, size_t max_colors) {
    struct C {
        uint32_t key, count;
    };
    std::map<uint32_t, uint32_t> hist;
    for (size_t p = 0; p < rgba.size(); p += 4)
        ++hist[(uint32_t(rgba[p]) << 16) | (uint32_t(rgba[p + 1]) << 8) | rgba[p + 2]];
    std::vector<C> colors;
    for (auto [k, n] : hist)
        colors.push_back({k, n});
    struct Box {
        size_t a, b;
    };
    std::vector<Box> boxes{{0, colors.size()}};
    auto component = [](uint32_t k, int c) { return int((k >> (16 - c * 8)) & 255); };
    while (boxes.size() < max_colors) {
        size_t best = boxes.size();
        int axis = 0;
        uint64_t score = 0;
        for (size_t i = 0; i < boxes.size(); i++) {
            auto q = boxes[i];
            if (q.b - q.a < 2)
                continue;
            uint64_t weight = 0;
            int lo[3] = {255, 255, 255}, hi[3] = {};
            for (size_t j = q.a; j < q.b; j++) {
                weight += colors[j].count;
                for (int c = 0; c < 3; c++) {
                    int v = component(colors[j].key, c);
                    lo[c] = std::min(lo[c], v);
                    hi[c] = std::max(hi[c], v);
                }
            }
            for (int c = 0; c < 3; c++)
                if (uint64_t(hi[c] - lo[c]) * weight > score) {
                    score = uint64_t(hi[c] - lo[c]) * weight;
                    best = i;
                    axis = c;
                }
        }
        if (best == boxes.size())
            break;
        auto q = boxes[best];
        std::stable_sort(colors.begin() + q.a, colors.begin() + q.b,
                         [&](C a, C b) { return component(a.key, axis) < component(b.key, axis); });
        uint64_t total = 0, n = 0;
        for (size_t j = q.a; j < q.b; j++)
            total += colors[j].count;
        size_t mid = q.a + 1;
        for (size_t j = q.a; j + 1 < q.b; j++) {
            n += colors[j].count;
            mid = j + 1;
            if (n * 2 >= total)
                break;
        }
        boxes[best] = {q.a, mid};
        boxes.push_back({mid, q.b});
    }
    std::vector<std::array<uint8_t, 4>> out;
    for (auto q : boxes) {
        uint64_t n = 0, sum[3] = {};
        for (size_t j = q.a; j < q.b; j++) {
            n += colors[j].count;
            for (int c = 0; c < 3; c++)
                sum[c] += uint64_t(component(colors[j].key, c)) * colors[j].count;
        }
        out.push_back({uint8_t(sum[0] / n), uint8_t(sum[1] / n), uint8_t(sum[2] / n), 128});
    }
    return out;
}
bool write_atlas(Bytes &image, const Page &p, const Bytes &rgba) {
    auto b = std::span<const uint8_t>(image).subspan(p.gim, p.size);
    ImageBlock im, pa;
    if (!image_blocks(b, im, pa) || im.fmt < 4 || im.fmt > 5 || pa.fmt > 3 || !pa.w)
        return false;
    size_t entries = std::min<size_t>(im.fmt == 4 ? 16 : 256, size_t(pa.w) * pa.h);
    auto pal = palette(rgba, entries);
    Bytes pixels(size_t(im.w) * im.h / (im.fmt == 4 ? 2 : 1), 0);
    std::unordered_map<uint32_t, uint8_t> cache;
    for (size_t i = 0; i < rgba.size() / 4; i++) {
        uint32_t key =
            rgba[i * 4] | (uint32_t(rgba[i * 4 + 1]) << 8) | (uint32_t(rgba[i * 4 + 2]) << 16);
        auto it = cache.find(key);
        uint8_t ix = 0;
        if (it == cache.end()) {
            int best = INT_MAX;
            for (size_t j = 0; j < pal.size(); j++) {
                int d = 0;
                for (int k = 0; k < 3; k++) {
                    int v = int(rgba[i * 4 + k]) - pal[j][k];
                    d += v * v;
                }
                if (d < best) {
                    best = d;
                    ix = uint8_t(j);
                }
            }
            cache[key] = ix;
        } else
            ix = it->second;
        if (im.fmt == 4)
            pixels[i / 2] |= uint8_t(ix << ((i & 1) * 4));
        else
            pixels[i] = ix;
    }
    if (im.order == 1) {
        Bytes sw(pixels.size());
        uint32_t row = im.w / (im.fmt == 4 ? 2 : 1);
        if (row % 16 || im.h % 8)
            return false;
        size_t dst = 0;
        for (uint32_t y = 0; y < im.h; y += 8)
            for (uint32_t x = 0; x < row; x += 16)
                for (uint32_t j = 0; j < 8; j++) {
                    std::memcpy(sw.data() + dst, pixels.data() + size_t(y + j) * row + x, 16);
                    dst += 16;
                }
        pixels = std::move(sw);
    }
    if (!range(b, im.pixels, pixels.size()) || !range(b, pa.pixels, entries * (pa.fmt == 3 ? 4 : 2)))
        return false;
    std::copy(pixels.begin(), pixels.end(), image.begin() + p.gim + im.pixels);
    for (size_t i = 0; i < entries; i++) {
        auto c = pal[std::min(i, pal.size() - 1)];
        size_t dst = p.gim + pa.pixels + i * (pa.fmt == 3 ? 4 : 2);
        if (pa.fmt == 3)
            std::memcpy(image.data() + dst, c.data(), 4);
        else {
            uint16_t v =
                pa.fmt == 0 ? uint16_t((c[0] >> 3) | ((c[1] >> 2) << 5) | ((c[2] >> 3) << 11))
                : pa.fmt == 1
                    ? uint16_t((c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10) | 0x8000)
                    : uint16_t((c[0] >> 4) | ((c[1] >> 4) << 4) | ((c[2] >> 4) << 8) | (8 << 12));
            put(image, dst, v);
        }
    }
    return true;
}
V texel(std::span<const DemasterImage> textures, int index, std::array<float, 2> uv) {
    if (index < 0 || size_t(index) >= textures.size())
        return {1, 1, 1};
    const auto &im = textures[index];
    if (!im.width || !im.height || im.rgba.size() != size_t(im.width) * im.height * 4)
        return {1, 1, 1};
    float x = (uv[0] - std::floor(uv[0])) * im.width - .5f,
          y = (uv[1] - std::floor(uv[1])) * im.height - .5f;
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - ix, fy = y - iy;
    V c{};
    for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
            int px = (ix + dx + int(im.width)) % int(im.width),
                py = (iy + dy + int(im.height)) % int(im.height);
            float weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            for (int k = 0; k < 3; k++) {
                float value = im.rgba[(size_t(py) * im.width + px) * 4 + k] / 255.f;
                if (im.srgb)
                    value = value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
                c[k] += value * weight;
            }
        }
    return c;
}
V hsv_adjust(V c, V p) {
    float v = std::max({c[0], c[1], c[2]}), d = v - std::min({c[0], c[1], c[2]}), h = 0;
    if (d > 0)
        h = v == c[0]   ? (c[1] - c[2]) / (6 * d)
            : v == c[1] ? (c[2] - c[0]) / (6 * d) + 1.f / 3
                        : (c[0] - c[1]) / (6 * d) + 2.f / 3;
    float s = std::clamp((v > 0 ? d / v : 0) * 2 * p[1], 0.f, 1.f);
    v = std::clamp(v * 2 * p[2], 0.f, 1.f);
    h = std::abs(h) + p[0] - .5f;
    h -= std::floor(h);
    V q{std::clamp(std::abs(h * 6 - 3) - 1, 0.f, 1.f), std::clamp(2 - std::abs(h * 6 - 2), 0.f, 1.f),
        std::clamp(2 - std::abs(h * 6 - 4), 0.f, 1.f)};
    for (float &x : q)
        x = std::clamp(v * ((x - 1) * s + 1), 0.f, 1.f);
    return q;
}
V sample(const DemasterTriangle &t, std::span<const DemasterImage> textures,
         std::span<const DemasterMaterial> materials, float a, float b) {
    float weights[3] = {1 - a - b, a, b};
    V color{};
    std::array<float, 2> uv{}, mask_uv{};
    for (int v = 0; v < 3; v++) {
        for (int k = 0; k < 3; k++)
            color[k] += weights[v] * t.vertices[v].color[k];
        for (int k = 0; k < 2; k++)
            uv[k] += weights[v] * t.vertices[v].uv[k];
        for (int k = 0; k < 2; k++)
            mask_uv[k] += weights[v] * t.vertices[v].mask_uv[k];
    }
    V albedo = texel(textures, t.texture, uv);
    if (t.material >= 0 && size_t(t.material) < materials.size()) {
        const auto &m = materials[t.material];
        auto tiled = [&](const DemasterLayer &l) {
            return std::array<float, 2>{uv[0] * l.tile[0], uv[1] * l.tile[1]};
        };
        auto &base = m.layers[0];
        albedo = hsv_adjust(texel(textures, base.texture, tiled(base)), base.hsv);
        float height = std::sqrt(texel(textures, base.height, tiled(base))[0]);
        V id = texel(textures, m.mask, {mask_uv[0] * m.mask_tile[0], mask_uv[1] * m.mask_tile[1]});
        for (int layer = 1; layer < 4; layer++) {
            const auto &l = m.layers[layer];
            if (l.texture < 0)
                continue;
            float x = l.invert ? 1 - id[layer - 1] : id[layer - 1],
                  cut = std::clamp(height - l.threshold, 0.f, 1.f);
            float w = std::clamp((l.contrast * x - cut) / std::max(height - cut, 1e-4f), 0.f, 1.f);
            w = w * w * (3 - 2 * w);
            V next = hsv_adjust(texel(textures, l.texture, tiled(l)), l.hsv);
            for (int k = 0; k < 3; k++)
                albedo[k] += (next[k] - albedo[k]) * w;
            height += (std::sqrt(texel(textures, l.height, tiled(l))[0]) - height) * w;
        }
    }
    for (int k = 0; k < 3; k++)
        color[k] *= albedo[k];
    for (float &c : color)
        c = std::clamp(c, 0.f, 1.f);
    return color;
}
std::vector<DemasterTriangle> simplify(std::span<const DemasterTriangle> source, size_t budget) {
    if (source.size() <= budget)
        return {source.begin(), source.end()};
    struct Group {
        std::vector<DemasterVertex> corners;
        double area = 0;
        size_t budget = 0;
    };
    std::map<std::pair<int, int>, Group> groups;
    double total_area = 0;
    for (const auto &t : source) {
        auto &g = groups[{t.texture, t.material}];
        g.corners.insert(g.corners.end(), t.vertices.begin(), t.vertices.end());
        double area = len(cross(sub(t.vertices[1].position, t.vertices[0].position),
                                sub(t.vertices[2].position, t.vertices[0].position))) *
                      .5;
        g.area += area;
        total_area += area;
    }
    if (total_area <= 0 || groups.size() > budget)
        return {};
    size_t allocated = 0;
    for (auto &[texture, g] : groups) {
        g.budget =
            std::min(g.corners.size() / 3, 1 + size_t((budget - groups.size()) * g.area / total_area));
        allocated += g.budget;
    }
    // Redistribute capacity that small material groups cannot use.
    while (allocated < budget) {
        bool changed = false;
        for (auto &[texture, g] : groups)
            if (g.budget < g.corners.size() / 3 && allocated < budget) {
                g.budget++;
                allocated++;
                changed = true;
            }
        if (!changed)
            break;
    }
    std::vector<DemasterTriangle> output;
    for (auto &[texture, g] : groups) {
        std::vector<uint32_t> remap(g.corners.size());
        size_t count =
            meshopt_generateVertexRemap(remap.data(), nullptr, g.corners.size(), g.corners.data(),
                                        g.corners.size(), sizeof(DemasterVertex));
        std::vector<DemasterVertex> vertices(count);
        for (size_t i = 0; i < g.corners.size(); i++)
            vertices[remap[i]] = g.corners[i];
        std::vector<uint32_t> reduced(remap.size());
        float weights[7]{.01f, .01f, .05f, .05f, .05f, .01f, .01f};
        auto collapse = [&](std::vector<uint32_t> &dst, float error, unsigned flags) {
            return meshopt_simplifyWithAttributes(
                dst.data(), remap.data(), remap.size(), vertices[0].position.data(), vertices.size(),
                sizeof(DemasterVertex), vertices[0].uv.data(), sizeof(DemasterVertex), weights, 7,
                nullptr, g.budget * 3, error, flags, nullptr);
        };
        size_t n = collapse(reduced, 1.f, meshopt_SimplifyPermissive);
        if (n > g.budget * 3) {
            // Retail assets contain thousands of disconnected bolts and trim
            // pieces. Prune only at the smallest error that meets the budget;
            // an unrestricted threshold can delete an entire hull or floor.
            n = collapse(reduced, 1.f, meshopt_SimplifyPermissive | meshopt_SimplifyPrune);
            std::vector<uint32_t> candidate(remap.size());
            float lo = 0, hi = 1;
            for (int pass = 0; pass < 12; pass++) {
                float error = (lo + hi) * .5f;
                size_t trial = collapse(candidate, error,
                                        meshopt_SimplifyPermissive | meshopt_SimplifyPrune);
                if (trial <= g.budget * 3) {
                    hi = error;
                    n = trial;
                    reduced.swap(candidate);
                } else
                    lo = error;
            }
        }
        for (size_t i = 0; i < n; i += 3) {
            DemasterTriangle t;
            t.texture = texture.first;
            t.material = texture.second;
            for (int v = 0; v < 3; v++)
                t.vertices[v] = vertices[reduced[i + v]];
            output.push_back(t);
        }
    }
    return output;
}

std::vector<DemasterTriangle> quantise(std::span<const DemasterTriangle> src, float cell) {
    std::vector<DemasterTriangle> out;
    out.reserve(src.size());
    std::set<std::array<int64_t, 9>> seen;
    for (auto t : src) {
        std::array<int64_t, 9> key{};
        for (int v = 0; v < 3; v++)
            for (int k = 0; k < 3; k++) {
                int64_t q = llround(t.vertices[v].position[k] / cell);
                key[v * 3 + k] = q;
                t.vertices[v].position[k] = float(q) * cell;
            }
        if (len(cross(sub(t.vertices[1].position, t.vertices[0].position),
                      sub(t.vertices[2].position, t.vertices[0].position))) < 1e-7f)
            continue;
        if (seen.insert(key).second)
            out.push_back(t);
    }
    return out;
}
} // namespace

bool tjzip_compress(std::span<const uint8_t> src, Bytes &out, std::string *error) {
    out.clear();
    if (src.size() < 2)
        return fail(error, "TJZIP requires at least two bytes");
    struct Match {
        size_t pos, len, dist;
    };
    std::vector<Match> matches;
    std::vector<int> head(65536, -1), prev(src.size(), -1);
    auto hash = [&](size_t p) { return (u32(src, p) * 2654435761u) >> 16; };
    auto insert = [&](size_t p) {
        if (p + 4 <= src.size()) {
            auto h = hash(p);
            prev[p] = head[h];
            head[h] = int(p);
        }
    };
    insert(0);
    insert(1);
    for (size_t p = 2; p + 4 <= src.size();) {
        size_t best = 0, dist = 0;
        int q = head[hash(p)], tries = 0;
        while (q >= 0 && p - size_t(q) <= 32767 && tries++ < 32) {
            if (u32(src, size_t(q)) == u32(src, p)) {
                size_t n = 4;
                while (n < 65535 && p + n < src.size() && src[size_t(q) + n] == src[p + n])
                    n++;
                if (n > best) {
                    best = n;
                    dist = p - size_t(q);
                }
            }
            q = prev[size_t(q)];
        }
        if (best >= 4) {
            matches.push_back({p, best, dist});
            for (size_t i = 0; i < best; i++)
                insert(p + i);
            p += best;
        } else {
            insert(p);
            p++;
        }
    }
    auto raw = [&](size_t p, size_t n) {
        if (n < 2 || n > 65535)
            return false;
        if (n >= 3 && n <= 257)
            out.push_back(uint8_t(n - 2));
        else {
            out.push_back(0);
            out.push_back(uint8_t(n));
            out.push_back(uint8_t(n >> 8));
        }
        out.insert(out.end(), src.begin() + p, src.begin() + p + n);
        return true;
    };
    size_t first = matches.empty() ? src.size() : matches.front().pos;
    if (!raw(0, first))
        return fail(error, "TJZIP literal span exceeds the native 65535-byte block limit");
    for (size_t i = 0; i < matches.size(); i++) {
        auto m = matches[i];
        size_t next = i + 1 < matches.size() ? matches[i + 1].pos : src.size();
        size_t literals = next - m.pos - m.len;
        uint8_t post = literals == 0 ? 3 : literals <= 2 ? uint8_t(literals) : 0;
        uint8_t d = uint8_t(((m.dist >> 8) & 63) << 2) | post;
        uint8_t top = 0xe0 | uint8_t((m.dist >> 14) << 4);
        if (m.len <= 18) {
            out.push_back(top | uint8_t(m.len - 3));
            out.push_back(d);
            out.push_back(uint8_t(m.dist));
        } else if (m.len <= 273) {
            out.push_back(top);
            out.push_back(uint8_t(m.len - 18));
            out.push_back(d);
            out.push_back(uint8_t(m.dist));
        } else {
            out.push_back(top);
            out.push_back(0);
            out.push_back(uint8_t(m.len >> 8));
            out.push_back(uint8_t(m.len));
            out.push_back(d);
            out.push_back(uint8_t(m.dist));
        }
        if (literals >= 3) {
            if (!raw(m.pos + m.len, literals))
                return fail(error, "TJZIP literal span exceeds block limit");
        } else
            out.insert(out.end(), src.begin() + m.pos + m.len, src.begin() + next);
    }
    Bytes check;
    size_t consumed = 0;
    if (!tjzip_decompress(out, src.size(), check, error, &consumed) || consumed != out.size() ||
        !std::equal(check.begin(), check.end(), src.begin(), src.end()))
        return fail(error, "TJZIP compression readback mismatch");
    return true;
}

bool demaster_wad(std::span<const uint8_t> host, std::span<const DemasterTriangle> source,
                  std::span<const DemasterImage> textures, const DemasterOptions &opt,
                  DemasterResult &out, std::string *error, std::span<const DemasterMaterial> materials) {
    out = {};
    if (source.empty() || source.size() > 500000 || opt.triangle_budget < 1 || opt.tile_size < 4 ||
        opt.tile_size > 32 || !std::isfinite(opt.grid_step) || opt.grid_step <= 0 ||
        !std::isfinite(opt.scale) || opt.scale <= 0)
        return fail(error, "Invalid PS2 conversion budget or empty source scene");
    for (float f : opt.offset)
        if (!std::isfinite(f))
            return fail(error, "Destination offset is not finite");
    for (const auto &t : source)
        for (const auto &v : t.vertices) {
            for (float f : v.position)
                if (!std::isfinite(f) || std::abs(f) > 1e6)
                    return fail(error, "Source position is invalid");
            for (float f : v.uv)
                if (!std::isfinite(f) || std::abs(f) > 1e6)
                    return fail(error, "Source UV is invalid");
            for (float f : v.mask_uv)
                if (!std::isfinite(f) || std::abs(f) > 1e6)
                    return fail(error, "Source mask UV is invalid");
            for (float f : v.color)
                if (!std::isfinite(f))
                    return fail(error, "Source colour is invalid");
        }
    for (const auto &m : materials) {
        for (float f : m.mask_tile)
            if (!std::isfinite(f))
                return fail(error, "Material mask tiling is invalid");
        for (const auto &l : m.layers) {
            for (float f : l.tile)
                if (!std::isfinite(f))
                    return fail(error, "Material layer tiling is invalid");
            for (float f : l.hsv)
                if (!std::isfinite(f))
                    return fail(error, "Material HSV adjustment is invalid");
            if (!std::isfinite(l.contrast) || !std::isfinite(l.threshold))
                return fail(error, "Material blend parameters are invalid");
        }
    }
    HigWad hdr;
    Bytes image;
    if (!unpack_hig_wad(host, hdr, image, error))
        return false;
    if (hdr.version != 6 || hdr.crc != wad_checksum(image))
        return fail(error, "Host WAD version or runtime checksum is invalid");
    LevelData host_level;
    if (!build_level(image, hdr, host_level, error) || !host_level.warnings.empty())
        return fail(error, "Host geometry did not pass bounded native validation");
    size_t base = hdr.sections[0];
    if (base < 192 || !range(image, base, 60) || u32(image, base) != 0x10001 ||
        u32(image, base + 4) != 0x10000000)
        return fail(error, "Host has no supported PS2 static terrain GDE");
    uint32_t size = u32(image, base - 88);
    if (!range(image, base, size))
        return fail(error, "Host GDE length is invalid");
    uint32_t nm = u32(image, base + 8), mo = u32(image, base + 12), nt = u32(image, base + 16),
             to = u32(image, base + 20), render = u32(image, base + 28);
    if (!range(image, base + mo, size_t(nm) * 248) || !range(image, base + to, size_t(nt) * 20) ||
        !range(image, base + render, 28) || u32(image, base + render) != 3)
        return fail(error, "Unsupported host GDE tables");
    size_t col_base = hdr.sections[2];
    if (col_base < 192 || !range(image, col_base, 88))
        return fail(error, "Host has no native terrain COL");
    size_t col_size = u32(image, col_base - 88);
    Collision original;
    if (!range(image, col_base, col_size) ||
        !decode_collision(std::span<const uint8_t>(image).subspan(col_base, col_size), original, error))
        return false;
    std::map<uint32_t, std::pair<size_t, size_t>> gims;
    for (size_t o = 192; o + 16 < image.size(); o += 4)
        if (!std::memcmp(image.data() + o, "MIG.00.1PSP", 11)) {
            size_t n = u32(image, o - 88);
            if (range(image, o, n))
                gims.emplace(u32(image, o - 192), std::pair{o, n});
        }
    std::vector<Page> pages(nt);
    for (uint32_t i = 0; i < nt; i++) {
        auto it = gims.find(u32(image, base + to + i * 20));
        if (it == gims.end())
            return fail(error, "A host terrain texture is external or missing");
        auto &p = pages[i];
        p.gim = it->second.first;
        p.size = it->second.second;
        ImageBlock im, pa;
        if (!image_blocks(std::span<const uint8_t>(image).subspan(p.gim, p.size), im, pa) ||
            im.fmt < 4 || im.fmt > 5)
            return fail(error, "Host texture is not a supported indexed PS2 GIM");
        p.width = im.w;
        p.height = im.h;
    }
    std::set<size_t> unique;
    for (auto &p : pages)
        if (!unique.insert(p.gim).second)
            return fail(error, "Host repeats a texture slot; choose another host level");
    size_t groups = base + u32(image, base + render + 20);
    uint32_t ng = u32(image, base + render + 16);
    if (!range(image, groups, size_t(ng) * 8))
        return fail(error, "Host group table is invalid");
    std::vector<size_t> material_texture(nm);
    std::array<uint8_t, 248> opaque{};
    bool found_opaque = false;
    for (uint32_t m = 0; m < nm; m++) {
        size_t p = base + mo + m * 248;
        if (u32(image, p + 108) == 1 && u16(image, p + 112) == 1) {
            uint16_t t = u16(image, p + 116);
            if (t >= nt)
                return fail(error, "Host material texture index is invalid");
            material_texture[m] = t;
            if (!found_opaque && image[p + 165] == 0) {
                std::copy_n(image.begin() + p, 248, opaque.begin());
                found_opaque = true;
            }
        } else
            return fail(error, "Host terrain has a multipass material; select a simpler host");
    }
    if (!found_opaque)
        return fail(error, "Host has no supported opaque material template");
    for (uint32_t g = 0; g < ng; g++) {
        size_t r = groups + g * 8;
        uint16_t mat = u16(image, r), nc = u16(image, r + 2);
        if (mat >= nm)
            return fail(error, "Invalid host material group");
        if (!nc)
            continue;
        size_t cp = base + u32(image, r + 4);
        if (!range(image, cp, size_t(nc) * 16))
            return fail(error, "Invalid host cluster table");
        for (uint32_t c = 0; c < nc; c++) {
            size_t cl = cp + c * 16;
            if (u16(image, cl + 8))
                return fail(error, "Host terrain uses secondary cluster geometry; choose another host");
            size_t rp = base + u32(image, cl + 4);
            uint16_t np = u16(image, cl + 2);
            if (!range(image, rp, size_t(np) * 24))
                return fail(error, "Invalid host packet table");
            for (uint32_t k = 0; k < np; k++) {
                Packet packet;
                packet.record = rp + k * 24;
                size_t po = base + u32(image, packet.record + 4),
                       ps = size_t(u32(image, packet.record) >> 20) * 16;
                if (!batches(image, po, ps, packet))
                    return fail(error, "Unsupported host static VIF packet");
                auto &p = pages[material_texture[mat]];
                for (auto bt : packet.batches)
                    if (bt.count >= 3)
                        p.capacity += (bt.count + 2) / 5;
                p.packets.push_back(std::move(packet));
            }
        }
    }
    size_t capacity = 0;
    for (auto &p : pages) {
        p.capacity = std::min(p.capacity, size_t(p.width / opt.tile_size) * (p.height / opt.tile_size));
        capacity += p.capacity;
    }
    if (!capacity)
        return fail(error, "Host has no writable textured triangle capacity");
    V lo = source[0].vertices[0].position, hi = lo;
    for (const auto &t : source)
        for (const auto &v : t.vertices)
            for (int k = 0; k < 3; k++) {
                lo[k] = std::min(lo[k], v.position[k]);
                hi[k] = std::max(hi[k], v.position[k]);
            }
    float scale = opt.scale;
    if (opt.fit_to_host) {
        float fit = 1e9f;
        for (int k = 0; k < 3; k++)
            if (hi[k] - lo[k] > .1f)
                fit = std::min(fit, .8f * (original.maximum[k] - original.minimum[k]) / (hi[k] - lo[k]));
        scale *= fit;
    }
    if (!std::isfinite(scale) || scale < 1e-6f || scale > 1e4f)
        return fail(error, "Scene fit scale is invalid");
    V translation;
    for (int k = 0; k < 3; k++)
        translation[k] = (original.minimum[k] + original.maximum[k]) * .5f -
                         (lo[k] + hi[k]) * .5f * scale + opt.offset[k];
    std::vector<DemasterTriangle> transformed(source.begin(), source.end());
    for (auto &t : transformed)
        for (auto &v : t.vertices)
            for (int k = 0; k < 3; k++)
                v.position[k] = v.position[k] * scale + translation[k];
    size_t budget = std::min<size_t>(opt.triangle_budget, capacity);
    float step = opt.grid_step;
    auto simplified = simplify(transformed, budget);
    auto reduced = quantise(simplified, step);
    for (int pass = 0; reduced.size() > budget && pass < 48; pass++) {
        step *= 1.22f;
        reduced = quantise(simplified, step);
    }
    if (reduced.empty() || reduced.size() > budget)
        return fail(error, "Quantisation could not meet the host budget; select a smaller source area");
    // Quantise once to the actual signed-16 position grid, then use those same
    // positions for COL, atlas bake, preview and in-game geometry.
    float max_abs = 1;
    for (const auto &t : reduced)
        for (const auto &v : t.vertices)
            for (float x : v.position)
                max_abs = std::max(max_abs, std::abs(x));
    float unit = max_abs / 32760.f;
    put(image, base + render + 4, unit * 100.f);
    put(image, base + render + 8, 64.f);
    for (auto &t : reduced)
        for (auto &v : t.vertices)
            for (float &x : v.position)
                x = std::round(x / unit) * unit;
    std::erase_if(reduced, [](const auto &t) {
        return len(cross(sub(t.vertices[1].position, t.vertices[0].position),
                         sub(t.vertices[2].position, t.vertices[0].position))) < 1e-7f;
    });
    if (reduced.empty())
        return fail(error, "All triangles collapsed on the native position grid");
    V mn = reduced[0].vertices[0].position, mx = mn;
    for (const auto &t : reduced)
        for (const auto &v : t.vertices)
            for (int k = 0; k < 3; k++) {
                mn[k] = std::min(mn[k], v.position[k]);
                mx[k] = std::max(mx[k], v.position[k]);
            }
    // Preserve native visibility-tree pointers, expanding every donor node to
    // contain the replacement. It is conservative until spatial rebuilding.
    size_t tree = u32(image, base + render + 24);
    if (tree < size && (size - tree) % 44 == 0) {
        for (size_t p = base + tree; p + 44 <= base + size; p += 44) {
            for (int k = 0; k < 3; k++) {
                put(image, p + k * 4, mn[k]);
                put(image, p + 12 + k * 4, mx[k]);
            }
        }
    } else
        return fail(error, "Unsupported host visibility tree layout");
    put(image, base + 56,
        len({std::max(std::abs(mn[0]), std::abs(mx[0])), std::max(std::abs(mn[1]), std::abs(mx[1])),
             std::max(std::abs(mn[2]), std::abs(mx[2]))}));
    for (size_t m = 0; m < nm; m++) {
        size_t p = base + mo + m * 248;
        std::copy(opaque.begin(), opaque.end(), image.begin() + p);
        put(image, p + 116, uint16_t(material_texture[m]));
    }
    size_t next = 0;
    for (auto &p : pages) {
        Bytes atlas(size_t(p.width) * p.height * 4, 128);
        for (size_t a = 3; a < atlas.size(); a += 4)
            atlas[a] = 255;
        size_t tile = 0;
        for (auto &packet : p.packets) {
            std::vector<V> packet_positions;
            for (const auto &bt : packet.batches) {
                std::vector<DemasterVertex> strip;
                while (next < reduced.size() && tile < p.capacity &&
                       strip.size() + (strip.empty() ? 3 : 5) <= bt.count) {
                    auto t = reduced[next++];
                    uint32_t tx = uint32_t(tile % (p.width / opt.tile_size)) * opt.tile_size,
                             ty = uint32_t(tile / (p.width / opt.tile_size)) * opt.tile_size;
                    tile++;
                    for (uint32_t y = 0; y < opt.tile_size; y++)
                        for (uint32_t x = 0; x < opt.tile_size; x++) {
                            float a = std::clamp((float(x) - 1) / (opt.tile_size - 3), 0.f, 1.f),
                                  b = std::clamp((float(y) - 1) / (opt.tile_size - 3), 0.f, 1.f);
                            if (a + b > 1) {
                                float sum = a + b;
                                a /= sum;
                                b /= sum;
                            }
                            auto color = sample(t, textures, materials, a, b);
                            for (int k = 0; k < 3; k++) {
                                // Native palette bytes are display colours; retain dark
                                // lighting detail before palette reduction and GPU sRGB sampling.
                                float encoded = color[k] <= .0031308f
                                                    ? 12.92f * color[k]
                                                    : 1.055f * std::pow(color[k], 1.f / 2.4f) - .055f;
                                atlas[(size_t(ty + y) * p.width + tx + x) * 4 + k] =
                                    uint8_t(std::lround(std::clamp(encoded, 0.f, 1.f) * 255));
                            }
                        }
                    t.vertices[0].uv = {(tx + 1.5f) / p.width, (ty + 1.5f) / p.height};
                    t.vertices[1].uv = {(tx + opt.tile_size - 1.5f) / p.width, (ty + 1.5f) / p.height};
                    t.vertices[2].uv = {(tx + 1.5f) / p.width, (ty + opt.tile_size - 1.5f) / p.height};
                    if (strip.empty())
                        strip.insert(strip.end(), t.vertices.begin(), t.vertices.end());
                    else {
                        size_t end = strip.size() + 4;
                        auto last = strip.back();
                        strip.push_back(last);
                        strip.push_back(t.vertices[0]);
                        strip.push_back(t.vertices[0]);
                        strip.push_back(t.vertices[end & 1 ? 2 : 1]);
                        strip.push_back(t.vertices[end & 1 ? 1 : 2]);
                    }
                }
                if (strip.empty())
                    strip.push_back(reduced.front().vertices[0]);
                while (strip.size() < bt.count)
                    strip.push_back(strip.back());
                for (size_t v = 0; v < strip.size(); v++) {
                    packet_positions.push_back(strip[v].position);
                    for (int k = 0; k < 3; k++)
                        put(image, bt.pos + v * bt.stride + k * 2,
                            int16_t(std::lround(strip[v].position[k] / unit)));
                    if (bt.stride == 8)
                        put(image, bt.pos + v * 8 + 6, int16_t(32767));
                    for (int k = 0; k < 2; k++)
                        put(image, bt.uv + v * 4 + k * 2,
                            int16_t(std::lround(std::clamp(strip[v].uv[k], 0.f, 1.f) * 16384.f)));
                    for (int k = 0; k < 4; k++)
                        image[bt.color + v * 4 + k] = 128;
                }
            }
            V center{};
            for (auto v : packet_positions)
                for (int k = 0; k < 3; k++)
                    center[k] += v[k] / packet_positions.size();
            float radius = 0;
            for (auto v : packet_positions)
                radius = std::max(radius, len(sub(v, center)));
            for (int k = 0; k < 3; k++)
                put(image, packet.record + 8 + k * 4, center[k]);
            put(image, packet.record + 20, radius + .01f);
        }
        if (!write_atlas(image, p, atlas))
            return fail(error, "Could not encode the host GIM palette");
    }
    if (next != reduced.size())
        return fail(error, "Host packet capacity disagrees with atlas allocation");
    std::map<V, uint32_t> vertex_map;
    std::vector<V> positions;
    std::vector<uint32_t> indices;
    for (const auto &t : reduced)
        for (const auto &v : t.vertices) {
            auto [it, added] = vertex_map.emplace(v.position, uint32_t(vertex_map.size()));
            if (added)
                positions.push_back(v.position);
            indices.push_back(it->second);
        }
    std::map<uint32_t, size_t> surfaces;
    for (const auto &f : original.faces)
        surfaces[f.surface]++;
    uint32_t surface = std::max_element(surfaces.begin(), surfaces.end(), [](auto a, auto b) {
                           return a.second < b.second;
                       })->first;
    Collision collision;
    if (!cook_collision(positions, indices, surface, collision, error))
        return false;
    auto col = encode_collision(collision, error);
    if (col.empty() || col.size() > col_size)
        return fail(error, "Cooked collision exceeds the host COL allocation; reduce triangles or area");
    std::fill(image.begin() + col_base, image.begin() + col_base + col_size, 0);
    std::copy(col.begin(), col.end(), image.begin() + col_base);
    if (opt.remove_host_scenery) {
        size_t bog = hdr.sections[3];
        if (!range(image, bog, 128) || std::memcmp(image.data() + bog, "BOG ", 4))
            return fail(error, "Host BOG header is unsupported");
        for (size_t o : {0x58u, 0x60u, 0x68u, 0x70u})
            put(image, bog + o, uint32_t(0));
    }
    Bytes compressed;
    if (!tjzip_compress(image, compressed, error))
        return false;
    if (compressed.size() > host.size() - 192)
        return fail(
            error,
            "Compressed conversion exceeds the original ISO extent; lower triangle or texture detail");
    DemasterResult result;
    result.wad.assign(host.begin(), host.end());
    std::fill(result.wad.begin() + 192, result.wad.end(), 0);
    std::copy(compressed.begin(), compressed.end(), result.wad.begin() + 192);
    // The runtime terminates on COMPRESSED bytes consumed and validates its
    // zero-initialised CRC32. Padding belongs to the ISO extent, not the stream.
    put(result.wad, 0x34, wad_checksum(image));
    put(result.wad, 0x40, uint32_t(compressed.size()));
    HigWad check_hdr;
    Bytes check;
    if (!unpack_hig_wad(result.wad, check_hdr, check, error) || check != image ||
        !build_level(check, check_hdr, result.preview, error))
        return false;
    if (!result.preview.warnings.empty())
        return fail(error, "Converted GDE did not pass native readback");
    // Check every native face, including winding. Header/reader success alone
    // cannot detect accidental strip connectors or incorrect position scales.
    using FaceKey = std::array<int, 9>;
    auto key = [&](std::array<V, 3> face) {
        FaceKey best;
        best.fill(INT_MAX);
        for (int start = 0; start < 3; start++) {
            FaceKey k;
            for (int v = 0; v < 3; v++)
                for (int a = 0; a < 3; a++)
                    k[v * 3 + a] = int(std::lround(face[(v + start) % 3][a] / unit));
            best = std::min(best, k);
        }
        return best;
    };
    std::multiset<FaceKey> expected, actual;
    for (const auto &t : reduced)
        expected.insert(key({t.vertices[0].position, t.vertices[1].position, t.vertices[2].position}));
    for (const auto &m : result.preview.models)
        if (m.kind == "terrain")
            for (const auto &mat : m.materials)
                for (size_t i = 0; i < mat.indices.size(); i += 3) {
                    std::array<V, 3> f;
                    for (int v = 0; v < 3; v++)
                        for (int k = 0; k < 3; k++)
                            f[v][k] = mat.positions[mat.indices[i + v] * 3 + k];
                    actual.insert(key(f));
                }
    if (actual != expected || collision.faces.size() != reduced.size())
        return fail(error, "Native render/COL geometry or winding readback mismatch");
    result.collision = std::move(collision);
    result.source_triangles = uint32_t(source.size());
    result.triangles = uint32_t(reduced.size());
    result.capacity = uint32_t(capacity);
    result.textures = uint32_t(pages.size());
    result.geometry_bytes = size;
    result.collision_bytes = uint32_t(col.size());
    result.compressed_bytes = uint32_t(compressed.size());
    result.applied_scale = scale;
    result.applied_grid = step;
    result.translation = translation;
    result.notes = {
        "Static geometry and baked texture atlases; host gameplay remains in place.",
        "Native memory and disc allocations are unchanged. Visibility bounds are conservative.",
        "In-game validation is required; native parser readback is not an emulator test."};
    out = std::move(result);
    return true;
}

bool write_demaster_iso(const std::wstring &source, const std::string &path,
                        std::span<const uint8_t> wad, const std::wstring &output, std::string *error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(output, ec))
        return fail(error, "Output already exists; choose a new ISO filename");
    if (fs::weakly_canonical(source, ec) == fs::weakly_canonical(output, ec))
        return fail(error, "The source ISO cannot be overwritten");
    IsoFs iso;
    if (!iso.open(source, error))
        return false;
    auto *f = iso.find(path);
    if (!f || wad.size() != f->size)
        return fail(error, "Replacement WAD must exactly match the original disc extent");
    uint64_t offset = uint64_t(f->lba) * 2048;
    auto original = iso.read(*f);
    iso.close();
    HigWad a, b;
    Bytes data;
    if (!parse_hig_header(original, a, error) || !unpack_hig_wad(wad, b, data, error) ||
        a.decomp_size != b.decomp_size || b.crc != wad_checksum(data) ||
        !std::equal(std::begin(a.sections), std::end(a.sections), std::begin(b.sections)))
        return fail(error, "Replacement WAD does not match the host layout/checksum");
    if (!fs::copy_file(source, output, fs::copy_options::none, ec))
        return fail(error, "Cannot create ISO copy: " + ec.message());
    std::fstream out(fs::path(output), std::ios::binary | std::ios::in | std::ios::out);
    out.seekp(std::streamoff(offset));
    out.write(reinterpret_cast<const char *>(wad.data()), std::streamsize(wad.size()));
    out.flush();
    if (!out)
        return fail(error, "ISO copy write failed; the source ISO is unchanged");
    out.close();
    IsoFs check;
    if (!check.open(output, error))
        return false;
    auto readback = check.read(path);
    if (!std::equal(readback.begin(), readback.end(), wad.begin(), wad.end()))
        return fail(error, "ISO replacement readback mismatch");
    return true;
}
} // namespace hig
