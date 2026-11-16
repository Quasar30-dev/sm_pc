#include "higclass.h"
#include <cctype>
#include <cstring>

namespace hig {
namespace {
uint32_t rd32(std::span<const uint8_t> b, size_t p) {
    uint32_t v = 0;
    if (p + 4 <= b.size())
        std::memcpy(&v, b.data() + p, 4);
    return v;
}
bool identifier_start(uint8_t c) { return std::isalpha(c) || c == '_'; }
bool identifier_char(uint8_t c) { return std::isalnum(c) || c == '_'; }
// A NUL-terminated string of printable bytes at p, at most `limit` long; empty when there is none.
std::string_view text(std::span<const uint8_t> b, size_t p, size_t limit) {
    if (!p || p >= b.size())
        return {};
    size_t n = 0;
    while (p + n < b.size() && n <= limit && b[p + n])
        n++;
    if (!n || n > limit || p + n >= b.size())
        return {};
    return {reinterpret_cast<const char *>(b.data() + p), n};
}
struct TypeSize {
    const char *name;
    uint32_t size, align;
};
// Engine structs embedded in properties have their sizes solved from classes with one such field.
const TypeSize types[] = {
    {"u8", 1, 1},           {"s8", 1, 1},          {"Toggle", 1, 1},      {"char", 1, 1},
    {"bool", 1, 1},         {"u16", 2, 2},         {"s16", 2, 2},         {"u32", 4, 4},
    {"s32", 4, 4},          {"f32", 4, 4},         {"int", 4, 4},         {"float", 4, 4},
    {"TriggerLink", 4, 4},  {"MobyLink", 4, 4},    {"CurveLink", 4, 4},   {"TieLink", 4, 4},
    {"ShrubLink", 4, 4},    {"VEC2", 8, 4},        {"VEC3", 12, 4},       {"VEC4", 16, 4},
    {"MATRIX4", 64, 4},     {"QUAT", 16, 4},       {"Navigate", 44, 4},   {"NavBound", 52, 4},
    {"Launch", 28, 4},      {"ANIM_Manipulator", 68, 4},                  {"ChasePosEntry", 24, 4},
    {"BotControllerEntry", 28, 4},                 {"TargetEntry", 16, 4},
};
// `Type name; Type name[ n ]; Type *name; const Type *name; Type name[ a ][ b ];`
bool layout(std::string_view declaration, std::vector<ClassField> &fields, uint32_t &end) {
    bool exact = true;
    uint32_t at = 0;
    size_t p = 0;
    while (p < declaration.size()) {
        size_t stop = declaration.find(';', p);
        if (stop == std::string_view::npos)
            stop = declaration.size();
        std::string_view part = declaration.substr(p, stop - p);
        p = stop + 1;
        // tokens: identifiers, '*', '[n]'
        std::vector<std::string_view> words;
        bool pointer = false;
        uint32_t count = 1;
        bool bad = false;
        for (size_t i = 0; i < part.size();) {
            uint8_t c = uint8_t(part[i]);
            if (std::isspace(c)) {
                i++;
            } else if (c == '*') {
                pointer = true;
                i++;
            } else if (c == '[') {
                size_t close = part.find(']', i);
                if (close == std::string_view::npos) {
                    bad = true;
                    break;
                }
                uint32_t n = 0;
                bool digits = false;
                for (size_t k = i + 1; k < close; k++)
                    if (std::isdigit(uint8_t(part[k]))) {
                        n = n * 10 + uint32_t(part[k] - '0');
                        digits = true;
                    } else if (!std::isspace(uint8_t(part[k])))
                        bad = true;
                if (!digits || !n || n > 4096)
                    bad = true;
                count *= n ? n : 1;
                i = close + 1;
            } else if (identifier_start(c)) {
                size_t k = i;
                while (k < part.size() && (identifier_char(uint8_t(part[k])) || part[k] == ':'))
                    k++;
                words.push_back(part.substr(i, k - i));
                i = k;
            } else {
                bad = true;
                break;
            }
        }
        std::erase_if(words, [](std::string_view w) { return w == "const" || w == "struct"; });
        if (words.empty() && !bad)
            continue; // blank between semicolons
        if (bad || words.size() != 2) {
            exact = false;
            continue;
        }
        ClassField f;
        f.type = words[0];
        f.name = words[1];
        f.count = count;
        f.pointer = pointer;
        uint32_t align = 4;
        f.size = 4;
        if (!pointer) {
            bool known = false;
            for (const auto &t : types)
                if (f.type == t.name) {
                    f.size = t.size;
                    align = t.align;
                    known = true;
                    break;
                }
            if (!known)
                exact = false;
        }
        at = (at + align - 1) & ~(align - 1);
        f.offset = at;
        at += f.size * f.count;
        fields.push_back(std::move(f));
    }
    end = at;
    return exact;
}
} // namespace

uint32_t class_hash(std::string_view name) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (char ch : name)
        crc = table[(crc ^ uint8_t(std::toupper(uint8_t(ch)))) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
const ClassField *ClassRecord::field(std::string_view wanted) const {
    for (const auto &f : fields)
        if (f.name == wanted)
            return &f;
    return nullptr;
}
bool decode_class_records(std::span<const uint8_t> b, std::vector<ClassRecord> &out, std::string *error) {
    out.clear();
    std::vector<ClassRecord> found;
    const size_t n = b.size();
    for (size_t at = 0; at + 64 <= n; at += 4) {
        uint32_t w[16];
        std::memcpy(w, b.data() + at, sizeof w);
        if (w[0] < 0x100 || w[0] >= n || !w[1] || (w[1] & 3) || w[1] >= n)
            continue;
        if ((w[2] | w[3] | w[4]) & 3 || w[2] >= n || w[3] >= n || w[4] >= n)
            continue;
        if (w[6] > 0x4000 || w[7] > 0x10000)
            continue;
        auto name = text(b, w[0], 48);
        if (name.size() < 3 || !identifier_start(uint8_t(name[0])))
            continue;
        bool plain = true;
        for (char c : name)
            plain &= identifier_char(uint8_t(c));
        if (!plain)
            continue;
        std::string_view declaration;
        if (w[5]) {
            declaration = text(b, w[5], 8192);
            if (declaration.empty() || declaration.find(';') == std::string_view::npos || !w[6])
                continue;
        } else if (w[6] & 3)
            continue;
        ClassRecord r;
        r.offset = uint32_t(at);
        r.name = name;
        r.hash = class_hash(name);
        r.declaration = declaration;
        std::copy(w + 1, w + 5, r.callbacks.begin());
        r.props_size = w[6];
        r.instance_size = w[7];
        std::copy(w + 8, w + 16, r.extra.begin());
        found.push_back(std::move(r));
    }
    if (found.empty()) {
        if (error)
            *error = "No class records in this module";
        return false;
    }
    // Records sit in the module's data, after all code: drop look-alikes that precede the first callback.
    uint32_t first_code = UINT32_MAX;
    for (const auto &r : found)
        first_code = std::min(first_code, r.callbacks[0]);
    for (auto &r : found) {
        if (r.offset <= first_code || r.callbacks[0] >= r.offset)
            continue;
        if (!r.declaration.empty()) {
            uint32_t end = 0;
            bool exact = layout(r.declaration, r.fields, end);
            r.layout_exact = exact && end <= r.props_size && r.props_size - end < 16;
        }
        auto table = [&](uint32_t at, std::array<float, 11> &values) {
            if (at <= r.offset || at & 3 || size_t(at) + 44 > n)
                return false;
            std::memcpy(values.data(), b.data() + at, 44);
            for (float v : values)
                if (!(v >= 0 && v <= 1)) {
                    values = {};
                    return false;
                }
            return true;
        };
        r.rewards = table(r.extra[5], r.bolt_share);
        if (r.rewards)
            table(r.extra[6], r.xp_share);
        out.push_back(std::move(r));
    }
    if (error)
        error->clear();
    return !out.empty();
}

bool find_level_tuning(std::span<const uint8_t> b, std::array<LevelTuning, 10> &out) {
    // the first level's row, as on the USA disc
    static const float first[16] = {1, 3, 5, 0, 48, 192, 288, 0, 1, 1.75f, 2, 0, 32, 59.5f, 80, 0};
    for (size_t at = 0; at + 0x280 <= b.size(); at += 4) {
        if (std::memcmp(b.data() + at, first, sizeof first))
            continue;
        for (size_t level = 0; level < 10; level++) {
            float row[16];
            std::memcpy(row, b.data() + at + level * 0x40, sizeof row);
            for (int k = 0; k < 4; k++) {
                out[level].health[0][k] = row[k];
                out[level].health[1][k] = row[4 + k];
                out[level].attack[0][k] = row[8 + k];
                out[level].attack[1][k] = row[12 + k];
            }
        }
        return true;
    }
    return false;
}

uint32_t ClassProps::word(size_t offset) const { return rd32(bytes, offset); }
float ClassProps::real(size_t offset) const {
    uint32_t v = rd32(bytes, offset);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
uint32_t ClassProps::u32(std::string_view name, unsigned index) const {
    const ClassField *f = record ? record->field(name) : nullptr;
    if (!f || index >= f->count)
        return 0;
    size_t at = f->offset + size_t(index) * f->size;
    if (f->size == 1)
        return at < bytes.size() ? bytes[at] : 0;
    if (f->size == 2)
        return at + 2 <= bytes.size() ? uint32_t(bytes[at] | bytes[at + 1] << 8) : 0;
    return rd32(bytes, at);
}
float ClassProps::f32(std::string_view name, unsigned index) const {
    const ClassField *f = record ? record->field(name) : nullptr;
    if (!f || index >= f->count || f->size < 4)
        return 0;
    return real(f->offset + size_t(index) * f->size);
}
uint8_t ClassProps::u8(std::string_view name, unsigned index) const {
    const ClassField *f = record ? record->field(name) : nullptr;
    if (!f || index >= f->count)
        return 0;
    size_t at = f->offset + size_t(index) * f->size;
    return at < bytes.size() ? bytes[at] : 0;
}
std::array<float, 3> ClassProps::vec3(std::string_view name, unsigned index) const {
    const ClassField *f = record ? record->field(name) : nullptr;
    if (!f || index >= f->count || f->size < 12)
        return {};
    size_t at = f->offset + size_t(index) * f->size;
    return {real(at), real(at + 4), real(at + 8)};
}
} // namespace hig
