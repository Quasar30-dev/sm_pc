# Pokitaru encounters, swimming and rigid props

Native engine pass for the user's SCUS-97615 USA PS2 ISO. Original game files
remain read-only. The source executable is an oracle; no guest code is executed.

## Playable behaviour

Pokitaru loads the original textured, skinned Crab, TrainingBot, TMRobotTorsoB
and TMRobotHeadB actors from its own WAD. Their authored BOG placements and
scales drive the native actors. Idle, notice, chase, attack, hit and death
states sample the original animation banks. Eight torso instances carrying
`initNoDraw` remain hidden. Nearby enemies require terrain line of sight.

Blaster projectiles sweep against enemies and terrain; modest native aim
assistance lets the hand-height weapon hit low crabs. The wrench damages
enemies within its forward strike window. Enemies attack Ratchet with melee
or the torso robot's three-shot projectile sequence. Damage updates the HUD,
plays hit/death poses and uses a short invulnerability period. Kills award
native bolt rewards; death returns Ratchet to the level start.

Walking into deep ocean enters swimming. WASD/left stick moves, Space/A jumps
out, and weapons are holstered while swimming. Ratchet uses the original
wade, tread, swim and water-jump clips; Clank stays attached to the animated
back joint. The swim clips already lower the root below the actor origin, so
buoyancy places that origin at the recovered sea plane (-4.9 m), keeping the
head above the water. Water jumping clears the authored beach edge. The same
fixed gameplay clock used by combat controls water movement.

## Original asset and placement evidence

Pokitaru's BOG begins at decompressed `/WAD/01.WAD` offset **0x1c8a40**.
Class definition records are 68 bytes at BOG +0x2c; instance records are 128
bytes. Definition +4 selects the variant table. Each 52-byte variant stores
GDE hash at +4, animation hash at +8 and collision hash at +12.

| Class | Class CRC | Instances | GDE payload offset | Animation payload offset |
|---|---|---:|---|---|
| Crab | b66d8ce9 | 58 | 2bee80 | 2d3440 |
| TrainingBot | 54a0cb18 | 30 | 2f8280 | 30b500 |
| TMRobotTorsoB | 5dda0218 | 24 | 296f80 | 2a7500 |
| TMRobotHeadB | 128c157b | 26 | 35f500 | 377cc0 |

The original class property declarations are in `LEVEL_01.REL` at:

| Class | Declaration offset | Property bytes | Fields consumed |
|---|---|---:|---|
| Crab | 21e8a0 | 50 hex | scale +44 |
| TrainingBot | 229480 | 40 hex | retained for further controller/wave work |
| TMRobotTorsoB | 228c80 | 60 hex | alertRadius +44, initNoDraw +5b |
| TMRobotHeadB | 2288c8 | 50 hex | alertRadius +40 |
| ShakeyBush | 226528 | 60 hex | retained, including enemyController +4c |

The shared BOG reader retains source offsets, flags and bounded known
properties. The 13 ShakeyBush objects resolve directly through their BOG
variant to rigid GDE **5c738e6c**, payload **2eb100**. They now appear at their
actual placements in both Theia and the native engine.

Animation v12 clip records are 64 bytes, starting at asset +12. The event
table pointer is +8 and event count is byte +53. Events are 32 bytes:
name[8], float seconds at +8, flags +12 and sound hash +20. Preserve event
times beyond the last pose frame: the torso's ATK3SHOT has FIRE markers at
**0.1187, 0.7021 and 1.2021 seconds**, all using sound **9841dd39**. Ending
the attack at the pose duration would silently drop its third shot.

Crab ATTACK has its strike marker at **0.3354** (sound c16417e2), TrainingBot
at **1.7021** (183cc598), and Head ATTACK at **0.4354** (81f9cd83). The shared
animation reader exposes these events; native playback dispatches their sound
hashes through the existing retail audio banks. Attack windows use these
markers as evidence, pending full translation of the class event handlers.

