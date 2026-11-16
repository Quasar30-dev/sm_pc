#include "higlevel.h"
#include <cmath>
#include <cstring>
namespace hig {
bool parse_bog_curves(std::span<const uint8_t> b, std::vector<BogCurve> &out, std::string &error) {
    auto u = [&](size_t p) {
        uint32_t v = 0;
        if (p + 4 <= b.size())
            std::memcpy(&v, b.data() + p, 4);
        return v;
    };
    if (b.size() < 0x54 || u(4) != 0x3c || u(8) > b.size() || u(8) < 0x54) {
        error = "Invalid BOG curve header";
        return false;
    }
    b = b.first(u(8));
    uint32_t start = u(0x4c), count = u(0x50);
    if (count > 30000 || start > b.size() || uint64_t(count) * 24 > b.size() - start) {
        error = "BOG curve directory exceeds section";
        return false;
    }
    std::vector<BogCurve> next;
    for (uint32_t i = 0; i < count; i++) {
        size_t p = start + i * 24;
        uint32_t points = u(p + 4), n = u(p + 20) & 65535, name = u(p);
        if (n > 16384 || points > b.size() || uint64_t(n) * 16 > b.size() - points ||
            name >= b.size()) {
            error = "BOG curve points/name exceed section";
            return false;
        }
        BogCurve c;
        c.source_offset = uint32_t(p);
        c.source_id = u(p + 12);
        c.flags = uint16_t(u(p + 20) >> 16);
        for (size_t j = name; j < b.size() && j < name + 128 && b[j]; j++)
            c.name += char(b[j]);
        for (uint32_t j = 0; j < n; j++) {
            std::array<float, 3> v;
            std::memcpy(v.data(), b.data() + points + j * 16, 12);
            for (float x : v)
                if (!std::isfinite(x)) {
                    error = "Non-finite BOG curve point";
                    return false;
                }
            c.points.push_back(v);
            uint32_t extra=u(points+j*16+12);
            if(extra && (extra>b.size() || b.size()-extra<4)) {
                error="BOG curve point data exceeds section";return false;
            }
            c.point_data_offsets.push_back(extra);
            c.point_data_words.push_back(extra?u(extra):0);
        }
        next.push_back(std::move(c));
    }
    out = std::move(next);
    error.clear();
    return true;
}
bool trigger_contains(const BogTrigger &t, std::array<float, 3> p) {
    std::array<float, 3> v{};
    for (int k = 0; k < 3; k++) {
        v[k] = p[0] * t.inverse[k] + p[1] * t.inverse[4 + k] + p[2] * t.inverse[8 + k] +
               t.inverse[12 + k];
        if (!std::isfinite(v[k]))
            return false;
    }
    // LEVEL_01.REL 0x84708: inverse transform, unit box/sphere/Y-cylinder.
    if (t.shape == 0)
        return std::abs(v[0]) <= 1 && std::abs(v[1]) <= 1 && std::abs(v[2]) <= 1;
    if (t.shape == 1)
        return v[0] * v[0] + v[1] * v[1] + v[2] * v[2] <= 1;
    return t.shape == 2 && std::abs(v[1]) <= 1 && v[0] * v[0] + v[2] * v[2] <= 1;
}
bool parse_bog_triggers(std::span<const uint8_t> b, std::vector<BogTrigger> &out,
                        std::string &error) {
    auto u = [&](size_t p) {
        uint32_t n = 0;
        if (p + 4 <= b.size())
            std::memcpy(&n, b.data() + p, 4);
        return n;
    };
    auto f = [&](size_t p) {
        uint32_t n = u(p);
        float v;
        std::memcpy(&v, &n, 4);
        return v;
    };
    if (b.size() < 0x44 || u(4) != 0x3c) {
        error = "Unsupported BOG trigger directory";
        return false;
    }
    uint32_t start = u(0x3c), count = u(0x40);
    if (count > 30000 || start > b.size() || uint64_t(count) * 140 > b.size() - start) {
        error = "BOG triggers exceed section";
        return false;
    }
    std::vector<BogTrigger> next;
    for (uint32_t i = 0; i < count; i++) {
        size_t p = start + i * 140;
        BogTrigger t;
        t.source_offset = uint32_t(p);
        t.source_id = u(p);
        t.shape = b[p + 136];
        for (int k = 0; k < 16; k++) {
            t.matrix[k] = f(p + 4 + k * 4);
            t.inverse[k] = f(p + 68 + k * 4);
            if (!std::isfinite(t.matrix[k]) || !std::isfinite(t.inverse[k])) {
                error = "Non-finite BOG trigger";
                return false;
            }
        }
        if (t.shape > 2) {
            error = "Unknown BOG trigger shape";
            return false;
        }
        next.push_back(t);
    }
    out = std::move(next);
    error.clear();
    return true;
}
} // namespace hig
