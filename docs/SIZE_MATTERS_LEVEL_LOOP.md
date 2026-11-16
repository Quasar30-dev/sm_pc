# Size Matters: movies, interactions and player animation

## Native behavior implemented (8 September 2026)

New Game loads Pokitaru and plays its original opening movie. Enter/Space/Esc,
or XInput A/B/Start, skips playback. Gameplay and level music pause during the
movie; its own soundtrack drives presentation when audio output is available.
Focus loss pauses both. Held dismissal buttons must be released before they can
jump or fire. The same release guard applies when leaving an interaction panel.

The original media is **Bink (`BIKi`), not MPEG**. The ISO contains 60 `.BIK`
files. A hidden FFmpeg decoder consumes a temporary copy of the selected ISO
file, with a four-frame RGBA queue and bounded 48 kHz stereo audio. Skip cancels
only that decoder, joins its worker, releases its buffers and removes its owned
temporary file. FFmpeg must be available on PATH (already installed here).
No original game code executes in that process. The ISO stays read-only.

Two original animated vendors appear at their BOG transforms. They open within
8 world units and offer interaction within 2.5. **R / Y** opens a native panel;
Enter / A buys a Lacerator ammo refill, Esc / B leaves. The panel checks missing
ammo, affordability and exact cost before changing ammo and bolts. It currently
offers the implemented weapon's ammo; new weapon purchases, retail vendor art,
upgrades and challenge-mode economics are not implemented.

**H / Back** opens Help Desk. It includes current native controls and 158 original
English manual entries from `HELPDAT.BIN`, with the four face-button glyphs
adapted to keyboard/XInput. Pokitaru's authored tutorial volumes notify the
player and select the relevant manual entry. Original manual entries also
describe mechanics not yet implemented by the native engine; they do not imply
those gadgets or mission interactions now work.

ContinuePoint volumes update Ratchet's respawn transform and heading. These are
session checkpoints, **not disk saves**. Each activation is edge-triggered.
The original Pokitaru EndMovie volume selects `01C.BIK`; completion or skipping
loads `/WAD/02.WAD` (Ryllus / `jungleruins`) and carries bolts/ammo across. This
handoff is implemented, but the complete mission scripting that leads to the
exit is not. Ryllus's scenery/player load does not imply its enemies are ported.

## Player firing and wrench corrections

The next pass adds [photo mode](SIZE_MATTERS_PHOTO_MODE.md), independent swing
movement, the wrench's own deformation clips, wrench throw and hyper strike.
See [wrench executable findings and native behavior](SIZE_MATTERS_WRENCH.md)
for recovered timings, movement windows, launch/return and remaining tuning.

