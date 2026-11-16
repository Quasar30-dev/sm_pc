# Size Matters: native Windows engine

## Contract and current scope

Build an owned engine that consumes the user's Size Matters ISO and reproduces
the game, **including its frontend**. The original executable, level REL modules
and VU microprograms are read-only behavioural oracles. They are not executed,
recompiled or relocated into a guest engine. Artwork embedded in a REL is data
and can be decoded directly. The ISO is opened read-only.

Initial target: PS2 USA **SCUS-97615**. The paused quantisation project remains
separate. The existing `src/runtime/` engine work for another game also remains
separate; this target lives in `src/sm/` and shares `hig_core` with Theia.

Final requirements include campaign/minigames; faithful movement, combat,
objects, animation, collision, camera, rendering and audio; complete progress
save/load; keyboard/XInput; in-game resolution and frame-rate settings; and
DX11, DX12 and Vulkan renderers. Simulation must be independent of presentation.

The final frontend must retain the game's identity, screens and navigation
flow. PC settings, rebinding, keyboard navigation and changing input prompts
belong inside that flow. The development level inspector is **not** a
replacement for New Game, Continue, save slots or the pause menus.

As of 9 September 2026, `sm_pc.exe` is a native **gameplay development host**;
the complete game is not implemented. It starts with original artwork and the recovered title menu. New Game
confirms entry to Pokitaru scenery/collision residency. Pause/resume, nested
Options, PC graphics and controls help run through a native state machine.
Pokitaru now has Ratchet's original skinned model and animation clips, a native
capsule controller, third-person camera, jumping, held-jump gliding, weapon
effects using original Blaster textures/parameters with interpolated terrain hits
(see [weapon VFX](SIZE_MATTERS_VFX.md)), running pistol animation and the three-swing wrench combo. Controller constants and
combat rules remain bring-up implementations rather than recovered retail behaviour.

| Area | Implemented | Still needed |
|---|---|---|
| Data | Shared ISO/HIG!/TJZIP, GDE/GIM, static BOG placements, terrain COL | Global residency and gameplay class registration |
| Rendering | Native DX11 geometry/textures, vertex colours, primary alpha passes, resize/capture, CPU skinning, original player/attachment animation, Pokitaru sky/ocean | Complete materials, per-instance lighting, other environment controllers/effects; DX12/Vulkan |
| Frontend | Original title data/art, HUD layout/font/icons, native New Game/Pause/Options/Graphics/Controls; skippable original movies with audio, vendor ammo panel, Help Desk | Full save-slot flow, original vendor/help presentation, remaining mission transitions |
| Input | Recovered button/axis semantics and edges, XInput adapter, native player actions | Full gameplay contexts/remapping, vibration and hardware verification |
| Timing | 30 Hz normal-loop step recovered from Pokitaru; independent presentation, input latching, bounded catch-up | Other modes and complete subsystem ordering |
| Persistence | Atomic PC display preferences | **Game progress save/load** |
| Audio | Original music, bank/tone decoding, mapped player/weapon/enemy sounds, native output and movie soundtrack | Complete sequencing, SPU reverb/envelopes and remaining action bindings |

The [mechanism pass](SIZE_MATTERS_MECHANISMS.md) adds Pokitaru's Luna route/photo
sequences, cranks and linked gate/bridge, the wrench zipline, elevator with moving
COL, intermediate movie triggers and the ship. Its planet panel unlocks all 24
disc WAD destinations; **H, then R** also opens it from any map. Special activity
maps remain Ratchet exploration, with their original modes still pending.
Shared BOG curve and variant-reference readers also improve Theia/IRIS inspection.

Pokitaru has 106,266 decoded scenery/sky triangles, 1,437
placed scenery instances and 10,681 world COL faces. Unplaced template grids
are excluded. COL drives the native capsule solver, camera ray and projectile
sweeps; retail contact-response equivalence remains unverified. Material composition and blended-batch
ordering are approximate. The frontend picture preview does not establish
retail HUD layout, crop or aspect handling.

The shared BOG reader now uses the version 0x3c gameplay directories, separately
from shrub placements: Pokitaru contains 155 definitions and 1,161 instance
records. Its Ratchet class hash `0x36919224` supplies the spawn transform.
The shared `higactor` reader decodes highest-detail skin meshes, skeletons and
version-12 animation banks directly from the ISO assets. Those readers and RE
notes are available to Theia through `hig_core`; this does not imply an editor
animation UI has been added.

## Pokitaru sky and ocean

