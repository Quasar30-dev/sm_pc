"""Size Matters: the property layout of every class from the declaration text in its class record. Research aid.

A class record's declaration (tools/sm/sm_rel_elf.py `class_records`) is the designers' C struct of the instance's
BOG properties: `Type name; Type name[ n ]; Type *name; ...`. This lays the fields out with C rules (natural
alignment, at most 4) and checks the end against the record's property size, for every level module.

    python tools/sm/sm_class_schema.py            # check all levels (needs build/sm_pc_work/classes_NN.json)
    python tools/sm/sm_class_schema.py 02 Pygmy   # print one class's fields
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'build/sm_pc_work'
# type -> (size, alignment)
TYPES = {
    'u8': (1, 1), 's8': (1, 1), 'Toggle': (1, 1), 'char': (1, 1), 'bool': (1, 1),
    'u16': (2, 2), 's16': (2, 2),
    'u32': (4, 4), 's32': (4, 4), 'f32': (4, 4), 'int': (4, 4), 'float': (4, 4),
    'TriggerLink': (4, 4), 'MobyLink': (4, 4), 'CurveLink': (4, 4), 'TieLink': (4, 4), 'ShrubLink': (4, 4),
    'VEC2': (8, 4), 'VEC3': (12, 4), 'VEC4': (16, 4), 'MATRIX4': (64, 4),
    # engine structs embedded in properties; sizes solved from classes with one such field and a known property size
    'Navigate': (44, 4), 'NavBound': (52, 4), 'Launch': (28, 4), 'ANIM_Manipulator': (68, 4), 'QUAT': (16, 4),
    'ChasePosEntry': (24, 4), 'BotControllerEntry': (28, 4), 'TargetEntry': (16, 4),
}
FIELD = re.compile(r'^(?:const\s+)?(?:struct\s+)?([A-Za-z_][A-Za-z0-9_:]*)\s*(\*?)\s*(?:const\s+)?(\*?)\s*'
                   r'([A-Za-z_][A-Za-z0-9_]*)?\s*(?:\[\s*([0-9A-Za-z_]*)\s*\])?(?:\s*\[\s*([0-9]+)\s*\])?$')


def layout(declaration):
    """[(type, name, offset, size, count)], end offset, [problems]."""
    fields, problems, at = [], [], 0
    text = re.sub(r'/\*.*?\*/', ' ', declaration, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    for part in text.split(';'):
        part = ' '.join(part.split())
        if not part:
            continue
        m = FIELD.match(part)
        if not m:
            problems.append(f'unparsed: {part!r}')
            continue
        kind, star1, star2, name, n1, n2 = m.groups()
        pointer = bool(star1 or star2)
        if name is None:                                 # `Type*name` matched the type as the name
            problems.append(f'unparsed: {part!r}')
            continue
        count = 1
        for n in (n1, n2):
            if n:
                if not n.isdigit():
                    problems.append(f'symbolic array length: {part!r}')
                    n = '1'
                count *= int(n)
        if pointer:
            size, align = 4, 4
        elif kind in TYPES:
            size, align = TYPES[kind]
        else:
            problems.append(f'unknown type {kind}')
            size, align = 4, 4
        at = (at + align - 1) & ~(align - 1)
        fields.append((kind + ('*' if pointer else ''), name, at, size, count))
        at += size * count
    return fields, at, problems


def main():
    if len(sys.argv) > 2:
        records = json.loads((WORK / f'classes_{sys.argv[1]}.json').read_text())['records']
        r = next(r for r in records if r['name'].lower() == sys.argv[2].lower())
        fields, end, problems = layout(r['declaration'])
        print(f'{r["name"]}: record {r["offset"]:#x}, callbacks {[hex(c) for c in r["callbacks"]]}, '
              f'properties {r["props_size"]:#x}, instance data {r["instance_size"]:#x}, extra {[hex(c) for c in r["extra"]]}')
        for kind, name, at, size, count in fields:
            print(f'  +{at:#05x} {kind:14s} {name}' + (f'[{count}]' if count > 1 else ''))
        print('  end', hex(end), problems)
        return
    unknown, bad, total = {}, [], 0
    for path in sorted(WORK.glob('classes_*.json')):
        for r in json.loads(path.read_text())['records']:
            if not r['declaration']:
                continue
            total += 1
            fields, end, problems = layout(r['declaration'])
            for p in problems:
                unknown.setdefault(p, []).append(f'{path.stem[-2:]}:{r["name"]}')
            padded = (end + 3) & ~3
            if not problems and r['props_size'] not in (end, padded) and not (end <= r['props_size'] <= end + 15):
                bad.append((path.stem[-2:], r['name'], end, r['props_size']))
    print(total, 'declarations;', len(bad), 'layouts off;', len(unknown), 'problems')
    for p, where in sorted(unknown.items(), key=lambda kv: -len(kv[1])):
        print(f'  {p}: {len(where)}  {where[:4]}')
    for b in bad[:60]:
        print('  off:', b)


if __name__ == '__main__':
    main()
