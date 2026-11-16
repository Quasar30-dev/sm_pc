#!/usr/bin/env python3
"""Look at a place of a Size Matters level in sm_pc: hidden runs, one capture each, on one contact sheet.

    python tools/sm/sm_look.py 03 OUT.png --at 122.2,15,102.3 --yaws 0,90,180,270
    python tools/sm/sm_look.py 03 OUT.png --shot "start|122,15,102|180|idle:10" --shot "door|7,10,76|0|run:30"

A --shot is NAME|X,Y,Z|CAMERA_YAW|SCRIPT (an empty place is the level's own start). Arguments this tool does not
know go to sm_pc as they are (for example --all-weapons, --waypoints "x,z;x,z"). The runs are hidden and muted and write no settings.
"""
import argparse
import os
import subprocess
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# SM_PC_EXE / SM_PC_ISO name another executable or disc image
EXE = os.environ.get('SM_PC_EXE') or os.path.join(ROOT, 'build', 'x64', 'sm_pc.exe')
ISO = os.environ.get('SM_PC_ISO') or os.path.join(ROOT, 'Ratchet  Clank - Size Matters (USA)', 'Ratchet & Clank - Size Matters (USA).iso')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level')
    ap.add_argument('out')
    ap.add_argument('--at', default='')
    ap.add_argument('--yaws', default='0,90,180,270')
    ap.add_argument('--script', default='idle:10')
    ap.add_argument('--shot', action='append', default=[])
    ap.add_argument('--columns', type=int, default=2)
    ap.add_argument('--skin', default='0')
    a, rest = ap.parse_known_args()
    a.rest = [r for r in rest if r != '--']
    shots = list(a.shot)
    if not shots:
        shots = [f'yaw {y}|{a.at}|{y}|{a.script}' for y in a.yaws.split(',')]
    folder = os.path.join(ROOT, 'build', 'sm_pc_work', 'look')
    os.makedirs(folder, exist_ok=True)
    thumbs = []
    for k, shot in enumerate(shots):
        name, at, yaw, script = (shot.split('|') + ['', '', '', ''])[:4]
        bmp = os.path.join(folder, f'look_{os.getpid()}_{k}.bmp')
        cmd = [EXE, ISO, '--level', f'/WAD/{a.level}.WAD', '--hero-skin', a.skin, '--camera-yaw', yaw or '0',
               '--gameplay-script', script or 'idle:10', '--frames', '3', '--capture', bmp, '--hidden', '--mute']
        if at:
            cmd += ['--player-position', at]
        cmd += a.rest
        log = os.path.join(folder, f'look_{k}.log')
        with open(log, 'w') as f:
            code = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=100).returncode
        if not os.path.exists(bmp):
            print(f'{name}: no capture (exit {code}), see {log}')
            continue
        im = Image.open(bmp).convert('RGB')
        im.thumbnail((800, 450))
        ImageDraw.Draw(im).text((8, 6), f'{name}  {at}  yaw {yaw}', fill=(255, 255, 0))
        thumbs.append(im)
        os.remove(bmp)
    if not thumbs:
        sys.exit('no captures')
    w, h = thumbs[0].size
    cols = min(a.columns, len(thumbs))
    rows = (len(thumbs) + cols - 1) // cols
    sheet = Image.new('RGB', (w * cols, h * rows))
    for i, t in enumerate(thumbs):
        sheet.paste(t, ((i % cols) * w, (i // cols) * h))
    sheet.save(a.out)
    print(a.out, len(thumbs), 'shots')


if __name__ == '__main__':
    main()
