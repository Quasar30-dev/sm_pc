# Size Matters wrench movement, deformation and attacks

Recovered 8 September 2026 from the user's unmodified USA PS2 image. The native
engine consumes models, animations and textures as data; it does not execute
the PS2 code. All offsets below are **file-relative**.

- `/LVL/LEVEL_01.REL` SHA256:
  `cbf4a983935a05b64bf0234889c663bd1522dc4366581837a74d8b42812c726c`
- `/SCUS_976.15` SHA256:
  `d8f1794a5b4f6bd7f70ca62ae11fb790d1c300c2a877ef1aef4c1855fd6575ac`

## Native controls and behavior

**F / X** starts a ground swing; subsequent presses queue the remaining two
swings within the original chaining windows. Each attack supplies its own forward
movement. The second and third windups permit steering before their drive window;
the lunge itself retains its heading and collides with terrain. Normal running
remains **6 units/second**, with the separately recovered RUN animation rate.

**G**, **C + F**, or **LT + X** throws the wrench from the ground. The original
windup precedes release from right-hand joint 56. The weapon spins outward,
returns toward Ratchet, and resumes the catch/recovery clip. It damages targets
on each travel leg, reflects its travel phase on an outbound terrain hit, and
does not consume ammunition. Movement becomes available after the original
release-recovery marker; the weapon continues its flight independently.

**F / X in the air** performs hyper strike: upward windup, slowed airborne pose,
accelerated descent, one ground impact and animation recovery. Glide and normal
jump input cannot replace those phases. Hurt, death, swimming and relocation
cancel stale attack/flight state.

## Three-swing controller

Player update `0x37ba0..0x37bec` performs two movement passes per 30 Hz update.
Current player state is `+0x100`; requested state is `+0x102`. Dispatch table
`0x20e260` maps states 9/10/11 to `0x44c04/0x44f50/0x45260`.

| Swing | Hero clip | Initial authored time | Forward-drive interval | Wrench deformation starts |
|---|---:|---:|---:|---:|
| First | 12 `COMBO_1o` | 0.4 | [0.4, 0.6666667) | 0.4 |
| Second | 13 `COMBO_2o` | 0 | [0.26666668, 0.53333336) | 0.06666667 |
| Third | 14 `COMBO_3o` | 0 | [0.16666667, 0.6) | 0.2 |

All intervals are **authored clip seconds**, not elapsed wall-clock seconds.
Selection at `0x3fd78..0x3fe78` sets hero playback to **1.6x**. The drive
windows at `0x44ca4..0x44d24`, `0x44fb4..0x45034` and `0x452c4..0x45344`
set target displacement to **0.08 per movement pass**, or zero outside the window.

The controller fields are current displacement `+0x23c`, target `+0x240`,
derivative `+0x244`, gain `+0x248`, damping `+0x24c`, clamp `+0x250`.
Initialization at `0x382a0`, through `0x13680`, sets current/target/derivative to
zero, gain **0.1**, damping **0.2**, and clamp **100000**. A pass computes:

```
derivative = clamp((target - current) * gain - derivative * damping, -limit, limit)
current += derivative
```

Deceleration multiplies gain by **1.5**. The native implementation keeps this
state across a chained combo and uses two 1/60-second capsule moves. An isolated
first swing from rest travels approximately **0.598 units**, with no stick input.
The normal RUN speed is not used to produce that movement.

Chaining at `0x44eb0..0x44f4c` accepts the next press in **[0.7, 1.0666667]**
and transitions once first-swing time is **greater than 0.8**. The second swing,
`0x451c0..0x4525c`, accepts **[0.33333334, 0.9]** and transitions after
**0.6666667**. Early presses do not queue another swing; holding the button does
not auto-combo. The previous native 0.25-second post-swing grace is removed.
Without a queued press, the full authored recovery plays.

Windup steering uses the planar form of angular spring `0x48948`, before each
stage's drive interval (`0x44cd0`, `0x44fe0`, `0x452f0`). Attack gain is **0.15**.
Initialization `0x38160..0x38298 -> 0x136a0` supplies damping **0.2**, cap **10000**;
aiming state 1 overrides the cap to **0.05** at `0x4289c`. The native controller
retains that override after standing fire. Short-arc error, stick-strength
response factors and the two movement passes follow the recovered arithmetic;
full three-dimensional aiming/contact modes remain separate controller work.

Crossfade durations and interruption behavior are documented in
[animation transitions](SIZE_MATTERS_ANIMATION_TRANSITIONS.md).

## The wrench is an animated model

`GLOBAL.WAD` fileinfo `0x27e9c0` contains the **2424-byte**
`WrenchDefaultMaster.mb.anim` (payload follows the 192-byte fileinfo). It has
five clips: `ANIM_IDL`, `ANIM_EXP`, `ANIM_CON`, `ANIM_1OF`, `ANIM_OPE`.
Clip 3 has two tracks and duration **0.53333336**. It changes the wrench's own
bone transforms and deforms the skinned geometry. Rendering a bind-pose wrench
under Ratchet's animated hand cannot reproduce this.

`0x44c54..0x44ca0`, `0x44f50..0x44fb0`, and `0x45268..0x452c0` select wrench
clip 3 and set its controller rate to **1.6x** at the times above. The weapon's
clock begins at its own start marker; it must not be sampled at Ratchet's raw
clip time. Hyper-strike landing starts wrench clip 3 at `0x3fa6c..0x3fa8c`.
The back attachment remains joint 5; the normal weapon grip remains joint 56.

