# Size Matters: Luna, mechanisms and planet travel

## Native implementation — 9 September 2026

Pokitaru now loads Luna, her camera, both bolt cranks, their linked gate/bridge,
the elevator and Ratchet's ship from their BOG model/animation/COL references.
These objects were absent from the ordinary scenery placement table. The native
engine consumes the original data; no PS2 executable code runs.

| Interaction | Keyboard | XInput |
|---|---|---|
| Grab the wrench zipline / jump off | Space near its upper endpoint | A |
| Engage a bolt crank | F or R nearby | X or Y |
| Turn the crank | Hold F or use WASD | Left stick or hold X |
| Release the crank | Space or R | A or Y |
| Ride the elevator | Stand on its platform | Stand on its platform |
| Ship's planet select | R near the landing pad | Y |
| Planet select from any map | H, then R | Back, then Y |
| Choose / travel / close | Arrows / Enter / Esc | D-pad / A / B |

The planet panel is a native ImGui adaptation. All **24 WAD destinations** on
the USA disc are unlocked, including ten campaign worlds, races, Giant Clank
areas, multiplayer variants and the Treehouse. Exact ISO paths survive menu
selection: `16C.WAD` is distinct from `16.WAD`. Travel rebuilds scenery, actors,
collision and audio, preserves bolts/ammo, and provides a grounded exploration
start. Shipless maps retain return travel through Help Desk. This is session
travel, not persistent campaign progression. Special maps currently use Ratchet;
their minigame/multiplayer controllers are not implemented. The two Giant Clank
maps have only two faces in their terrain COL, so walking there is particularly
limited. Their scenery loading does not establish their original movement mode.

Luna follows the centers of her original NavBound curves and all 12 authored
wait points. Her advance volumes, photo targets and crank links govern waits.
Enemy-controller links identify local avoidance actors, not combat-clearance
gates. Camera lift/click/lower use her original clips;
her separate camera attaches to bone 61. Her transitions blend over 0.15 clip
seconds and interpolate between simulation frames. Pokitaru's two intermediate
movie volumes must be entered in order to play `01B.BIK`; normal completion and
skip remove Luna and prevent retriggering. The existing `01C.BIK` exit to Ryllus
remains in place.

Luna now uses the optional point-data pointers in the left NavBound curve.
Ten points carry jump clearances, including 3.2 units at the large log. The
previous reader discarded this fourth word and forced her to walk the entire
route. The native launch now solves the original ballistic arc to the next
NavBound section, at 0.26666668 units per 30 Hz tick, with the apex above the
higher endpoint by the authored clearance. JUMP_STA (10), JUMP_CYC (11), and
JUMP_LAN (12) play with the existing pose transitions. Grounded walking and
waiting use terrain/dynamic COL height; authored wait-point Y no longer makes
her hover. Advancing past a photo stop does not require witnessing its photo.

### Log-stop regression (9 September)

The jump arc was working, but two independent wait-state bugs prevented live
play from reaching it. The native code required every living enemy whose home
was inside the linked controller's detection volume to die, and only checked
the thin advance volume when Luna was idle. Crossing it during a photo or
while she approached her wait point was forgotten.

The original `LEVEL_01.REL` routine at **0x18EA08–0x18EA9C** checks the hero
against the current authored advance volume independently of the animation
state, increments the route index at runtime +0x4F8 and retains the update
flag at +0x4F9. **0x18EF60** consumes that flag in the state selector. Native
`advance_stage` now remembers sequential trigger crossings independently of
the currently approached wait point. Camera lowering and required crank
completion still finish before the pending stage is consumed.

**0x18F7D0** uses the controller's actor list for nearby path avoidance. The
distance rejection at **0x18FA00–0x18FA2C** compares squared X/Z distance
against **2.25 (1.5 units)**; it is not a test that the entire controller
volume has no surviving enemies. The incorrect combat gate is removed.
Exact lateral avoidance remains separate work; this correction does not
replace it with another guessed kill requirement.

`sm_luna_probe <ISO>` reproduces the old deadlocks and now checks four cases:
live original enemies, crossing during a photo, both together, and crossing
while Luna is still walking toward her first wait point. Each reaches the
crank-side wait beyond the log, observes clearance above Y=8 at NavBound
section 11, and plays JUMP_STA/JUMP_CYC/JUMP_LAN. The live-enemy cases retain
130 visible surviving actors. The previous general mechanism probe did not
call `start_enemies` and revisited each advance volume after finishing the
photo, so it could not detect these failures. That probe now explicitly
isolates waypoint/grounding checks; the dedicated replay covers trigger timing
and the real actor population.

Evidence: `mods/sm_native/luna_stall_reproduced_20260909.log` (old failures),
`luna_stall_verified_20260909.log` (four passing replays), and
`luna_mechanisms_verified_20260909.log` (full mechanism regression).

The zipline uses its authored endpoints, original grab/slide animations,
1.5-unit hanging offset, 0.004-unit per-tick acceleration, 0.3-unit per-tick
speed cap and release two units before the endpoint. Cranks use the recovered
16.6669998-second turn duration, 1.24-unit orbit and SCREW animations. Ratchet
faces tangent to the orbit, with the crank on his right, at the bolt's raised
height. The first crank lowers the gate six units; the second raises its bridge
three. The elevator follows its authored two-point path over ten seconds using
`3t²−2t³`. Live transformed COL geometry supports Ratchet, camera and weapon
queries; platform displacement carries a standing player. Jumping, damage and
death can interrupt interactions without leaving the player locked.

