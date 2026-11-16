"""Size Matters, Kalidon (level 03): the level in one continuous hidden run of sm_pc, as far as it is built.

Regression aid for the native level. It drives build/x64/sm_pc.exe with a gameplay script - the pad a player would
give, tick by tick. Sections, in the level's order:

  outside   the landing pad, down the slope, the robot heads, the Hypershot swing over the pit, the ramp to the
            factory's front and its four robot heads
  lock1     the Grind Lock beside the factory door: the rails, the bombs, the three switches and the two gates
  door1     through the factory door once the lock is open
  rooms     the showcase hall, the ramp, round the conveyor room's two belts, the corridor to the shaft's ring
  shaft     the shaft platform: its four stops and their seven waves, the two bomb sets, off it at the console's
            ledge before it blows, the console, and the climb ahead of the fluid: four lifts and sixteen ledges
            up to the bridge
  east      the armour piece, the drones' corridor, the two workers on the belt shot from the platform, the Hypershot
            pull onto the belt's stub, east against the belt, the swing to the north side
  showcases the showcase room: its door shuts behind him, eight robots out of their cases, both doors open, the lift
  lake      the bolt crank that raises four targets and six plates out of the fluid, and the way round the lake:
            a swing, three plates, two swings with nothing between, three plates, a last swing to the far side
  hall      the hook hall: its front door shuts, the four waves the hooks bring (seventeen enemies), and its
            Grind Lock: the main rail's switch and four bombs, both side loops with a switch and a bomb each, four
            crossings; then the back door

    python tools/sm/sm_kalidon_run.py                 # every section; log build/sm_pc_work/logs/kalidon_level.log
    python tools/sm/sm_kalidon_run.py --only lock1    # one section, started at its own place (kalidon_lock1.log)
    python tools/sm/sm_kalidon_run.py --until lock1   # from the landing through that section
    python tools/sm/sm_kalidon_run.py --view          # also print one line a command (place, clip, health, status)
    python tools/sm/sm_kalidon_run.py --print         # only print the waypoints and the script

Nothing here is retail behaviour: the waypoints and the tick counts are this test's own. TESTING AIDS, said in the
report: --all-weapons (the level is started directly, so the Hypershot of Ryllus and the Grind Boots the skyboard
race gives are not earned in the run) and hp:250 in the fights. The window is hidden and the run is muted. Exit
code 0 when every milestone of the sections run was met.
"""
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sm_magne_route import MagneFaces  # noqa: E402
from sm_ryllus_run import EXE, ISO, LOGS, Script, last  # noqa: E402

POINTS3 = []  # the places of the mgo:N walks (--waypoints3)


def outside(s):
    # TESTING AID, said in the report: the run is about the level's mechanisms, not the fights
    s.do('hp:250', 'idle:20')
    # off the pad by its ramp, and down the road between its kerbs; the three robot heads come up it
    s.go((119, 97), (99, 97), (95.5, 101), (95, 105), (96.5, 110), (99, 116), (101, 123)).fight(30)
    s.go((102, 130), (101, 137), (98, 143), (92, 146.5), (80, 147), (70, 145), (64, 141), (62.3, 139.6))
    # the Hypershot at the target over the pit, held through the swing's first pass and let go on its rise
    s.do('equip:14', 'turn:251', 'idle:3', 'fire:66', 'idle:60')
    # the west bank, the robots at the foot of the factory's stairs, the stairs, the four heads at the door
    s.go((22, 123), (12, 116)).fight(20).go((6.5, 108)).fight(10).go((6.5, 103), (6.5, 86)).fight(30).go((7, 80)).fight(20)
    return s


