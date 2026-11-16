# Size Matters USA: frontend and gameplay loop oracle

Read-only findings from the user's SCUS-97615 ISO. Addresses below are **file
offsets within the named REL**, not resident virtual addresses. No guest
instructions are executed. Disassembly windows under `mods/sm_native/` include
complete source-module SHA256 and exact relocation annotations.

Additional [wrench movement, deformation, throw and hyper-strike findings](SIZE_MATTERS_WRENCH.md)
include the original controller windows, springs and independent weapon bank.
[Photo mode](SIZE_MATTERS_PHOTO_MODE.md) also documents the shared skin-reader
fixes found by checking model/animation pairs across the entire disc.

## Frontend title and localization

`FRONTEND.REL` SHA256:
`81f834bdf092587eb94e6a198fe2ab6181877dbd215f7124b405edda9b1a3daf`.

Title update is state 12 in function `0x19860`. State lives at `0x30574`;
the switch has 54 entries at `0x108b90`, indexing state minus one. State 12
enters at `0x19f48`. Its menu structure is `0x309f0`: pointer table, count,
selection at offsets 0, 4, 8. Four pointers at `0x309d8` identify these items:

| Item offset | Text ID | English label | Action |
|---|---:|---|---:|
| `0x30720` | 697 | LOAD GAME | 99 |
| `0x30638` | 698 | NEW GAME | 98 |
| `0x30808` | 452 | OPTIONS | 79 |
| `0x308f0` | 598 | MULTIPLAYER GAME | 94 |

Each item contains text ID at +0, normal/selected anchors at +4/+8,
enabled/focusable bytes at +CC/+CD, and action at +D0. Load Game's enabled
byte is replaced at runtime from `0x20810()`; its on-disc true value does
not prove a save exists.

`0x12200..0x122d4` implements wrap/skip navigation. Up takes precedence over
Down, disabled current selection advances automatically, disabled or
non-focusable destinations are skipped, and a full rejected cycle terminates.
Title state checks Cross before calling navigation and only dispatches enabled
items. It resets/decrements an attract timer at `0x30630` (900); movies and
the complete attract return path remain unimplemented.

New Game action 98 calls `0x19740` -> `0x16bf0`. The latter sets `0x30428=0`
and requests mode 15 through `0x1ae0`. Load uses the related `0x16c48` wrapper
with `0x30428=1`. These are screen/mode requests, not WAD IDs. The reused
wrappers conditionally call unresolved `PAUSEMODE_GetCurrentPauseScreen`
when byte `0x30540` is set. Four save-slot rows are drawn in
`0x162f0..0x16868`. Full slot selection/overwrite/retry reconstruction remains.

`/HUD/LOCALDAT.BIN` has 734,904 bytes. Header magic is `LOC_DATA`; bytes 8..11
are preserved version bytes, byte 12 is language count 5, and little-endian
u16 at 13 is text count 2,132. `SCOL` at 0x10 is followed by eight color words;
`LTOC` at 0x34 precedes 12-byte language records `{base,size,auxiliaryOffset}`:

| Language index | Base | Size | Relative auxiliary offset |
|---:|---|---|---|
| 0 (English) | `0x74` | `0x21bb8` | `0x21174` |
| 1 (Spanish) | `0x21c2c` | `0x233f0` | `0x229ac` |
| 2 (French) | `0x4501c` | `0x24788` | `0x23d44` |
| 3 (Italian) | `0x697a4` | `0x23f64` | `0x23520` |
| 4 (German) | `0x8d708` | `0x25980` | `0x24f3c` |

Each language begins with 2,132 `{TDEF,id,relativeTextOffset}` records of 12
bytes. Offsets are relative to **that language's base**, and point four bytes
past an `ENTR` marker to NUL-terminated encoded text. The reader validates
bounds, uniqueness and all entries. Auxiliary structures and the final 1,584
bytes remain uninterpreted. Raw strings are preserved; inspection strips
observed color control 0x90 plus argument, and shows unresolved controller
glyph bytes explicitly. Latin-1 inspection is not a complete font mapping.

