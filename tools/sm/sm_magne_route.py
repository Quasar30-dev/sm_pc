#!/usr/bin/env python3
"""Size Matters: ways along a level's magnetic faces, for the hidden runs' Magneboots walks (mgo:N, --waypoints3).

The faces are the level's own collision faces of surface type fa58270b (flag 4 of the "STD " table: the ground of
type 3 that puts the hero on his Magneboots), read from a hidden sm_pc run (SM_FACE_DUMP) and kept in
build/sm_pc_work/cache. A way is the shortest chain of faces that share an edge, given as the middles of the edges
it crosses: on a track two faces wide that is the track's middle.

    python tools/sm/sm_magne_route.py 03 -25.471,8.132,8.712,30 -25.4,6.2,-14.2 -44.5,15.2,8.7
        prints the way from the first place to the second as "x,y,z;x,y,z;..." (the faces within 30 m of the centre)

    from sm_magne_route import MagneFaces
    faces = MagneFaces('03', (-25.471, 8.132, 8.712), 30)
    points = faces.track((-25.4, 6.2, -14.2), (-44.5, 15.2, 8.7))     # [[x, y, z], ...] or None
"""
import heapq
import math
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# SM_PC_EXE / SM_PC_ISO name another executable or disc image
EXE = Path(os.environ.get('SM_PC_EXE') or ROOT / 'build/x64/sm_pc.exe')
ISO = Path(os.environ.get('SM_PC_ISO') or ROOT / 'Ratchet  Clank - Size Matters (USA)' / 'Ratchet & Clank - Size Matters (USA).iso')
CACHE = ROOT / 'build/sm_pc_work/cache'
MAGNETIC = 'fa58270b'


def _sub(a, b):
    return [a[k] - b[k] for k in range(3)]


def _dot(a, b):
    return sum(a[k] * b[k] for k in range(3))


def _nearest_on(p, a, b, c):
    """The point of the triangle nearest to p (Ericson, Real-Time Collision Detection 5.1.5)."""
    ab, ac, ap = _sub(b, a), _sub(c, a), _sub(p, a)
    d1, d2 = _dot(ab, ap), _dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        return a
    bp = _sub(p, b)
    d3, d4 = _dot(ab, bp), _dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return b
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3)
        return [a[k] + ab[k] * v for k in range(3)]
    cp = _sub(p, c)
    d5, d6 = _dot(ab, cp), _dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return c
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6)
        return [a[k] + ac[k] * w for k in range(3)]
    va = d3 * d6 - d5 * d4
    if va <= 0 and d4 - d3 >= 0 and d5 - d6 >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return [b[k] + (c[k] - b[k]) * w for k in range(3)]
    denom = 1 / (va + vb + vc)
    v, w = vb * denom, vc * denom
    return [a[k] + ab[k] * v + ac[k] * w for k in range(3)]


class MagneFaces:
    def __init__(self, level, centre, radius, surface=MAGNETIC):
        self.level = level
        name = f'magne_{level}_{centre[0]:g}_{centre[1]:g}_{centre[2]:g}_{radius:g}_{surface}.txt'
        path = CACHE / name
        if not path.exists() or path.stat().st_mtime < EXE.stat().st_mtime:
            CACHE.mkdir(parents=True, exist_ok=True)
            env = dict(os.environ, SM_FACE_DUMP=f'{centre[0]},{centre[1]},{centre[2]},{radius},{surface}')
            out = subprocess.run([str(EXE), str(ISO), '--level', f'/WAD/{level}.WAD', '--hero-skin', '0',
                                  '--gameplay-script', 'idle:1', '--hidden', '--mute'], env=env, capture_output=True,
                                 text=True, errors='replace', timeout=100).stdout
            rows = [row for row in out.replace('\r', '').split('\n') if row.startswith('Tri ')]
            path.write_text('\n'.join(rows) + '\n')
        self.faces = []
        for row in path.read_text().split('\n'):
            v = [float(x) for x in row.split()[1:]]
            if len(v) == 9:
                self.faces.append((v[0:3], v[3:6], v[6:9]))
        key = lambda p: (round(p[0], 2), round(p[1], 2), round(p[2], 2))  # noqa: E731
        edges = {}
        for i, t in enumerate(self.faces):
            k = [key(p) for p in t]
            for a, b in ((0, 1), (1, 2), (2, 0)):
                edges.setdefault(frozenset((k[a], k[b])), []).append(i)
        self.centres = [[sum(p[k] for p in t) / 3 for k in range(3)] for t in self.faces]
        self.links = {}
        for e, members in edges.items():
            ends = list(e)
            if len(ends) != 2:
                continue
            middle = [(ends[0][k] + ends[1][k]) / 2 for k in range(3)]
            for i in members:
                for j in members:
                    if i != j:
                        self.links.setdefault(i, []).append((j, middle))

    def nearest(self, p):
        """(index of the face nearest to the place, its nearest point)."""
        best = None
        for i, (a, b, c) in enumerate(self.faces):
            q = _nearest_on(list(p), a, b, c)
            d = math.dist(q, p)
            if best is None or d < best[0]:
                best = (d, i, q)
        return None if best is None else (best[1], best[2])

    def track(self, start, goal, apart=2.0):
        """The way from one place to another along the faces, a place every `apart` metres or more; None if the
        faces of the two places are not joined."""
        a, b = self.nearest(start), self.nearest(goal)
        if a is None or b is None:
            return None
        (a, _), (b, end) = a, b
        best = {a: 0}
        came = {}
        heap = [(0, a)]
        while heap:
            cost, i = heapq.heappop(heap)
            if i == b:
                break
            if cost > best.get(i, 1e9):
                continue
            for j, m in self.links.get(i, []):
                c = cost + math.dist(self.centres[i], m) + math.dist(m, self.centres[j])
                if c < best.get(j, 1e9):
                    best[j] = c
                    came[j] = (i, m)
                    heapq.heappush(heap, (c, j))
        if b not in best:
            return None
        out = []
        i = b
        while i != a:
            i, m = came[i]
            out.append(m)
        out.reverse()
        thin = []
        for p in out:
            if not thin or math.dist(p, thin[-1]) >= apart:
                thin.append(p)
        if thin and math.dist(thin[-1], end) < apart / 2:
            thin.pop()
        thin.append(end)
        return thin


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    numbers = lambda text: [float(x) for x in text.split(',')]  # noqa: E731
    *centre, radius = numbers(sys.argv[2])
    faces = MagneFaces(sys.argv[1], centre, radius)
    way = faces.track(numbers(sys.argv[3]), numbers(sys.argv[4]))
    if way is None:
        sys.exit(f'no way along the {len(faces.faces)} magnetic faces')
    print(';'.join(f'{p[0]:.2f},{p[1]:.2f},{p[2]:.2f}' for p in way))
    print(f'{len(way)} places, {sum(math.dist(way[k], way[k + 1]) for k in range(len(way) - 1)):.1f} m, '
          f'{len(faces.faces)} magnetic faces', file=sys.stderr)


if __name__ == '__main__':
    main()