def lock1(s):
    # at the lock: Triangle, then the rails. `rail:N` waits until he is N metres along the rail he is on.
    s.go((11.5, 73.3)).do('idle:5', 'interact:2', 'idle:30')
    s.do('rail:38', 'ascend:2', 'idle:20')              # over the two bombs at 43 m
    s.do('rail:76', 'wrench:2', 'idle:30')              # the first switch (78.7 m); its gate is at 88 m
    s.do('rail:165', 'hopleft:2', 'idle:60')            # the left rail begins beside 163 m
    s.do('rail:30', 'ascend:2', 'idle:55', 'rail:48', 'ascend:2', 'idle:55')  # its bombs at 35 m and 52.5 m
    s.do('rail:106', 'wrench:2', 'idle:30', 'rail:117', 'wrench:2', 'idle:60')  # the two switches of the last gate
    s.do('idle:120')
    return s


def door1(s):
    # the door opens for him once its lock is open and its four heads are gone; in, to the showcase hall's mouth
    s.go((7.4, 75)).do('idle:45').go((7.4, 68), (7.4, 62)).do('idle:20')
    return s


def rooms(s):
    s.go((7.5, 55), (7.5, 48)).do('clear:12').go((13, 44.5), (20, 40), (24, 37), (23.5, 28), (23.5, 24)).do('clear:14')
    s.go((16, 22), (16, 13)).do('clear:10').go((23, 13), (27, 9), (27, 1), (27, -8), (31, -10), (35, -10), (40, -9.6))
    return s


def shaft(s):
    centre, west, east = (46.9, -9.3), (43.5, -9.3), (50.5, -9.3)
    stop = lambda: s.do('idle:100', 'clear:14').go(centre).do('idle:80', 'clear:14').go(centre).do('idle:40', 'clear:14').go(centre)
    bombs = lambda: [s.go(west, east) for _ in range(8)] and s.go(centre)
    # down: `ydown:N` waits until he is at or below -N/10 (the platform carries him)
    s.go(centre).do('ydown:41')
    stop()
    s.do('ydown:141')
    stop()
    s.do('ydown:260')
    bombs()  # the first four pod bombs: he keeps moving
    s.do('ydown:387')
    stop()
    s.do('ydown:490')
    bombs()
    s.do('ydown:627')
    stop()
    # it gives way: to its west edge, and off onto the console's ledge as it passes
    s.go((39.3, -9.4)).do('ydown:885').go((35, -9.4), (30, -9.3)).do('idle:60', 'interact:2', 'idle:165')
    # the climb: jump onto the first lift, then lift by lift; `yup:N` waits until he is at or above -N/10
    hops = lambda *places: [s.go(p, hop=True).do('idle:10') for p in places]
    s.go((38.0, -9.6)).go((41.1, -10.0), jump=True).do('idle:20', 'yup:701', 'idle:5')
    s.go((42.63, -5.51), jump=True).do('idle:10')
    hops((51.83, -6.29), (51.06, -13.54), (42.73, -13.79))
    s.do('idle:60').go((44.32, -8.64), jump=True).do('idle:10', 'yup:485', 'idle:5')
    s.go((42.81, -5.59), jump=True).do('idle:10')
    hops((51.27, -5.3), (53.03, -9.64), (51.28, -14.04))
    s.go((42.97, -13.92), jump=True).do('idle:60').go((44.8, -9.44), jump=True).do('idle:10', 'yup:282', 'idle:5')
    s.go((41.87, -6.25), jump=True).do('idle:10')
    hops((50.99, -5.27), (49.91, -15.1), (43.34, -14.69), (40.32, -9.76), (44.03, -3.63), (50.51, -4.76))
    s.do('idle:60').go((49.04, -8.91), jump=True).do('idle:10', 'yover:110', 'idle:30').go((44, -9.6), (40, -9.6)).do('idle:20')
    return s


def east(s):
    # off the bridge to the west, and round the ring to its east side (the shaft's middle is open now)
    # (the walkway north of the open shaft is a metre wide)
    s.go((36.2, -9.6), (36.9, -14.8), (40.5, -19.4), (43.5, -20.05), (50, -20.05), (54.1, -16.9), (57.5, -9.6))
    s.go((60, -9.6), (64, -10), (68, -10), (74, -10), (74, -17)).do('clear:14').go((74, -21.5), (74.2, -24.8))
    # the two workers on the belt's stubs, from here with the Lacerator; then the Hypershot at the target over it
    shots = ['fire:3', 'idle:6'] * 12
    s.do('equip:0', 'turn:189', 'idle:3', *shots, 'turn:187', *shots, 'idle:10', 'clear:8').go((74.2, -24.8))
    s.do('equip:14', 'turn:187', 'idle:3', 'fire:50', 'idle:30')
    # on the stub; onto the belt and east against it, and the swing to the north platform
    s.go((73.1, -37.2), (80, -37.3)).do('turn:187', 'fire:44', 'idle:40').go((79.5, -62), (79.5, -69)).do('idle:5')
    return s


