# Size Matters animation transitions

Implemented 8 September 2026. Ratchet now crossfades between actions, including
interruptions before an earlier transition finishes. The moving-fire arm has
its own blend clock. Skinning, the pistol/wrench grip and Clank's mounting socket
use the same evaluated skeleton. Normal movement remains 6 units/second and the
separately recovered RUN playback rate remains approximately 1.200048x.

## Original executable evidence

All offsets are file-relative in USA `/LVL/LEVEL_01.REL`, SHA256
`cbf4a983935a05b64bf0234889c663bd1522dc4366581837a74d8b42812c726c`.
The executable is read as an oracle; no guest instructions are executed.

`0x4da70` selects the player clip through `0x7cd28`. The latter alternates the
two main controller slots and calls `0x88ca0`, passing `f12` as blend duration
and `f13` as incoming clip time. `0x88dfc/0x88e10/0x88e14` store the incoming
time, duration and zero blend elapsed respectively.

On interruption, `0x88d70..0x88da8` calls `0x88a90` to evaluate and copy the
current mixed pose, then clears the old main slot. Consequently, the next
transition starts from that evaluated pose, rather than a freshly sampled old
clip. For the player selectors' `a2=0`, the old slot is frozen during the fade
(`0x88e28..0x88e38`).

`0x88ec0` advances elapsed time and calculates **elapsed / duration**, saturating
at completion. This is a linear blend weight. Main-layer updates at `0x890a8`
use time multiplied by controller speed at `0x88fe8`; auxiliary layers use the
unscaled clock at `0x89148`. For example, a 0.10-second wrench fade at 1.6x takes
0.0625 seconds of simulation time. It is not tied to monitor refresh rate.

| Destination | Duration in controller seconds | Selector evidence |
|---|---:|---|
| Stand | 0.20 | `0x42be0..0x42c00` |
| RUN | 0.10 | `0x4a354..0x4a374` |
| Jump / Fall | 0.20 | `0x40b50`, `0x3f6d4` |
| Glide | 0.10 | `0x3fb04..0x3fb24` |
| Standing pistol fire | 0.05 | `0x3f280..0x3f290` |
| Moving-fire arm, fade in/out | 0.10 | `0x6b1d4..0x6b1e4`, `0x6b038..0x6b054` |
| First wrench swing / Throw | 0.10 | `0x3fd68`, `0x3fcb4` |
| Second / Third wrench swing | 0.15 | `0x3fdec`, `0x3fe38` |
| Hyper strike | 0.20 | `0x3fa3c..0x3fa5c` |
| Hurt | 0.15 | `0x3fea4..0x3fec0` |
| Death | 0.125 | `0x45ebc..0x45ed4` |

Other native motion selectors currently use a 0.125-second fallback. The full
retail WALK2/RUN phase selection, special aiming states and every possible
blend layer have not been reconstructed.

## Native implementation

`hig::PoseTransition` captures local bone poses and applies shortest-arc
quaternion interpolation, with linear translation and scale. It is shared with
Theia through `hig_core`. Raw animation sampling and photo-mode scrubbing retain
the exact requested clip/time; those operations do not trigger gameplay fades.

The controller evaluates poses once per simulation tick and retains both
endpoints. Rendering interpolates those poses and heading at the presentation
alpha. Rendering never advances blend clocks, collision, events or gameplay.
Clip changes within the same motion, such as the second jump and successive
wrench swings, are detected independently of the broad motion enum.

Moving-fire transitions affect only branch 52 and its descendants. The RUN
phase, legs, left arm and back socket continue uninterrupted. When a full-body
action interrupts layered fire, its source includes the evaluated firing arm.
Relocation, respawn and loading reset pose history.

## Validation

`sm_transition_probe` checks source instruction fixtures, rate-scaled fades,
interruption from an already mixed pose, rapid action changes, normalized
quaternions, independent RUN legs, pistol attachment to the blended joint,
unchanged running speed and read-only rendering at 60/120 Hz. It also checks
retail combo windows, windup steering and contacts from the original deformed
wrench joints. The synthetic target grid records 26 contacted targets out of
49, with one hit per target per swing.

The gameplay, level-loop, encounter and photo/wrench probes pass. The photo
catalog still renders all 306 entries and all their clips. Hidden DX11 captures
of rapid RUN/fire/RUN changes and the three-hit combo report zero debug errors.
The 60/120 Hz rapid-transition captures are byte-identical (SHA256
`e4cdcbaa667254c8b228402a9a62ba6e8082000ffda73c6383b20e99ebdac5f9`).

Evidence and logs are under `mods/sm_native/tuning_*`. These are headless and
native-renderer checks, not a physical-controller or emulator comparison.