Text 591 is the new-game confirmation, 779 contains the OK glyph/text, 536
contains Cancel, and 2684 is EMPTY. PC prompts substitute keyboard/XInput
identities rather than displaying unidentified original glyphs to players.

## New Game handoff

State 23 (`0x1a99c`) preserves 0x48 bytes of settings starting at context +8
around `0x2f80(context,1)`. In the reset routine:

- `0x2fd8..0x2fe4` calls memset with size **0x13f60**.
- `0x2fc8` sets s0=1; `0x3030` stores it at context **+0x1c6c**.
- `0x305c` clears context **+0x13f40**, subsequently incremented in the
  normal gameplay mode. Its precise progress/save meaning is not established.
- `0x1ab58` starts the save request; later `0x1abd8` passes context +0x1c6c
  to `0x1b40(level,1)`.
- `0x1b40` requests mode 7 with the level as its argument. The eventual
  handoff is received by `0x2e88`, stored through `0x2d30` at `0x27e90`, read
  by `0x2dc8` and sent through `MCPI_SendNewLevel` at `0x194dc`.

This establishes **level 1** as the reset destination. The native bootstrap
loads `/WAD/01.WAD` using shared data readers. It does not serialize the reset
context or claim to reproduce save/memory-card behavior yet.

## Normal gameplay mode and time step

`LEVEL_01.REL` SHA256:
`cbf4a983935a05b64bf0234889c663bd1522dc4366581837a74d8b42812c726c`.

Entry `0x196f8` calls the per-frame mode dispatcher **0x1a590** at `0x197cc`.
The current/requested modes and argument occupy `0x22fbe8..0x22fbf0`.
The ten-entry switch is at `0x20c270`. Mode 4 calls **0x1ad98** and **0x1b388**
(normal update/render paths), increments context +0x13f40, and continues the
outer frame services. Requested mode changes are committed later by
`0x1a938`; there are distinct mode entry/exit dispatchers.

At **0x1adf8..0x1ae00**, the normal update loads IEEE754 **0x3d088889** into
f20: **1/30 second**. The same value is passed to several update calls,
including `0x94098`, `0x7d9e8`, `0x380c8`, `0x7d578` and conditionally
`0xa0008`. Therefore the native normal-loop clock uses 30 Hz while presentation
can be 60/120 fps. This does not establish all minigame/cutscene cadences,
internal substeps or equivalence of every timing-sensitive behavior.

## Ratchet binding and input/update chain

The `Ratchet` class record at `0x225460` points to name `0x225458` and four
callbacks: `0x19d4f8`, `0x19d558`, `0x19d560`, `0x19d568`. The two middle
callbacks are empty return stubs. The last gates event dispatch against the
pointer at `0x2356dc` before calling `0x3bc00`; event meanings remain unknown.

The first callback calls `0x3b120(moby,0)`, which calls
**0x3a5a8(playerState=0x235140,moby,0)**. That routine rejects a second binding
if state +0x5b0 is nonzero, otherwise stores the moby there. It queries
per-object data using `0x18f38`, copies moby translation +0x30/+0x34/+0x38
into player state, and initializes contact/orientation data. It also has a
conditional setup path; merely reading a BOG transform is not the full spawn
procedure. The wrapper ORs moby +0x64 with 0x04000000 and conditionally calls
`0x19dc18` when the binding returns 1.

Normal update calls **0x380c8(dt)**, which forwards player state 0x235140 to
**0x37f18**. This clears/constructs its input region +0xC0..+0xEC, allows a
special handler through +0xDD4, and obtains the selected input context using
`0x87e48`/`0x87e78`/`0x87f88`. Contexts 2 and 0x10 are explicitly distinguished.
The gathered data passes through `0x484b0`, then
**0x37a60(playerState,dt)**, followed by `0x40f28` and `0x4b4c0`.

Inside `0x37a60`, a special handler at +0xDD4 may take over; missing actor
pointers have separate handling. A flag in the 64-bit state field +0x960 can
select a two-iteration branch that calls `0x4b4e0`, `0x3dff8`, `0x41418`,
`0x42078`, `0x3eed8`, `0x3eb20` and `0x23150`. Do not label all of these as
movement or collision without tracing them. The presence of multiple passes
is another reason not to replace this with an arbitrary delta-time controller.

