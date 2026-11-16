#!/usr/bin/env python3
"""Size Matters: a compact ground-height map of a level's static collision, from a hidden sm_pc run (SM_GROUND_MAP).

    python tools/sm/sm_ground.py 03 90 100 112 150 [--step 1] [--top 40] [--bottom -12] [--tenths]
    python tools/sm/sm_ground.py 03 60 90 130 160 --step 0.25 --png OUT.png [--mark x,z ...]

Rows are z, columns x; a cell is the first floor below `top`, in whole metres (or tenths with --tenths); '.' none.
With --png: a picture instead, height as colour (blue low to red high over the map's own range) shaded by slope so
kerbs, walls and steps show; a grid line every 10 m with its coordinate; x grows to the right, z downwards.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# SM_PC_EXE / SM_PC_ISO name another executable or disc image
EXE = Path(os.environ.get('SM_PC_EXE') or ROOT / 'build/x64/sm_pc.exe')
ISO = Path(os.environ.get('SM_PC_ISO') or ROOT / 'Ratchet  Clank - Size Matters (USA)' / 'Ratchet & Clank - Size Matters (USA).iso')


def picture(a, rows):
    import colorsys
    from PIL import Image, ImageDraw
    xs = [float(x) for x in rows[0]]
    zs = [float(r[0]) for r in rows[1:]]
    grid = [[None if v == '.' else float(v) for v in r[1:]] for r in rows[1:]]
    known = [v for row in grid for v in row if v is not None]
    low, high = min(known), max(known)
    scale = max(1, round(4 * a.step / 0.25)) if a.step < 1 else 8
    im = Image.new('RGB', (len(xs) * scale, len(zs) * scale), (0, 0, 0))
    px = im.load()
    for j, row in enumerate(grid):
        for i, v in enumerate(row):
            if v is None:
                continue
            east = row[i + 1] if i + 1 < len(row) and row[i + 1] is not None else v
            south = grid[j + 1][i] if j + 1 < len(grid) and grid[j + 1][i] is not None else v
            slope = min(1.0, (abs(east - v) + abs(south - v)) / a.step / 1.5)
            r, g, b = colorsys.hsv_to_rgb(0.66 * (1 - (v - low) / max(1e-6, high - low)), 0.75, 1 - 0.8 * slope)
            for dy in range(scale):
                for dx in range(scale):
                    px[i * scale + dx, j * scale + dy] = (int(r * 255), int(g * 255), int(b * 255))
    d = ImageDraw.Draw(im)
    for i, x in enumerate(xs):
        if abs(x / 10 - round(x / 10)) < 1e-6:
            d.line([(i * scale, 0), (i * scale, im.size[1])], fill=(255, 255, 255), width=1)
            d.text((i * scale + 2, 2), f'x{x:g}', fill=(255, 255, 255))
    for j, z in enumerate(zs):
        if abs(z / 10 - round(z / 10)) < 1e-6:
            d.line([(0, j * scale), (im.size[0], j * scale)], fill=(255, 255, 255), width=1)
            d.text((2, j * scale + 2), f'z{z:g}', fill=(255, 255, 255))
    for mark in a.mark:
        parts = mark.split(',')
        x, z = float(parts[0]), float(parts[1])
        cx, cy = (x - xs[0]) / a.step * scale, (z - zs[0]) / a.step * scale
        d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], outline=(255, 255, 255), fill=(255, 0, 255))
        if len(parts) > 2:
            d.text((cx + 6, cy - 6), parts[2], fill=(255, 255, 255))
    im.save(a.png)
    print(a.png, im.size, f'heights {low:.1f}..{high:.1f}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level')
    for name in ('x0', 'z0', 'x1', 'z1'):
        ap.add_argument(name, type=float)
    ap.add_argument('--step', type=float, default=1)
    ap.add_argument('--top', type=float, default=40)
    ap.add_argument('--bottom', type=float, default=-12)
    ap.add_argument('--tenths', action='store_true')
    ap.add_argument('--png')
    ap.add_argument('--mark', action='append', default=[], help='x,z[,label]: a dot on the picture')
    a = ap.parse_args()
    env = dict(os.environ, SM_GROUND_MAP=f'{a.x0},{a.z0},{a.x1},{a.z1},{a.step},{a.top},{a.bottom}')
    out = subprocess.run([str(EXE), str(ISO), '--level', f'/WAD/{a.level}.WAD', '--hero-skin', '0', '--gameplay-script',
                          'idle:1', '--hidden', '--mute'], env=env, capture_output=True, text=True, timeout=100).stdout
    rows, on = [], False
    for line in out.split('\n'):
        if line.startswith('Ground map'):
            on = True
            continue
        if on:
            if not re.match(r'\s*-?\d', line):
                if rows:
                    break
                continue
            rows.append(line.split())
    if not rows:
        sys.exit('no map in the output')
    if a.png:
        return picture(a, rows)
    width = 5 if a.tenths else 4
    fmt = (lambda v: f'{float(v):{width}.1f}') if a.tenths else (lambda v: f'{round(float(v)):{width}d}')
    print(' ' * 6 + ''.join(f'{float(x):{width}g}' for x in rows[0]))
    for r in rows[1:]:
        print(f'{float(r[0]):6g}' + ''.join(' ' * (width - 1) + '.' if v == '.' else fmt(v) for v in r[1:]))


if __name__ == '__main__':
    main()
