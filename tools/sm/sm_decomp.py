"""Size Matters: read functions out of a level module's Ghidra export (tools/sm/sm_rel_elf.py + ghidra_ps2.py export).

    python tools/sm/sm_decomp.py 02 Crate__init Crate__event      # by name (substring match lists candidates)
    python tools/sm/sm_decomp.py 02 0x15efc0 --callees 1          # by address, with its callees' bodies one level down
    python tools/sm/sm_decomp.py 02 --grep "PLAYER_Damage"        # functions whose C mentions a text
    python tools/sm/sm_decomp.py 02 --callers 0x8bcc8             # who calls an address

The export is build/sm_pc_work/ghidra/L<level>_decomp.json: {functions: [{entry, name, size, callees, c}]}.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DECLARATION = re.compile(r'^  [A-Za-z_][A-Za-z0-9_ ]*\**[A-Za-z_][A-Za-z0-9_]*( \[[0-9]+\])*;$')
_cache = {}


def load(level):
    if level not in _cache:
        path = ROOT / f'build/sm_pc_work/ghidra/L{level}_decomp.json'
        fs = json.loads(path.read_text(encoding='utf-8', errors='replace'))['functions']
        _cache[level] = ({int(f['entry'], 16): f for f in fs}, {f['name']: f for f in fs})
    return _cache[level]


def find(level, key):
    by_addr, by_name = load(level)
    try:
        a = int(key, 16)
        if a in by_addr:
            return [by_addr[a]]
        inside = [f for e, f in by_addr.items() if e <= a < e + max(f['size'], 4)]
        if inside:
            return inside[:1]
    except ValueError:
        pass
    if key in by_name:
        return [by_name[key]]
    return [f for n, f in by_name.items() if key.lower() in n.lower()]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level')
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--callees', type=int, default=0, help='also print callees to this depth (module functions only)')
    ap.add_argument('--grep')
    ap.add_argument('--callers')
    ap.add_argument('--max', type=int, default=400, help='most lines printed per function')
    ap.add_argument('--brief', action='store_true', help='leave out the local variable declarations')
    a = ap.parse_args()
    by_addr, by_name = load(a.level)
    if a.grep:
        for f in by_addr.values():
            if a.grep in f['c']:
                print(f['entry'], f['name'], f['size'])
        return
    if a.callers:
        target = find(a.level, a.callers)
        names = {t['name'] for t in target} | {t['entry'] for t in target}
        for f in by_addr.values():
            if any(c in names for c in f['callees']):
                print(f['entry'], f['name'], f['size'])
        return
    printed = set()

    def show(f, depth):
        if f['entry'] in printed:
            return
        printed.add(f['entry'])
        lines = f['c'].strip('\n').split('\n')
        if a.brief:
            lines = [l for l in lines if l.strip() and not DECLARATION.match(l)]
        print(f'// ---- {f["name"]} @ {f["entry"]} ({f["size"]} bytes)')
        print('\n'.join(lines[:a.max]))
        if len(lines) > a.max:
            print(f'// ... {len(lines) - a.max} more lines')
        print()
        if depth > 0:
            for c in f['callees']:
                g = by_name.get(c)
                if g is None:
                    try:
                        g = by_addr.get(int(c, 16))
                    except ValueError:
                        g = None
                if g and int(g['entry'], 16) < 0x1000000:   # module code only, not the resident
                    show(g, depth - 1)

    for key in a.keys:
        fs = find(a.level, key)
        if len(fs) > 1:
            print(f'// {key}: {len(fs)} matches:', ', '.join(f['name'] for f in fs[:40]))
            continue
        if not fs:
            print(f'// {key}: not found')
            continue
        show(fs[0], a.callees)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