Crank entry also selects the wrench's own `ANIM_OPE` clip (4), with a 0.3-second
blend and an independent clock across Ratchet's SCREW clip changes. This extends
the tool to reach the bolt; its normal held pose is too short.

## Original-data evidence

Addresses are file-relative in USA `/LVL/LEVEL_01.REL`, SHA256
`cbf4a983935a05b64bf0234889c663bd1522dc4366581837a74d8b42812c726c`.
BOG offsets are relative to `01.WAD`'s BOG at decompressed offset `0x1c8a40`.

| Evidence | Location |
|---|---|
| BOG curve directory | Header `+0x4c/+0x50`, 24-byte records, 16-byte XYZ point records |
| Variant asset references | Definition `+4`, 52-byte descriptors; model `+4`, animation `+8`, COL `+20`, variant signature `+32` |
| Luna NPC / wait route | BOG `0xe930`; properties `+0x90` → curve `0x44a40` |
| Luna jump metadata / launch / flight | `0x18eab0`, `0x18f3c8`, `0x18f158`, `0x18ff78`; left point 11 metadata `0x3b65c` = 3.2 |
| Luna advance/controllers/targets | Properties `+0x38` / `+0x64` / `+0x98`; NavBound curves at `+0x18/+0x1c` |
| Luna state clips / blend | `0x18ebb0`; camera socket `0x190354` |
| Luna's removal after movie | `0x18e230`, linked cutscene controller's destroyed flags |
| Zipline | BOG `0x1bfb0`, curve `0x43fd8`; initialization `0x1ca668` |
| Zipline hero states | `0x50dd8` grab, `0x50f30` slide, `0x5112c` offset, `0x515a0` acceleration, `0x516a8` exit |
| Cranks | BOG `0x15630/0x156b0`; inactive/completed heights `0x155824` onward |
| Crank duration / orbit | Constants `0x1f4794..0x1f479c`; perpendicular orbit and bolt-height placement `0x4f0bc..0x4f1e8`; progress `0x4f390` |
| Open wrench for cranking | `0x3ff30..0x3ff54`: weapon clip 4, blend 0.3, start time zero, followed by hero crank entry `0x4e6d8` |
| Gate and bridge | BOG `0x15830/0x158b0`; variant movement table `0x21f040` |
| Elevator | BOG `0x1c530`, curve `0x44b30`; init `0x1909c8`, update `0x190d50` |
| Ship | BOG `0x329b0`; model `0xac748bc2`, animation `0x61a15418`, collision `0xaf34aa15` |
| Intermediate movie | Controller BOG `0xe6b0`; properties `+0x54/+0x58` link triggers `0x32a30/0x32abc`; dispatch `0x18a678` |

The read-only inventory helper is `mods/sm_native/mechanisms_re.py`; annotated
disassembly and inventory outputs are alongside it. These are local research
artifacts derived from the user's disc, not bundled game assets.

## Theia / IRIS spill-over

`hig_core` now exposes BOG curves and each gameplay placement's exact asset
references. After `open_hig_level`, IRIS can inspect them with:

```json
{"cmd":"bog_info","curves":1,"curve_offset":0,"curve_limit":20,"point_limit":64}
{"cmd":"bog_info","placements":1,"placement_offset":0,"placement_limit":40}
```

Curve results include source offsets, names, flags, total point counts and
bounded point arrays. Each point also has a `point_data` entry containing the
optional data source offset, raw first word and finite float interpretation. Placement results include class-definition index, source
offset, flags, transform and model/animation/collision/variant hashes. Existing
trigger inspection remains available. Native state machines are not an editor
simulation feature; the shared reader/inspection improvements are available to
Theia independently.

## Verification and remaining work

`sm_mechanisms_probe` loads the real ISO, finishes both cranks, checks gate/bridge
endpoints, rides the elevator, exercises zipline grab/slide/release, visits all
12 Luna waits and her camera sequences, asserts all ten jumps, all three jump
clips, log clearance and ground contact at every wait, checks movie trigger ordering and loads
all 24 destinations with return travel. The existing gameplay, encounter,
level-loop and transition probes pass. Hidden DX11 runs cover the ship, planet
panel, Luna, cranking, zipline and actual travel to `16C.WAD`, with zero debug
errors. Theia and the native host rebuild successfully. No physical controller
or emulator comparison was performed.

This remains a native reconstruction, not verified retail equivalence. Luna's
4.5-unit/second corridor navigation remains an approximation and exact nearby
enemy avoidance is not yet reproduced;
her full avoidance, cower states and reactive script behavior remain. The
continuous ballistic equation and exact endpoint snap replace the original
ceil-to-ticks integration; lateral route choice still follows corridor centers.
Cranked-object spring return and release dynamics need the remaining original
update translated. Elevator crush handling, activation scripts and other
variant-specific policies remain. Ship boarding/takeoff/landing clips exist
but travel currently loads directly.

The next campaign priorities are the intermediate movie's scenery destruction
and enemy-wave callback, full EnemyController/EnemyWave sequencing, shootable
door targets, destructible crates and pickups, remaining weapons/gadgets and
their vendor upgrades, other planets' mission scripts and persistent save slots.
Further rendering work includes other planets' environment controllers,
material/effect fidelity and DX12/Vulkan backends.
