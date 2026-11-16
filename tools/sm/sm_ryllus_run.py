"""Size Matters, Ryllus: the whole level in one continuous hidden run of sm_pc.

Regression aid for the native level. It drives build/x64/sm_pc.exe with a gameplay script - the pad a player would
give, tick by tick - from the landing to the button in the temple hall that plays the level's last movie:

  as Ratchet: down the slope, the basin's pygmy waves and the gate they hold, the razorbacks, the movie on the way
    down, the bolt crank and its gate, the Sprout-O-Matic; an ExplodeSprout's berry through the north wall; the
    LadderSprout and its climb; a CannonSprout's throw to the east ground; the Hypershot from the vendor and the
    swing over the chasm; the second ExplodeSprout's berry into the temple wall and the second CannonSprout's throw
    through the breach; the temple's front, where the level's movie hands over to Clank;
  as Clank: the lift, the three gadgebot houses, the button and its door, the pygmies and the stone idols, the
    first BotFlinger (three bots up the ledge), the second (two bots at the bridge's buttons), the bridge, the third
    (three bots on the column), its door, the second three houses, the ramps down, the fourth BotFlinger (three bots
    into each port), the statue, the hole into the hall, the last button.

    python tools/sm/sm_ryllus_run.py            # the whole level; log build/sm_pc_work/logs/ryllus_level.log
    python tools/sm/sm_ryllus_run.py --ratchet  # the first half only (ryllus_ratchet.log)
    python tools/sm/sm_ryllus_run.py --clank    # the second half only, from the temple's front (ryllus_clank.log)
    python tools/sm/sm_ryllus_run.py --print    # only print the waypoints and the script

Nothing here is retail behaviour: the waypoints and the tick counts are this test's own. Two testing aids are used
in Ratchet's half and reported with the result: `hp:250` (the run is about the level's mechanisms, not the fights)
and `bolts:6000` at the vendor (the run breaks no crates; the Hypershot costs 5000). The window is hidden and the
run is muted. Exit code 0 when every milestone was met.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# SM_PC_EXE / SM_PC_ISO name another executable or disc image
ISO = Path(os.environ.get('SM_PC_ISO') or ROOT / 'Ratchet  Clank - Size Matters (USA)' / 'Ratchet & Clank - Size Matters (USA).iso')
EXE = Path(os.environ.get('SM_PC_EXE') or ROOT / 'build/x64/sm_pc.exe')
LOGS = ROOT / 'build/sm_pc_work/logs'


class Script:
    def __init__(self):
        self.points, self.words = [], []

    def do(self, *words):
        self.words += [w for word in words for w in word.split(',')]
        return self

    def go(self, *places, jump=False, hop=False):
        for x, z in places:
            self.points.append((x, z))
            self.words.append(('gohop:' if hop else 'gojump:' if jump else 'go:') + str(len(self.points) - 1))
        return self

    def fight(self, swings):
        return self.do(*['seekwrench:7'] * swings)

    def house(self, x, z, facing):
        return self.go((x, z)).do(f'turn:{facing}', 'wrench:1', 'idle:25')


def ratchet_section(s):
    # TESTING AID, said in the report: the run is about the level's mechanisms, not the fights
    s.do('hp:250', 'idle:20')
    # the landing, down the slope to the basin; its pygmies come in waves, and their end lifts the gate
    s.go((-27, 84), (-23, 76), (-21.5, 69), (-26, 61), (-30, 55), (-40, 46)).fight(60).do('idle:30').fight(60).do('idle:200')
    # through the gate to the west ground, its razorback, south to where the level's second movie plays
    s.go((-52, 44), (-54.5, 40.6), (-56, 38.3), (-58, 33), (-57, 26)).fight(45)
    s.go((-55, 19), (-51, 9), (-45, 3), (-44, -3)).do('idle:20')
    # east to the bolt crank: its razorback, then the crank
    s.go((-33, -3), (-24, -1.5)).fight(45).go((-14.9, -1.7)).do('idle:10', 'interact:1', 'run:520', 'idle:40')
    # the gate it lifts, the pygmies it wakes, and on to where the Sprout-O-Matic is given
    s.fight(60).go((-11, -4), (-8.5, -7), (-5, -10), (0, -14)).do('idle:30')
    # the first seedling: sprayed out of the ground, led to its soil, planted; its berries open the east wall
    s.go((3, -16)).do('turn:64', 'fire:20', 'idle:40').go((13, -16), (13, -1.4)).do('idle:80', 'turn:23', 'fire:20', 'idle:200')
    s.go((13.35, 0.3)).do('turn:33', 'idle:2', 'wrench:2', 'idle:90')
    # through where the north wall stood; the second seedling is led to the soil under the cliff and grows the
    # LadderSprout; up its leaves (a double jump each two) and onto the ledge
    s.go((11.3, -0.5), (10.8, 4), (12.7, 9), (12.7, 14.3)).go((12.7, 17.8), jump=True).go((15, 23), (19, 26), (26, 26.3)).do('turn:90', 'fire:20', 'idle:40')
    s.go((19, 26), (16.1, 32.7)).do('idle:80', 'turn:270', 'fire:20', 'idle:200').go((12.9, 33.2), (12.9, 33.9)).do('idle:10')
    s.do(*['ascend:8', 'idle:2', 'ascend:8', 'idle:20'] * 5).go((12.9, 37.5), jump=True).do('idle:20')
    # the third seedling, led east along the ledge to its soil: the CannonSprout throws him to the east ground
    s.go((13.4, 45), (10.4, 52.6)).do('turn:90', 'fire:20', 'idle:40').go((30, 52.6)).fight(40)
    s.go((49.4, 52.6), (49.4, 46.8)).do('idle:60', 'turn:90', 'fire:20', 'idle:150').go((50.9, 46.77)).do('idle:3', 'wrench:2', 'idle:120')
    # the east vendor sells the Hypershot (TESTING AID: the bolts for it are given, the run breaks no crates)
    s.go((77.5, 25.6)).go((80.8, 25.6), jump=True).do('idle:20', 'bolts:6000', 'idle:20', 'interact:1', 'idle:10', 'ddown:1', 'ddown:1', 'accept:1', 'idle:10', 'back:1', 'idle:10')
    # the swing over the chasm, held for its second pass
    s.go((77.5, 24.5), (75.2, 24), (75.2, 21)).do('turn:180', 'idle:3', 'fire:62', 'idle:60')
    # west up the slope to the fourth seedling; it is led back to the soil before the temple's wall, whose berry
    # opens the wall
    s.go((70, -7.5), (58, -7.5), (44.5, -7.2)).do('weapon:1', 'idle:5', 'turn:270', 'fire:20', 'idle:40')
    s.go((44.5, -8.2), (58, -8.2), (69.9, -8.2)).do('idle:80', 'turn:90', 'fire:20', 'idle:200')
    s.go((72.9, -6.6)).do('turn:180', 'idle:2', 'wrench:2', 'idle:120').fight(40)
    # the breach is high in the wall: the fifth seedling, a CannonSprout's, is fetched from the east rise and
    # planted on the same soil (the berry plant ends when it comes), and its throw carries him through
    s.go((84, -7), (89, -7), (101.5, -8)).do('turn:90', 'fire:20', 'idle:40')
    s.go((89, -7), (84, -7.5), (69.9, -8.2)).do('idle:100', 'turn:90', 'fire:20', 'idle:260')
    s.go((72.9, -6.6)).do('turn:180', 'idle:2', 'wrench:2', 'idle:150')
    # on to the temple's front, where the level's movie hands over to Clank
    s.go((72.6, -35), (72, -48), (72, -61.5)).do('idle:30')
    return s


def clank_section(s):
    throw = lambda force: f'run:{force},fire:1'
    # the temple's front: the movie, and Clank where the level puts him
    s.do('idle:20')
    # the lift (it goes while he stands on its button, at its middle), the first idol, the three houses
    s.go((89.48, -76.83)).do('idle:120').go((93.2, -77.2), (95.5, -80.3)).fight(8)
    s.house(101.2, -82.3, 90).house(101.2, -84.6, 90).house(100.4, -84.5, 180)
    # the button and its door: the bots wait on the first button, he stands on the one beyond the door
    s.go((94, -80.5), (89.5, -81), (89.5, -87.5)).do('idle:200', 'ddown:1', 'idle:60')
    s.go((89.5, -92.5), (89.5, -95.2)).do('idle:40', 'dup:1', 'idle:170')
    # east and up: the pygmies, the idols, the ramp to the first BotFlinger
    s.go((89.5, -103), (93, -105.5), (103, -105.5), (110, -105)).fight(18)
    s.go((118, -106), (122.5, -109.5)).fight(18)
    s.go((123.5, -113.5), (125.5, -114.5), (127.5, -114.8), (129.5, -115.5), (131, -117), (132, -119.5), (132.4, -123),
         (132.4, -126.0)).do('idle:240')
    # the first BotFlinger: round to the bots behind it, one up the ledge, three times
    s.do('interact:1')
    for _ in range(3):
        s.do('left:52', 'fire:1', 'right:52', throw(22), 'idle:45')
    s.do('interact:1', 'idle:10').go((130, -131.5)).go((130, -135.3), jump=True).do('idle:20', 'dup:1', 'idle:60')
    # the stairs, round the idols' floor, west to the second BotFlinger
    s.go((133, -136.8), (133, -143.8), (135.5, -146.5), (135.5, -149.5), (132, -151), (129.5, -148.8), (124, -148.7),
         (118, -148.5), (115.95, -148.44)).do('idle:240')
    # the second: a bot at each of the bridge's buttons across the gap; the bridge; over it with the third bot
    s.do('interact:1')
    s.do('left:52', 'fire:1', 'right:52', 'left:5', throw(21), 'idle:50', 'right:5')
    s.do('left:52', 'fire:1', 'right:52', 'right:5', throw(21), 'idle:50', 'left:5')
    s.do('idle:260', 'interact:1', 'idle:10').go((113, -148.5), (109, -148.5), (106, -148.5), (104.2, -148.5))
    s.do('dup:1').go((104.2, -146.8), (104.2, -150.2), (104.2, -148.5)).do('idle:150')
    # west to the top floor: its idols, the third BotFlinger, three bots on the column, the door it opens
    s.go((100.5, -148.5), (96, -148.5), (88, -148.8), (81, -149), (75, -148.6), (72, -146.5), (71.5, -143.5)).fight(26)
    s.go((69, -141), (65.95, -136.6)).do('idle:260', 'interact:1')
    for _ in range(3):
        s.do('left:13', 'fire:1', 'right:13', throw(42), 'idle:60')
    s.do('idle:90', 'interact:1', 'idle:10', 'dup:1').go((70, -133)).do('idle:150')
    s.go((70, -130.5), (69.6, -127), (69.6, -124.9)).do('idle:150')
    # the second three houses, and down the ramps to the port room with all six
    s.house(67.5, -124.5, 0).house(69.5, -124.5, 0).house(71.5, -124.5, 0).do('idle:60')
    s.go((63, -122.8), (59.3, -121.5), (59.3, -110), (59.3, -98.5), (68, -98), (77, -98), (79, -99), (79, -106), (79, -112.5),
         (74.2, -112.8), (68.67, -110.45)).do('idle:420')
    # the fourth BotFlinger: three bots onto the ledge of each port
    s.do('interact:1', 'right:24')
    for _ in range(3):
        s.do('fire:1', 'left:14', throw(36), 'idle:80', 'right:14')
    for _ in range(3):
        s.do('fire:1', 'left:34', throw(36), 'idle:80', 'right:34')
    # the statue opens; through it, down the stairs, through the hole into the hall, and the last button
    s.do('idle:260', 'interact:1', 'idle:10')
    s.go((69, -108), (69, -103.5), (69, -99), (73.12, -90), (73.12, -85.6), (73.12, -83.5), (69.5, -81), (69.5, -72),
         (72.6, -69.04)).do('idle:90')
    return s


def last(pattern, text, default=None):
    found = re.findall(pattern, text, flags=re.M)
    return found[-1] if found else default


def main():
    half = 'ratchet' if '--ratchet' in sys.argv else 'clank' if '--clank' in sys.argv else 'level'
    s = Script()
    if half != 'clank':
        ratchet_section(s)
    if half != 'ratchet':
        clank_section(s)
    points = ';'.join(f'{x:g},{z:g}' for x, z in s.points)
    script = ','.join(s.words)
    if '--print' in sys.argv:
        print(points)
        print(script)
        return 0
    log = LOGS / f'ryllus_{half}.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(EXE), str(ISO), '--level', '/WAD/02.WAD', '--hero-skin', '0',
               *(['--player-position', '72,19.9,-62'] if half == 'clank' else []),  # the temple's front
               '--camera-yaw', '0', '--waypoints', points, '--gameplay-script', script, '--hidden', '--mute']
    with open(log, 'w', encoding='utf-8', errors='replace') as out:
        code = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, timeout=110).returncode
    text = log.read_text(encoding='utf-8', errors='replace').replace(chr(13), '')
    movies = re.findall(r'^Movie: (\S+)', text, flags=re.M)
    temple = last(r'^Temple: (.*)$', text, '')
    walls = re.findall(r'wall_pieces=(\d+)', text)
    checks = []
    if half != 'clank':
        checks += [
            ('the movie on the way down played', '/VID/L02/02B.BIK' in movies),
            ("the basin's gate went up", re.search(r'^Gate: \w+ state=2', text, flags=re.M) is not None),
            ('the Sprout-O-Matic was given', 'Sprouts: gadget=1' in text),
            ('the north wall fell to a berry', '10' in walls),
            ('the LadderSprout grew and was climbed', 'LadderSprout:1@' in text and re.search(r'^gojump:\d+: position=[-\d.]+,2[34]\.', text, flags=re.M) is not None),
            # where the throws set him down: beside the east ground's continue point, and beyond the temple wall
            ('the first CannonSprout threw him to the east ground',
             re.search(r'^idle:120: position=72\.[5-9]\d*,14\.\d+,25\.', text, flags=re.M) is not None),
            ('the second CannonSprout threw him through the breach',
             re.search(r'^idle:150: position=7[1-4]\.\d+,1[4-6]\.\d+,-3[0-9]\.', text, flags=re.M) is not None),
            ('the Hypershot was bought', 'hypershot=1' in text),
            ('he swung over the chasm', 'clip=GRPL_CYC' in text),
            ("the temple wall's chunks fell", '4' in walls),
        ]
    checks.append(('the hand-over movie played', '/VID/L02/02D.BIK' in movies))
    if half != 'ratchet':
        checks += [
            ('all six houses were broken', 'houses=0' in temple),
            ('the bridge is out', 'bridge=4@' in temple),
            ('the column is down', 'column=2@' in temple),
            ('both ports have their bots', 'ports_need=[ 0 0 ]' in temple),
            ('the statue is open', 'statue=3@' in temple),
            ('the last movie played', '/VID/L02/02E.BIK' in movies),
        ]
    ok = code == 0
    for name, met in checks:
        print(('PASS  ' if met else 'FAIL  ') + name)
        ok = ok and met
    print(f'exit code {code}; movies {movies}')
    if temple:
        print(f'last: Temple: {temple}')
    if half != 'clank':
        print('testing aids in this run: hp:250 (health), bolts:6000 at the vendor (the run breaks no crates)')
    print(f'log: {log.relative_to(ROOT).as_posix()}')
    print('RESULT: ' + ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
