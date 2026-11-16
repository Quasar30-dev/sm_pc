"""Size Matters: the weapon / gadget definition table of a level module, decoded. Research aid.

The table (LEVEL_02.REL 0x208308, found by its first entry's name "blaster") has 23 entries of 0x58 bytes:

  +0x00 name pointer      +0x04 type (2 weapon, 1 gadget, 3 passive item)      +0x08, +0x0c two floats
  +0x10 eight pointers to 0x50-byte tier rows (the weapon's levels; unused slots 0)
  +0x30 three pointers to modification records (0x20 bytes)
  +0x3c current level (run time)      +0x44 experience (run time)      +0x48 the level index it first appears at
  +0x4c three "modification owned" bytes, +0x4f three "modification on" bytes (run time)

Accessors in the module: entry 0x24f88, tier 0x25030 / 0x24fd0, experience 0x250f8 (the award is scaled by how far
the current level is past the weapon's first level, capped at a third of the next level's cost), level up 0x24e20.

A tier row: +0x00 the weapon's class hash, +0x04 a second class hash (the same, or its HUD model's), +0x08 float,
+0x0c flag, +0x10 / +0x14 text ids, +0x18 HUD icon id, +0x1c price, +0x20 experience to the next level (0 = none),
+0x34 ammo unit price, +0x38 ammo capacity, +0x3c ammo per pickup; the rest is printed raw until its readers are found.

    python tools/sm/sm_weapon_table.py build/sm_pc_work/LEVEL_02.REL [classes_02.json] [OUT.json]
"""
import json
import struct
import sys
import zlib
from pathlib import Path


def cstring(d, p):
    e = d.find(b'\0', p, p + 48)
    return d[p:e].decode('latin-1') if 0 < p < len(d) and e > p else ''


def find_table(d):
    at = d.find(b'blaster\0')
    while at >= 0:
        pointer = struct.pack('<II', at, 2)
        table = d.find(pointer)
        if table >= 0:
            return table
        at = d.find(b'blaster\0', at + 1)
    raise SystemExit('weapon table not found')


def decode(d, names=None):
    names = names or {}
    table = find_table(d)
    out = []
    for i in range(23):
        e = table + i * 0x58
        w = struct.unpack_from('<22I', d, e)
        f = struct.unpack_from('<22f', d, e)
        entry = dict(index=i, offset=e, name=cstring(d, w[0]), type=w[1], f8=round(f[2], 4), fc=round(f[3], 4),
                     first_level=w[18], tiers=[], mods=[])
        for k in range(8):
            p = w[4 + k]
            if not p:
                continue
            r = struct.unpack_from('<20I', d, p)
            rf = struct.unpack_from('<20f', d, p)
            entry['tiers'].append(dict(
                level=k, offset=p, class_hash=f'{r[0]:08x}', class_name=names.get(r[0], ''),
                class2_hash=f'{r[1]:08x}', class2_name=names.get(r[1], ''), f08=round(rf[2], 4), flag0c=r[3],
                text10=r[4], text14=r[5], icon=r[6], price=r[7], xp_next=r[8], w24=r[9],
                w28=r[10], w2c=r[11], w30=r[12], ammo_price=r[13], max_ammo=r[14], ammo_pickup=r[15],
                f40=round(rf[16], 4), w44=f'{r[17]:08x}', w48=f'{r[18]:08x}', w4c=f'{r[19]:08x}'))
        for k in range(3):
            p = w[12 + k]
            if p:
                entry['mods'].append(dict(offset=p, words=[f'{x:x}' for x in struct.unpack_from('<8I', d, p)]))
        out.append(entry)
    return table, out


def main():
    d = Path(sys.argv[1]).read_bytes()
    names = {}
    if len(sys.argv) > 2:
        for r in json.loads(Path(sys.argv[2]).read_text())['records']:
            names[r['hash']] = r['name']
    table, entries = decode(d, names)
    if len(sys.argv) > 3:
        Path(sys.argv[3]).write_text(json.dumps(dict(table=table, entries=entries), indent=1))
    print(f'table at {table:#x}')
    for e in entries:
        print(f'{e["index"]:2d} {e["name"]:16s} type {e["type"]} f {e["f8"]} {e["fc"]} first level {e["first_level"]} '
              f'tiers {len(e["tiers"])} mods {len(e["mods"])}')
        for t in e['tiers']:
            print(f'     L{t["level"]} {t["class_name"] or t["class_hash"]:14s} {t["class2_name"] or t["class2_hash"]:14s} '
                  f'f {t["f08"]} flag {t["flag0c"]} text {t["text10"]},{t["text14"]} icon {t["icon"]} price {t["price"]} '
                  f'xp {t["xp_next"]} | {t["w24"]} {t["w28"]} {t["w2c"]} {t["w30"]} | ammo price {t["ammo_price"]} '
                  f'max {t["max_ammo"]} pickup {t["ammo_pickup"]} | f40 {t["f40"]} {t["w44"]} {t["w48"]} {t["w4c"]}')
        for m in e['mods']:
            print(f'     mod {m["offset"]:#x} {" ".join(m["words"])}')


if __name__ == '__main__':
    main()
