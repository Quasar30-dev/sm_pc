#!/usr/bin/env python3
"""Size Matters, Metalis (level 04): the story's way through the level in one hidden sm_pc run.

    python tools/sm/sm_metalis_run.py            # the run; exit code 0 when every milestone was met
    python tools/sm/sm_metalis_run.py --print    # only print the waypoints and the script

The way, as the original has it: the level begins with Clank in the arena's lobby and its first Destruction Derby;
the crane takes him over to the ship's ground; the growth pad there (awake once the derby's movie has been seen)
makes him the giant and loads level 15. What this run does NOT do, because it is not built: the arena and its
challenges - of the first derby only its movie is asked for. Nothing here is retail behaviour: the waypoints and
the tick counts are this test's own. The window is hidden and the run is muted; no testing aids are used.
"""
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sm_ryllus_run import EXE, ISO, LOGS, Script, last  # noqa: E402


def build():
    s = Script()
    s.do('idle:10')
    # along the lobby to its east end and into the crane's tray; the swing over (131 frames)
    s.go((-52, 52.8), (-46.5, 52.8)).go((-43.2, 52), jump=True).do(*['idle:30'] * 5)
    # off it onto the ship's ground, round the ship, up the ramp to the growth pad
    s.go((-4, 52.8), (1, 56), (5.5, 58.3), (8.8, 60), (9, 65), (9, 70), (9.05, 73.6)).do('idle:10', 'interact:2', 'idle:30')
    return s


def main():
    s = build()
    points = ';'.join(f'{x:g},{z:g}' for x, z in s.points)
    script = ','.join(s.words)
    if '--print' in sys.argv:
        print(points)
        print(script)
        return 0
    log = LOGS / 'metalis_level.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(EXE), str(ISO), '--level', '/WAD/04.WAD', '--hero-skin', '0', '--camera-yaw', '0', '--waypoints', points,
               '--gameplay-script', script, '--hidden', '--mute']
    with open(log, 'w', encoding='utf-8', errors='replace') as out:
        code = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, timeout=300).returncode
    text = log.read_text(encoding='utf-8', errors='replace').replace(chr(13), '')
    crane = [int(x) for x in re.findall(r'^Crane: state=(\d)', text, flags=re.M)]
    objects = last(r'^Objects: (.*)$', text, '')
    checks = [
        ('start: he is Clank, in the arena\'s lobby', re.search(r'^idle:10: position=-57\.\d+,14\.0\d+,52\.', text, flags=re.M) is not None and
         'Character: Clank' in text),
        ('start: the first derby\'s movie was asked for (the derby itself is NOT BUILT)', 'Movie: /VID/L04/04B.BIK' in text),
        ('crane: it swung over with him in its tray', 2 in crane and (4 in crane or 0 in crane)),
        ('crane: he stepped off onto the ship\'s ground', re.search(r'^go:\d+: position=-4\.\d+,1[34]\.\d+,52\.', text, flags=re.M) is not None),
        ('pad: he stands on the growth pad and it offers to grow him', re.search(r'^idle:10: position=[89]\.\d+,16\.\d+,73\.', text, flags=re.M) is not None and
         'prompt=Grow' in text),
        ('pad: the grow movie was asked for and level 15 is where it leads', 'Movie: /VID/L04/04C.BIK' in text and
         re.search(r'^Travel: /WAD/15', text, flags=re.M) is not None),
        ('he did not die on the way', 'deaths=0' in objects),
    ]
    ok = code == 0
    for name, met in checks:
        print(('PASS  ' if met else 'FAIL  ') + name)
        ok = ok and met
    print(f'exit code {code}; log {log}')
    print('not built, so not in this run: the arena and its challenges (the Destruction Derby, the Microbot Toss, the Microbot Survival)')
    print('RESULT: ' + ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