def showcases(s):
    s.go((73, -69), (67, -69)).do('idle:20', 'clear:16', 'idle:30', 'clear:16').go((60, -69), (54, -69)).do('idle:40')
    s.go((50, -69.15), (46.5, -69.2), (44.5, -69.25)).do('idle:90').go((40, -69.3), (37.3, -69.1)).do('idle:10')
    return s


def lake(s):
    # the crank: Square at the bolt, then round it until it is home (16.7 s of turning)
    s.go((36.3, -76.4)).do('idle:5', 'wrench:2', 'idle:30', 'run:250', 'run:270', 'idle:10', 'equip:14')
    # the first target, held 62 frames, onto the first plate; the plates sink 1.25 s after he lands
    s.go((31.5, -80), (30.7, -80.4)).do('turn:234', 'idle:3', 'fire:62', 'idle:30')
    s.go((3.8, -96.38), jump=True).do('idle:4').go((-4.8, -88.9), hop=True).do('idle:3')
    # two targets with nothing between: off the first, the second is taken in the air
    s.do('turn:336', 'fire:54', 'idle:3', 'fire:62').go((-14.2, -50)).do('idle:5')
    s.go((-14.63, -43.92), jump=True).do('idle:3').go((-8.61, -43.7), jump=True).do('idle:3')
    s.do('turn:81', 'fire:76', 'idle:40').go((23, -36.5), (23, -33), (23.5, -28), (23.5, -24), (20, -23.5), (16.5, -23.5)).do('idle:10')
    return s


def hall(s):
    s.go((8, -23.5)).do('idle:30')
    for _ in range(12):  # an enemy every 2.5 s, a wave only when the one before is dead
        s.do('clear:20', 'idle:90')
    s.do('clear:20', 'idle:30')
    # lock 2. Its loops begin and end at the same place beside the main rail, so the hops wait until he is past it.
    s.go((-6, -24.5), (-8.6, -26.2)).do('idle:5', 'interact:2', 'idle:30')
    s.do('rail:17', 'wrench:2', 'idle:25')                                    # the first gate's switch (19.4 m)
    for at in (28, 44, 63, 76):                                               # bombs at 32.4, 48.4, 67.7, 80.6 m
        s.do(f'rail:{at}', 'ascend:2', 'idle:55')
    s.do('rail:127', 'hopleft:2', 'idle:60')                                  # onto the left loop
    s.do('rail:22', 'ascend:2', 'idle:55', 'rail:78', 'ascend:2', 'idle:55')  # its bombs at 26.8 and 82.3 m
    s.do('rail:97', 'wrench:2', 'idle:30', 'rail:124', 'hopright:2', 'idle:60')  # its switch (99.3 m); back
    s.do('rail:132', 'hopright:2', 'idle:60')                                 # onto the right loop
    s.do('rail:42', 'ascend:2', 'idle:55', 'rail:71', 'wrench:2', 'idle:30')  # its bomb (46.9 m) and switch (74.1 m)
    s.do('rail:128', 'hopleft:2', 'idle:60', 'idle:300', 'idle:60')           # back before its last bomb; the last gate
    s.go((-10.5, -23.5), (-14, -23.5)).do('idle:10')
    return s