Ratchet's bank has WATER_WA **35**, SWIM_TRE **36**, SWIM_JUM **37**, SWIM **38**,
GET_HIT **18** and DEATH **62**. Clip 2 is WALK2, not a death animation.

## Umbrella and foliage corrections shared with Theia

`umbrella01.mb.ps2.gde` has hash **2bbe4b75**, payload **4a8340**, and nine
authored rigid placements. LOD 0 has eleven stream records. Stream 0 is the
intact canopy/pole; streams with part selector **0x11..0x1a** are separate
break pieces with their own entries in the part transform table. Combining
them all at the intact transform caused the deformed canopy. Intact decoding
now excludes bit-4 break-piece streams. The intact umbrella contains **76
triangles**, including a pole spanning local Y **-4.074..0.783 m**.

The invisible pole had a second cause. The resident executable's VU renderers
use different texture fixed-point scales:

| Renderer | File offset | UV immediate | Immediate offset |
|---|---|---|---|
| tie_vu_renderer | a4ca0 | 3a800000 = 1/1024 | a4df8 |
| shrub_vu_renderer_fast | a42e0 | 3a800000 = 1/1024 | a4548 |
| staticmesh_vu_renderer_fast | a0ae0 | 3b800000 = 1/256 | a0d00 |

Rigid props and shrubs divide raw UVs by `1024`; terrain uses `256`.
The render-header float is not an additional PS2 UV divisor (see the
[subsequent shrub correction](SIZE_MATTERS_SCENERY.md)).
The pole's raw pair **(759,-1567)** becomes
**(0.7412109375,-1.5302734375)**. Wrapped into its original 64x64 canopy atlas,
it samples opaque white. The old divisor sampled alpha zero. This also fixes
the fourfold repetition on the beach chairs, foliage and other local props.

## Validation and remaining work

`sm_encounter_probe <ISO>` checks original assets/events, all 138 placements,
intact umbrella geometry and opaque pole UVs, finite actor skinning, blaster
and wrench kills, enemy melee and ranged hits, damage cooldown, death/respawn,
terrain occlusion, paused updates, swimming buoyancy/poses/weapon suppression,
water jumping and returning onto Pokitaru's actual beach collision.

The combat arena portion isolates native combat with synthetic floor/wall
geometry while using the actual decoded actors and animation events. The
swimming and asset checks use the original Pokitaru world. Hidden DX11 runs
exercise the real host without a controller or emulator. `--player-position
x,y,z` provides an explicit diagnostic starting position; it does not change
the ISO, a level file or a save.

Validated builds: `sm_pc`, `theia`, `sm_encounter_probe`, `sm_gameplay_probe`
and `hig_probe`. The encounter and player probes pass, and the ISO-wide HIG
inventory/decoder checks pass. Hidden 60/120 fps captures of the same swimming
state are byte-identical (SHA256
`94855968CDD56AE84E840C3AD95EC0D45F4FF91FAF69A774371D4FDA52F7FCCE`), with zero
DX11 validation errors. A real Pokitaru robot encounter at (-13.65,-4,2)
damages Ratchet from 10 to 8 health; four blaster shots defeat it. Its headless
audio capture contains 21 mapped action events. Local evidence:
`mods/sm_native/encounters_verified.log`, `combat_verified.log`,
`combat_verified.bmp`, `combat_verified.wav`, `swim_verified_60.bmp`, and
`enemies_umbrella_fixed.bmp`.

This is a playable native encounter foundation, not a claim of PS2 gameplay
equivalence. Native health, rewards, speeds, collision bodies, aim assistance,
invulnerability and water dynamics still need retail tuning. EnemyController,
EnemyWave, navigation curves, scripted spawning, torso/head mount/return
relationships and ShakeyBush ambush/shake logic are not yet executed. Bushes
are currently placed scenery. Sharkagator water attacks and full level
objectives/progression remain separate work; drowned/underwater gameplay and
the full retail arsenal are not implemented by this surface-swimming pass.