Position setter **0x3b180** writes player transform translation +0x30, mirrors
it into the actor at +0x59C when present, clears +0x2E4, and writes the supplied
Y to +0x2DC/+0x2E0. `0x3b210` projects an input direction onto the plane normal
to player up (+0x10), normalizes above an epsilon and stores +0x200/+0x210.
These field uses are evidence; the complete actor/physics schema is pending.

The shared BOG and player/rig readers now consume this residency. The native
controller still needs the original input contexts and contact-response stages
above; current movement, weapon trails and terrain collision do not establish
retail gameplay equivalence.

## RUN animation playback

The raw `RUN` clip (3, duration 0.76666665 seconds) does not supply its gameplay
playback rate. In USA `LEVEL_01.REL`, the normal locomotion call at
`0x42e28..0x42e40` passes table `0x1f4638` to the gait selector `0x4a150`.
Its 28-byte rows contain reference displacement, maximum displacement, clip,
phase landmarks, and gait links. Relevant original float values are:

| Row | Reference displacement | Maximum displacement | Clip |
|---|---:|---:|---|
| `0x1f4654` | 0.008333001285791397 | 0.012000001035630703 | 2 / WALK2 |
| `0x1f4670` | 0.08332999795675278 | 0.12000000476837158 | 3 / RUN |

For RUN, `0x4a394..0x4a3d0` clamps displacement to the maximum and writes
`1 + (maximum/reference - 1) * (distance-reference)/(maximum-reference)`
to `player->actor(+0x59c)->animation(+0x48)->speed(+8)`. Algebraically this
is `distance/reference` above the reference. The lower branch
`0x4a3d4..0x4a458` uses `(distance-previous_reference)/(reference-previous_reference)`.
The selector also changes gait clips and preserves stride phase; that complete
WALK2/RUN transition controller is not yet implemented by the native player.

The timing distinction matters: `0x37ba0..0x37bec` runs the movement path twice
per normal 30 Hz update. Each pass snapshots position at `0x3e020`, adds its
horizontal movement at `0x48ee8`, and measures that pass's XZ displacement at
`0x3eee0..0x3efc0`, storing it in player `+0x270`. It is **not** the full
30 Hz frame's displacement or an arbitrary display-frame delta. Normal RUN
target selection at `0x481b8..0x481f4` multiplies the reference by **1.2**
(`0x3f99999a`), yielding about 0.099996 units per pass and ordinary 1.2x
playback. The approximately **1.440058x** table limit is a cap, not the
normal-running constant.

The native player retains its existing **6 units/second** movement. Actual
horizontal travel is normalized to 1/60-second passes, then evaluated by shared
`hig::ratchet_run_playback_rate`. At exactly 6 units/second this produces
approximately 1.200048x because the authored reference is rounded. Collision
can reduce the measured travel. Both simulation animation time and presentation
interpolation use the resulting rate. Starting/stopping running fire retains
the gait phase; the firing layer, weapon recoil, Clank and helipack have separate
unscaled clocks. Generic animation sampling and other actors' rates are unchanged.

The ISO-backed gameplay probe checks the original table, normal rate and cap,
3-unit travel over half a second, accelerated RUN time, interpolated skinned
vertices, and independent attachment/recoil clocks. These checks establish the
native integration of this recovered calculation, not complete retail movement
or collision equivalence. No original executable code is executed.

## Player skin UVs and attachments

All addresses below are original **file offsets** for the USA revision above.
Original binaries remain read-only. Captures and disassembly windows are local
research artifacts under `mods/sm_native/`.

### Texture coordinates

The resident `skinnedmesh_vu_renderer_fast` is at `0x9e3a0..0x9ea70`.
Relative to that exported window (which includes its VIF header), the immediate
at `+0x228` is IEEE754 **0x3a800000 = 1/1024**. `MULi.xy` at `+0x234`
scales the texture transform; `ITOF0.xyw` at `+0x2ec` converts integer UVs,
then the multiply at `+0x34c` applies the scale. Thus skinned UVs must not use
the static GDE decoder's fixed-point divisor. Default Ratchet's first UV pair
in HEROSKIN.PAK entry 29, GDE `+0x2f24`, is `(1735,995)`, decoded as
`(1.6943359375,0.9716796875)`, retaining texture wrap.

