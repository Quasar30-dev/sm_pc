"""Size Matters: the sound bindings of a level, short form. Research aid.

Reads build/sm_pc_work/sounds.txt (sm_class_inventory's SOUNDS output: signature, bank path, index, source path per
binding) and prints `bank index signature name` with the directories dropped.

    python tools/sm/sm_sounds.py 02                    # every binding of the level
    python tools/sm/sm_sounds.py 02 Blitz AcidBomb     # these banks (case-insensitive substrings)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def base(path):
    return re.split(r'[\\/]', path)[-1]


def load(level):
    rows, on = [], False
    for line in (ROOT / 'build/sm_pc_work/sounds.txt').read_text(encoding='latin-1').split('\n'):
        if line.startswith('=='):
            on = line.split()[1] == f'/WAD/{level}.WAD'
            continue
        m = re.match(r'(\w{8}) (.*?\.bnk) (\d+) ?(.*)', line.strip()) if on else None
        if m:
            rows.append((base(m.group(2))[:-4], int(m.group(3)), m.group(1), base(m.group(4))))
    return sorted(rows)


def main():
    rows = load(sys.argv[1])
    wanted = [w.lower() for w in sys.argv[2:]]
    bank = None
    line = []
    for b, index, signature, name in rows:
        if wanted and not any(w in b.lower() for w in wanted):
            continue
        if b != bank:
            if line:
                print(' '.join(line))
            bank, line = b, [b + ':']
        line.append(f'{index}={signature}({name[:24]})')
    if line:
        print(' '.join(line))


if __name__ == '__main__':
    main()