def magnet(s):
    # the inspectors' room: its six fall and the magnet room's door opens
    s.go((-20, -23.5)).do('clear:14', 'idle:60')
    # through the door: the floor of the doorway is magnetic, and the boots take him
    s.go((-25.5, -19), (-25.5, -14.5)).do('idle:10')
    # round the room on its tracks to the threshold of its west door (the way is found on the level's own faces)
    way = MagneFaces('03', (-25.471, 8.132, 8.712), 30).track((-25.4, 6.2, -14.2), (-44.54, 15.20, 8.71))
    if way is None:
        raise SystemExit('magnet: no way along the room\'s magnetic faces')
    for p in way:
        POINTS3.append(p)
        s.do(f'mgo:{len(POINTS3) - 1}')
    # over the threshold he steps off the boots onto the passage's floor
    s.do('run:30', 'idle:20')
    return s


def west(s):
    # the feed belt carries him from the magnet room's door to the junk conveyor, and drops him on it
    s.go((-55, 8.7)).do('idle:150')
    # down the conveyor between its twelve workers, and from its end over the beam pit to the vendor's floor
    s.go((-73, 0), (-73, -20), (-73, -40), (-73, -45.5)).go((-73, -54), hop=True).do('idle:30')
    return s


def boss(s):
    # over the bridge onto Mungo's platform: the entry (the bridges sink for 4 s, his hop and roar)
    s.go((-73, -60), (-73, -66), (-73, -71), (-73, -75)).do('idle:310')
    # his first charge and swing; then, with TESTING AID foe:6 (his health set to 6), the shots that fell him
    s.do(*['idle:15'] * 10, 'foe:6', 'equip:0', 'turn:180', *['fire:10', 'idle:10'] * 5, 'idle:300')  # 0: the Lacerator
    # over the far bridge, through the back door, to where the level ends
    s.go((-73, -100), (-73, -112), (-73, -119), (-73, -125)).do('idle:30')
    return s


SECTIONS = [('outside', outside, None), ('lock1', lock1, '11.5,10.1,75'), ('door1', door1, '11.5,10.1,75'),
            ('rooms', rooms, '7.6,10.2,62'), ('shaft', shaft, '40,11.3,-9.6'), ('east', east, '40,11.3,-9.6'),
            ('showcases', showcases, '79.5,13.2,-69'), ('lake', lake, '37.3,4.2,-69.1'), ('hall', hall, '16.5,4.2,-23.5'),
            ('magnet', magnet, '-13,4.5,-23.5'), ('west', west, '-48,16.2,9.5'),
            ('boss', boss, '-73,15.6,-55')]


def view(text):
    """One line a command: its place, clip and health, and the status lines that matter here."""
    line = ''
    for row in text.split('\n'):
        if 'position=' in row and ': ' in row:
            words = row.split()
            keep = [w for w in words[1:] if w.split('=')[0] in ('position', 'grounded', 'clip', 'health')]
            line = words[0] + ' ' + ' '.join(keep)
        elif row.startswith('Grind:'):
            g = re.sub(r'locks=\[[^]]*\] ', '', row[7:])
            print((line + ' | ' + g)[:300])
            line = ''
        elif row.startswith(('Movie:', 'Notice')):
            print(row[:200])
    shown = ''
    for row in re.findall(r'^Shaft: (state=\d+ y=\S+ stop=\d+ wave=\d+ bombset=\d+)', text, flags=re.M):
        if row.split()[0] + row.split()[2] + row.split()[4] != shown:
            print('Shaft:', row)
            shown = row.split()[0] + row.split()[2] + row.split()[4]
    for key in ('Objects:', 'Grind:', 'Doors:', 'Shaft:'):
        found = last(r'^' + key + r' (.*)$', text)
        if found:
            print(key, found[:260])