Normal movement is restored to **6 world units/second**. The earlier increase to
8 addressed the wrong issue: the RUN clip was playing at raw 1x. Its playback
now follows the executable's displacement-based gait calculation, approximately
**1.2x** during ordinary unobstructed running. Simulation and render interpolation
use the same rate, including running while firing. Pistol recoil and Clank's own
clips use separate clocks. The shared `hig::ratchet_run_playback_rate` helper
preserves the retail table's float precision and upper cap; see
[the locomotion oracle](SIZE_MATTERS_GAMEPLAY_ORACLE.md#run-animation-playback).
The native movement/collision controller still has its existing limitations.

Stationary pistol fire uses `FBOD_BLA` (26). Moving pistol fire keeps `RUN` (3)
and layers `FIRE_BLA` (25) on bone **52 and its descendants**, the right-arm
branch. The layer leaves the legs, left arm and Clank socket unchanged. Run
phase survives transitions between running and running/fire; recoil has its own
clock. The similarly named `FIRE_RUN` (73) is not used as the pistol's full-body
replacement. `hig::sample_animation_layer` is shared with Theia.

Repeated F / X presses buffer the next wrench swing without rewinding the one
already playing. The sequence is `COMBO_1o`, `COMBO_2o`, `COMBO_3o` (12–14).
Original clip lengths are 1.2, 0.9 and 1.2 seconds. The executable starts the
first clip at 0.4 seconds and sets all three to **1.6x** playback. The animation
and attack lifetime now share this clock, replacing the unrelated 0.55-second
cutoff. Holding F alone performs one swing. Death, hurt, swimming and relocation
clear attack state. Combo input now follows the original per-clip windows and transition thresholds;
the extra post-swing grace has been removed. See
[the wrench recovery](SIZE_MATTERS_WRENCH.md) for deformation, contact and damage
rules, and [animation transitions](SIZE_MATTERS_ANIMATION_TRANSITIONS.md) for
interruption-safe pose blending. The third clip's time-zero `atkvoc`
sound event is retained.

## Original-data evidence

Offsets below are file-relative in USA `/LVL/LEVEL_01.REL`, SHA256
`cbf4a983935a05b64bf0234889c663bd1522dc4366581837a74d8b42812c726c`.

| Evidence | Location / interpretation |
|---|---|
| IntroMovie | Class record `0x2226c8`, init `0x17d030`, update `0x17d0c8` |
| Movie path | `0x267a8` adds `'A'` to movie index; template at `0x20cad8` is `L%02i\\%02i%c` |
| EndMovie | Class record `0x220820`, init `0x16c660`, update `0x16c740` |
| Vendor | Class record `0x229be8`, init `0x1c6008`, update `0x1c61e8` |
| Vendor radii/input | `0x1c62c0` distance squared 64; `0x1c6354` 6.25; `0x1c63ac` Triangle mask `0x1000` |
| Ammo cost/capacity | Vendor `0xd0c18..0xd0c40` reads definition `+0x38` capacity and `+0x34` unit price. Initial template `0x1f0ff0` supplies 40 and 1. Mode multiplier follows at `0xd0c60` |
| ContinuePoint | Class record `0x21e510`, init `0x159010`, update `0x159040`; properties: trigger link and byte checkpoint index |
| Trigger containment | `0x84708`: inverse transform then unit box, sphere or Y-axis cylinder |
| Help manager | Class record `0x2230f0`, init `0x18ad90`, update `0x18afa0`; link/ID pairings at `0x18b114..0x18b5ac` |
| Pistol layer | `0x55b38..0x55b54` uses clip 25 when the current full-body clip is not 26; layer request `0x6afd8`, application `0x6afe8` |
| Pistol branch | `0x6af3c..0x6af58` initializes branch 52; `0x8be88` sets the layer branch |
| RUN gait | `0x42e28..0x42e40` passes table `0x1f4638`; row `0x1f4670` selects clip 3 |
| RUN playback | `0x4a394..0x4a458` derives controller `+8` from displacement; `0x37ba0..0x37bec` runs two movement passes; `0x3eee0` measures each pass |
| Wrench selection | `0x3fd84`, `0x3fe00`, `0x3fe48` select clips 12–14; `0x3fd78` supplies the first start time 0.4 |
| Wrench speed | `0x3fda0..0x3fdb4`, `0x3fe68..0x3fe78` write 1.6 to animation controller `+8`; `0x7cd28` and `0x88ca0` establish the clip/start-time path |

Pokitaru BOG begins at decompressed `01.WAD +0x1c8a40`. Gameplay instances are
128 bytes; class definition `+0x2c` supplies bounded property size for every
class, replacing the earlier whitelist. Header `+0x3c/+0x40` describes **256
trigger records**, each 140 bytes: source identifier `+0`, matrix `+4`, inverse
`+0x44`, shape byte `+0x88`. The identifier's meaning is still unknown.
There are 183 boxes, 71 cylinders and two spheres.

ContinuePoint `0x1b130` links trigger `0x36e8c`, index 2, with spawn near
(-23.600, -3.866, 24.857). The other points are index 3 (`0x1b1b0`) and index 0
(`0x1b230`). EndMovie `0x1c4b0` links `0x3a4b0`, selects movie C and destination
2. Vendor instances are `0x1c030` and `0x1c0b0`; their SP.WAD model hash is
`0x6d436be0`, animation hash `0x283f35fa`. The animation bank contains OPEN,
CLOSE (both 0.4 s), and IDLE (7.966667 s).

The help manager maps property offsets 0, 8, 12, 16, 20, 24, 32, 36 to help IDs
-4, -6, -7, -8, -9, -10, -11, -159. Full original retry/once-shown rules and
dependencies on bolt cranks and door targets have not yet been translated.

Theia receives the shared property, trigger, movie-header and animation-layer
readers. `open_hig_level` reports trigger count. IRIS
`{"cmd":"bog_info","triggers":1,"trigger_offset":0,"trigger_limit":40}`
returns bounded pages of source offsets, shapes and both transforms.

## Validation and remaining work

`sm_level_loop_probe` checks original trigger fixtures, all 256 volumes' center
and exterior points, vendor assets/transactions, help decoding, modal input,
checkpoint respawn, outro selection, pistol branch isolation, rapid combo
presses, complete playback, held F and cancellation. `sm_movie_probe` validates
all 60 headers and decodes all 2,900 opening frames, with source A/V duration
agreement, audible PCM, focus pause, isolated level music, skip during loading
and buffering, replay and EOF. Existing gameplay and encounter probes cover the
previous skinning, attachment, combat and swimming behavior. `hig_probe` audits
all 24 WADs after the shared BOG changes.

Hidden DX11 captures cover moving fire, the third wrench swing, vendor/help,
movies, and the actual Pokitaru movie-to-Ryllus handoff. No physical controller
or emulator comparison was used. These checks establish native behavior and
source-data agreement, not whole-game retail equivalence.

The final moving-fire captures at 60 and 120 fps are byte-identical (SHA256
`f29ce0ed9f9165ecc92fcb5a5ae6d2dd76923758ee0adcfee328c52c0df065b4`), with zero
DX11 debug errors. Theia and the native host both rebuilt successfully.

The [mechanism and planet-travel pass](SIZE_MATTERS_MECHANISMS.md) adds Luna's
escort/photo sequences, bolt cranks and linked gate/bridge, the wrench zipline,
moving elevator collision, intermediate `01B.BIK` trigger ordering and the ship.
All 24 disc destinations are available through its planet panel or **H, then R**
from any level. Special activity maps currently support Ratchet exploration.

Remaining campaign gaps include the intermediate movie's scenery/EnemyWave
callback, full EnemyController activation, shootable door targets, destructible
crates/pickups, the remaining arsenal/gadgets and persistent save slots. The
mechanism notes distinguish recovered constants from native policy and list
the remaining Luna, crank and elevator behavior.

```bat
run_size_matters.bat
run_size_matters.bat --movie /VID/L01/01A.BIK
run_size_matters.bat --movie /VID/L01/01A.BIK --movie-frame 120 --hidden --frames 2 --capture NEW.bmp
run_size_matters.bat --gameplay-script wrench:1,idle:5,wrench:1,idle:9,wrench:1,idle:17 --hidden --frames 2 --capture COMBO.bmp
```
