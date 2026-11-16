#!/usr/bin/env python3
"""Size Matters: a sound table of a level module, its hashes named. Research aid.

Many classes keep their sounds as rows of {hash, four floats} (0x14 bytes) that an init resolves with 0x1282d0.

    python tools/sm/sm_sound_table.py 03 0x23b108 6            # six rows of 0x14 bytes
    python tools/sm/sm_sound_table.py 03 0x23b108 6 --stride 4 # bare hashes

Names come from build/sm_pc_work/sounds_02.txt (sm_sounds.py); a hash it does not hold prints without one.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sm_elfmem import Mem  # noqa: E402

WORK = Path(__file__).resolve().parents[2] / 'build/sm_pc_work'


def names():
    out = {}
    for f in sorted(WORK.glob('sounds_*.txt')):
        for line in f.read_text(encoding='latin-1').split('\n'):
            p = line.split()
            if len(p) >= 4:
                try:
                    out[int(p[0], 16)] = p[-1].replace('\\', '/').split('/')[-1]
                except ValueError:
                    pass
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level')
    ap.add_argument('address')
    ap.add_argument('count', type=int)
    ap.add_argument('--stride', type=lambda v: int(v, 0), default=0x14)
    a = ap.parse_args()
    m = Mem(str(WORK / 'ghidra' / f'LEVEL_{a.level}.elf'))
    known = names()
    base = int(a.address, 16)
    for i in range(a.count):
        at = base + a.stride * i
        h = m.u32(at)
        extra = [round(m.f32(at + 4 + 4 * k), 3) for k in range((a.stride - 4) // 4)]
        print(f'{i:2d} {at:x} {h:08x} {known.get(h, "?")} {extra if extra else ""}')


if __name__ == '__main__':
    main()
