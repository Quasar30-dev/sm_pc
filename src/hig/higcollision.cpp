#include "higcollision.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace hig {
namespace {
uint32_t u32(std::span<const uint8_t> b, size_t p) {
    uint32_t v;
    std::memcpy(&v, b.data() + p, 4);
    return v;
}
template <class T> void append(std::vector<uint8_t> &b, const T &v) {
    const auto *p = reinterpret_cast<const uint8_t *>(&v);
    b.insert(b.end(), p, p + sizeof v);
}
bool fail(std::string *e, const char *s) {
    if (e)
        *e = s;
    return false;
}
using V = std::array<float, 3>;
V sub(V a, V b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float dot(V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
bool unit(V &a) {
    float l = std::sqrt(dot(a, a));
    if (!std::isfinite(l) || l < 1e-9f)
        return false;
    for (float &x : a)
        x /= l;
    return true;
}
bool valid(const Collision &c, std::string *error) {
    if (c.vertices.empty() || c.vertices.size() > 65535 || c.faces.empty() || c.faces.size() > 65535)
        return fail(error, "COL requires 1..65535 vertices and triangles");
    for (const auto &v : c.vertices)
        for (float x : v)
            if (!std::isfinite(x))
                return fail(error, "COL vertex is not finite");
    for (const auto &f : c.faces) {
        for (auto i : f.vertices)
            if (i >= c.vertices.size())
                return fail(error, "COL index outside vertex table");
        for (float x : f.planes)
            if (!std::isfinite(x))
                return fail(error, "COL plane is not finite");
    }
    for (int k = 0; k < 3; k++)
        if (!std::isfinite(c.minimum[k]) || !std::isfinite(c.maximum[k]) ||
            c.minimum[k] > c.maximum[k] || !std::isfinite(c.cell_size[k]) || c.cell_size[k] <= 0)
            return fail(error, "COL bounds or cell dimensions are invalid");
    for (float x : c.sphere)
        if (!std::isfinite(x))
            return fail(error, "COL sphere is not finite");
    uint64_t cells = uint64_t(c.dimensions[0]) * c.dimensions[1] * c.dimensions[2];
    if (cells != c.cell_counts.size() || cells > 1000000)
        return fail(error, "COL grid size mismatch");
    uint64_t refs = 0;
    for (auto n : c.cell_counts)
        refs += n;
    if (refs != c.cell_faces.size())
        return fail(error, "COL grid reference count mismatch");
    for (auto i : c.cell_faces)
        if (i >= c.faces.size())
            return fail(error, "COL grid index outside triangle table");
    return true;
}
} // namespace
bool decode_collision(std::span<const uint8_t> b, Collision &out, std::string *error) {
    out = {};
    if (b.size() < 88 || std::memcmp(b.data(), "COL ", 4) || u32(b, 4) != 8)
        return fail(error, "Expected native COL version 8");
    uint32_t nv = u32(b, 8), nf = u32(b, 12), grid_bytes = u32(b, 16), cells = u32(b, 20);
    uint64_t end = 88ull + uint64_t(nv) * 12 + uint64_t(nf) * 76 + grid_bytes;
    if (end > b.size() || grid_bytes < uint64_t(cells) * 2 || nv > 65535 || nf > 65535 ||
        cells > 1000000 || grid_bytes % 2)
        return fail(error, "COL tables exceed payload bounds");
    Collision c;
    std::memcpy(c.dimensions.data(), b.data() + 24, 12);
    std::memcpy(c.cell_size.data(), b.data() + 36, 12);
    std::memcpy(c.minimum.data(), b.data() + 48, 12);
    std::memcpy(c.maximum.data(), b.data() + 60, 12);
    std::memcpy(c.sphere.data(), b.data() + 72, 16);
    c.vertices.resize(nv);
    std::memcpy(c.vertices.data(), b.data() + 88, size_t(nv) * 12);
    size_t p = 88 + size_t(nv) * 12;
    c.faces.resize(nf);
    for (auto &f : c.faces) {
        std::memcpy(f.vertices.data(), b.data() + p, 6);
        std::memcpy(&f.flags, b.data() + p + 6, 2);
        std::memcpy(f.planes.data(), b.data() + p + 8, 64);
        f.surface = u32(b, p + 72);
        p += 76;
    }
    c.cell_counts.resize(cells);
    std::memcpy(c.cell_counts.data(), b.data() + p, size_t(cells) * 2);
    p += size_t(cells) * 2;
    c.cell_faces.resize((grid_bytes - size_t(cells) * 2) / 2);
    std::memcpy(c.cell_faces.data(), b.data() + p, c.cell_faces.size() * 2);
    c.trailing.assign(b.begin() + size_t(end), b.end());
    if (!valid(c, error))
        return false;
    out = std::move(c);
    return true;
}
std::vector<uint8_t> encode_collision(const Collision &c, std::string *error) {
    if (!valid(c, error))
        return {};
    std::vector<uint8_t> b;
    append(b, uint32_t(0x204c4f43));
    append(b, uint32_t(8));
    append(b, uint32_t(c.vertices.size()));
    append(b, uint32_t(c.faces.size()));
    append(b, uint32_t((c.cell_counts.size() + c.cell_faces.size()) * 2));
    append(b, uint32_t(c.cell_counts.size()));
    append(b, c.dimensions);
    append(b, c.cell_size);
    append(b, c.minimum);
    append(b, c.maximum);
    append(b, c.sphere);
    for (const auto &v : c.vertices)
        append(b, v);
    for (const auto &f : c.faces) {
        append(b, f.vertices);
        append(b, f.flags);
        append(b, f.planes);
        append(b, f.surface);
    }
    for (auto n : c.cell_counts)
        append(b, n);
    for (auto n : c.cell_faces)
        append(b, n);
    b.insert(b.end(), c.trailing.begin(), c.trailing.end());
    return b;
}
bool cook_collision(std::span<const V> vertices, std::span<const uint32_t> indices, uint32_t surface,
                    Collision &out, std::string *error) {
    out = {};
    if (vertices.empty() || vertices.size() > 65535 || indices.empty() || indices.size() % 3 ||
        indices.size() / 3 > 65535)
        return fail(error, "Collision mesh exceeds COL limits or has no triangles");
    Collision c;
    c.vertices.assign(vertices.begin(), vertices.end());
    c.minimum = c.maximum = vertices.front();
    for (const auto &v : vertices)
        for (int k = 0; k < 3; k++) {
            if (!std::isfinite(v[k]))
                return fail(error, "Collision vertex is not finite");
            c.minimum[k] = std::min(c.minimum[k], v[k]);
            c.maximum[k] = std::max(c.maximum[k], v[k]);
        }
    for (int k = 0; k < 3; k++) {
        c.minimum[k] -= .01f;
        c.maximum[k] += .01f;
        c.sphere[k] = (c.minimum[k] + c.maximum[k]) * .5f;
    }
    for (const auto &v : vertices) {
        V d = sub(v, {c.sphere[0], c.sphere[1], c.sphere[2]});
        c.sphere[3] = std::max(c.sphere[3], std::sqrt(dot(d, d)));
    }
    for (size_t i = 0; i < indices.size(); i += 3) {
        CollisionFace f;
        f.surface = surface;
        for (int k = 0; k < 3; k++) {
            if (indices[i + k] >= vertices.size())
                return fail(error, "Collision index out of bounds");
            f.vertices[k] = uint16_t(indices[i + k]);
        }
        V a = vertices[f.vertices[0]], b = vertices[f.vertices[1]], d = vertices[f.vertices[2]];
        V n = cross(sub(b, a), sub(d, a));
        if (!unit(n))
            continue;
        for (int p = 0; p < 4; p++) {
            V normal = n, point = a;
            if (p) {
                point = vertices[f.vertices[p - 1]];
                normal = cross(sub(vertices[f.vertices[p % 3]], point), n);
                if (!unit(normal))
                    return fail(error, "Degenerate COL edge");
            }
            for (int k = 0; k < 3; k++)
                f.planes[p * 4 + k] = normal[k];
            f.planes[p * 4 + 3] = -dot(normal, point);
        }
        c.faces.push_back(f);
    }
    uint64_t cells = 1;
    for (int k = 0; k < 3; k++) {
        float extent = c.maximum[k] - c.minimum[k];
        if (!std::isfinite(extent) || extent > 10000)
            return fail(error, "Collision extent exceeds grid limits");
        c.dimensions[k] = uint32_t(std::ceil(extent / c.cell_size[k]));
        cells *= c.dimensions[k];
    }
    if (cells > 1000000)
        return fail(error, "Collision area is too large for the COL grid");
    std::vector<std::vector<uint16_t>> grid(size_t(cells), std::vector<uint16_t>{});
    // Conservative triangle AABBs preserve contacts. Runtime narrow-phase
    // uses the face/edge planes; extra cell candidates only cost traversal.
    uint64_t refs = 0;
    for (size_t i = 0; i < c.faces.size(); i++) {
        V lo = vertices[c.faces[i].vertices[0]], hi = lo;
        for (auto ix : c.faces[i].vertices)
            for (int k = 0; k < 3; k++) {
                lo[k] = std::min(lo[k], vertices[ix][k]);
                hi[k] = std::max(hi[k], vertices[ix][k]);
            }
        uint32_t a[3], b[3];
        for (int k = 0; k < 3; k++) {
            a[k] = uint32_t(std::max(0.f, std::floor((lo[k] - c.minimum[k]) / c.cell_size[k])));
            b[k] = std::min(c.dimensions[k] - 1,
                            uint32_t(std::floor((hi[k] - c.minimum[k]) / c.cell_size[k])));
        }
        for (uint32_t z = a[2]; z <= b[2]; z++)
            for (uint32_t y = a[1]; y <= b[1]; y++)
                for (uint32_t x = a[0]; x <= b[0]; x++) {
                    auto &cell = grid[(size_t(z) * c.dimensions[1] + y) * c.dimensions[0] + x];
                    if (cell.size() >= 65535 || ++refs > 16000000)
                        return fail(error, "Collision grid exceeds reference budget");
                    cell.push_back(uint16_t(i));
                }
    }
    for (const auto &cell : grid) {
        c.cell_counts.push_back(uint16_t(cell.size()));
        c.cell_faces.insert(c.cell_faces.end(), cell.begin(), cell.end());
    }
    if (!valid(c, error))
        return false;
    out = std::move(c);
    return true;
}
} // namespace hig