def main():
    names = [n for n, _, _ in SECTIONS]
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    until = sys.argv[sys.argv.index('--until') + 1] if '--until' in sys.argv else None
    run = [only] if only else names[:names.index(until) + 1] if until else names
    s = Script()
    start = None
    for name, build, place in SECTIONS:
        if name in run:
            if only:
                start = place
                s.do('hp:250', 'idle:10')
            build(s)
    points = ';'.join(f'{x:g},{z:g}' for x, z in s.points)
    script = ','.join(s.words)
    if '--print' in sys.argv:
        print(points)
        print(script)
        return 0
    log = LOGS / f'kalidon_{only or (until and "until_" + until) or "level"}.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(EXE), str(ISO), '--level', '/WAD/03.WAD', '--hero-skin', '0', '--all-weapons',
               *(['--player-position', start] if start else []),
               '--camera-yaw', '0', *(['--waypoints', points] if points else []),
               *(['--waypoints3', ';'.join(f'{p[0]:.3f},{p[1]:.3f},{p[2]:.3f}' for p in POINTS3)] if POINTS3 else []),
               '--gameplay-script', script, '--hidden', '--mute']
    with open(log, 'w', encoding='utf-8', errors='replace') as out:
        code = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, timeout=900).returncode
    text = log.read_text(encoding='utf-8', errors='replace').replace(chr(13), '')
    if '--view' in sys.argv:
        view(text)
    grind = last(r'^Grind: (.*)$', text, '')
    doors = last(r'^Doors:(.*)$', text, '')
    checks = []
    if 'outside' in run:
        checks += [
            ('outside: he swung over the pit on the Hypershot', 'clip=GRPL_CYC' in text),
            ('outside: he stands at the factory\'s front', re.search(r'^go:\d+: position=[\d.]+,(9\.9|10\.)\d*,(7\d|80)\.', text, flags=re.M) is not None),
        ]
    if 'door1' in run:
        checks += [
            ('door 1: both leaves opened', re.search(r'^Doors: 11f30:[12] 11fb0:[12]', text, flags=re.M) is not None),
            ('door 1: he is inside the factory', re.search(r'^idle:20: position=[\d.]+,[\d.]+,6[0-3]\.', text, flags=re.M) is not None),
        ]
    if 'rooms' in run:
        checks.append(('rooms: he reached the shaft\'s ring', re.search(r'^go:\d+: position=(39|40)\.\d+,1[01]\.\d+,-9\.', text, flags=re.M) is not None))
    if 'shaft' in run:
        states = re.findall(r'^Shaft: state=(\d+) y=\S+ stop=(\d+) wave=\d+ bombset=(\d+)', text, flags=re.M)
        checks += [
            ('shaft: the platform went down its four stops, every wave beaten', any(s[1] == '4' for s in states)),
            ('shaft: both sets of pod bombs came', any(s[2] == '2' for s in states)),
            ('shaft: he was off it at the console when it blew', any(s[0] == '7' for s in states) and 'deaths=0' in last(r'^Objects: (.*)$', text, '')),
            ('shaft: the console let the fluid in', any(s[0] == '9' for s in states)),
            ('shaft: he climbed out ahead of the fluid and the bridge rose', bool(states) and states[-1][0] == '10' and
             re.search(r'^idle:20: position=4\d\.\d+,11\.\d+,-9\.', text, flags=re.M) is not None),
        ]
    if 'east' in run:
        checks += [
            ('east: the Hypershot pulled him onto the belt\'s stub', re.search(r'^idle:30: position=7[23]\.\d+,1[23]\.\d+,-3[45]\.', text, flags=re.M) is not None),
            ('east: he swung to the north side', re.search(r'^go:\d+: position=79\.\d+,13\.\d+,-6[89]\.', text, flags=re.M) is not None),
        ]
    if 'showcases' in run:
        checks += [
            ('showcases: both doors opened once the eight robots were beaten', re.search(r'^Doors: \S+ \S+ 12030:[15] 120b0:[15]', text, flags=re.M) is not None),
            ('showcases: the lift took him down to the crank', re.search(r'^go:\d+: position=37\.\d+,4\.0\d+,-69\.', text, flags=re.M) is not None),
        ]
    if 'lake' in run:
        lake_line = last(r'^Lake: (.*)$', text, '')
        checks += [
            ('lake: the bolt crank raised the targets and plates', 'cranker=1' in lake_line and 'h0@7.0' in lake_line),
            ('lake: he crossed it and stands at the hook hall\'s door', re.search(r'^go:\d+: position=1[67]\.\d+,[34]\.\d+,-23\.', text, flags=re.M) is not None),
        ]
    if 'hall' in run:
        checks += [
            ('hall: the hooks\' four waves were beaten and its lock switched on', re.search(r'^Grind: locks=\[ \S+ 20030:[12],', text, flags=re.M) is not None),
            ('hall: lock 2 is open, both its gates opened', '20030:0,locked=0' in grind and '21cb0:1 21d30:1' in grind),
            ('hall: he is through the back door', re.search(r'^go:\d+: position=-1[34]\.\d+,[45]\.\d+,-23\.', text, flags=re.M) is not None),
        ]
    if 'magnet' in run:
        boots = re.findall(r'^mgo:\d+: position=\S+ .*?clip=(\S+)', text, flags=re.M)
        checks += [
            ('magnet: the inspectors fell and the room\'s door opened', re.search(r'^Doors:(?: \S+){6} 12230:[12]', text, flags=re.M) is not None),
            ('magnet: the boots took him at the doorway', re.search(r'^go:\d+: position=\S+ .*clip=MAGNA_WA', text, flags=re.M) is not None),
            ('magnet: he walked the room\'s tracks on them, every place of the way', len(boots) > 20 and
             all(c in ('MAGNA_WA', 'IDLE_HAN') for c in boots) and sum(c == 'MAGNA_WA' for c in boots) >= len(boots) - 2),
            ('magnet: he stepped off through the west door, alive',
             re.search(r'^idle:20: position=-4[789]\.\d+,1[56]\.\d+,\d+\.\d+ .*clip=IDLE', text, flags=re.M) is not None and
             'deaths=0' in last(r'^Objects: (.*)$', text, '')),
        ]
    if 'west' in run:
        belt = re.findall(r'^Belt: 14430 state=(\d) run=\S+ items=\[(.*?)\]', text, flags=re.M)
        checks += [
            ('west: the feed belt carried him onto the junk conveyor', re.search(r'^idle:150: position=-7[12]\.\d+,15\.\d+,[78]\.', text, flags=re.M) is not None),
            ('west: the conveyor ran and stood by turns, its junk riding it',
             any(b[0] == '2' for b in belt) and any(b[0] == '3' for b in belt) and any(' 2@' in b[1] for b in belt)),
            ('west: he went down it past the workers and jumped the beam pit to the vendor\'s floor, alive',
             re.search(r'^idle:30: position=-7[234]\.\d+,15\.3\d+,-5[345]\.', text, flags=re.M) is not None and
             'deaths=0' in last(r'^Objects: (.*)$', text, '')),
        ]
    if 'boss' in run:
        mungo = re.findall(r'^Mungo: state=(\d+) health=(\S+?)/(\S+) phase=(\d) entries=(\d+)', text, flags=re.M)
        states = {int(m[0]) for m in mungo}
        checks += [
            ('boss: the fight began when he came onto the platform', 2 in states and bool(mungo) and mungo[-1][4] == '1'),
            ('boss: Mungo charged at him and swung', 7 in states and 9 in states),
            ('boss: Mungo fell (TESTING AID foe:6), the far bridge rose and the back door opened',
             bool(mungo) and mungo[-1][0] == '38' and re.search(r'^Doors:(?: \S+){7} 122b0:1', doors and 'Doors:' + doors or '', flags=re.M) is not None),
            ('boss: he left by the back door and the level\'s end movie played', 'Movie: /VID/L03/03C.BIK' in text),
        ]
    if 'lock1' in run:
        checks += [
            ('lock 1: he rode its rails', 'riding=1' in text),
            ('lock 1: both gates opened', '21bb0:1 21c30:1' in grind),
            ('lock 1: it is open and he is back at the door', '1ffb0:0,locked=0' in grind),
        ]
    ok = code == 0
    for name, met in checks:
        print(('PASS  ' if met else 'FAIL  ') + name)
        ok = ok and met
    print(f'exit code {code}; sections {run}; log {log}')
    if doors:
        print('last: Doors:' + doors)
    print('testing aids: --all-weapons, hp:250' + (', foe:6 (the boss\'s health)' if 'boss' in run else ''))
    print('RESULT: ' + ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