The native host enables the three original sky layers and ocean by default.
Sky follows the camera in X/Z, keeps its authored height, and rotates the cloud
layer at the recovered controller rate. It draws in source order before world
geometry, with depth disabled. The starting cloud angle is deterministic (zero)
for now; retail initialization randomizes that phase.

The ocean resolves the `WaterWaves` class through its BOG model descriptor to
the original `waterwaves` GDE, using both original LOD meshes and the original
`watersurface.gim`. Its BOG height is **-4.9 metres**, wave scale **0.07**, and
wave speed **1.2**. The native draw reconstructs the 6x6 tile grid around the
player, nine near tiles and 27 distant tiles, with authored vertical scaling,
outer-tile height offset and texture scrolling. The two uploaded template LODs
add 1,000 triangles; the renderer reuses them for the moving grid.

Sky, opaque scenery/actors and translucent water render in that order. The
development overlay has independent **Sky** and **Ocean** toggles. Environment
time follows the fixed level clock and freezes on pause. Bounded gameplay-script
captures keep their final simulation state fixed regardless of frame rate.

Headless DX11 captures of the same one-second gameplay state at 60 and 120 fps
were byte-identical, with zero debug-layer errors. A later eleven-second capture
confirmed visible cloud and water animation. The native and gameplay probes
also check the recovered sky layers, water properties, tile counts, LOD selection
and grid movement across tile boundaries. Both the native host and Theia build.