### Joint composition and Clank's root pose

`LEVEL_01.REL` back-attachment routine `0x38a60` loads the joint ID from
`0x1f4714` (**5**) at `0x38a74`, calls the joint query `0x8bcc8`, copies
the animated joint matrix and multiplies its translation by GDE render `+0x10`
(**8** for these assets). It composes this with Ratchet's world matrix using
`0x125128` at `0x38b6c`, writing the actor in player state `+0x5a0`.
The optional companion at `+0x5ac` receives the same matrix. The matrix helper
does ordinary `joint * actorWorld` composition; it does not supply a guessed
backwards offset or a camera-facing constraint.

The missing correction is data in `SP.WAD`'s `ClankPackMaster.mb.anim`
(decompressed payload **0x256340**, 9,080 bytes). Clip 0 (`CLANK_ID...`) and
clip 1 (`CLANK_GL...`) both have a bone-0 rotation/translation track. Its first
translation is signed source values **(-81,0,51)**, decoded to metres as
**(-0.081,0,0.051)** in the attachment's local coordinates. The unpacked
10-bit quaternion is approximately **(-0.10264,0.00098,0.99413,0.00098)**
before normalization. Applying this root pose skins/turns/offsets Clank before
the back-joint composition, placing him behind Ratchet and facing rearward.
The Clank class callbacks at `0x158e60` and `0x158e98` initialize/update the
actor animation state through `0x639e8`. The renderer must not substitute an
unanimated Clank bind pose for that animation state.

The blaster path at `0x3925c..0x39290` selects joint ID `0x1f4704` (**56**),
with composition at `0x393c4`. The second weapon path uses `0x1f4708`
(**76**). Other weapon classes select 54 or 55; these are distinct sockets,
not interchangeable fixes for handedness. `0x8bcc8` optionally applies the
skeleton `+0x14` LOD remap before indexing 64-byte joint matrices. The current
highest-detail Ratchet skeleton has no remap. The host's joint translations
are already in metres, so its attachment composition must not multiply by 8
a second time.

### Camera handedness

Resident `LookAt` at **0x1ff90** constructs its view axis from **eye - target**
(`0x1ffb8`, `0x1ffc4`, `0x1ffe4`). `Perspective` at **0x20570** subtracts
the old fourth row when constructing the third row (`0x20664..0x2067c`),
producing clip W = -view Z from an identity input. These agree with a
right-handed camera. A left-handed native camera mirrors the original world:
the original right-hand joint 56 has negative model X, while left-hand 76 has
positive X. Correct the camera and movement/orbit basis together; moving the
weapon to joint 76 would put it on the anatomical wrong hand.

The shared animation reader uses the original compressed tracks: version 12,
64-byte clip records, sparse 16-byte track records, time u16/256 seconds,
four big-endian 10-bit quaternion components, signed translation components
in tenths of a source centimetre, and 10-bit scale components /64. Rotation
unpacking is visible at LEVEL_01 `0x8a148`; translation at `0x8a35c`; scale
at `0x8a470`. Native interpolation is SLERP/linear; main-layer crossfades and interruptions are now recovered and implemented; see
[animation transitions](SIZE_MATTERS_ANIMATION_TRANSITIONS.md). Complete retail
blend trees and special state selection remain separate work.

## Pokitaru sky and ocean

These offsets use the same USA `LEVEL_01.REL` and resident hashes above.
The decompressed `01.WAD` BOG begins at **0x1c8a40**; BOG offsets below are
relative to that section. Data is consumed without executing or patching the ISO.

### Sky controller

The `Level01SkyController` class record is at **0x2234e0**, referencing
callbacks **0x18bd60**, **0x18bdb8**, **0x18bdc0**. BOG class signature is
**0x88a4e775**. Its initialization samples a random angle in [0,2pi), while
update adds float **0x3a11a373** (0.0005555666867 rad) per normal 30 Hz tick.
At `0x18be48..0x18be68`, sky index **1** receives the rotating matrix; the
other layers receive zero translation. The render callback at
**0x18beb0..0x18bf30** copies active camera matrix X/Z into all sky matrices
(`0x18bf04..0x18bf18`), leaving Y unchanged. The controller's .01 scaling
converts source centimetres, already accounted for by the shared GDE decoder.