## Throw

State 36 selects hero clip **55 `WRENCH_T`** at `0x3fcb4..0x3fd00`, seeking
**0.4 seconds** with rate **2.3x**. Update `0x44b2c..0x44c00` permits normal
movement after **1.16666663**, slows playback to **0.2x** until **2.33333325**,
then uses **1x**. Moving can leave the full-body throw pose while flight continues.

Wrench class update `0x1c7680` releases the weapon at hero time **1.1** and calls
`0x12fe40`. That helper copies the weapon matrix, captures its origin, constructs
a flight basis from Ratchet's right/up/forward axes, and creates a target **6 units**
along the aim vector. Pivot data at `0x218280` is
`(-0.1537400037, 0.0321599990, -0.1537200063)`.

Flight `0x1301f8` uses configuration `0x22a208`: progress step **0.010638297535**,
spin step **0.41887906194 radians**, hit callback **0x1c7af8**. At `0x12f218`,
progress `p` becomes `t = 2p` outbound or `t = 2(1-p)` returning, with weight
**`2t - t*t`**. Position interpolates from the current endpoint toward the captured
outward target using that weight. On return, the endpoint follows Ratchet at
Y + **0.3**. `0x123f10` constructs the weapon's Z spin matrix; `0x1241b8` rotates
the pivot around Y before applying Ratchet's matrix.

At `0x130514`, an outbound collision reflects phase to `1-p`. Completion occurs
at full phase or when the returning weapon is within one unit of the return
endpoint. `0x1c7844..0x1c786c` seeks the hero clip to **2.66666675** for recovery.
The native host advances weapon updates at its 30 Hz simulation cadence;
per-class scheduling equivalence still needs an in-game oracle comparison.
Presentation interpolates flight position and spin without advancing simulation.

## Hyper strike

States **16/17/18** dispatch to `0x442e4/0x44418/0x4455c`.
Entry `0x3f9e0` selects hero clip **10 `JUMP_ATT`** and sets acceleration bits
**0xbc449ba8** per movement pass. With `passes = float(0x41600001)` (approximately
14), initial vertical displacement is:

```
(1.5 - 0.5 * acceleration * passes * passes) / passes
```

Windup switches to falling at clip time **0.23333335**, setting rate **0.1x**.
Gravity helper `0x4a490` clamps descent to **-0.38 per pass**. Ground contact
through `0x4b390` requests state 18 and restores hero playback to **1x**.
The landing entry starts the independent wrench deformation clip.

Hero sound events now follow authored clip time. Examples: clip 55 `throw` at
**0.71875**, hash **0x85c34866**; clip 10 `atkvoc` at **0.26875001**, hash
**0x10739765**; clip 14's same vocal at zero. The authored footstep events are
also dispatched. Pose scrubbing in photo mode does not emit gameplay sounds.

## Contact and damage tuning

The weapon pose callback `0x1c8ed4..0x1c9090` calls `0x130c90` and `0x1307a8`.
The first helper takes wrench joints **3 and 9** (joint 3 supplies both Y values),
transforms them with the held weapon matrix and constructs two spheres at the
quarter and three-quarter points. Radius is **min(endpoint distance / 4, 0.25)**.
Two additional spheres use the midpoints between current and previous centers.
The native collision path now samples the deformed wrench and blended hero grip
to construct these four contacts. The first swing gates contacts to
**0.56666666 < time < 0.73333335** (`0x1c9030..0x1c9068`). The second and third
swings and hyper falling/landing states dispatch the contact helper directly.
A target is recorded after its first hit; it cannot take repeated damage from
successive contact samples in the same attack. Hyper strike therefore contacts
with the weapon itself, replacing the old fixed 2.5-unit radial blast.

`0x1c7af8` obtains base damage from the progression table at `0x22a170`. Its
initial row/column is **1.0**. Landed hyper damage multiplies it by **1.6** at
`0x1c7ba4`; enemy health now retains fractional damage. Initial melee and throw
damage is 1, landed hyper damage is 1.6. Falling hyper contacts use the base
value. The full progression/upgrade table and enemy starting-health mapping
remain separate from the current native encounter health values.

Throw terrain collision now sweeps a **0.3-unit sphere**, matching the radius
passed at `0x13055c`, rather than using a thin ray. Contact with a wall reflects
outbound travel into the return phase. Target hit records remain separate for
the outbound and return legs. Terrain occlusion is checked both to the weapon
contact and into the target body, preventing broad target volumes from allowing
hits through thin walls.

## Limits and validation

Player and enemy collision responses still use native capsules; complete retail
target-class primitives, health/progression responses, special aiming modes and
wrench upgrade particle callbacks remain under RE. Geometry-free controller
harnesses use an explicit fallback volume; live play supplies the original
Ratchet and wrench assets.

`sm_photo_wrench_probe` checks source-table constants, deformation, independent
movement, retained RUN speed, throw release/return/terrain/damage, joint 56
launch, authored sound dispatch, hyper-strike phases and single impact.
The existing gameplay and level-loop probes cover attachment clocks, moving
fire, all three combo clips, input buffering and prior interactions. These are
headless checks; no physical controller or emulator comparison was performed.

Shared rules are in `src/hig/higwrench.h`; the CPU controller is in
`src/sm/gameplay.cpp`. Disassembly evidence is retained under `mods/sm_native/`
as `wrench_*`, `hyper_strike_*` and `skin_draw_opcode_oracle.asm`.
