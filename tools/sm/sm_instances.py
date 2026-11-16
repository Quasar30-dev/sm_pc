"""Size Matters: the placed instances of a level with their properties decoded through the class declarations.

Reads build/sm_pc_work/instances.txt (sm_class_inventory's INSTANCES output, which carries each instance's raw
property bytes) and build/sm_pc_work/classes_NN.json. Link fields (TriggerLink, MobyLink, CurveLink) print the class
and BOG offset of the instance they name.

    python tools/sm/sm_instances.py 02 Level02Button Level02TriggeredDoor     # these classes
    python tools/sm/sm_instances.py 02 --near -36,31,91 --radius 25           # everything placed near a point
    python tools/sm/sm_instances.py 02 --source 0x12a30                       # one instance, and who links to it
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sm_class_schema import layout  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'build/sm_pc_work'
places = {}  # trigger volumes and curves of the loaded level by BOG offset: their own dump line
ROW = re.compile(r'\s+(\w+) flags (\w+) pos\s+(\S+)\s+(\S+)\s+(\S+) model (\w+) variant (\w+) props (\d+) ?(\w*)')


def load(level):
    records = {r['name'].lower(): r for r in json.loads((WORK / f'classes_{level}.json').read_text())['records']}
    rows, cls, on = [], None, False
    places.clear()
    for line in (WORK / 'instances.txt').read_text(encoding='latin-1').split('\n'):
        if line.startswith('=='):
            on = line.split()[1] == f'/WAD/{level}.WAD'
            continue
        if not on or not line:
            continue
        if not line.startswith(' '):
            cls = line.split()[0]
            continue
        if line.startswith(' T ') or line.startswith(' C '):
            parts = line.split()
            places[int(parts[1], 16)] = line.strip()
            continue
        m = ROW.match(line)
        if m:
            source, flags = int(m.group(1), 16), int(m.group(2), 16)
            pos = tuple(float(m.group(k)) for k in (3, 4, 5))
            rows.append(dict(cls=cls, source=source, flags=flags, pos=pos, model=m.group(6),
                             props=bytes.fromhex(m.group(9)), record=records.get(cls.lower())))
    return rows


def decode(row, by_source):
    record = row['record']
    if not record or not record['declaration']:
        return []
    fields, _, _ = layout(record['declaration'])
    out = []
    data = row['props']
    for kind, name, at, size, count in fields:
        if name.startswith('pad') or kind.endswith('*'):
            continue
        values = []
        for k in range(min(count, 16)):
            o = at + k * size
            if o + size > len(data):
                break
            if kind in ('f32', 'float'):
                values.append(f'{struct.unpack_from("<f", data, o)[0]:g}')
            elif kind in ('VEC2', 'VEC3', 'VEC4', 'QUAT'):
                values.append('(' + ','.join(f'{v:g}' for v in struct.unpack_from(f'<{size // 4}f', data, o)) + ')')
            elif kind.endswith('Link'):
                v, = struct.unpack_from('<I', data, o)
                if v == 0:
                    values.append('0')
                elif v in by_source:
                    values.append(f'{by_source[v]["cls"]}@{v:x}')
                else:
                    values.append(f'{v:x}')
            elif size == 1:
                values.append(str(data[o]))
            elif size == 2:
                values.append(str(struct.unpack_from('<H', data, o)[0]))
            elif size == 4:
                values.append(str(struct.unpack_from('<i', data, o)[0]))
            else:
                values.append(data[o:o + min(size, 16)].hex())
        if count > 1:
            while len(values) > 1 and values[-1] == '0':
                values.pop()
        out.append(f'{name}=' + (values[0] if len(values) == 1 else '[' + ' '.join(values) + ']'))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level')
    ap.add_argument('classes', nargs='*')
    ap.add_argument('--near')
    ap.add_argument('--radius', type=float, default=20)
    ap.add_argument('--source')
    ap.add_argument('--pooled', action='store_true', help='also list instances at the origin')
    ap.add_argument('--place', nargs='*', default=[], help='print trigger volumes / curves by BOG offset (hex)')
    a = ap.parse_args()
    rows = load(a.level)
    by_source = {r['source']: r for r in rows}
    if a.place:
        for key in a.place:
            print(places.get(int(key, 16), f'{key}: not a trigger or curve of this level'))
        return
    wanted = {c.lower() for c in a.classes}
    near = tuple(float(v) for v in a.near.split(',')) if a.near else None
    source = int(a.source, 16) if a.source else None
    for r in rows:
        placed = any(r['pos'])
        if not placed and not a.pooled and source is None:
            continue
        if wanted and r['cls'].lower() not in wanted:
            continue
        if near and sum((p - q) ** 2 for p, q in zip(r['pos'], near)) > a.radius ** 2:
            continue
        if source is not None and r['source'] != source:
            # who links to it
            if struct.pack('<I', source) not in r['props']:
                continue
        state = (r['flags'] >> 8) & 0xff
        print(f'{r["cls"]}@{r["source"]:x} variant {r["flags"] & 0xff} state {state} '
              f'at {r["pos"][0]:.2f},{r["pos"][1]:.2f},{r["pos"][2]:.2f}  ' + ' '.join(decode(r, by_source)))


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