`0x17f68..0x1804c` disables depth testing/writing around the sky loop and
draws enabled sky layers in array order. Treating the domes as ordinary opaque
world objects, or leaving the host's old shell-preview flag disabled, loses
that behaviour. The native renderer now has an explicit background pass.

The WAD's three section-1 GDEs are:

| Asset | Decompressed payload | Signature |
|---|---|---|
| pokisky01 | 0xd2bc0 | 0xc089e49d |
| pokisky02 | 0xd39c0 | 0xb717366d |
| pokisky03 | 0xd4680 | 0x2cb27a02 |

They retain their original materials and GIM references; the middle layer
rotates and the last includes distant scenery/horizon geometry.

### WaterWaves binding and tile draw

`WaterWaves` class record **0x229d20** references initializer **0x1c6530** and
update **0x1c6648**. Update queues custom draw **0x1c6690** and advances its
time values by the simulation dt. The BOG class signature is **0x49742992**:
definition **0x2c5c**, instance **0xe7b0**. Definition +4 points to a 52-byte
model descriptor at **0x114**; descriptor +4 binds GDE signature **0x89c489d1**.
That is `waterwaves.mb.ps2.gde`, WAD payload **0x24a080**. This is an exact
descriptor binding, not a class-name substring match. The asset has two LOD
meshes; neither was previously placed by the native host's static-scene path.

Instance +0x34 gives sea Y **-4.900000095**. Instance +0x54 points to the
property data at **0x5630**, floats **0.07** and **1.2**. The property reads
at `0x1c6704..0x1c6748` give Y scale:

`waveScale * (1 + 0.5 * sin(time * waveSpeed))`.

The original geometry contains the wave shape. No new procedural sine mesh is
needed. Its normal GDE material references **0x4e89ab44**, `watersurface.gim`
at WAD **0x580a40**. Initialization also resolves environment texture
**0x5dd0eb6a**; that pointer is not read by this recovered primary tile draw.

Original constants **0x208f7c = 100**, **0x208f80 = 0.7** determine a 70 m
cell. At `0x1c6774..0x1c67b0`, grid centre is rounded with
`floor(playerXZ / 70 + 0.5)`. Loops `0x1c6d78..0x1c7020` cover indices
`centre-3` through `centre+2` on each axis (36 tiles). A squared grid distance
greater than **2** selects outer LOD 1; other tiles use LOD 0, giving 9 near
and 27 far tiles. Outer translation Y is lowered **0.5 m** at
`0x1c6ef0..0x1c6f10`. Tile X/Z comes from the grid; the WaterWaves object's
authored X/Z is not used as an ocean-sized placement offset.

The draw adds **0.0001** to 0.7 for mesh X/Z scale at `0x1c66bc`, giving
slight seam overlap. It does not store this back into the global scale, so
the ocean must not grow with rendered frame count. LOD radius/centimetre
conversion at `0x1c6d1c..0x1c6d40` is already included in decoded positions.

UV offset code **0x1c6c64..0x1c6cd8**, using constants at
**0x208f90..0x208f9c**, gives, with `t = seconds/10`:

- U: `fmod(t/17 + cos(t/40), 1)`
- V: `fmod(t/22 + sin(t/45), 1)`

Texture wrapping is enabled at `0x1c6d68`. The primary native water pass
keeps depth testing, disables depth writes and composites after opaque actors.
The source uploads light/alpha constants including `(128,128,128,64)` at
`0x1c6a48..0x1c6a94`; the host retains half-alpha water in its primary pass.
Full lighting/effect equivalence remains unverified.

The portable `higenvironment` functions reconstruct these transforms/UVs.
Shared BOG reads are bounded to the declared BOG section, including descriptor
and property references. The editor receives a static preview; the native
renderer updates the tile grid, wave scale and sky matrices every frame from
the fixed simulation clock. Other environment-controller classes must be
recovered independently rather than inheriting Pokitaru's constants by name.
