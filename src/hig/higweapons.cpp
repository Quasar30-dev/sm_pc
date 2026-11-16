#include "higweapons.h"
#include <cmath>
#include <cstring>

namespace hig {
namespace {
uint32_t rd32(std::span<const uint8_t> b, size_t p) {
    uint32_t v = 0;
    if (p + 4 <= b.size())
        std::memcpy(&v, b.data() + p, 4);
    return v;
}
size_t find(std::span<const uint8_t> b, const void *pattern, size_t n, size_t from = 0) {
    for (size_t at = from; at + n <= b.size(); at++)
        if (!std::memcmp(b.data() + at, pattern, n))
            return at;
    return SIZE_MAX;
}
} // namespace
bool decode_weapon_tables(std::span<const uint8_t> b, WeaponTables &out, std::string *error) {
    auto fail = [&](const char *why) {
        if (error)
            *error = why;
        return false;
    };
    WeaponTables tables;
    // The definition table starts with {pointer to "blaster", 2}.
    size_t table = SIZE_MAX;
    for (size_t name = find(b, "blaster", 8); name != SIZE_MAX && table == SIZE_MAX; name = find(b, "blaster", 8, name + 1)) {
        const uint32_t key[2] = {uint32_t(name), 2};
        table = find(b, key, sizeof key);
    }
    if (table == SIZE_MAX || table + WeaponCount * 0x58 > b.size())
        return fail("Weapon definition table not found");
    for (unsigned i = 0; i < WeaponCount; i++) {
        const size_t e = table + i * 0x58;
        WeaponDefinition d;
        const uint32_t name = rd32(b, e);
        if (!name || name >= b.size())
            return fail("Weapon definition name is out of range");
        for (size_t k = name; k < b.size() && b[k] && d.name.size() < 32; k++)
            d.name += char(b[k]);
        d.type = rd32(b, e + 4);
        d.first_level = rd32(b, e + 0x48);
        for (unsigned level = 0; level < 8; level++) {
            const uint32_t row = rd32(b, e + 0x10 + level * 4);
            if (!row)
                break;
            if (size_t(row) + 0x50 > b.size())
                return fail("Weapon tier row is out of range");
            WeaponTier t;
            t.class_hash = rd32(b, row);
            t.class2_hash = rd32(b, row + 4);
            t.name_text = rd32(b, row + 0x10);
            t.description_text = rd32(b, row + 0x14);
            t.icon = rd32(b, row + 0x18);
            t.price = rd32(b, row + 0x1c);
            t.xp_next = rd32(b, row + 0x20);
            t.ammo_price = rd32(b, row + 0x34);
            t.max_ammo = rd32(b, row + 0x38);
            t.ammo_pickup = rd32(b, row + 0x3c);
            d.tiers.push_back(t);
        }
        if (d.tiers.empty())
            return fail("Weapon definition has no tier");
        tables.weapons.push_back(std::move(d));
    }
    // The damage table starts {0, 0, 0, 1}, {0, 2.5, 5, 2}, {0, 5, 5, 2}: the first Lacerator rows.
    const float first[2] = {2.5f, 5.f};
    uint32_t signature[12] = {0, 0, 0, 1, 0, 0, 5, 2, 0, 0, 5, 2};
    std::memcpy(&signature[5], &first[0], 4);
    std::memcpy(&signature[9], &first[1], 4);
    const size_t damage = find(b, signature, sizeof signature);
    if (damage == SIZE_MAX)
        return fail("Weapon damage table not found");
    for (size_t at = damage; at + 16 <= b.size() && tables.damage.size() < 256; at += 16) {
        DamageRow r;
        r.kind = int32_t(rd32(b, at));
        const uint32_t bits = rd32(b, at + 4);
        std::memcpy(&r.amount, &bits, 4);
        r.effect = rd32(b, at + 8);
        r.strength = rd32(b, at + 12);
        if (r.kind < 0 || r.kind > 4 || !std::isfinite(r.amount) || r.amount < 0 || r.amount > 1e6f || r.effect > 64 ||
            r.strength > 16)
            break;
        tables.damage.push_back(r);
    }
    // the level experience table: {0, 0}, {6500, 25000}, {9000, 35500}, ...
    const uint32_t xp_signature[6] = {0, 0, 6500, 25000, 9000, 35500};
    if (const size_t xp = find(b, xp_signature, sizeof xp_signature); xp != SIZE_MAX)
        for (size_t level = 0; level < tables.level_xp.size() && xp + level * 8 + 8 <= b.size(); level++) {
            const uint32_t first = rd32(b, xp + level * 8), challenge = rd32(b, xp + level * 8 + 4);
            if (first > 10000000 || challenge > 10000000)
                break;
            tables.level_xp[level] = first;
            tables.level_xp_challenge[level] = challenge;
        }
    out = std::move(tables);
    if (error)
        error->clear();
    return true;
}
} // namespace hig