Theia receives a static 36-tile ocean preview at the recovered height, in its
own ocean layer; IRIS `open_hig_level` reports the environment parameters. The
editor's sky preview stays opt-in. Camera-following/animated environment rendering
currently lives in the native host. Shared readers and transform functions are
in `higlevel` and `higenvironment`. The primary water pass is reconstructed;
full retail lighting/effect parity and other levels' controllers remain separate
work. See [the original-data evidence](SIZE_MATTERS_GAMEPLAY_ORACLE.md#pokitaru-sky-and-ocean).

## Movies, interactions and checkpoints

New Game now plays the original Pokitaru opening movie. Enter/Space/Esc or
A/B/Start skips it. Vendors, contextual Help Desk notifications, session
checkpoints and the authored exit-movie handoff to Ryllus are implemented.
See [source evidence, controls, validation and remaining campaign scripting](SIZE_MATTERS_LEVEL_LOOP.md).

## Player appearance and attachment corrections

Pokitaru now also loads its original Crab, TrainingBot and Technomite torso/head
actors at their BOG placements, with native combat and original animation-event
sounds. Surface swimming, water jumping and damage/respawn are playable.
Space/A jumps out of water; weapons stay holstered while swimming. Theia also
receives the intact umbrella and corrected rigid/shrub texture mapping.
See [encounter evidence, checks and remaining retail behaviour](SIZE_MATTERS_ENCOUNTERS.md).

Skinned UVs use the original VU program's **1/1024** conversion. Ratchet's
right-hand grip is original joint **56**, and Clank attaches to joint **5**.
The original camera is right-handed; the initial left-handed host camera
mirrored the entire world and made the correct grip appear left-handed. The
view/projection and camera-relative movement/orbit controls now agree.

Clank's mounted orientation and rear offset come from the root track in
`ClankPackMaster.mb.anim`, applied before the animated back joint and Ratchet's
world matrix. Rendering his bind pose alone omitted this authored correction.
His original idle/glide clips are now sampled, and he remains present alongside
the deployed helipack. The blaster also samples its original idle/shoot clips.
See [the oracle notes](SIZE_MATTERS_GAMEPLAY_ORACLE.md#player-skin-uvs-and-attachments)
for original file offsets and the distinction between the socket transform and
the attachment's own root animation.

`sm_gameplay_probe` verifies the retail bind matrices, UV fixture, Clank root
pose, right/left grip convention, movement handedness and bounded animated
attachments. Hidden DX11 idle, run, fire and glide captures pass with zero
debug errors; the front view also confirms Ratchet's facial texture mapping.
No emulator or physical-controller equivalence is claimed.

Controls: WASD move; Space jump/double-jump/hold to glide; E or left mouse fire;
F wrench combo; R nearby vendor; H Help Desk; Esc pause; right mouse orbit. F1 exposes the development overlay.
The existing Tab weapon switch remains a prototype, not a complete retail arsenal.

```bat
run_size_matters.bat --gameplay-script idle:30 --hidden --frames 2 --debug --capture NEW.bmp
run_size_matters.bat --gameplay-script fire:5 --camera-yaw 45 --hidden --frames 2 --capture NEW_SIDE.bmp
```

The optional camera yaw is in degrees relative to the initial player heading.
Omit `--frames` for CPU-only scripted gameplay checks.

## Build and run

```bat
build_ra.bat sm_pc
run_size_matters.bat
run_size_matters.bat --level /WAD/01.WAD
```

The launcher uses the existing local USA ISO. For a different location:

```bat
build\x64\sm_pc.exe "D:\Games\Size Matters.iso" --level /WAD/17.WAD
```

Choose **New Game** to load the initial level data, or **Options > Graphics**.
F1 opens the development panel with **Inspect a level**. The graphics panel offers
720p, 1080p, 1440p, 4K, 30/60/120/144/240 caps and V-sync. Apply performs real
swapchain recreation and saves
`%LOCALAPPDATA%\Theia\SizeMatters\display.ini`. These are PC preferences,
not game saves. The client window scales the chosen render resolution.
V-sync and monitor refresh can limit observed presentation rate.

Keyboard navigation: arrows/Tab, Enter, Escape. Controller: D-pad, A, B.
Inspection camera: right mouse with WASD/Q/E, or sticks and bumpers. F1 toggles
the development panel; Escape/Start pauses the level and exposes Resume,
Options and Return to title. These are development
controls, not recovered player bindings.

```bat
run_size_matters.bat --level /WAD/01.WAD --inspect
run_size_matters.bat --level /WAD/01.WAD --hidden --debug --frames 100 --display-cycle --capture mods\sm_native\NEW.bmp
```

The bounded display audit recreates 1080p/4K buffers and changes 60/120 caps
without saving preferences. `--debug` requires the D3D11 debug layer and fails
clearly if unavailable. Capture paths must be new. `--settings PATH` selects
an isolated preference file; later display CLI options override it.

### Optional PS2 Ratchet

F1 > **Ratchet model** swaps in the R&C1, GC or UYA Ratchet: his armour, rig and animation sets, read from the
PS2 discs. It also adds the ACiT hoverboots, toggled with B or L3, and UYA/GC charge boots: double-tap R1 (Shift),
then hold. **Save as default** writes
`%LOCALAPPDATA%\Theia\SizeMatters\ratchet.ini`. For one run, use `--ratchet uya|gc|rac1|sm`,
`--ratchet-anims ...` and `--ratchet-armor N`. The default stays Size Matters. Details and evidence:
[docs/ps2/ANIM_PORT.md](ps2/ANIM_PORT.md#size-matters-the-ps2-ratchet-as-an-option-srcsmps2_ratchet-h).

## Shared implementation and editor access

- `src/hig/higprogram.*`: bounded ELF32/SNR2 symbols and reference sites.
- `src/hig/higinput.*`: native implementations of recovered input semantics.
- `src/hig/higfrontend.*`: embedded frontend GIM data/source-name decoder.
- `src/hig/higlocalization.*`: bounded LOC_DATA/SCOL/LTOC/TDEF/ENTR reader.
- `src/hig/higmenu.*`: original menu data, navigation and New Game constants.
- `src/sm/frontend.*`: native PC frontend lifecycle, sampled-input latch and menu repeat.
- `src/sm/world.*`: owned data residency and clock.
- `src/sm/scene.*`: graphics-API-independent CPU geometry/placement snapshot.
- `src/sm/render_dx11.*`: first graphics backend, with no guest GPU execution.
- `src/sm/platform_win32.*`: XInput mapping and PC display persistence.
- `src/app/hig_research.inl`: Theia's **HIG research** panel: module selection,
  filtered symbols, reference sites, original artwork, menu actions and text.
- IRIS **`hig_program`**: machine-readable evidence and bounded original byte
  windows, defaulting to the loaded level's REL. On `/LVL/FRONTEND.REL`,
  optional `text_id` and `language` (0..4) return readable text and its exact
  encoded hex. Results also include menu rows and New Game reset constants.

The CPU snapshot can be consumed by future graphics backends. The host's current
platform/UI boundary is still DX11-specific; this is not DX12/Vulkan support.

Running the editor beside the host exposed an existing pinned ImGui bug:
`GetPropA` could return a viewport pointer owned by the other process.
`src/app/imgui_win32_safe.cpp` checks HWND owner PID before reading properties
for both applications, without modifying FetchContent sources.
See [upstream issue 8162](https://github.com/ocornut/imgui/issues/8162).

## Reproducible executable oracle

```bat
build_ra.bat hig_oracle
build\x64\hig_oracle.exe "D:\Games\Size Matters.iso" mods\sm_native\NEW-report
python tools\hig_oracle_disasm.py mods\sm_native\oracle-v3 /LVL/LEVEL_17.REL 0x7a120 0xc8
```

Reports record source extent, length, checksum, address space, symbol kind,
reference offset, relocation/addend and original instruction. The Python tool
requires Capstone; it re-reads and validates ISO bytes, then annotates the
window. Left-column offsets are file-relative; resident operands use original
PS2 virtual addresses. Generic MIPS64 decoding is suppressed for known
EE-specific opcode families, which otherwise can be mislabelled. Unknown
EE/MMI/VU instructions remain raw words.

Raw checksums use reflected CRC32 `EDB88320`, initial register 0, final
complement: `zlib.crc32(data, 0xffffffff)`. They are not SHA256. Disassembly
windows separately include the complete original module's SHA256.

The local `mods/sm_native/oracle-v3/index.json` reports 1,804 resident symbols,
21 modules, 5,517 import declarations and 85,694 reference sites. Every level
module's imports resolve against resident names. One frontend import does not:
`PAUSEMODE_GetCurrentPauseScreen__Fv`, called at `0x16c14` and `0x16c6c`.
It is also absent from other module export tables. Both calls are gated by
frontend byte `0x30540` inside reused New Game/Load Game save-dialog wrappers;
they compare the pause-screen result with `0x38`. This is not evidence that
every frontend boot needs that unresolved call. Its conditional resolution
path still needs investigation.

SNR2 symbol/reference records are 12 bytes. Symbol kinds are nil, import,
relative, weak, absolute (`0..4`). Reference records are
`{target, symbolIndex << 8 | relocationType, addend}`. Relocation type 4 plus
MIPS opcode 3 identifies a direct imported JAL. Internal relocation tails
remain unknown oracle data; no original code is executed through these tables.
Resident ELF entry: `0x01e62508`. Allocatable sections map addresses to file
offsets; BSS symbols have no on-disc bytes. REL addresses are file-relative.

Layouts were cross-checked against local MIT-licensed Chaos Compiler Collection
(`ccc/src/ccc/sndll.*`). EE opcode exclusions were checked against local
`ghidra-emotionengine-reloaded/data/languages` specifications.

## Input evidence

`LEVEL_17.REL` SHA256:
`1473d503cecb37732c8239f20a52b1f9d1170752b26f504e9edc53df8ca9076b`.

| File offset | Observation |
|---|---|
| `0x79888` | `scePadRead` call |
| `0x798ac..0x79a60` | Active-low PS2 buttons become HIG engine masks |
| `0x79ea0` | Axis signs become directional button bits |
| `0x7a120` | Axis wrapper passes dead-zone parameter `0x40` |
| `0x7a140..0x7a1e4` | Normalize `raw/255*2-1`, dead zone `64/128`, rescale remainder |

The observed dead zone is **50% per axis**. Bytes 64..191 are neutral; 0/255
map to -1/+1. XInput endpoints are mapped into these original byte ranges,
reversing Y to the game's down-positive convention. There is no second stick
dead zone. The PC trigger threshold of `>30` is an explicit adapter policy.

Masks (hex): Select `1`, Start `8`; D-pad `10/20/40/80`; L1/R1 `100/200`;
L2/R2 `400/800`; Triangle/Circle/Cross/Square `1000/2000/4000/8000`;
L3/R3 `40000/80000`. Press/hold/release edges use previous/current button masks.

Conditional inversion near `0x79b00`, context-dependent right-stick fallback
near `0x79bd0`, device negotiation, pressure and gameplay contexts are still
unimplemented. The 60 Hz clock is a **bring-up policy**, not evidence of retail
simulation cadence. Trace original timers, frame/swap loop, animation and
pause behaviour before selecting final subsystem step rates.

## Frontend evidence and next work

`FRONTEND.REL` identifies `../LEVEL_BIN/levelstartup_disc.elf`. SHA256:
`81f834bdf092587eb94e6a198fe2ab6181877dbd215f7124b405edda9b1a3daf`.

| Site/data | Observation |
|---|---|
| `0x19410` | `lib_main` frontend entry |
| `0x19478` | `SND_LevelInit` |
| `0x194a8..0x194c8` | Counter loop calls `0x1878`/`0x2dd8`, waits for state `+4` to leave -1 |
| `0x194dc` | `MCPI_SendNewLevel` receives selected level |
| `0x19970..0x19998` | Reads/clears boot-movie bypass on return |
| `0x19b48..0x19b60` | Separate memory-card-check bypass references |
| `0x19d00` | `MEMCARD_GetWideScreenFromMemcard` |
| `0x205d8`, `0x20788` | `MEMCARD_Busy` / `MEMCARD_Saving` |
| `0x212b8`, `0x212d8` | Slot status/header queries |
| `0x1b408` | `SND_LevelShutdown` |
| `0x2ad50` | Embedded 16x16 GIM |
| `0x32880`, `0x76980` | Two 256x272 `S2\ART\SYSTEM\hiresfrontend.gim` tiles |
| `0x109bc8` | Localization path `cdrom0:\HUD\LOCALDAT.BIN` |

The shared image decoder validates GIM root and following file-information
bounds. The second 256-wide picture tile contains 224 picture columns and
32 all-black padding columns; the PC composition crops that padding and uses
a 256:224 width split. Original HUD layout/font fidelity remains unverified.
Next complete the original screen transitions and boot/return paths. Replace memory-card
device assumptions with PC save-slot operations while preserving progression,
retry and error behaviour. Keep PC preferences separate from progress saves.
Menu sound, boot movies and attract mode also need reconstruction.

## Validation

`sm_native_probe` checks button/axis boundaries, press/hold/release, XInput
endpoints, integer timing at 60/120/144 presentation rates, hitches, valid/invalid
preference persistence, malformed program/frontend bounds and the real Pokitaru
scene. With the USA ISO it audits all 21 modules and 85,694 references, including
truncated-module rejection. Additional probes cover all 5 x 2,132 localized
entries, malformed text offsets, the original title action table, disabled-item
wrap/activation precedence, New Game cancellation/loading failure, nested
pause/options return, and short input taps at 60/120/144 presentation rates.

Native DX11 debug runs rendered frontend artwork and Pokitaru, cycled real
1080p/4K buffers and 60/120 caps, captured the result and reported **zero DX11
errors**. Theia/IRIS resolved `scePadRead` and returned original analogue-wrapper
bytes, original title menu actions, five language tables and text ID 591.
A CPU-only scripted run entered Pokitaru, advanced 60 level ticks, paused for
60 ticks without advancing the level, traversed nested graphics/controls
menus and resumed. The title UI was also rendered unattended with zero DX11
errors. No physical controller was connected during these checks; hardware
integration remains unverified. No emulator or gameplay equivalence is claimed.

Full-game acceptance requires original-behaviour comparisons for frontend
navigation, new game, save/reload after exit, transitions, combat and minigames.
Rendering alone cannot establish a playable port.

Platform references: [XInput processing](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/xinput/getting-started-with-xinput.md),
[DXGI resize requirements](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers).

## Headless frontend flow

No controller, window or graphics device is required for these commands:

```bat
build_ra.bat sm_native_probe
build\x64\sm_native_probe.exe "D:\Games\Size Matters.iso"
run_size_matters.bat --menu-script idle,up,down,accept
run_size_matters.bat --flow-script accept,accept,load,wait:60,start,wait:60,down,accept,accept,back,back,start
```

`--menu-script` exercises the recovered title navigation/dispatch only.
`--menu-has-save` enables Load Game **only in that fixture**, without writing
progress data. The real frontend keeps it disabled. `--flow-script` uses the
same native lifecycle as the UI; each command is a tap with release.
`load` services a pending New Game request, and `wait:N` advances N fixed
simulation ticks. Errors reject invalid load ordering. Add `--hidden --frames N
--capture NEW.bmp` to render the final scripted screen unattended. This
optional capture path uses a real DX11 device, unlike the default CPU-only run.

The confirmation and PC subpage layout are owned adaptations, not a claim
that the retail save-slot flow has been completely reproduced. The original
frontend contains four save-slot rows. Progress saving and multiplayer are
still unavailable, and their menu presence must not imply otherwise.

The PC input latch preserves sampled press/release events until the next
simulation tick, then consumes them once. Menu navigation repeat is explicitly
a PC policy (400 ms initial delay, 133 ms repeat at 30 Hz), separate from the
recovered retail direction and activation rules. Gameplay input contexts,
remapping and retail repeat timing remain separate RE work.

See [the gameplay and frontend oracle notes](SIZE_MATTERS_GAMEPLAY_ORACLE.md)
for exact code/data offsets behind the new state and timing findings.
