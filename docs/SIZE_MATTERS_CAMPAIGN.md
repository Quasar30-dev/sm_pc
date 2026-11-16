# Size Matters native engine: all levels, weapons, armour and the pause menu

Working ledger of the campaign that takes `sm_pc` beyond Pokitaru (started 2026-11-15). Target: PS2 USA SCUS-97615.
The original executable and level modules are read as data and read in Ghidra; nothing of them is executed.

Goal (the user's, 2026-11-15): levels outside Pokitaru working end to end; the native pause menu with PC settings;
every weapon and gadget working with its sound and effects; the armour sets with their own functions.

Status words used here: **done** (built into `sm_pc`, a hidden run and a looked-at capture are named), **partial**
(named what is missing), **not started**.

## 1. What the disc holds

`tools/sm_class_inventory.cpp` (`build/x64/sm_class_inventory.exe ISO OUT.json ANIMS.txt INSTANCES.txt`) lists every
level WAD's BOG classes, instances, models and animation clips. Outputs of the last run are in `build/sm_pc_work/`
(`inventory.txt`, `anims.txt`, `instances.txt`).

| WAD | Level | Classes | Instances |
| --- | --- | ---: | ---: |
| 01 | Pokitaru | 155 | 1161 |
| 02 | Ryllus (jungleruins) | 180 | 974 |
| 03 | Kalidon | 170 | |
| 04 | Metalis | 172 | |
| 05 | Dreamtime | 137 | |
| 06 | Medical Outpost Omega | 138 | |
| 07 | Challax | 169 | |
| 08 | Dayni Moon | 217 | |
| 09 | Inside Clank | 141 | |
| 10 | Quodrona | 164 | |
| 15 / 21 | Giant Clank | 10 | |
| 16-19 (+C) | multiplayer maps | 71-88 | |
| 20 | template | 2 | |
| 22 / 23 | skyboard races | 46 / 104 | |
| 24 | HIG treehouse | 123 | |

About 700 distinct classes. Every campaign level carries the full weapon, gadget and armour class set (pooled).

### BOG instances

- An instance at the exact origin is a pooled template (shots, spare pickups, break pieces, weapons in hand). An
  authored placement is never at the origin. (Level 02: 0 of 78 `Crate` at the origin, 21 of 21 `RynoRocket`.)
- Instance flags, low byte: the variant (which of the class's model / animation / collision sets).
- Class definition flags, top byte: the update group. 01 enemies, 02 allies and creatures, 03 the player's actors,
  04 world objects, 05 crates, 06 controllers with no body, 08 cameras and regions, 09 enemy shots, 0a / 0b the
  player's shots, 0c effects, 0d weapons, 0e gadgets and packs, 0f turrets.
- At run time the 128-byte instance record is the object itself: +0x00 matrix (place at +0x30), +0x40 class
  definition, +0x45 a state byte, +0x54 properties, +0x58 per-instance data, +0x64 flags (bit 0: alive).

### Class records (the level modules)

Every class a level's BOG can place has a sixteen-word record in `/LVL/LEVEL_nn.REL` (`src/hig/higclass.*`,
`tools/sm/sm_rel_elf.py`):

| Word | Meaning |
| --- | --- |
| 0 | pointer to the class name; the BOG class hash is CRC32 of the upper-cased name |
| 1-4 | callbacks (working names: init, cb1, update, event) |
| 5 | pointer to the property declaration text, or 0 |
| 6 | property byte size (equals the BOG definition's) |
| 7 | per-instance data size |
| 8-15 | further callbacks, data pointers, flags |

The declaration is the designers' C struct of the instance properties (`Navigate navigateBehavior; TriggerLink
detectArea; ... f32 scale;`). `tools/sm/sm_class_schema.py` lays it out with C rules: all 940 declarations of the
disc end at their record's property size (log: its own output line `940 declarations; 0 layouts off; 0 problems`).
Embedded engine structs, sizes solved from the data: Navigate 44, NavBound 52, Launch 28, ANIM_Manipulator 68,
QUAT 16, ChasePosEntry 24, BotControllerEntry 28, TargetEntry 16. Link fields (TriggerLink, MobyLink, CurveLink,
TieLink, ShrubLink) hold the BOG offset of what they name.

Enemy classes carry two float tables indexed by level (record words 13 and 14): the class's share of the level's
enemy bolts and of its experience.

### Rules read from the code (LEVEL_02 offsets)

- **Bolts** (`BoltController` init 0x15ba78, payout 0x15bce8 / 0x15be48, spawn 0x15b678 / 0x15aac0). The controller's
  `boltAmnt` is the level's total (`boltAmountReplay` in challenge mode). 30% is shared equally among the `Crate` and
  `CrateExplosive` instances; a crate pays its share less a fifth plus a random amount up to two fifths. 70% is paid
  by enemies: class share (record table) divided by the class's instance count. A payout is rounded up to four and
  thrown out as bolts of 500, 100, 20, 5 and 1 (table 0x232670, 0x2c a row: model variant, value, radius .12, pickup
  sound, tint); challenge mode multiplies values by four.
- **The slide** (LEVEL_01 offsets; `SlideController` init 0x1b03e0, update 0x1b0670; levels 01, 06 and 09 have
  one or two). Properties: initDir, detectArea, path, azimuth, camDist, camHeight, camOfsY, slideSp. While the hero
  is in `detectArea` the controller names itself in the player state (+0x598); the hero's check 0x517a8 (allowed
  once flag bit 39 of +0x960 is set, which state 0x13's timer does) enters state 0x1a. Entry 0x51808 plays clip
  0x22 (WATER_SL) with blend .4. Update 0x51a00, one 30 Hz frame: the distance along the path grows by `slideSp`
  (Pokitaru: .4, about 12 m/s); the position is sprung to the path's point there, twice a frame, by
  velocity = (goal - position) * .1 - velocity * .2 held within .4; the facing is sprung to the segment's direction
  by velocity += (goal - facing) * .1 - velocity * .2. Six metres short of the path's end (or past it) he eases, at
  gain .05, to a place six metres ahead at the end point's height, and stands up (state 0) when the spring's
  velocity is under .01 on every axis. No input acts during the ride. Variant 1 adds a spray effect and the loop
  sound 0x56a4062e (variant 2: 0x27d0ab9e); camera 0x518f0 uses camOfsY, camDist and a height from the path's
  point data or camHeight.
- **Crates** (`Crate` event 0x173248): any hit but events 6 and 7 breaks it; events 10 and 11 skip the debris.
  Break sound: GLOBAL.WAD sound 0x9a914c5a (0x1dfb40).
- **Enemy strength** (table 0x22ed80, 0x40 a level): hit points of three tiers, then challenge mode's, then three
  attack values and challenge mode's. Level 1: 1 / 3 / 5 hit points; level 2: 2 / 8 / 12; level 10: 37 / 160 / 200.
  `Pygmy` init 0x1b33b0 reads tier 0 of its level.

### Weapons and gadgets: the tables (LEVEL_02 offsets)

`python tools/sm/sm_weapon_table.py build/sm_pc_work/LEVEL_02.REL build/sm_pc_work/classes_02.json OUT.json` prints
everything below for all 23 entries.

- **Definition table** 0x208308, 23 entries of 0x58 bytes, in this order (the code's weapon id is the index + 2:
  Blaster 2 ... BoxBreaker 24; accessors 0x24f88 entry, 0x25030 / 0x24fd0 tier, 0x25c38 current level):
  blaster, blitzgun, bombglove, agentsglove, beemineglove, shieldcharger, shockrocket, crossbowgun, flamethrower,
  lasertracer, suckcannon, mootator, miniturretglove (unused), ryno, hypershot, sproutomatic, polarizer, pda,
  shrinkray, boltgrabber, grindboots, mapomatic, boxbreaker.
  Entry: +0 name, +4 type (2 weapon, 1 gadget, 3 passive item), +0x10 eight tier pointers (levels 0-3 the first
  playthrough's V1-V3 and the bought top level, 4-7 challenge mode), +0x30 three modification records,
  +0x3c current level, +0x44 experience, +0x48 the level index the weapon first appears at, +0x4c / +0x4f
  modification owned / switched on (run time).
- **Tier row** (0x50 bytes): +0x00 the weapon's class hash, +0x04 a second class hash, +0x10 / +0x14 name and
  description text ids, +0x18 HUD icon, +0x1c price, +0x20 experience to the next level, +0x34 ammo unit price,
  +0x38 ammo capacity, +0x3c ammo per pickup. The Lacerator: 60 / 70 / 80 rounds, then 120 as the dual pair
  (the native HUD's 40 was the name's text id at +0x10, read as a capacity by mistake).
- **Experience** (0x250f8): an award is multiplied by (level index, +10 in challenge mode, minus the weapon's first
  level, minus its current level; at least 1), capped at a third of the next level's cost; past the cost the weapon
  levels up (0x24e20).
- **Damage table** 0x22dcd8, 0x10 a row: {kind (0 a hit, 1 per frame), amount, effect, strength class}. A shot
  carries a row index (its weapon's base row plus the weapon's level), in the hit point units of the enemy table
  (level 1 enemies: 1 / 3 / 5). The Lacerator's shots (0x14eba0) use row 0x29 + level: 1, 3, 5, 4 (the dual pair
  fires two), challenge mode 13, 18.5, 26.5, 44; with its first modification on, row 0x31 + level: 1.25, 3.75,
  6.25, 5. The Scorcher's flame (0x188528) uses row 0x51. Rows 1-8 (2.5 ... 192): reader not found yet.
  A shot delivers it by calling the target class's event callback (record word 4) with the amount, the row's two
  words, the hit place and direction (`BlasterShot` update 0x14f330), then reports the hit for experience (0x16950).
- **Lacerator shot** (0x14f330): lives 5 frames times the player's time scale, moves its velocity each frame, sweeps
  with a .85 back-off on a hit.
- **Ratchet's weapon clips** (RatchetStdMaster bank): 25 FIRE_BLA, 26 FBOD_BLA, 27-31 LG*, 39 BT, 40 BT_RUN,
  57-59 GRPL*, 72 F2H, 73 FIRE_RUN, 76 FLAMETHR, 78 FIRE_POI, 85-88 Sprt*, 89-91 *_AKI, 92 FIRE_HEA, 93 F2H_RELO,
  94 SHRINK, 95 ENGLARGE.

### Weapons: what each shot reads (LEVEL_02 offsets)

| Weapon (table name) | Classes | Damage row base (+ weapon level) | Reader | Sounds (signature) |
| --- | --- | --- | --- | --- |
| Lacerator (blaster) | Blaster, BlasterAkimbo, BlasterShot | 0x29; 0x31 with modification 1 | 0x14eba0 | fire b78c792f, impact be624601 |
| Concussion Gun (blitzgun) | BlitzGun, BlitzGunShot | passed in by the gun: **not established** | 0x155a90 | fire 6cd520e6, charge b29aad12 |
| Acid Bomb Glove (bombglove) | Bombglove, Acidbomb, AcidbombEpoxy | taken from the bomb: **not established** | 0x13d320 | blast 94c7661d, pool c12fd5fe, epoxy 8b090b9b |
| Agents of Doom (agentsglove) | AgentsGlove, AgentsBomb, AgentOfDoom, AgentsGrenade / Rocket / Laser | through per-level row tables 0x2306b8, 0x230c78, 0x230dc0: **not established** | 0x13f9c0, 0x147ac0, 0x148a18 | |
| Bee Mine Glove (beemineglove) | BeeMineGlove, BeeMine, ExplosiveBee | 0x8d (the bee) | 0x185680 | buzz 4172c8a9, blast 58482e4e, bees die edbcf6c4 |
| Static Barrier (shieldcharger) | ShieldCharger, StaticBarrierBelt, ShieldChargerBolt | 0xd7 (touch, per frame), 0xdf (bolt) | 0x1c5a30, 0x1c8a38 | 650f1bd8, 0249fa02, 967f54b4, 6a457449 |
| Shock Rocket (shockrocket) | ShockRocket, ShockRocketShot, ElectroBall | 0x55 (hit), 0x5d (blast) | 0x1ca758, 0x1c9e30 | da845aee |
| Sniper Mine (crossbowgun) | CrossbowGun, CrossbowShot | 0xae | 0x178f00 | expand d247711b, fire 4fe967b3, zoom f8dc144a |
| Scorcher (flamethrower) | Flamethrower, Spitfire | 0x41 (per frame); the flame shot row 0x51 | 0x1890c0, 0x188528 | dabb9e47, 0bdcd782 |
| Laser Tracer (lasertracer) | LaserTracer | 0x6d, 0x7d, 0x85 (per frame, three beam forms) | 0x194ec0 | 7ca9ac90 and six more of its bank |
| Suck Cannon (suckcannon) | SuckCannon, SuckCannonManager, SuckCannonComet | 0xb6 (the comet) | 0x1f0b00 | 3cd26ea5, 49409c4f, 2bd83a0d, 647e4dde, aab6d8d0 |
| Mootator (mootator) | Mootator, MutantCow, MutantMadCow, CowBell | none (rows 19 / 20 have effect 6 / 7: not established) | | 71ee0217 |
| RYNO (ryno) | Ryno, RynoRocket | 0xe7 | 0x1c3bf0 | rocket 2ac52a27, reload 39686f69, fire fe00ca14 |

A hit of damage kind 2 is halved (every reader above). Each shot class has one spawner (found by its class hash in
the code): BlasterShot 0x14f090, BlitzGunShot 0x156960, Acidbomb 0x13c3d8, AcidbombEpoxy 0x13e950, ShockRocketShot
0x1c99d0, CrossbowShot 0x177550, RynoRocket 0x1c37f0, BeeMine 0x14a208, ExplosiveBee 0x184fd0, AgentsBomb 0x144b98,
Spitfire 0x1ce650, ElectroBall 0x17ea10, SuckCannonComet 0x1d6820.

The Lacerator's shot (0x14f090 / 0x14f330): range 21 m (0x21c1dc) covered in five frames; its target cone table is
at 0x231d90 (0x24 a row; the second row once the weapon is past level 2).

The weapons in hand all carry IDLE / EXPAND / CONTRACT / SHOOT clips (gloves RUNTOSS / STANDTOS, the Suck Cannon
FIRE).

**Native state** (`src/sm/weapons.cpp`): every weapon above fires with its own model in the hand, its shots' own
models, the damage rows above, its sounds and effect glows. Flight speeds, rates of fire, blast radii, counts
(pellets, bees, agents, RYNO rockets), durations (acid pool, barrier, agents) and all targeting are native values,
marked `native` in the source, until each weapon's update function is translated. Modifications, the quick-select
ring and weapon vendors are not started.

### The pause mode's screens (LEVEL_02 offsets)

The pause mode's draw (0xc2658) switches on the current screen (0x33ff40); each screen draws one screen of
`/HUD/MAINHUD.W3D` (the call 0xb53b8(0, screen, -1)) and its texts (0x138860(text id)).
`sm_pc --hud-screen N` draws one HUD screen alone (capture `build/sm_pc_work/shots/hud/pause-a.png`: 40, 45, 49, 55,
57, 64). English strings: `python tools/sm/sm_text.py` (`build/sm_pc_work/text_en.txt`).

| Screen | Draw | HUD screen | What it is (its texts) |
| ---: | --- | ---: | --- |
| 1 | 0xd13c0 | | the pause menu itself (items from a menu table: not read yet) |
| 2 | 0xcc2f0 | 55 | MAP (444), "NO MAP AVAILABLE", missions |
| 4 | 0xd9f68 | | WEAPONS (447): Equip, Info, Quick Select, Ammo:, Next level: |
| 5 | 0xc83b8 | 49 | GADGETS (448) |
| 7 | 0xc65d8 | | AUDIO VISUAL SETTINGS (476) |
| 8 | 0xc6e80 | | CAMERA & CONTROLS (475) |
| 9, 18 | 0xd66c0 | 71 | VENDOR / PDA: ammo |
| 10 | 0xd2018 | | QUICK SELECT (450) |
| 11 | 0xdb068 | 47 | CLANK CHALLENGES |
| 12 | 0xc45e8 | 40 | ARMOR (449): Toggle Equipment, Damage reduction |
| 14 | 0xd8718 | 57 | VENDOR: weapons |
| 15 | 0xcb4e0 | 56 | MISSIONS |
| 16 | 0xc9790 | | GALACTIC MAP |
| 20 | 0xc5398 | | ARMOR SETS (708): Equip Armor Set |
| 21 | 0xdbff8 | 39 | SKYBOARD CHALLENGES |
| 27 | 0xca9a8 | 52 | HELP MESSAGE LOG |
| 28 | 0xca470 | 54 | MOVES HELP |
| 30 | 0xd2d08 | 45 | CHEATS |
| 31 | 0xd3678 | 65 | SKINS |
| 32 | 0xd4ca8 | 64 | SKILL POINTS |
| 33 | 0xd5580 | 46 | CINEMATICS |
| 56 | 0xccae0 | 29 | save slots |

Menu vocabulary (text ids): 444 MAP, 447 WEAPONS, 448 GADGETS, 449 ARMOR, 450 QUICK SELECT, 451 HELP, 452 OPTIONS,
453 SPECIAL, 466 Subtitles, 467 Music Volume, 468 Effects Volume, 469 Helpdesk Text, 470 Helpdesk Voice, 471 On,
472 Off, 473 SAVE, 474 LOAD, 475 CAMERA & CONTROLS, 476 AUDIO VISUAL SETTINGS, 477 QUIT, 480 Normal, 481 Inverted,
482 Camera Up/Down, 483 Camera Left/Right, 484 Quick Select Pause, 486 Camera Type (487 Follow, 488 Passive),
545 Camera Mode, 546 View Mode (489 3rd Person, 490 1st Person), 540 RESTART LEVEL, 549 HELMET, 550 BODY ARMOR,
551 GLOVES, 552 BOOTS, 553 none, 689 Strafe Controls, 708 ARMOR SETS, 718 SKILL POINTS, 788 16:9 (WIDESCREEN),
790 Vibration.

**The menus' item tables.** Every level module carries the menus as runs of 0xe8-byte items: +0 text id, +4 the
button plate (a HUD element), +8 the text's box (a bounds element), +0xc0 two floats 1.0, +0xc8 the plate drawn
when selected, +0xcc / +0xcd enabled / can take the cursor, +0xd0 the action (the screen to open, or 1000 SAVE,
1001 LOAD, 1003 QUIT). `hig::decode_hud_menus` finds them by that shape in any level's module. The menus by first
text and their HUD screens: 447 the pause menu (screen 36: WEAPONS, GADGETS, QUICK SELECT, ARMOR, HELP, OPTIONS,
SPECIAL), 473 options (37, with a sixth plate the original leaves empty: elements 898 / 904), 711 help (53), 699
special (66: CHEATS, SKINS, SKILL POINTS, CINEMATICS, ARMOR SETS), 466 audio visual (42), 546 camera and controls
(44). Menu struct 0x2159f8: text scale .9, colours 0xff09a5d0 (the gold d0 a5 09) and white.

**HUD references.** A reference element (kind 3) places its template at the reference's own position: the
template's origin is only where it sits in the authoring sheet (`hig::hud_triangles` subtracts it). Templates of the
USA disc: 764 button plate (231 x 29), 765 its selected fill, 773 value box, 769 title bar, 760 and 1323 / 1328 hint
bars, 771 / 772 slider plates. The settings screens reach past 0..272 (title bar at y -11 or -4, hint bar at 274 or
282): the PS2's 4:3 frame shows that much more of the layout.

**The skins.** The SKINS page's table is at 0x229190, seven rows of 16 bytes {hero skin, unlock, name text, saved
id}; row 3 (Super Incognito, skin 6) is skipped when the page is built (0xd3d50), so the page lists Wear Current
Armor, Kanga-Ratchet, Hiro Ratchet, Tropical Vacation, Plundering Pirate Captain, Ratchetzilla. The hero skins
themselves are the resident's table at 0x1edcc10 (`HEROSKIN_Init` 0x1e65a18), seven rows of 24 bytes {model
entry, texture entry (both in /ART/HEROSKIN.PAK, counted from 1), short name}: 0 default (entry 30), 1 Kangaroo,
2 Samurai, 3 Scuba, 4 Zilla, 5 Pirate, 6 Garbage. A second table (0x1edccb8, 13 rows of 0x40) is the multiplayer
characters with their red / blue team textures. HEROSKIN.PAK holds 56 entries: the 7 single-player models (112
bones each), 13 multiplayer models (not decoded by our reader beyond their bounds box), and 34 textures - the 6
skins' own and 28 that fit 7 armour sets of 4 pieces. A skin's meshes name their textures by signature = CRC-32 of
the upper-cased source path with backslashes (checked on five of the six skin textures).
`sm_class_inventory` writes the archive's entries as its seventh output (`build/sm_pc_work/heroskins.txt`).

### Armour (LEVEL_02 offsets; reader `src/hig/higarmor.*`, game `src/sm/armor.cpp`)

**Tables.** The pieces (0x220480, 28 rows of 20 bytes): {pickup model signature, place (0 body armor, 1 helmet,
2 gloves, 4 boots), the place's partner (6, 6, 3, 5: the other glove, the other boot), set (1 to 7), the piece's
text (0x9e to 0xa1)}. Beside it: a set's protection a piece (0x220430: 0, .05, .09, .12, .16, .21, .23, .24), the
six places' classes (0x220418: ArmorTorso, ArmorHelmet, ArmorLeftHand, ArmorRightHand, ArmorLeftFoot,
ArmorRightFoot), the sets' piece variants (0x2203f8), the sets' texts (0x2206e8: name, description) and their
recipes and bonuses (0x220790, 14 rows of 20: the helmet's, body armor's, gloves' and boots' sets, a bonus).

| Set | Name (text) | Recipe: helmet, body, gloves, boots | Bonus |
| --- | --- | --- | --- |
| 1 | Wildfire (162) | whole | .08 |
| 2 | Sludge Mk. 9 (165) | whole | .06 |
| 3 | Crystallix (163) | whole | .08 |
| 4 | Electroshock (164) | whole | .10 |
| 5 | Mega-Bomb (166) | whole | 0 |
| 6 | Hyperborean (167) | whole | 0 |
| 7 | Chameleon (168) | whole | 0 |
| 8 | Fire-Bomb (169) | 5, 5, 1, 5 | .16 |
| 9 | Shock Crystal (170) | 4, 3, 3, 4 | .08 |
| 10 | Wildburst (172) | 2, 1, 1, 1 | .12 |
| 11 | Triple Wave (171) | 1, 4, 2, 4 | .18 |
| 12 | Ice II (174) | 3, 6, 6, 6 | .11 |
| 13 | Stalker (175) | 1, 7, 2, 7 | .34 |

Descriptions are texts 891 to 904 (902 Shardblast has a text and no recipe).

**Rules.** The save holds the set worn at each of six places (+0x854) and a byte a set of owned places (+0x859 +
set): 0x4c8 grants, 0x4f8 asks, 0x580 wears (either glove sets both, either boot both), 0x378 is a pickup (worn
at once on a bare place), 0x6b0 a whole set, 0xaa4 a mixed one. Protection (0x768) is the sum over the body
armor, the helmet, one glove and one boot of their sets' amounts, plus the worn set's bonus; the hero's damage
(0x40330) is taken times (1 - protection), on the default 'ratchet' model only, and he flinches whatever is left.
`ArmorPickup` (init 0x149110) finds its row by its model's signature and is not placed when the piece is owned.

**Drawing.** His model (entry 30) is in 7 parts; found by hiding them (`SM_HIDE_PARTS`): 1 left glove, 2 feet,
3 torso and thighs, 4 the head's cap, 5 shins, 6 right glove. 0x3e908 hides what a worn piece covers (body armor
part 3, helmet 4, gloves 6 and 1, boots 2, part 5 under body armor or boots; the torso piece's own part 1 gives way
to boots). A piece is a small skinned model whose bones carry the numbers of the hero's bones (the skeleton's byte
table at +0x10): each takes the hero's bone of its number (0x3e5a0 / 0x92920); a piece's own root stands with the
bone it hangs from. Capture `build/sm_pc_work/shots/armor-sheet-4.png`.

**Powers** (the Wrench class: update 0x1dc748, a hit 0x1dcf68, the swing 0x1dd380, the throw 0x1dcad0, the Hyper
Strike's impact 0x1de5b0 at position + forward * 2.2). Amounts are records of the damage table (0x22dcd8, 16 bytes:
kind, amount, type, flags; kind 1 is an amount a frame). A fire is 45 frames and one at a time (0x7aa10, 0x7aa70,
tick 0x7b390); a poison lasts the frames given, a second dose adds a quarter of its own, to 1800 (0x7b7f0, tick
0x7bae8); ice holds 45 frames (MobyIce, 0x1a73a8).

| Set | What its wrench does |
| --- | --- |
| Wildfire | a hit sets fire (0xf0: .1333 a frame); the Hyper Strike bursts for 8 within .5 (0xf1) |
| Sludge Mk. 9 | the thrown wrench leaves an Acid Bomb puddle under it every third frame (120 frames, size 1 to 1.2; .2 a frame 0xf2 and 120 frames of poison 0xf3); the Hyper Strike a puddle of 2 to 2.2 |
| Crystallix | the third swing (clip time .4) raises eight CrystalGroundShards in an arc ahead (0x241770; 25 frames, one every two frames), the Hyper Strike a ring of eight .75 about the blow (27 to 32 frames); a shard touches with two balls up its length, 1.4 a frame each (0xf4) |
| Electroshock | the Hyper Strike sends an ElectroshockWave: 96 within 4.75 (0xf5) |
| Mega-Bomb | what the armour takes off hits is stored (0x1e8a68); over 15 it is charged and flashes, a flash taking one off (9 frames a flash over 10 left, 18 over 5, then 48); a hit while charged adds 100 (0xf6), 60 within 2 (0xf7) and fire (0xf8: 1.3333 a frame) |
| Hyperborean | a hit freezes for 45 frames |
| Chameleon | moving, enemies aim at where he was (a ChameleonDecoy, 0x167418) |
| Fire-Bomb | the third swing throws a Spitfire: .4 a frame for 30 frames, 200 (0xfb) within .84, at once on what it strikes, five frames after a near miss |
| Shock Crystal | the Hyper Strike raises a GravityCrystal for 450 frames that takes enemy shots within 2, three at a time; an enemy within .25 of it or the hero shatters it |
| Wildburst | the Hyper Strike leaves a puddle and a NapalmBubble that swells for 45 frames and bursts |
| Triple Wave | the Hyper Strike plants a TripleWaveBeacon for 270 frames: a wave after 22.5 frames and every 45, electric (64 within 3.5, 0xfe), fire (0xff: 1.3333 a frame to what its ring passes) and sludge (0x100: .4 a frame for 180 frames) in turn |
| Ice II | the Hyper Strike raises a wall of eight MobyIce blocks on the arc (105 frames) that freeze what touches them for what they have left; one wall at a time |
| Stalker | the wrench deals nothing: fire (0x101: 1.3333 a frame) and 180 frames of poison (0x102: 4 a frame) |

**Native state.** Ownership, wearing, the sets, protection and the pieces on the hero are built as above; the ARMOR
page lists the four places (left / right changes the set); SPECIAL > ARMOR SETS (text 708; the original's screen
41, 0xc5398) lists the sets whose whole recipe is owned with what each would take off and its description, and
puts one on whole (capture `shots/sets-sheet-2.png`; it stands in the CHEATS panels, not the original's screen 41
with the turning hero); SPECIAL > CHEATS has All Armor (`--all-armor`, `--armor-set N`). The powers are built with the amounts above (`Shot::Kind::ArmorField` / `ArmorFireball`); the
shard, ice, bubble and beacon draw their own models. Hidden runs with each set against Pokitaru's robot
(`build/sm_pc_work/logs/power_N.log`, `power_Nb.log`: the `Armour:` and `Nearest enemy:` lines) show the fire,
ice, poison, fireball, crystals, puddle, waves, wall and the Mega-Bomb's charge and release acting; captures
`shots/power-sheet-2.png`. NATIVE or not done: the Chameleon's decoy is a one-second lag of his position and he is
not drawn faint; the NapalmBubble's sixteen burning drops are a burst within 2.5; a puddle's size is taken as its
radius; the sludge off a swinging wrench and the flames, frost and arcs along it are puffs; Mega-Bomb's flash is a
burst, not the pieces' own colour; no power has its own sound bank entry yet (four borrowed sounds); armour pickups are granted by their variant but
not proven in a run; an enemy's own answer to a damage type (which of them can burn or freeze, 0x7aa10 / 0x1a8408)
is not read: all can.

### Ryllus (level 02): the route and what stands on it (read 2026-11-16; translation in progress)

Places are the BOG instances' (x, y, z); addresses LEVEL_02 offsets.

1. **Landing** (-48, 33, 90): RatchetShip, IntroMovie, RatchetShipAnimation, the hero (-38, 31, 83), a vendor, a
   Teleporter (-37.7, 31, 100) paired with one at the temple door (69.7, 19.7, -56.3).
2. **Lower jungle** (y about 14): ContinuePoint (4.7, 14, -17); `SproutomaticPickup` (7.5, 14.1, -16.6); five
   `SPROUT` creatures at (29.2, 13.3, 26.3), (104.8, 16, -8), (40.9, 16, -7.2), (7.1, 14.2, -14), (13.4, 20.1, 52.6);
   four `ElectroSoil` at (12.9, 13.9, 32.8), (73, 14, -8.3), (14.3, 13.9, 1.6), (52.3, 19.8, 46.8); `BoltCrankBolt`
   (-13.5, 14.2, -1.7) with `CrankedObject` (-7.4, 14.2, -7.7) and `BoltCrankCam`; `MovieTeleport` (-40, 14.1,
   -11.9); `MovedObject` (-56.8, 15.6, 42.1); `PushFallingPlayer` (13.3, 28.1, 46.5).
3. **East** (x about 75): vendor (82.4, 14.8, 25.6), ContinuePoint (75.5, 14.1, 23.5), `Hookshot` (75.2, 17.1,
   9.4), `Level02WallCutscene` (72.6, 22.4, -24) with six WallChunks and eight WallPieces (the wall a berry opens).
4. **Temple door** (72.6, 19.7, -65.5): its button (72.6, 20.1, -69), `MovieButtonTriggered`, then `PlayerChange`
   (89.4, 19.6, -59.8) / `MoviePlayerChange`: the level's second half is played as Clank (`ClankPlr` at (89.5,
   19.7, -70.8)).
5. **Clank's temple** (y 36.7 and 47.7): two `Lvl3Elevator`, six `ClankBotHouse` each with a `clankbot`,
   `ClankBotController`, `ClankBotPath`, two `ClankBotPort`, five buttons and four doors in all, a
   `Level02ExpandableBridge`, a `Level02Column`, four `dartshooter`, four `BOTFLINGER`, a `BoulderController`,
   eleven `StoneIdol`, `CBEnterCutscene` x2, `CBHappyPlace` x2, `CBSadPlace`, `CBWaitForPlayerArea`.
6. **The statue** (68.2, 25.8, -102.3): `Level02Statue`, two StatueParts, `Level02StatueCutscene`: the level's end.

**The Sprout-O-Matic chain.** `SproutomaticPickup` (update 0x1d3998): the hero in its trigger is given weapon 0x11
(0x27080) with message 586. `Sproutomatic` (update 0x1d3220): held fire (pad 0x2000) sprays (state 1, clip 3). A
`Sprout` (init 0x1d0e38, update 0x1d1280, fourteen states) is the seedling of one plant, told by its model
variant: LadderSprout 0, CannonSprout 1, ExplodeSprout 2 (two more: 0x71e93128 3, 0x1585f42c 4). Its properties:
navBound, setTargetArea, path, endPathSoil, followArea, checkPointIndex. An `Electrosoil` (init 0x180210) names
the plants that grow on it (target[2]), a bound, and for a cannon its angle and throw speeds. The plants:
`LadderSprout` (update 0x192a88), `CannonSprout` (0x165aa8, event 0x165bd0), `ExplodeSprout` (0x1847d0, event
0x184950) with its `ExplodeBerry` (0x183d30), which `Level02WallCutscene` (0x1a46c0) waits for.

**What the code says of the chain** (read after the note above). The stream's landing point does the work
(0x1d35c0): in an Electrosoil's `bound` it lights the soil (state 2, 0x180558) and sends the follower to it
(0x1d1cf8: a free soil inside the Sprout's `setTargetArea`); else the first Sprout sitting in the ground (state 1)
within 3 m of it and .8 of its height becomes the follower (0x1d1c88: an earlier follower dies back to its place).
A Sprout's fourteen states (0x1d2468 sets the clip: 0 BURY_TO_, 1 GROUND, 2 GROUND_T, 3 IDLE for 30 to 59 frames,
4 a fidget, 5 WALK and 6 RUN after the hero by 0x1d2ab8 (over 5 m run, over 3 m walk, thirty frames after it last
arrived, inside its `followArea`), 7 to the soil, 8 PLANT1 / PLANT2, 10 inside its plant, 11 DEATH, 12 its path,
13 out of the stream's way); speeds .0667 and .2 a frame (0x23fce0 / 0x23fd00). It grows (0x1d20b8) the plant of
its variant on the soil and waits in it until the plant is gone (0x1d1e60 takes it home). A plant has fifteen frames
(0x23fb48) that the stream held on it or on its soil runs down (0x1d21d8); a follower waiting for the soil ends
it at once.

| Plant | What it does |
| --- | --- |
| LadderSprout (0x192a88) | stands 1.25 behind the soil and 3 up; solid from its eleventh frame, the hero moved out of its box (1.75, 7, .75) first |
| CannonSprout (0x165aa8, event 0x165bd0) | struck by the wrench it tosses; the hero within 2.5 m is thrown (his state 0x22, 0x6a0c0 / 0x6a238: forty passes held, ten drawn in, then the soil's `throwSpeedXZ` along his facing and `throwSpeedY` up, falling .006944445 a pass, sixty passes a second) |
| ExplodeSprout (0x1847d0, event 0x184950) | struck by the wrench it throws its `ExplodeBerry` (0x184070: 1.1666667 a frame at the soil's `angle`, along the hero's facing or at one of the soil's two `target`s within 45 degrees of it) and grows another; the berry (0x183d30) falls .02777778 a frame a frame and bursts (0x1842d0) on what a ball of .475 meets: 3 to what stands within 2 |

Which seedling may plant where (its `setTargetArea`): the Sprout at (7.1, 14.2, -14) is an ExplodeSprout for the
soil at (14.3, 13.9, 1.6), whose targets are the two cracked walls (`Level02WallPiece` x4 at about (12.7, 16.8,
15.9) and x4 at (21.4, 16.3, -7.6); a piece hit by a berry falls with its three `neighbors`, 0x1a5280 / 0x1a5448);
the one at (29.2, 13.3, 26.3) a LadderSprout for the soil at (12.9, 13.9, 32.8); those at (104.8, 16, -8) (Cannon)
and (40.9, 16, -7.2) (Explode) for the soil at (73, 14, -8.3), whose target is the wall of `Level02WallCutscene`;
the one at (13.4, 20.1, 52.6) a CannonSprout for the soil at (52.3, 19.8, 46.8).

**Native state** (`src/sm/classes_sprout.cpp`, `classes_level02.cpp`; the pool in `ObjectWorld::spawn`): the
pickup, the gadget held and cycled like a weapon (`Gameplay::sprout_spray`), the Sprouts, the soils, the three
plants, the berry and the wall pieces are built as above. A hidden run from the pickup to both walls down:
`build/sm_pc_work/logs/sprout_5.log` (`wall_pieces=8`, then `=4` at `explosions=1`, then `=0` at `explosions=2`);
captures `shots/sprout-sheet-1.png`. NATIVE or not done: the stream lands 3 m ahead of the hero (the original
follows its drops, 0x711f0 / 0x71520) and is drawn as puffs; the Sprouts walk straight over the ground (no
navigation, no path following beyond its points); the cannon throws .83 s after the blow without holding the hero
or drawing him in, and its facing is the soil's (the soil's `angle` is used for the berry only); the berry takes
the aimed direction always (not the wrench's sweep late in the blow); a fallen wall piece flies off and is gone
(no bouncing debris, not saved); enemies' hits on Sprouts and plants; the soil's glow; the gadget in the plant
man's hand; `Level02WallCutscene` and its chunks. The ladder grows on its soil and is solid (its collision is
82 faces of rungs, 2.1 wide and 10 tall: he jumps up it; `logs/sprout_6.log`, capture `shots/ladder-sheet-1.png`;
the climb itself is not yet run). The cannon on the upper ledge's soil throws him along the soil's facing to
(72.7, 14.2, 25.8), beside the east area's ContinuePoint (`logs/sprout_8.log`: `launched=1` over seven lines, then
grounded there); he is thrown from over the plant (the original takes him from two metres behind it, 0x223cd0).

**The east wall** (`Level02WallCutscene`, init 0x1a45c0, update 0x1a46c0; six `Level02WallChunk`s named by its
`shrapnel` links). The original watches, with the hero in its `area`, for an ExplodeBerry flying at the wall (its
way over the ground within about 16 degrees of -z), follows it with the camera (to (73, 21, -12) over twelve
frames), throws the chunks when the berry is gone (0x1a43a0) and gives the camera back thirty frames later. Native
(`classes_level02.cpp`): a berry's burst that reaches a chunk brings all six down; no camera scene. Hidden run:
the ExplodeSprout seedling at (40.9, 16, -7.2) is pulled, led to the soil at (73, 14, -8.3), planted, and struck
facing -z: `build/sm_pc_work/logs/east_2.log` (`wall_pieces=14`, then `=8` at `explosions=1`).

**Ryllus, natively passable so far** (superseded by "Ryllus end to end" below) (each step run hidden, logs above): the pickup; the first ExplodeSprout and
both cracked walls; the ladder on the north soil; the cannon on the upper ledge to the east area; the Hypershot
swing over the chasm (bought at the east vendor); the east ExplodeSprout and the wall. Not yet: the climb of the
ladder itself in a run, the BoltCrank and its CrankedObject, the MovedObject, the temple door and its movie, and
everything from `PlayerChange` on (the level's second half as Clank with the gadgebots, to the statue).

**The second half, as read so far** (not built). `PlayerChange` (update 0x1aeb48: states 0x1aebe8 .. 0x1af0b8):
with the player in its `switchTrigger` it shows text 520 and, on the interact button, fades for thirty frames,
makes its `switchPlayer` (the `ClankPlr` instance at (89.5, 19.7, -70.8)) the controlled actor at
`switchStartMatrix` (0x70c18: the same hero controller, in its Clank mode 0x68558, weapons off), stands the other
five metres behind him, and fades in; `revertTrigger` changes back (0x70d90). Clank's own model is
ClankMaster_rar (clips IDLE_DEF, WALK, WALK2, RUN, JUMP, FALL_CYC, LANDING, PUNCH, GET_HIT, DEATH, MESSAGE, GLIDE).
The temple door at (72.6, 19.7, -65.5) wants a count of two (`progress`); the button behind it at (72.6, 20.1,
-69) names `MovieButtonTriggered`, not the door, so the door is opened from inside by what the gadgebots do
(`ClankBotController`, `clankbot` with clips ATTACK, FLY, JOG2ms, RUN3ms, DETPREP, ELECTRO, JUMPBITE, TRAPBITE,
BRIDGECR; `BOTFLINGER`; `StoneIdol`). Next to translate, in order: PlayerChange and Clank's mode; the gadgebots and
their controller, houses and ports; the buttons that take a gadgebot; the bridge, column, elevators and dart
shooters; the statue's cutscene and the way out.

**The level's movies and the change of character, built** (`object_classes.cpp`). A movie class plays the
level's movie of its letter (0x27080: `/VID/Lnn/nnX.BIK`, X = 'A' + the instance's state byte, bits 8..15 of its
BOG flags), once: `MovieTeleport` (0x1abdc0; Ryllus: 02B on the way down to the jungle, then the hero stands at the
instance), `MoviePlayerChange` (0x1abaf0; 02D at the temple front, after which its `switchPlayer`, the ClankPlr, is
the controlled character where he stands, 0x1abb68), `MovieButtonTriggered` (0x1ab7b0; 02E once the button that
names it is pressed, after which Ratchet is back in control at the instance and the next level is open, 0x1ab888:
this is the level's end), and `PlayerChange` as above (its prompt is text 520, "Press <interact> to have Clank
enter the hole in the wall"). `Gameplay::clank`: no weapons or gadgets; drawn with ClankMaster_rar and its clips.
Hidden run: from the temple front he walks into the trigger, `Movie: /VID/L02/02D.BIK` is asked for and the next
line has him at (89.5, 19.7, -71.6) as Clank, who then rides the first elevator to the upper floor
(`build/sm_pc_work/logs/clank_1.log`); capture `shots/clank-sheet-1.png`. Native or not done: no fade; the
character left behind is not shown; Clank moves with Ratchet's speeds and jump, and his punch is the wrench's
swing without the wrench (his own state table is not read); a scripted hidden run takes a movie as seen.

**The gadgebots, as read so far** (not built; about 35 KB of code, 0x1679c8 to 0x170700, and BotFlinger).
`ClankBotController` (update 0x16c088) runs while Clank is the controlled character (the hero's state 0x21, or
0x2a / 0x2f as Clank). Its command menu is the HUD's selector 1 (0xd12c8 opens it, 0xd1378 reads the choice); a
choice (0x16c270) is given to every living gadgebot within 20 m whose state is outside that command's own range,
by 0x1698b8 (the bot's state change), and Clank plays his MESSAGE clip (0x689a0):

| Command | Bot state it sets | States that already obey it |
| --- | --- | --- |
| 0 | 3 | 2 to 4 |
| 1 | 6 | 5 to 9 |
| 2 | 16 | 15 to 19 |
| 3 | 11 | 10 to 14 |

(tables 0x234d18, 0x234d30, 0x234d48). A `clankbot` has 26 states, one function each (0x168f50 dispatches: 0
0x169c48, 1 0x169c50, 3 0x169c58, 6 0x169c78, 7 0x169ce0, 8 0x169ef8, 11 0x169f30, 12 0x16a0e0, 13 0x16a3d0, 16
0x16a570, 17 0x16a7a0, 18 0x16a830, 20 0x16a910, 21 0x16aa98, 22 to 25 0x16ace8 .. 0x16ad80), and its properties
name its controller, path, target manager and group. On the upper floor the first door (89.5, 36.7, -90) is held
open by the button before it (89.5, 36.7, -85.3; not permanent) and the one behind it (permanent): a gadgebot is
meant to stand on the first.

**The gadgebots, first native version** (superseded by "The temple as Clank, as built" below; kept for its
runs) (`src/sm/classes_clankbot.cpp`). Built: the controller wakes when Clank
is in its `initTrigger` (command "follow", 0x16c270); its bots near him leave their houses and follow, keeping
close behind him three abreast at their clips' paces (2 and 3 m/s); a command reaches the bots within 20 m; a bot
told to wait stays where it is. The buttons (`Level02Button`): who may press is as 0x19f290 (the controlled
character and, for a button with a `botController`, that controller's bots out of their houses), and a button
that is not permanent comes back up and takes its count back when what pressed it leaves (0x19f5a8; the earlier
native version left it down). Hidden runs on the upper floor: with the bots told to wait on the first button the
door stays open while Clank goes through to the permanent button behind it
(`build/sm_pc_work/logs/bots_2.log`: `buttons=[ 3 ...] doors=[ 3 ...]` to the end, then `buttons=[ 3 3`); with
the bots kept away the button and the door go back (`bots_3.log`: `buttons=[ 2` / `doors=[ 2`, then 0).
NATIVE or not done: the commands are on the D-pad (up follow, down wait, left enter, right attack) with a line
of text, not the HUD's selector; a bot wakes when Clank is within 25 m and a storey of it; the places behind
Clank; no navigation (a bot walks straight over the ground); attack and enter do nothing yet; the houses do not
open; the bots' ears; Clank's MESSAGE clip; respawning.

**The temple as Clank, as built (2026-11-16)**: the level's second half runs from the hand-over movie to the last
movie in one hidden run. Files: `src/sm/classes_clankbot.cpp` (controller, bot, house, port, BotFlinger),
`classes_level02.cpp` (column, bridge, statue), `mechanisms.cpp` (the lift's button), `gameplay.*` (Clank's body).

The route, by the level's own data (instances and the level collision, mapped with `SM_GROUND_MAP`):

| Where | What stands there | What it takes |
| --- | --- | --- |
| (89.5, 19.7, -70.8) | Clank's start; the lift (89.5, -76.8) to the floor at 36.7 | stand on the lift's button |
| (100..103, 36.7, -82..-86) | three gadgebot houses; a StoneIdol | break the houses |
| (89.5, 36.7, -85.3 / -90 / -95.2) | button, door, permanent button | a bot waits on the first button |
| (104..116, 38.7, -98..-112) | four PygmyB | |
| (121..126, 40.7, -113..-118) | three StoneIdols; the ramp up to 42.7 | |
| (132.5, 42.9, -127.1) | BotFlinger 1; a ledge a metre up at z -133.5 | three bots up the ledge |
| (128..138, 45.7, -143..-156) | four StoneIdols (in their niches here) | |
| (115.0, 45.9, -148.4) | BotFlinger 2; a gap x 106..110; wall buttons at (103.1, 46.7, -145.1 / -151.8) | a bot before each button; the bridge (4.947 m) comes out |
| (84..100, 45.7, -147..-150) | the corridor of the four dart shooters | |
| (62..78, 47.7, -130..-142) | three StoneIdols; BotFlinger 3 (65.0, -136.6); the column (75.3, -133.2), .725 m high | three bots on the column; the door at z -128.9 opens |
| (67.5 / 69.5 / 71.5, 47.7, -123.2) | the second three houses | break them |
| (58..60, z -120..-98), then east, then south | three ramps down from 47.6 to 26.7 (the boulders' way) | |
| (68.7, 26.9, -111.4) | BotFlinger 4; ledges 2.1 m up before the two ports at (62.0 / 74.9, 28.8, -120) | three bots onto each ledge |
| (68.2, 25.8, -102.3) | the statue; stairs down behind it to a wall at z -85 | both ports full |
| (72.7..73.5, 20.4..21.2, -85) | a hole in that wall, .8 m wide and .8 m high, into the hall | Clank's size |
| (72.6, 20.1, -69.0) | the last button | movie 02E; Ratchet at the temple's front |

Read from the module for this (addresses are LEVEL_02.REL offsets):

- **ClankBotHouse** (init 0x16e730, update 0x16e9c8, event 0x16eb48, end 0x16eb58): any hit on the house ends it
  (twelve pieces thrown about) and its bot is one of the controller's from then on (0x169208). Bots are not woken
  by being near.
- **A lost bot** (0x16bb18) is back 150 frames later at its first place or at one of the controller's five respawn
  points ((89.5, 36.7, -97.5), (132.5, 43.7, -134.7), (103.6, 45.7, -148.5), (65.9, 47.7, -118.6), (72.8, 46.3,
  -147.8)), by how far along the path Clank is.
- **BotFlinger** (init 0x15d660, update 0x15d9a8; states 0 empty, 1 getting in, 2 manned, 3 taking a bot, 4
  holding, 5 throwing, 6 getting out). Within 1.5 m the interact button gets in (text 750). The stick turns it
  and sets its `force` between 1 and `forceMax`, .08 a frame (0x15df80). Its beam takes the nearest bot within 15 m
  and 22 degrees of its facing that is out of its house and not on its way into a port (0x15f298), on the fire
  button; the same button throws. Where the throw will land (0x15fd78): the bot leaves at force x flingStrength /
  30 a frame, times .6 along the facing and times `upScale` upwards, and falls at .010888890 a frame a frame (9.8
  m/s2); the first thing a straight line to the top of that arc meets, or else a line ten times as far on and
  down, is the place, and the throw (0x15eef8, .1333 into its clip) is then timed to come down exactly there. A
  thrown bot flies up to 300 frames (0x16aa98) and waits where it lands (0x16ace8 gives state 3).

  | Flinger | flingStrength | forceMax | upScale | Faces (degrees, 0 = +z) |
  | --- | --- | --- | --- | --- |
  | 1 (132.5, 42.9, -127.1) | 5.5 | 5 | .2 | 180 |
  | 2 (115.0, 45.9, -148.4) | 10 | 5 | .1 | 270 |
  | 3 (65.0, 47.9, -136.6) | 5 | 7 | .2 | 72 |
  | 4 (68.7, 26.9, -111.4) | 4.5 | 8 | .27 | 180 |

- **Where a thrown bot goes on**: `CBEnterCutscene` (0x165f70; one before each port) takes a bot that lands in its
  `bound`, walks it to the port and in; `CBSadPlace` (0x167098) sends one that lands in its `landingArea` (beside
  the column) back to Clank; `CBWaitForPlayerArea` (0x167260) makes a bot that walks into its area wait while he is
  four metres away or more; `CBHappyPlace` (0x166ce0) holds a bot in its region and lets it follow again when he
  is `reqPlayerDist` away.
- **ClankBotPort** (init 0x16fc28, update 0x16fed0): takes `botsNeeded` (3) bots; a bot told to enter (state 16,
  0x16a570) takes the nearest port within 4 m at its own height (.1 m), one bot at a time, walks to .8 m before it
  and is gone (0x16a830). A full port adds one to its `triggered` object, the statue.
- **Level02Column** (0x1a0928 / 0x1a0b38, weight 0x1a0f80, test 0x1a1160, move 0x1a0c18): what stands on its top
  within 1.1 m of its axis weighs one each; it sinks .725 m x weight / `reqWeight` on a spring (.2, .2, at most
  .0241667 a pass); with more than `reqWeight` (3), or just that without Clank among it, it goes all the way and
  adds one to its door.
- **Level02ExpandableBridge** (0x1a1338 / 0x1a1508): with `reqProgress` (2) buttons down it slides out
  `maxBridgeLength` at up to a thirtieth of a metre a frame, then rises .246 m.
- **Level02Statue** (0x1a2898 / 0x1a2940): at a count of `progress` (2) its parts slide apart along their own
  first axes, .011 m a frame (0x21d5ec), 2.168 m each (0x21d5f0). `Level02StatueCutscene` (0x1a2d18) is a camera
  along two curves while that happens.
- **Lvl3Elevator with `useButton`** (0x1a6400): the lift carries a `Button` (0xcafe6bbb, made by 0x165608 at its
  own place) that is pressed while the hero is within .6 m of it (0x1654c0, radius squared .36 at 0x3aafe8); the
  lift goes on a press and, at the other end, waits for him to step off before it will go again.
- **StoneIdol**'s event (0x1d4540): any hit is its end.

NATIVE here, or not done:

- **How a bot finds its way.** The original walks a NavBound along the ClankBotPath's 70 segments, which the doors
  and the bridge open (0x16b230, 0x34100). Here a bot retraces the way Clank himself walked, a step at a time over
  ground it can stand on (.55 m up, .8 m down), round small things in its way, and stops at a step, a gap or
  something solid; a bot off his way (thrown, just freed) joins it where it comes within 2.5 m, or walks straight
  to him in the same room. The six stand in a line behind him.
- **Clank's body** is .2 m round and .7 m high (Ratchet's is .23 and 1.15), so that he fits the holes; the sizes
  are native. His moves are still Ratchet's (speeds, jump, the wrench swing as his punch).
- The commands are on the D-pad with a line of text (the HUD's selector is not rebuilt). Attack does nothing: the
  target manager, the chase positions and the StoneIdols' own state machine are not translated (the idols are the
  generic native enemy; the four of the second group stay in their niches).
- The BotFlinger turns at 1.8 radians a second with the stick (the original eases towards the camera's facing
  moved by the stick); Clank stands behind it rather than in it; the held bot hangs 1.1 m ahead and .9 m up; a
  ring and a beam are drawn for the landing place and the beam. A thrown bot that meets a wall drops from there
  (the original's thrown body bounces, 0x37740), and lands beside a bot that already stands on the spot.
- Not built: the dart shooters, the boulders, the houses' pieces, the bots' ears, CBHappyPlace and
  CBWaitForPlayerArea, the statue's cutscene camera, ElevatorRings, the help manager's lines, the bridge sliding
  back, the temple's front door (nothing names it; the last movie stands Ratchet outside it).
- Known sequence break: with the statue shut Clank can still climb over it natively (the hero's slope limit).

Proof. `python tools/sm/sm_ryllus_run.py` drives one hidden run of sm_pc from the temple's front to the last
button with the pad a player would give (waypoints and tick counts are the test's own) and checks the milestones
in `build/sm_pc_work/logs/ryllus_clank.log`: its seven lines are PASS (the hand-over movie, six houses broken, the
bridge out, the column down, both ports full, the statue open, the last movie), with `movies ['/VID/L02/02D.BIK',
'/VID/L02/02E.BIK']` and the last `Temple:` line `houses=0 ... ports_need=[ 0 0 ] bridge=4@4.95 column=2@46.95
statue=3@2.17`. The four SM probes pass after it (`build/sm_pc_work/logs/sm_*_probe.log`, last line). Capture
`build/sm_pc_work/shots/flinger-sheet-1.png` (a held bot at flingers 1 and 2, a freed bot by its house, the six
at flinger 4). The two halves in a single run: "Ryllus end to end" below. Not run: a human at the pad.

**Ryllus end to end (2026-11-16).** `python tools/sm/sm_ryllus_run.py` plays the whole level in one continuous
hidden run of sm_pc, from the landing to the last movie, with the pad a player would give: seventeen milestone
lines, all PASS, `movies ['/VID/L02/02B.BIK', '/VID/L02/02D.BIK', '/VID/L02/02E.BIK']`, in
`build/sm_pc_work/logs/ryllus_level.log` (`--ratchet` and `--clank` run the halves: `ryllus_ratchet.log`,
`ryllus_clank.log`, both PASS). Two testing aids are used in Ratchet's half and printed with the result: `hp:250`
(the run is about the level's mechanisms; with ten hit points the script's wrench-only fights lose to the razorbacks
and the BuryBlossoms) and `bolts:6000` at the vendor (the run breaks no crates; the Hypershot costs 5000). Not
covered by it: a human at the pad, the movies themselves (a scripted run takes a movie as seen), the save.

Ratchet's way, as the level's data has it and as the run takes it:

1. The landing (31.2) and the slope down to the basin (16.2; x -54..-27, z 36..57). Its pygmies come in waves
   (`EnemyWave` e9d0, ea50, ebd0, ec50), the first ones out of their huts along their `initPath`; the last four are
   `MobyGroup@10350`, whose end lifts the `MovedObject` (-56.9, 15.6, 42.1) six metres: the way west.
2. The west ground (14.2) with a razorback, then south into `MovieTeleport`'s trigger (x -46..-41, z -21..19): movie
   02B, and he stands at (-40, 14.1, -11.9).
3. East along z -3..0 (a second razorback) to the bolt crank (-13.5, 14.2, -1.7): 16.667 s of turning lifts the
   `CrankedObject` (-7.4, 14.25, -7.7) five metres; pygmies come while it turns. Beyond it the Sprout-O-Matic is
   given (trigger about (-0.2, 15, -14.3)).
4. The seedling at (7.1, -14) to the soil at (14.3, 1.6): the ExplodeSprout's berry, thrown with the hero facing the
   plant from the south-west, takes the north wall. The hole it leaves is a metre and a bit over the ground (x 11.5
   to 13.5 at z 15.4): he jumps through. (The east wall of this soil opens only a walled corner.)
5. The north ground: down the ramp at z 24..28 to the seedling at (29.2, 26.3), back to the soil at (12.9, 32.8):
   the LadderSprout. Its nine leaves are level faces a metre apart, 2.1 m wide and half a metre deep, from 1.9 m to
   9.9 m over the soil; he jumps up through them (a double jump takes two) and over to the ledge at 24.2.
6. The ledge north (20.2): the seedling at (13.4, 52.6) is led east, past a pygmy wave, to the soil at (52.3, 46.8);
   the CannonSprout throws him to (72.7, 14.2, 25.8), beside the east ground's continue point.
7. The vendor stands on a step (14.8; x 79..86, z 22..29) and sells the Hypershot for 5000. The Hookshot over the
   chasm (75.2, 17.1, 9.4): fired from (75.4, 21.2) facing south and held 62 ticks, he lands at (75.1, 14.2, -2.3).
8. The south-east ground: west up the slope (z -9..-6) to the rise (16.0) and its seedling at (40.9, -7.2), back to
   the soil at (73, -8.3): its ExplodeSprout's berry, thrown facing south, takes the six chunks out of the temple
   wall. The breach is seven to nine metres up (y 21.5..23.7 at z -24.3; the wall below it is solid).
9. So the fifth seedling, a CannonSprout's at (104.8, 16, -8) on the east rise, is led to the same soil (the berry
   plant ends when it comes); its throw (18 along, 18 up) carries him through the breach to (73.0, 15.6, -34.0).
10. South and up to the temple's front; `MoviePlayerChange`'s trigger (z -64.1..-59.1): movie 02D, and Clank.

Built for this half, with what it was read from:

- **MovedObject** (init 0x1ab130, update 0x1ab370; table 0x23c308): when what its `target` names is gone it moves
  by its model's row of the table. Ryllus's one row (model 0x21e4c89e): speed -.0333 a frame, amount -6, spring
  .1 / .2, so it rises six metres at up to a metre a second (`object_classes.cpp`; other levels' rows are not read).
- **The enemies' way in** (`Enemy::entry`): a class with an `initPath` curve walks it before anything else, a
  StoneIdol only once the hero is in its `initPathTrigger`. NATIVE: at its running speed, straight from point to
  point, through what stands there. Without it a pygmy stayed in its hut, its wave never ended and the gate never
  went up.
- **The LadderSprout's leaves** (`Object::steps`): the original's collision holds only what is above a face, so
  only the level faces at or below the hero's feet are given to his collision each tick.
- **The vendor** from a script: the interact button within 2.5 m, D-pad down to the row, Cross; `Vendor:` in the
  status lines lists its stock and prices.

Gaps this run showed, not fixed: the razorbacks and BuryBlossoms are the generic native enemy (their own attacks
are not translated) and beat a wrench-only script at ten hit points; the script's fights are `seekwrench`; a hero
who dies as Clank before reaching a continue point of his own came back at Ratchet's (now he comes back where the
movie put him); natively Ratchet cannot be seen waiting at the temple while Clank plays.

**Sounds: the game's own, and the mixer's programs (2026-11-16).** The level's banks name every sound
(`build/sm_pc_work/sounds_02.txt`: hash, bank, index, name), and a scripted run now ends with a `Sounds:` line: how
many events were played, which sounds, which were not in the banks, and which have a program the mixer cannot play.
The whole-level run: `played=2649 ... not_in_the_banks=[ ] program_not_played=[ ]`
(`build/sm_pc_work/logs/ryllus_level.log`).

- **The mixer's programs** (`src/sm/audio.cpp`; Scream grain types as the banks have them). Added: 4 an LFO's
  settings (read past, not applied), 5 a child sound (its record in the grain data: volume, pan, registers, and at
  +12 its index in the bank; `hig::SoundGrain::child`), 21 / 22 a loop (sixteen turns laid out; a stop event ends
  it), 30 to 33 and 40 the sound's registers, 34 a test of a register (the next grain is skipped when it fails; a
  negative register is one of the game's, given by `AudioEvent::param`), 35 / 36 markers, 41 key-off (read past),
  43 the sound's ending, played when it is stopped. Before this the footsteps (RATCHET_FOOTSTE: twelve children
  by the ground, 308 events in the run), Ratchet's attack voice, the bolt crank's cycle, the gadgebots' command
  and the gate were silent.
- **The armour powers** have the original's sounds: the element code loads two tables (0xa08: 0x2208a8 SPLAT,
  CRYSTAL_GROWTH, GENERALELECTRIC_01..04, got by 0xa68; 0x23910: 0x221708 six explosions, GLASS_SHATTER,
  TORSOFREEEZETHAW, got by 0x24b98) and the calls name them by index: the blow 0x1de5b0 (Wildfire explosion M2,
  Crystallix and IceII the freeze, Electroshock explosion S2, Wildburst the splat), the Spitfire 0x1ce650 / 0x1ceb30
  (S1, M2), the bubble 0x1adfa0 (M6), the rings 0x17f480 (one of GENERALELECTRIC_02..04), the shards 0x179d30
  (glass), the Mega-Bomb 0x1dcf68 (L1).
- **By name from the bank** (the code reaches these by bank index, so the match is by name; BOTCASE_BREAK,
  BOTFLINGER_STOP, L02_GATE01, SPROUT_FIRE and berryexplode are also named in the code): Clank's punch, jump and
  hurt (level-PUNCH_WHOOSH, level-JUMP, level-CLANK_DAMAGE, level-DEATH); the gadgebots (level-COMMAND with the
  command's number, level-BOTCASE_BREAK, MicroBot-PortIn, level-GADGEBOT_DEATH / _RESPAWN); the BotFlinger
  (BOTFLINGER_START / _STOP / _THROW, BOT_PICKUP); level-STATUE_GATE; the Hypershot (Hypershot-FIRE,
  swingtarget-CONNECT); the Sprout-O-Matic and its plants (SPROUT_FIRE, SPROUT_THROW, berryexplode).
- Not done: a footstep's ground (the program's register is given 0, concrete; the level's surface numbers are not
  read), LFOs, key-off, the BotFlinger's arc loop and turn, the doors', buttons', column's and bridge's sounds,
  the burn loop.

**The Hypershot** (gadget 14; the hero's grapple state 0x6b240). A `Hookshot` (init 0x191b78, update 0x191ce8) is
a glow in the air: green to be pulled to, yellow to swing from (`swing`); its `range` is how far it can be taken
from and `ropeLength` what a swing's rope settles to. Ryllus has one, over the chasm of the east area (75.2, 17.1,
9.4): swing, rope 4, range 15; the chasm runs from z 18 to z 1 at x 75 and kills (DeathTrigger). Levels 03, 04,
05, 09 and 10 have five to nine each; Pokitaru none.
- The target (0x6acd0): the nearest Hookshot ahead of the hero, within its range, within 45 degrees of his facing
  over the ground, not straight overhead (flat distance at least a tenth of the height), in plain sight.
- Fired (0x6ab98): clip GRPL_FIR (0x39) at twice its speed; fire let go before it ends cancels.
- Pulled (sub-states 2, 3): along the line from where he stood, .4 a frame, to three metres short of the target
  (the rope a metre less than the distance), then off in the jump state.
- Swinging (sub-state 4, a frame): the rope is sprung to the target's own length (two passes of velocity = (goal
  - length) * .1 - velocity * .2, held within .5); he is held on its end; position += velocity, a hundredth down,
  held again; velocity = what he moved * .99, plus the stick's direction * .006, or for the first swing a push of
  .004 towards the target until it turns or he is less than two metres under it; he starts with .1 a frame
  towards it. Never lower than .4 over the ground (0x6c9a0). Clip GRPL_CYC (0x3b).
- Let go: the jump state 0x40 with its rise (+0x1b8, a pass of a sixtieth) = the swing's rise + .1, at most .2,
  and its way over the ground (+0x23c) .21 a pass.

Native (`Gameplay::hook_in_reach`, `tick_grapple`, `Motion::Grapple`; `Hookshot` in `object_classes.cpp`): built
as above; the gadget is held and cycled like a weapon. Hidden runs from the east area's ledge: held 62 or 70
ticks and let go, he lands on the far side at (75.2, 14.16, -2.4) and (75.2, 14.16, -4.5)
(`build/sm_pc_work/logs/hookr_62.log`, `hookr_70.log`: last `position=` lines, `deaths=0`); let go at 50 ticks, at
the bottom of the swing, he falls short and dies (`hookr_50.log`), as he should. NATIVE or not done: after letting
go he is carried as a throw is, with no control until he lands; a target riding its path; the line is a plain
beam and the Hypershot's own hook model and sounds are not used; the pull has not been run (Ryllus has no pull
target).

**The vendor's stock.** The native vendor now sells, beside the refill: the weapons whose first level (the weapon
table) is this level or an earlier one and that have a price, and the Hypershot (5000) from level 2 on; bought
things are taken in hand (`Gameplay::vendor_stock` / `buy`; capture `shots/vendor-1.png`: Concussion Gun 10000,
Hypershot 5000 on Ryllus). NATIVE: where the original first offers each gadget is not read (the vendor class
0x1dbff8 and its menu are not translated); the panel is the PC's plain one, not the original's vendor screen.

### The Grind Lock (levels 03, 06, 07, 08, 09, 10; read in LEVEL_03 2026-11-16; built)

Ten doors of the game have a `GrindrailLock` beside them (two each on Kalidon, the Medical Outpost, Challax and
Quodrona, one on Dayni Moon and inside Clank). A locked door's own class does nothing while the lock's instance
byte +0x50 is set (Kalidon's `Lvl3Door` 0x18b700 asks 0x179b70).

**The lock** (init 0x179b80, update 0x179e28; props `promptArea`, `endArea`, `matrixMoby`, `lockID`,
`destroyMobys`). In `promptArea`: without item 20 of the weapon table (`grindboots`; 0x24d98) the help line 659
("You'll need the Shrink Ray to get through this door..."), with it the prompt 657 ("Press Triangle to enter the
Grind Lock"). Triangle: his matrix is kept, he plays his shrink state, then he is put at `matrixMoby` (0x4bf28) -
a place on the rails of a `GrindrailController` far below the level (y about -300) - and the lock's bombs, gates
and switches inside the box about that controller's rails (plus 5 m) are set for the try (0x5a448). Mode 2 lasts
until he is in `endArea`; thirty frames later (the screen fading from frame 22) he is put back at the door, +0x50
is cleared and the save flag (0x4e, lockID) is set.

**The rails** are the hero's state 0x37 (0x56170 mounts, 0x56290 a frame: 0x57e50 the ride, 0x56378 the sub-state
changes, 0x57848 the pad). The controller's props are data for it: `path` (the main rail), nineteen `secPaths`,
`grindrailSp` (0.2; times 0.5 * 1/30 * 60 = 0.2 m a frame, 6 m/s), `bombDamageFrac` 0.2, `gateDamageFrac` 0.5.

| Sub-state (+0x108) | What it is |
| --- | --- |
| 1 | riding: the place `speed` further along the rail each frame (0x57a18); he springs to it (velocity = (goal - place) * 0.2 - velocity * 0.4, within 0.8, twice a frame) and his facing to the rail's (the slide's spring) |
| 2 | crossing to another rail: for 60 frames the goal slides from where he is to the new rail; a jump at the same time |
| 3 | a jump over the rail: up at 0.16 m a frame, falling 0.0064 a frame each frame (2 m high, 50 frames, 10 m of rail) |
| 5 | off the rails (past a rail's last point): he falls; a metre below where he left it is death (his state 0x2c) |
| 6 | the wrench: clip 0x46 at 1.5; frames 5.33 to 13.33 hit what is within 2 m of half a metre ahead or 1 m of 1.75 m ahead (0x56a80) |
| 7 | thrown back by a bomb or a wall: up 0.0667, falling 0.00556, back along the rail at 0.133 to 0.267 a frame (0x56d18) |
| 8 | thrown back by a shut gate: the same, held in place (0x56e30) |

The pad (0x57848): the stick past half or the D-pad leans him left or right (clips 0x43, 0x44; table 0x2277c0);
Cross jumps, or with a lean crosses to the nearest other rail within six metres on that side that runs his way
(0x56688); Square swings. Hits (0x57368, only after the 60 frames a try starts with and 15 after a throw back):
a bomb's ball costs ceil(0.2 * his full health) and the bomb is gone; a shut gate costs ceil(0.5 * full health);
each has its own 60-frame rest; the level's collision along his way costs 1 (0x56ef8). Sounds, table 0x23b108:
ratchet-GRIND_RAIL1_LOOP, -JUMP, -GRIND_LAND, -GRIND_SHOCK1 and -GRIND_SHOCK_VO (a gate), -GET_HIT (a bomb).

**Switches and gates.** A `GrindrailSwitch` hit by the wrench turns on, or off again; with `toggleOnOff` clear it
can be hit once (0x17afc0). It sets or clears bit `gateMember` in its `gate`; a `GrindrailGate` is open while the
set bits are all the bits its switches registered at their init (0x179618, 0x179668), moving 1/30 a frame.

**Kalidon's first lock** (lockID 1, at the factory's front door): the main rail is 272.8 m (two bombs at 43 m,
the first gate's switch at 78.7 m, the gate at 88 m, a bomb at 171.6 m and 243.3 m); from 163 m a rail runs on
its left (149.6 m: bombs at 35.1 m and 52.5 m, the last gate's two switches at 108.7 m and 120 m, the gate at
128.4 m, `endArea` behind it) and from 169 m one on its right (bombs at 15.8 m and 79.6 m). The second lock
(lockID 2): a main rail of 190.6 m with four bombs and a switch, two looped side rails with a switch each.
`build/sm_pc_work/kalidon/rail_along.py` prints these from the curves (`SM_CURVE`).

**Native** (`Gameplay::tick_grind` in `src/sm/gameplay.cpp`; `src/sm/classes_grind.cpp`): the sub-states, the
springs, the jump, crossing rule, swing window, damage shares, rests and sounds above. Not translated, said in
the code: the shrink itself (a twenty-frame pause with ratchet-GRINDRAIL_SHRINK) and the fade out, the save flag,
`destroyMobys`, the rails' own camera (it stays behind him), the sparks under his boots, the collision of the
level along his way (0x56ef8), the gate's and the switch's own animations (an open gate is lowered out of the
way; a switch that is on glows green), the gate's ball is the one about its model's box (the original sweeps a
0.4 m ball against the gate's collision), the bomb's ball likewise.

Proof: `python tools/sm/sm_kalidon_run.py --only lock1` (hidden; log `build/sm_pc_work/logs/kalidon_lock1.log`):
three milestone lines PASS - he rode the rails, both gates opened, the lock is open and he is back at the door -
with no hit taken. Captures `build/sm_pc_work/shots/kalidon-lock-2.png`. TESTING AID in that run: `--all-weapons`
(the Grind Boots come from Kalidon's skyboard race, level 22, which is not built).

### Kalidon (level 03): what is read and built so far (2026-11-16; in progress)

The level's own classes, with what the native side has:

| Class | Read | Native |
| --- | --- | --- |
| `DeathTrigger` (0x160280 / 0x1602e0; LEVEL_02 0x17bd50) | the instance's state byte is the kind of death (0x2c, 0x2e, 0x2b); +0x46 set by another object holds it off for a frame | built (`death_tick`); the kinds of death are one |
| `TriggerCollection` (count 0x15540: its links up to the first empty one) | yes | built (`inside_collection`) |
| `Lvl3MagnaRoom` (0x18f218 / 0x18f358) | holds its DeathTrigger (kill outside the room's sphere, 22.1 m) off unless he is in the room | built: without it the hero died at the level's own start; the room's lightning and scrolling wall are not |
| `Lvl3Door` (0x18b358 / 0x18b700) | states 0 to 5, `controller`, `activate`, `lockWhenClosed`, `noLockMoby`, `grindLock` | built (`classes_level03.cpp`); portal groups, sound and save flag are not |
| `AirRaceInvitation` (0x140f08) | loads level 22 (the skyboard race) from level 3; movie 3 before it, movie 4 once item 20 is owned | not built; the race gives the Grind Boots |

The route, by the continue points: the landing pad (122, 15, 102); the skyboarder's platform (76, 15, 83); the
low ground (73, 1, 145) and the Hypershot swing at (45.6, 3.5, 133.6) over the pit; the ramp to the factory's
front (7.4, 10, 71.8) with four robot heads and lock 1; the showcase hall, the conveyors and guard beams
(27, 11.5, 18); the vendor (37.7, 11, -11.8) and the shaft platform `Lvl3Platform` (46.8, 10.8, -9.6) with its
enemy waves down to y -70; the conveyor over the fluid (74, 13, -37) with the Hypershot targets; the showcase
room (59.8, 18.5, -69.3); the bolt crank (35, 4, -77.6) that raises the sinking platforms; the hook ride
`Lvl3Hooks` (-0.8, 15, -24.8) and lock 2 at its back door; the inspectors' room (-25.6, 6, -23) and the magnet
room (-25.5, 6, 8.7); the junk conveyors (-73, 15, -8); the guard beams over the fluid (-72, 8.6, -49); Mungo
(-73, 6, -90); the end movie's trigger (-71.3, 25.6, -128.8) to level 4.

**Outside, as built.** `python tools/sm/sm_kalidon_run.py` (hidden; `build/sm_pc_work/logs/kalidon_level.log`,
seven milestone lines PASS): the pad's ramp, the road between its kerbs (the hero cannot cross a kerb), three robot
heads, the Hypershot swing at (45.6, 3.5, 133.6) held 66 frames and let go on the rise (the original's release:
0.21 a pass along the swing, up what the swing had plus 0.1, at most 0.2), the factory's stairs (a 0.4 m lip at
their foot: `TerrainQuery::move`'s step), four heads at the door, lock 1, the door. Maps:
`shots/kalidon-map-outside-east.png`, `-west.png`, `kalidon-map-factory-1.png` (`tools/sm/sm_ground.py`).

**Inside, the first rooms** (map `shots/kalidon-map-factory-1.png`): the entry corridor (x 3..12, z 55..70), the
round showcase hall (centre 7.5, 48), a ramp up to the conveyor room (x 11..36, z 2..26; two junk conveyors with a
guard beam at each mouth), a corridor north (x 24.5..29, z -9..2) to the ring room about the shaft (centre
46.8, -9.6; the vendor at 37.7, -11.8; the armour piece at 62.2, -8.8 and the way on to the east).

**The shaft: `Lvl3Platform`** (init 0x190430, update 0x190d38, 11 KB; props 352 bytes; read and built 2026-11-16).
The platform at (46.82, 10.8, -9.6) is the level's set piece. Its states (the instance's state byte):

| State | What happens |
| --- | --- |
| 0 | its twenty `platforms` are reset (elevators 0x18d0a8, static platforms 0x197298); to 1 |
| 1 | waits for the hero in `startTrigger` (the platform's middle); then the pad is off for the camera's move, `curStop` = 0; to 2 |
| 2 | down 0.1333 a frame (4 m/s). At y of table 0x23dc98[`curBombSet`] (-26, -49; while `curBombSet` < 2) to 4; at y of table 0x23dc88[`curStop`] (-4.1, -14, -38.7, -62.7) to 3 with `curWave` = 0 |
| 3 | a stop: the wave `stopNWaves[curWave]` done (its flags & 6) is the next wave; after the stop's last wave (1, 2, 2, 2 waves: 0x193860) `curStop`++ and to 2, or after the fourth stop to 5 |
| 4 | the bombs: down 0.0333 a frame; every 1.75 s the next `PODBOMB` of the set is sent at the platform (0x1abcf0), four in all; when none of the set is active, `curBombSet`++ and to 2 |
| 5 | it gives way: down 0.1333 a frame while he is in `mainShaft` (0.4 when not); below -76 the line 435 "Jump off the elevator, Ratchet!"; sparks and smoke. Twenty frames after he is off it below -84 (or he has gone below -92 with it) to 6 |
| 6 | it plunges 0.6667 a frame to -109 and blows up (fifteen `Lvl3PlatformShrapnel`, more blasts every 0.75 s, ten in all); a hero still on it is carried down and dies at -92 (0x4a430); else to 7 |
| 7 | in `consoleArea` (a ledge at 29.9, -87.8, -9.3) the prompt 434 "Activate console"; Triangle: the fluid (`fluidMoby`) is put at -109.5, `platforms[0]` at -107, the two `Lvl3Pipe`s pour; to 8 |
| 8 | five seconds: the fluid rises 0.0333 a frame, then `platforms[0]` 0.0443 a frame; then he is put at (36.92, -89, -9.42), the fluid at -92, `platforms[0]` at -89.23, the static platforms are reset, save flag (2, 1); to 9 |
| 9 | the climb: the fluid rises by its speed (+0x48), a spring towards 0 (table 0x22138c) or 0.1 a frame when he is more than 3 m above it, half speed while he is busy, and stops at 10; the elevators `platforms[1..3]` move 0.2333 a frame to their far end while he stands on them (table 0x23dd38: 12, 17, 9, 0); once he is above y 10 the `bridge` rises from 5 m under the fluid to 10.8 in a second and the sequence is over (save flag (2, 2)) |

The camera through states 2 to 5 looks down the shaft from (46.82, hero y + 10, -1.41). Waves come out of
`HutDoor`s at each stop (y -3, -13, -37.7, -61.6); the `PODBOMB`s sit on the walls at y -19 and -43.

What the shaft's other classes do, as read:

| Class | Rule |
| --- | --- |
| `HutDoor` (0x17c770 / 0x17ca10; table 0x220384) | opens while one of its `mobysToCheck` that is alive is inside `trigger` (or the hero in `playerTrigger`), shuts otherwise; along its up axis 2.5 m (variant 0) or 1.6 m down (variant 1), speed a spring to 0.167 m a frame |
| `PODBomb` (0x1ab4c8 / 0x1ab548; sent 0x1abcf0) | flies in 15 frames to a metre short of where the hero stood, rolls the last metre in 30 frames with three bounces, sits a second, goes off: 1 damage within 2 m |
| `Lvl3DeadlyFluid` (0x18add8 / 0x18ae88) | in `activeArea` with his feet below the instance's height he dies (state 0x2e) |
| `Lvl3StaticPlatform` (0x1970f0) | no life of its own: put away (0x197298) and brought back (0x197308) by the shaft |
| `Lvl3Elevator`, the shaft's four | put away (0x18d0a8) until the shaft sends each up out of the fluid to its low end (0x18d118), then a lift |
| `Drone01` | it flies; the robots of the shaft's waves are placed behind the hut doors where the level has no floor |

**Native** (`src/sm/classes_level03.cpp`): the ten states with the original's stops, speeds, bomb interval, console,
heights, the place he is put for the climb, the fluid's spring (base speed 0.01 a frame as the init sets it, not the
instance's `fluidSpeed`) and the lifts' rise; `HutDoor`, `PODBomb`, `Lvl3DeadlyFluid`. The shaft's four lifts are
taken from the generic lifts (`mechanisms.cpp` skips what a `Lvl3Platform` names) and are objects with a native
ride rule (he boards at an end, it goes to the other; off it, it comes to the end nearer his height). Enemies that
fly, or stand where the level has no floor, come straight at him at their own height until they are over ground
(`Enemy::hovers`; the drone's own flight 0x160b28 is not translated). Not translated, said in the code: the
shaft's cameras and the pad being off for them, sparks, smoke, shrapnel and sounds, the save flags that restore
the climb after a death, the slower fluid after each death, the pipes' pouring.

**The hero's second jump** was too low for the climb (ledges rise up to 2.4 m): it is now the original's - up at
0.08 a pass (hero +0x114, defaults LEVEL_01 0x380e8) under the light gravity of 0.002 a pass (+0x124, states
0xd / 0xe of 0x40338): 1.6 m in two thirds of a second, 2.8 m with the first jump. The first jump keeps its
bring-up numbers (the original's 0.17 a pass under 0.012 reaches the same 1.2 m sooner). Also from the defaults:
falling gravity 0.012 a pass (43.2 m/s2; native 22), the glide's 0.002.

Proof: `python tools/sm/sm_kalidon_run.py` - thirteen milestone lines PASS (`logs/kalidon_level.log`): outside,
lock 1, the door, the rooms, and the shaft: its four stops with every wave beaten, both bomb sets, off at the
console when it blew with no death, the console, the climb to the bridge. `--only shaft` runs it alone
(`kalidon_shaft.log`). Captures `shots/kalidon-shaft-1.png`, `kalidon-climb-1.png`, `kalidon-conveyor-room-1.png`.

**East of the shaft to the hook hall, as read and built (2026-11-16).** Maps `shots/kalidon-map-factory-2.png`,
`kalidon-map-lake.png`.

| Class | Read | Native |
| --- | --- | --- |
| `MovePlayerHack` (0x19c0e0 / 0x19c198) | in `area` the hero is given `speed` m/s along the area's z axis (added to the velocity the world gives him, 0x250a64) | built (`mover_tick`): the belt over the fluid carries him west at 4 m/s |
| `ConveyerJunkController` (0x156ed0 / 0x157010) | junk objects along `junkPath` with pauses, the belt's sound, `stopArea` | not built (this belt carries no junk; the two junk belts of the conveyor room and the one at (-73, 15, -8) do) |
| `Lvl3Showcase` (0x195950 / 0x195be8 / break 0x195c48) | breaks for the hero in `breakTrigger`, on a hit, or when its `enemyWave` lets go; its robot (which names it in `showcase`) wakes | built; the shards, the sound and the broken glass left standing are not |
| `Lvl3BoltCranker` (0x189a58 / 0x189bf0) | while `bolt` is turned its ten `platforms` stand at a smoothstep of the crank's progress (ten metres); at 0.99 they stay, let go before and they sink back in a second | built, on the mechanisms' crank (`ObjectWorld::cranks`) |
| `Lvl3SinkingPlatform` (0x196180 / 0x1962f0) | 1.25 s after he lands it drops 0.2667 m a frame to y -2, stays under 4 s, rises in 60 frames | built; its trembling is not |
| `ContinuePoint` (0x1569d0) | the hero in its trigger makes that instance the current one | fixed: the native rule went by `index`, which is 0 for all seven on Kalidon |

The Hypershot on this stretch: the pull target over the belt (he comes off the line carried at 0.08 a pass, 4.8 m/s,
until he lands: the stub under it is 2.5 m long), the swing from the belt to the north side, and round the lake
four swing targets, two of them with nothing between: off a swing he is in his jump state and takes the next target
in the air (the native release was a throw with no control; now the Hypershot fires during it and the stick bends
his way by up to 60 degrees a second, a NATIVE figure).

Proof: `python tools/sm/sm_kalidon_run.py` - nineteen milestone lines PASS, RESULT: PASS (`logs/kalidon_level.log`;
`--only east`, `--only showcases`, `--only lake` run the new sections alone): the belt's workers shot from the
platform, the pull, the swing, the showcase room's eight robots and both its doors, the lift, the bolt crank, the
lake's four swings and six plates, to the hook hall's door at (16.5, 4, -23.5).

**The hook hall: `Lvl3Hooks`** (init 0x18e030, update 0x18e370; read and built 2026-11-16). The hero in `activate` shuts the
front door behind him (its path's point 1 is the shut place) and takes the camera; four waves are brought in on
the eight `hooks` (ties) along `positions`, a hook every 2.5 s: six drones, two robots, six drones, three robots;
each wave must be dead and the hooks back before the next; then lock 2 (`grindLock`) is switched on beside the
back door, the back door is set to open (it waits for its lock) and the front door is given `doorTrigger` as its
`activate`. Native: the waves, the doors and the lock's switch as read; the hooks themselves are not translated (an
enemy is set down under their line when its turn comes), nor the hall's camera.

**Kalidon's second lock** (lockID 2, in the hook hall): the main rail is 190.6 m - the first gate's switch at
19.4 m, bombs at 32.4, 48.4, 67.7 and 80.6 m, the last gate at 168.4 m with `endArea` behind it. Two side loops
begin and end at the same place beside it: the left one (132.2 m) from 124.1 m, with bombs at 26.8 and 82.3 m and
a switch at 99.3 m, and the right one (138.3 m) from 129.2 m, with a bomb at 46.9 m, a switch at 74.1 m and a bomb
at 135.2 m; each comes back alongside the main rail's 109 to 124 m. The last gate needs both loops' switches. A
crossing is refused at a rail's last point (0x56688), so beside a loop's end the hop waits until he is past it
(`build/sm_pc_work/kalidon/rail_sides.py` prints the sides and gaps).

Proof: `python tools/sm/sm_kalidon_run.py --only hall` - three milestone lines PASS (`logs/kalidon_hall.log`):
seventeen enemies, lock 2 with no hit taken (four crossings, three swings), the back door.

**West of the hall: the inspectors' room and the magnet room (2026-11-16; built).** The inspectors' room
(-24.5, 6, -24): its three robots and three `FactoryInspector`s fall to the native enemies' rules and its door
(`Lvl3Door`@12230, controller EnemyController@f630) opens. Behind it is the magnet room, a ball of 22 m about
(-25.5, 8.1, 8.7) entered at its south (`doorway1`, y 6) and left at its west 12 m higher (`doorway2` at
(-44.2, 17.9, 8.5)). The way between is on its walls: tracks 2 m wide of magnetic faces that wind over the whole
ball (maps unwrapped about its centre: `shots/kalidon-magnet-unwrapped.png`, `-zoom1..5.png`). `Lvl3MagnaRoom`
(update 0x18f358) holds the room's `DeathTrigger`@183b0 off while he is outside the room (`updateArea`) or in a
doorway; inside, the trigger kills him as soon as he is NOT in its sphere of 22.1 m about the centre
(`killHeroInTriggers` 0): the tracks lie 19.9 to 22.1 m out, the ball's shell beyond that. So a step off a track's
side is death, and it was the only thing that killed the first native boots (he walked off the side of the first
track, his scripted stick bent by the pad's dead zone).

NOT BUILT in the magnet room: its 43 `Lvl3Bomb`s (20 pooled, the rest set on ten paths over the tracks; class in
the enemy tiers with attack tier 0) - the tracks are empty in the native room.

**The junk conveyor (2026-11-16; read and built).** Map `shots/kalidon-map-west.png`. West of the magnet room's
door a feed belt (`ConveyerJunkController`@144b0: no path, no pause) runs 23 m west at y 16 and drops onto the
junk conveyor (@14430), which runs 70 m south along x -73 at y 15.3 over the fluid (`Lvl3DeadlyFluid`@15a30 at
13.2) between twelve `FactoryWorker`s on pads 2.2 m either side, six pairs 6.6 m apart; its last 4.5 m to the
vendor's floor (z -52, y 15.3) are a gap over the pit of the eight `Lvl3GuardBeam`s (y 8.6), where
`DeathTrigger`@18330 kills in a slab at y 8.
- `ConveyerJunkController` (init 0x156ed0, update 0x157010): only while the hero is in `updateArea`. State 2:
  it runs `conveyerSpeed` (4) m/s for `distBetweenPause` (6.6) m, then (0x157968) sets the first waiting item on
  the first point of `junkPath` and stands (state 3) `pauseTime` (1) s. Its `movePlayerHack` is given the belt's
  speed each frame, 0 while it stands (the instance's own `speed` is not used). A riding item goes from point to
  point of the path (12 points, 6.6 m apart: each stop leaves every item at a pair of workers) while the belt runs
  (0x1573f8), and off the last falls .3267 m a frame into `destroyArea`, to wait again.
- `ConveyerItem` (init 0x156b20): gives itself to its `pConveyer` (0x157d30; ten here), solid when `treadOnMe`. The
  hut the items come out of is a `HutDoor` whose `mobysToCheck` are the items.
- `FactoryWorker` (init 0x16db28, update 0x16df60, event 0x16dda8): it never leaves its pad. It works (clips
  TO_WORK, WORKING) while the hero is before it (within 4 m and .75 of its facing: 0x16e478, 0x14378) or its belt
  stands (0x157c60), else idles (IDLE1, sometimes IDLE2). Working, its tools hurt him between .4 and 1.4 s of
  WORKING (its model's rows of 0x23a198 / 0x23a1a8) when he is within 2.24 m and .8 of its facing. Any hit of his
  ends it (state 6: thrown, then explosion 0x17).
Native (`classes_level03.cpp`: `belt_tick`, `item_start`; `Enemy::stationed` in `Gameplay::tick_enemies`): the
belts' two states, the mover's speed, the items' release, ride and fall, the hut's door for them, the workers'
station, their two reasons to work, the hit's window and reach, one hit to fell them. Not translated: the belt's
scrolling texture (`uvSpeed`) and loop sounds, the items' turn into their way, the worker's TO_WORK / TO_IDLE
clips and its IDLE2 (its WORKING, TO_WORK and TO_IDLE read as one frame each from the disc - not looked into),
the beams' own drawing (they stand as models; the death under them is the trigger's).
Run: `python tools/sm/sm_kalidon_run.py --only west` (`build/sm_pc_work/logs/kalidon_west.log`): the feed belt
carries him onto the conveyor, it runs and stands by turns with its junk riding, he goes down it and jumps the
pit to the vendor's floor alive: 3 / 3 PASS. Captures `shots/kalidon-workers-1.png` .. `-3.png`.
`SM_ENEMY_CLIPS=1` prints the level's enemy classes with their clips.

**The boss: `Mungo`** (-73, 6, -90; init 0x19c2a0, update 0x19cb90, event 0x19c9f8, its states' entry 0x19fdd0,
its phases 0x1a0de8; 28 KB with `MungoDrone`, `MungoCC` and `MungoShockwave`; 38 states; read 2026-11-16, built in
a first version). The arena is a round platform (the tie `platform`, top y 16; `arenaTrigger` a cylinder of
31.7 m) reached from the vendor's floor by `startBridge` and left by `endBridge` and `backDoor` to the level's
end (`EndMovie`: trigger at (-72.9, 17.6, -126.3), movie 03C, level 4). His model has 24 clips (IDLE, SLAM,
ATK_SWIN, ANTICIPA, CELEB, CHARGE, CHARGE_P, DEATH, GET_DRON, TOSS_DRO, JUMP, FALL, LAND, ATK_PIPE, GET_PIPE,
REAR_BAC, ROAR, PIPE_RUN, SWING, TURN, HOP, TURN_LEF, TURN_RIG, TURN_OBJ).
- Entry. 1: he waits, 10 m down with the platform, for the hero in `enterTrigger` (a slab across z -72.8).
  2: 4 s, the platform and he rise 2.5 m/s, both bridges and the two `ArmorPickup`s sink as far, `door` is set
  open. 3: HOP, then `backDoor` shuts. 4: ROAR. A count of entries (0x2216ec) sets his difficulty at the next
  start (3 for the first three, then 2, 2, 1, 1, 0); his health is 375 (0x2216dc; 10000 in the other mode,
  0x2216e4) times the difficulty's row of 0x23f7c8 (.7, .8, .9, 1), his `attackSpeed` the same row (0x1a26b8).
  He is hurt in states 6 to 0x23 only.
- Phase 0 (above 3/4). 5: TURN_LEF / TURN_RIG by the side the hero is on (0x1a1698), turning to him inside the
  clip's window (.5 to .833 s, or .233 to .567 s: 0x1a1790) until .96 of facing him. 6: CHARGE_P; with the hero
  beyond 7.07 m, 7: CHARGE, over which he goes by a smoothstep of the clip's time from where he stood to the
  hero until within 7.07 m (0x1a2218). 9: ATK_SWIN: in its first .5 s the hero is hit when the line from his
  feet to his left hand (joint 0x1f; read by the joint callback 0x1a0b40) passes within 1 m of the hero's middle,
  or the hand's own sweep meets the hero's collision; measured natively the hand comes 5.2 m out, so a hero
  standing where the charge stopped is not reached. Then 5 again when the hero is out of his facing or his
  7.07 m, else 8: ANTICIPA at attackSpeed, and 9 again. `reposTimer` runs from each charge; when it passes 6 s
  during an ANTICIPA he charges (0x23) to 5 m beside the hero (0x1a2758: across his line to him, a side by
  chance, the other when that one has no floor) and swings from there.
- Phase 1 (at 3/4): he takes the side the hero is not on (z -74.8 or -105.6: 0x23f788) and throws his
  `northDrones` / `southDrones` (six each, on `northPaths` / `southPaths`) at him: 0x14 to the place, 0x15 facing
  it, 0x16 GET_DRON when a drone is within reach of his hand, 0x17 / 0x18 TOSS_DRO, 0x19 a wait of 4 - 4 x
  attackSpeed s. A drone the hero strikes back at him (the event: attacker of class hash 0xe70c9c6c) stuns him
  (0x1b) and he jumps to the other side (0x1d JUMP to y 20, 0x1e FALL, 0x1f LAND).
- Phase 2 (at 1/2): 0xc to the nearest of the ten `pipes`, 0xe GET_PIPE (the pipe comes off at 2.5 s), 0xf
  PIPE_RUN to the hero, 0x11 ATK_PIPE at 3/4 speed: the pipe hits between .767 and 1.133 s, and where it strikes
  the floor (.867 to 1.167 s) sparks fly.
- Phase 3 (at 1/4): 0x12 he throws the pipe (it flies 2 m a frame along where the hero was); 10 REAR_BAC, 0xb
  SLAM up to six times, each sending a `MungoShockwave` at .133 s; then the drones again, with one chance in two
  of a SLAM after each throw (0x1a1f98).
- At nothing: 0x24 DEATH; `endBridge` rises in 2/3 s (5.9 to 15.99 by a smoothstep) with the armour 1 m above it;
  at the clip's end 0x26: `backDoor` opens and the drones go; 2 s later he is a corpse (0x25).
Native (`classes_level03.cpp`: `mungo_tick`): the arena's bridges, both doors and the armour through the entry
and his death, the entry's timing (4 s, HOP, ROAR), his health and difficulty, phase 0 whole (the turn's windows,
CHARGE_P, the charge, the swing with the arm's line from the clip's own left hand, ANTICIPA at attackSpeed, the
re-placing after 6 s), the phases counted at their thresholds, DEATH and the way out to the level's end movie.
NATIVE: his turn's rate (4 rad/s in the window; the original's is a spring, 0x23f8dc); the side he re-places to
alternates; when the hero dies in the fight the arena and Mungo are set back (the original starts the level's
objects anew).
NOT BUILT, said plainly: phases 1, 2 and 3 - the drones (`MungoDrone`, about 13 KB from 0x1a30e8), the pipes, the
shockwaves and the crates (`MungoCC`): the native Mungo keeps to phase 0's charge and swing through all four
phases. Also not built: the platform's rise (the level's platform stands risen, and he stands on it from the
start), the entry's cutscene (the hero is not held), the hand's own sweep against the hero (only the arm's line
is tested), the `hd03_strafe` help line, his health bar, the sounds his clips do not carry.
Run: `python tools/sm/sm_kalidon_run.py --only boss` (`build/sm_pc_work/logs/kalidon_boss.log`): the fight
begins as he comes onto the platform, Mungo charges and swings, falls - with the TESTING AID `foe:6`, which sets
his health to 6 before the shots - the far bridge rises, the back door opens, and the level's end movie
(/VID/L03/03C.BIK) plays as he leaves: 4 / 4 PASS. Capture `shots/kalidon-mungo-1.png`. `SM_OBJECT_CLIPS=Class`
prints a class's model and clips; `SM_BOSS_DEBUG=1` his hand through a swing.

**Kalidon end to end (2026-11-16).** `python tools/sm/sm_kalidon_run.py` plays the level in one hidden run from
the landing pad to its end movie: outside, lock 1, the factory's rooms, the shaft, the east side, the showcases,
the lake, the hook hall and lock 2, the inspectors' room, the magnet room on the Magneboots, the junk conveyor,
the boss (`build/sm_pc_work/logs/kalidon_level.log`). Testing aids, printed by the run: `--all-weapons` (the
level is started directly, so the Hypershot of Ryllus and the Grind Boots of the skyboard race are not earned in
it), `hp:250`, and `foe:6` at the boss. Still not built on Kalidon: Mungo's phases 1 to 3, the magnet room's
bombs (`Lvl3Bomb`), the skyboard race (`AirRaceInvitation`, level 22) that gives the Grind Boots, and what each
section above lists as not translated.

### The Magneboots (the hero's state 0x1f; read in LEVEL_03 2026-11-16; built)

**Surface types.** A collision face carries a surface hash (+0x48), looked up at load (0x10e330) in the "STD "
table (asset 0x41 of type 0xb; version 3, a count, 16 bytes a type {hash, u16 flags, sound, -}), which is not in
the level's WAD but in the shared ones (`World::surface_types` reads it from the level image, then /SP.WAD, then
/GLOBAL.WAD; `SM_SURFACES=1` prints it). Sixteen types: `fa58270b` flags 4 (magnetic), `c4734dda` 1 (ground type
5: no standing), `77a7a394` and `e13093ea` 2 (type 6: hurts), `0c67d66a` 8, `c0a10633` 0x10, `2ba8642f` 0x20,
`8ab65c40` 0x80, `4cc6f2cd` 0x100, the rest 0. The ground probe (0x47ee8) reports a face of flag 4 as ground of
type 3, and the walking state's ground handler (0x3fc48) answers type 3 with state 0x1f.

**The state** (0x528a0 a frame; its sub-states 0x542f0, their changes 0x529a0). It was first taken for the code at
0x62408 (that is state 0x21, another controller): state 0x1f's walk plays clip 0x29, MAGNA_WA(LK).
- His up (+0x2f4) eases to the normal of the face under him (+0x304; the spring 0x4b370), and the camera takes his
  up for its own, 5 m behind him (0x53000).
- The stick, in the camera's frame and laid on the surface, is the way he turns to about his up (0x53550); he
  walks the way he faces at .045 x .5 a call, or x 1.5 with the stick full over (0x542f0 case 1; 0x20ce84): 1.35
  and 4.05 m/s at 60 calls a second.
- A sphere of .4 m, .75 m up his own up, keeps him out of walls (0x53d38; 0x226d48, 0x226d4c).
- The probe (0x53ba0): from 1 m up his up, 3 m down it. Nothing there, a face that is not magnetic, or one more
  than .5 m under his feet, and he falls off (0x530d0: state 4, his up back to the world's); else he stands where
  it hit (0x53e28).
- Cross (0x529a0 case 2): a jump of .07 a call along his up under .003 a call each call (0x540a8; 4.2 m/s and
  10.8 m/s^2: .82 m high, .78 s), sound ratchet-JUMP; coming down, the probe from .75 m up lands him on a magnetic
  face (0x54168), with a landing of .1 s when he came down faster than .156 a call, .233 s when faster than .38
  (0x227158, 0x227154).
- Square swings the wrench from the boots (case 5, clip COMBO_1), a weapon is fired by leaving for its own state
  (case 9), cases 6 to 8 and 10 are a weapon's and a hit's.

**Native** (`Gameplay::tick_magne`, `begin_magne` in `src/sm/gameplay.cpp`; the camera in `src/sm/main.cpp`): the
entry, the up and its ease, the camera with his up (the right stick turns it about his up; it swings in behind him
as he goes), the stick in the camera's frame, both speeds, the sphere, the probe and both ways off (onto plain
ground he walks on; else he falls), the jump with its landing, the clips (MAGNA_WA, JUMP_TAK / FALL_CYC /
JUMP_LAN). NATIVE values, not the original's: the rates of the up's ease (10 /s), of his turn (9 rad/s) and of the
camera's swing (1.2 /s) - the original's are springs whose rates are in the hero's record. Not translated: the
wrench and the weapons on the boots, the hit reaction, the glow on the boots (0x38728, effect 0x478fe9a),
footsteps.

**Run.** `python tools/sm/sm_kalidon_run.py --only magnet` (hidden; `build/sm_pc_work/logs/kalidon_magnet.log`):
the inspectors fall and the door opens, the boots take him at the doorway, he walks 163 m of track round the
ball - down the south wall, across the bottom, up the east wall, over the north and down to the west door from
below - and steps off through `doorway2` alive: 4 / 4 PASS. The way is not written into the test: it is found on
the level's own magnetic faces (`tools/sm/sm_magne_route.py`: the shortest chain of faces sharing an edge, as the
middles of the edges crossed), and walked with the script's `mgo:N` (`--waypoints3`), which sets the stick in the
boots' camera frame, corrected for the pad's dead zone. Captures `shots/kalidon-magne-1.png`, `-2.png`.
`SM_MAGNE_DEBUG=1` prints a line a tick; the Objects line of a run now names what killed him (`death_by`).

### Metalis (level 04): the route as read, and its story's way as built (2026-11-16)

**Built** (`src/sm/classes_level04.cpp`; run `python tools/sm/sm_metalis_run.py`, hidden, no testing aids,
`build/sm_pc_work/logs/metalis_level.log`, 7 / 7 PASS): the level begins with Clank in the arena's lobby
(`Level04StartMoby`), the first derby's movie (04B) is asked for, the crane swings him the 38 m over to the ship's
ground (`Level04Crane`: its two ends, its 131-frame swing with the original's three-part pace, the hero carried
round with it, its calls from the far area), he walks round the ship and up the ramp, the growth pad offers to
grow him and Triangle asks for the grow movie (04C) and level 15 (`GiantClankGrowthPad`). Captures
`shots/metalis-start.png`, `metalis-lobby.png`.
NOT BUILT, said plainly: the arena and its three games (the Destruction Derby, the Microbot Toss, the Microbot
Survival) with `ArenaController` and `ClankChallengeMenu` - the level's first derby does not start, only its movie
is asked for (which is what wakes the pad, as in the original: the pad waits for the level's movie 1 to have been
seen, not for a win); level 15 itself (Giant Clank); Ratchet's side of the yard is as the shared classes leave it.
NATIVE: the crane's tray is found by place (27.1 m out, .069 rad round from the arm, its floor 1.81 m up: measured
on the native collision) with a third of a second's wait for boarding and leaving; the pad's reach is 1.6 m and its
prompt a plain "Grow"; save flags 5 and 0x1b are not kept (he is Clank at every start).
The low death slab between the two grounds (`DeathTrigger`@16c80: y 8.97 to 12.97 over the whole yard) is why a
step off the ship's pad to the west is death.

**As read:**

The level loads and draws natively (`shots/metalis-start.png`: the ship's pad, the growth pad, the junkyard); the
hero is put down as Ratchet at the level's `ratchet` instance (9.1, 15.8, 69.8), which is NOT how the level
begins. Instances `build/sm_pc_work/metalis/inst04.txt` (568), decompilations `metalis/*.c`. 67 classes; the ones
on the story's way:
- `Level04StartMoby` (init 0x196490, update 0x196510; props `arenaController`, `clankPlayer`): on its first frame,
  unless save flag 5 is set, the player becomes Clank (0x6d5e8: the `ClankPlr` at (-57.4, 14.1, 52.7), in the
  arena's lobby), and unless save flag 0x1b is set the level's movie is asked for (0x13f720) and the first
  challenge of `ArenaController`@d200 started (0x14f840(controller, 0): `DestructionDerby`@aa80). Then it is gone.
- `ArenaController` (update 0x14efa0, 1.8 KB; three here: the Destruction Derby, the Microbot Toss (locked), the
  Microbot Survival (locked); `arenaChallenge[7]`, `challengeCount`, `newMusic`, `locked`): the flow about a
  challenge - it watches the running challenge's state (3 won, 4 lost, 5 left), pays the reward (0x14ed98),
  shows the messages (texts 0x40a, 0x40c, 0x3f3, 0x3f0, 0x40d with 0x21a) and records how many of its
  challenges are beaten (0x27110). `ClankChallengeMenu` (the lobby's console) starts one;
  `Level04ChallengeUnlocker` unlocks the other two; `Level04HelpManager` the first derby's help.
- `DestructionDerby` (init 0x171b98, update 0x171c88; `vehicleController`, `resetMobys`, `movieNumber`) with
  `ClankBotFlingerVehicle` x5, `EnemyBotFlingerVehicle` x10, `vehicleController` x10, `DerbyTeleporter*`,
  `Obstacle*` (bouncers, saws, spinning floors, vents, walls), `SMASHERBOT` x2, `DamageSphere`: the vehicle fights.
  `MicrobotToss` (+ goals, teleporters) and `MicroBotSurvival` (+ `Survival*` tiles, ports, spawners) are the other
  two games. None of them read yet.
- `Level04Crane` (init 0x193fa8, update 0x1941b0; `arenaArea`, `shipArea`): carries him between the arena's side
  and the ship's when he stands on it (states 0 / 1 at an end, 2 / 3 travelling, 4 / 5 waiting for him to step off).
- `GiantClankGrowthPad` (init 0x185488, update 0x185658; `giantClankLevel` 15, `growMovie` 2, `shrinkMovie` 3,
  `movieIndex` 1) at (9.1, 16.1, 73.9): once the level's flag `movieIndex` is set (0x271b8), standing on it shows
  its prompt (table 0x221250) and Triangle plays the grow movie and loads level 15 (Giant Clank).
- For Ratchet's side of the yard: `Level04Crusher` x12 (pairs, `timeOffset`), `CONVEYORBELT` x5, `FIRE` x20,
  `TMRobotTorsoB` x13 with three `EnemyController`s, `Hookshot` x5, two `Teleporter`s (one to level 24, secret),
  `Level04SecretDoor`, a vendor at (62.9, 14, -17.3), four `ContinuePoint`s.
So the story's way through Metalis is: Clank in the arena, the first Destruction Derby won, the growth pad, level
15. To build, in that order: the start (Clank in the lobby), the arena's flow and the derby, the pad, level 15.

**The Destruction Derby, as read so far** (about 50 KB with its vehicles; decompilations `metalis/derby.c`,
`vehctl.c`). The vehicles of a derby are "torsos": Clank's `ClankSawTorso` / `ClankRamTorso` / `ClankElectroTorso`
(2.4 to 3.0 KB each) and `EnemyRamTorso` / `EnemySawTorso` / `EnemyElectroTorso` (3.6 to 4.9 KB), each naming its
`vehicleController`; the `*BotFlingerVehicle`s belong to the Microbot Toss. The first derby
(`DestructionDerby`@aa80, movie 1) is controller @a480, a ring of 15 m about (-90.5, 15, 52): Clank's
`ClankSawTorso`@a980 against `EnemyElectroTorso`@ad00 and `EnemyRamTorso`@b800, @b880, among four `ObstacleWall`s,
an `ObstacleSaw` and four `ObstacleVent`s, with a health and a power-up teleporter.
- `DestructionDerby` (init 0x171b98, update 0x171c88): state 1 switches its controller on (0x1d4ea8) and plays
  its movie the first time, state 7 asks each frame who is left (0x171f98: a vehicle's class flag 1 is Clank's
  side, 2 the enemies'): 1 won when none of theirs is left - 30 frames (state 8), then state 3 for the
  `ArenaController` to see - 2 lost when none of his is (state 5 or 4). State 6 switches the controller off,
  resets the `resetMobys` collection and goes to 2.
- `VehicleController` (init 0x1d4828, update 0x1d4970, 12.5 KB; `obstacles[16]`, `healthTeleporters[4]`,
  `powerupTeleporters[4]`, `radius`, `hexagon`): holds up to eight vehicles; while on, each frame it runs every
  vehicle's own update and measures how far it went, then each obstacle's callback, the teleporters (0x1711f8,
  0x171728), the vehicles against each other (0x1d7270), the ring's edge (0x1d6bc8), and the camera on the
  player's vehicle (0x1d6d18). Switched off it hides vehicles, obstacles and teleporters.
- A torso's drive (0x32680 -> 0x31d58, shared by every vehicle; its numbers a block of floats, the saw torso's at
  0x244a60: .2, .0667, .0785, .0056, .0028, .0111, .0056, .0044, .1, .2, 1000, 0, .74, .4, .74, .4): the stick
  gives a turn and a throttle (0x329c8). Throttle forward: speed += [3] x throttle up to [0] (m a frame);
  throttle back: braking by [5] while it still goes forward, then reversing by [4] down to -[1]; no throttle: it
  slows by [6] to rest. The turn is 0x32d58 ([2] radians a frame). It then moves along its facing by its speed,
  and by a knock (a size along a kept direction) that fades by [7] a frame.
- `ClankSawTorso` (init 0x166fc8, update 0x167138, event 0x167378): registers with its controller (0x1d4d88, as
  the player's vehicle); each frame the drive, then its saw (0x1e58e8) on Cross or Square (pad bits 0xc000):
  clip 3 blended in over half the clip, a cooldown from 0x2383b0 before the next; the controller's teleporters
  are tested against it (0x1d5bb8 health when hurt, 0x1d5c38 power-up); R1 (0x1e72c8) picks up and throws.
  Clank's health in a derby is the hero's own (0x2502bc of 0x2502c0); a hit gives 15 frames in which nothing
  else hurts; at nothing his vehicle leaves the controller and blows up (0x1d7a28). Its collision callback
  (0x1677a0) deals 2.0 to what it touches while the saw's clip is past 0x244a40 of its way (0x1e5b18), then takes
  the blow: the part along its facing onto its speed, the rest into its knock.
- Vehicle against vehicle (0x1d7270, 0x1d7078 / 0x1d6d40): two whose radii (their collision spheres, 0x1d7428)
  overlap are pushed apart along the line between them in the measure of their masses and speeds, each is given
  the other's blow through its collision callback, and above .0667 m a frame a crash sound (0x22270c) plays. The
  ring's edge (0x1d6bc8: a circle, or a hexagon when `hexagon`) puts a vehicle back inside and turns its blow
  (0x1d6698).
- `EnemyRamTorso` (init 0x17b310, update 0x17b508, event 0x17b8c0; its numbers at 0x2447b8, 0x2c a head type): 20
  health; states 0 seeking its target (0x17bd70 picks it; 0x1d57e0 the way round obstacles, 0x17c230 the stick,
  0x1e4c58 the drive), 1 charging for a time after a blow from behind, 2 stopped, 3 shrunk (the power-up), 4
  running from one; below 20 health it uses the health teleporters; at nothing it blows up (0x1d7a28) and counts
  for the derby's end.
  Decompilations on disk, not yet read through: `metalis/enemy_ram.c` (its target, its ram's damage),
  `enemy_electro.c`, `obstacles.c`, `teleporters.c`, and the camera 0x1d6d18.
None of it is built.

### Level 15 (Giant Clank) and Dreamtime (level 05): surveyed only (2026-11-16)

**Level 15** is the Giant Clank shooter the growth pad of Metalis leads to: 105 instances
(`build/sm_pc_work/giant/inst15.txt`) of ten classes - `GCS` (its controller; `curveCameraPath`), `GCS_Ship` (the
giant), `GCS_Models`, 43 `GCS_Asteroid`s, and 58 wave grids (`GCS_WaveMiniGrid` 29, `GCS_WaveMaxiGrid` 23,
`GCS_WaveCorvetteGrid` 4, `GCS_WaveBomberGrid`, `GCS_WaveLancerGrid`: `waveID`, the enemies' health, guns and
lifetimes, a grid's centre, size, angles and scale, how they enter, idle, strafe and flee). The classes' own
callbacks are a few bytes each; the game is the module's code between them (about 270 KB from 0xf7bd0). The level
loads natively and nothing of it runs. Its Ghidra project is `build/sm_pc_work/ghidra/proj_15` (`L15_decomp.json`
once exported). NOT BUILT.

**Dreamtime (05)**: 430 instances (`build/sm_pc_work/dream/inst05.txt`), 50 classes; the hero's start is
(-4.9, 50, 2.9); six `ContinuePoint`s; the mini-boss `Lvl05MiniBoss` at (-150.6, 26.9, 280.9) with its
`Lvl05MBBomb`. Its own classes are about 110 KB: `Lvl05Controller` (3 KB), `Lvl05Portal` x8 (`newPos`,
`respawner`, `levelChanger`), `Lvl05Boat` x3 (a `path`, a rider), `Lvl05Platform`, `Lvl05MagnaPlatform`,
`Lvl05Eye` x8, `Lvl05Image` x15, `Lvl05UFO` x19, `Lvl05Umbrella` x6, `Lvl05WaveIsle`, `Lvl05LightTrail`,
`Lvl05Secret`, `DreamMirror` x2, `StupidDoor` x6, `QuarkDoctor` x14 and `FlyingHypo` x70 with nine
`EnemyRespawner`s and five `enemyWave`s, a `Sharkagator` with two fins, a `wallJumpTrigger`, nine `Hookshot`s,
the Sprout-O-Matic's soil. NOT BUILT beyond what the shared classes do, with one exception that is written but
NOT YET PROVEN by any run: `Lvl05Portal` (`src/sm/classes_level05.cpp`; init 0x18a2e0, update 0x18a4a8) - within
30 m of the hero, stepping to the point half a metre before a portal holds him, a second later carries him over
one second to its `newPos`, and a portal with a `respawner` asks for it after 5 s nearby; a `levelChanger` portal
exists only once level 6 is open in the save (it then leaves for level 0x17), so natively it is never there; a
portal with no `newPos` changes to level 6. The level's way is start, the continue points 3, 10, 15 and 17, the
portal at (-116.5, 35, 0.6), four dream rooms each with its respawners, the mini-boss.

### The pause menu's native state

`src/sm/pause_menu.*`, `pause_ui.inl`: the pause menu, options, help, special, audio visual and
camera pages are drawn from the tables above in the original's plates, scaled uniformly to any window shape
(captures `build/sm_pc_work/shots/b-43-av.bmp` 4:3, `b-219-av.bmp` 21:9, `d-av.png` 16:9). Ours, in the original's
plates and panels: GRAPHICS under OPTIONS (resolution, frame cap, v-sync), the WEAPONS / GADGETS / ARMOR lists,
SPECIAL > CHEATS (the PC's play options: UYA camera, strafe, air pack, Hydro-Pack, weapon set, presentation, charge
boots, all weapons, all armor, the UYA wrench) and SPECIAL > SKINS (the seven skins of the disc and the Ratchets imported from UYA / GC /
R&C1), and SPECIAL > ARMOR SETS. Not rebuilt: QUICK SELECT, SAVE / LOAD, HELP MESSAGE LOG, MOVES HELP, SKILL
POINTS, CINEMATICS, the MAP, View Mode, the original's own fifteen cheats (Big Head ..., text 4001-4015), and the original WEAPONS /
GADGETS / ARMOR / SKINS screen layouts (HUD screens 49, 40, 65 are used only as panels).

## 2. Ghidra

`tools/sm/sm_rel_elf.py NN OUT.elf --classes OUT.json` writes one ELF per level module: the module at 0 (as linked)
with its import sites resolved, the resident executable at 0x1e62500, the resident's SNR2 symbols and one name per
class callback (`Crate__event`, `Pygmy__init`, ...). Import and export with `tools/ps2_re/ghidra_ps2.py` (about six
minutes a level); read functions with `tools/sm/sm_decomp.py NN name-or-address [--callees N] [--grep text]`.

Decompiled so far (`build/sm_pc_work/ghidra/LNN_decomp.json`): 01 to 10.

## The public repository (2026-11-16)

`sm_pc` is also published on its own, under the GNU General Public License version 3: the repository `sm_pc`,
assembled from this tree by `tools/sm/export_sm_pc_repo.py OUT_DIR`. It carries Size Matters' own code - `src/sm`,
`src/hig`, `src/media`, the meshoptimizer subset, these docs and `tools/sm` - and none of the engine the borrowed
play options are built on (a Ratchet, weapons, packs and wrenches read from the other PS2 games' discs, their
follow camera, the dive). Its build defines `SM_PC_STANDALONE` (`src/sm/extras.h`): the four source files of those
options are replaced by empty shells (`src/sm/extras_absent.cpp`) and two stand-in headers (`compat/ps2game`), the
dive is compiled out of `gameplay.cpp`, and the F1 window and the pause menu's CHEATS and SKINS pages leave their
rows out. Lines between `sm_pc-private-begin` and `sm_pc-private-end` in a source file are not exported. The
export refuses to write anything that names a private path. The level runs take another executable and disc image
from `SM_PC_EXE` and `SM_PC_ISO`, which is how the public build is checked before a release.

## 3. Native systems

| System | State |
| --- | --- |
| Object world (`src/sm/objects.*`): every placed instance of a class no other system owns, drawn with its own model and idle clip, solid when it has collision | partial: built; capture `build/sm_pc_work/shots/obj-02-y180.png` shows the Ryllus crate stacks, teleporter and ship |
| Crates, bolts, ammo and health pickups, titanium bolts, armour pickups | partial: built, break / stack / payout rules above; not yet proven in a run |
| Generic enemies (`add_generic_enemies`): model, clips by name, level hit points and damage, bolt share | partial: built; native chase / strike only |
| The Grind Lock (ten doors on six levels) | built: `Gameplay::tick_grind`, `src/sm/classes_grind.cpp`; Kalidon's first lock proven in a hidden run; see "The Grind Lock" |
| Level-specific classes (doors, buttons, bridges, cutscenes, bosses) | partial: Ryllus doors and buttons; the slide (`SlideController`, `Gameplay::begin_slide`) rides Pokitaru's path with WATER_SL and stands up at its end (log `build/sm_pc_work/logs/slide_1.log`, capture `shots/slide-sheet-1.png`); its camera, spray and loop sound are not rebuilt |
| An imported Ratchet's own controller | built for UYA (`Ps2RatchetSettings::own_controller`, SPECIAL > SKINS "Imported: Own Gameplay"): ps2game::hero_tick owns him on foot (run, skid, jumps, double jump, flips, glide, Heli-Pack and Thruster-Pack high and long jumps, charge boots) on SM's collision; SM keeps weapons, wrench, hits, water, slides and mechanisms. Captures `shots/uc-sheet-1.png`, `uc-close-2.png`, `ut-close-1.png`. His swim clips are UYA's (tread 0x35, surface 0x52, underwater 0x36: `logs/swim_uya.log`). Not done: its footstep and jump sounds, UYA's own slide clip (SM's WATER_SL is moved onto his rig; UYA's slide state is not identified), ledge grabs against SM's level, GC and R&C1 untried |
| Imported UYA guns and wrenches | the Shock Blaster and Nitro Launcher (CHEATS, Weapon Set) fire with their own effects and now their own sounds (the held gun's class sounds from the UYA disc: discharge, charge loop, empty; `logs/uws_39.log`, `uws_119.log`: 4/4 and 2/2 requests played; heard by nobody yet). UYA's three OmniWrench meshes (ARMOR global words 118 silver, 122 blue, 126 dark / orange; CHEATS, Wrench; `--uya-wrench`) replace SM's wrench model in SM's own swings, throw and hyper-strike, turned in the grip to lie as SM's wrench does (`shots/wrench-sheet-2.png`, `wrench-sheet-3.png`); hits and damage stay SM's. Not done: which mesh is which OmniWrench tier, UYA's wrench trail, the Nitro explosion's sound, further UYA weapons (each needs its update translated) |
| Weapons and gadgets beyond the Lacerator and wrench | partial: all fourteen weapons fire with their models, damage rows, sounds and glows; flight and rates are native values; gadgets not started |
| Pause menu with PC settings | partial: built on the original's menu tables and plates; see "The pause mode's screens", Native state |
| Hero skins | built: the disc's seven (`sm::load_hero_skin`, `--hero-skin N`) and the imported PS2 Ratchets, chosen on SPECIAL > SKINS; seen in game (`build/sm_pc_work/shots/skins-sheet-1.png`, `skins-sheet-2.png`) |
| Armour sets | built: tables, ownership, the thirteen sets, protection, the pieces drawn on the hero, each set's wrench power with the original's amounts; see "Armour" for what is native or not done |

## 4. Order of work

1. Object world and shared classes on every level (crates, pickups, enemies, doors, teleporters, triggers, movies).
2. Ryllus (02) start to finish, then the campaign levels in order.
3. Weapons and gadgets from the executable's definition tables (region 0x1f1000-0x1f4000 of LEVEL_01).
4. Pause menu, armour.
