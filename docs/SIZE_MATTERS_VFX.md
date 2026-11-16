# Size Matters weapon effects — 9 September 2026

The native host now draws the currently implemented pistol shots with the
original Blaster effect textures and recovered parameters. Flat, untextured
projectile strips have been replaced by layered glows, scrolling ribbons and
collision-driven spark sprays. Shots originate at the original local muzzle
point, composed with Ratchet's animated right-hand grip. Terrain between the
chest and muzzle prevents firing through a wall penetrated by the hand.

This is the first weapon-effects port, not completion of the game's arsenal.
The existing development weapon selections labelled Blaster and Lacerator
still share the same Blaster model/effect definition. Enemy shots use this
rendering infrastructure with a native red tint. Their separate original
effect definitions, wrench upgrade/contact particles, other weapons and
dynamic effect lighting remain to be recovered and implemented.

## Original data

All offsets below are file-relative in USA `/LVL/LEVEL_01.REL`, SHA256
`cbf4a983935a05b64bf0234889c663bd1522dc4366581837a74d8b42812c726c`.
The original executable is read as data, never executed. Texture payloads
come from the user's GLOBAL/SP WADs, decoded through the shared GIM reader.

| Evidence | Location / result |
|---|---|
| Global SFX registry builder | `0x91630`; 203 hashes at `0x210218`, runtime handles at `0x297dc0` |
| BlasterShot registry | `0x21cca8`; resource initialization `0x14d660` |
| Variant effect rows | `0x21cd10`, stride `0x48`; initial weapon uses row zero |
| Glow tint/radius | Row `+0/+4/+8`; RGB `(255,128,64)`, radius `0.55`; draw adds alpha `0xc8` at `0x148ff8` |
| Trail tint/width | Row `+0xc/+0x1c`; RGBA `0xff4080ff`, width parameter `0.4` |
| Texture mapping | Slots 31/12/83/30/60/15 → splotch, AlphaSpark, spark_bright, wavyline, firecracker_plasma_spark, FireRing |
| Muzzle point | `(0,-0.95,0)` at `0x21cb10`; `0x1471b0` transforms it by the weapon world matrix |
| Weapon attachment | Right-hand joint 56, selected by `0x39264` |
| Impact dispatch | `0x14d7c0`; emitters at `0x14d9f4` and `0x14da9c` |
| Impact counts | 20 + 7 plasma sparks for the base weapon (upgrade branches use 12 + 5) |
| Spark templates | `0x207268` and `0x207290`; sizes/tints patched from row `+0x28/+0x2c` and `+0x30/+0x34` |
| Spark ranges | First: size 0.2–1, life 8–30 frames, step 0.05–0.4; second: size 0.3–1, life 16–60 frames, step 0–0.01 |
| Spark colour | Base sprays use RGBA `0xff00ffff` (yellow) |
| Particle field interpretation | Emitter `0xe6848`, output `0xe6c90`: lifetimes in ticks, movement ranges and gravity |
| Shot scrolling/spin | `0x14de20`: phase +0.2 and rotation +1 radian per 30 Hz update |
| Conditional impact ring | Row `+0x44` tested at `0x14dab4`; zero for the initial weapon, so no FireRing is emitted |

## Native rendering and enhancements

Weapon effects have a dedicated additive pass with depth testing and no depth
writes. Sprites face the camera; ribbons construct their width from the view
direction, including a stable fallback for views directly along the shot.
Ordinary world materials retain their existing shaders and blend state.

The effect shader reconstructs magnified textures using a four-tap cubic
B-spline filter. This smooths the original low-resolution artwork without
inventing detail or producing sharpening halos. SFX texture uploads include
mipmaps for distant particles. Sprites clamp at their edges; the original
wavyline ribbon repeats and scrolls. Geometric edges are feathered separately
from the scrolling pattern, with derivative-based antialiasing. These work at the selected display/photo
resolution, including 1080p and 4K.

Projectile snapshots interpolate at display rate. A collision records the
fraction of the simulation tick at contact: the shot remains visible until
that fraction and the impact particles appear afterwards. Particle trajectories
and fading are evaluated continuously from fixed-step state. Extra 60/120 Hz
render calls cannot emit particles, advance combat, consume ammunition, or
change lifetimes. Pause and photo mode retain the same simulation state.

Explicit native policies are: deterministic particle RNG and surface-facing
hemisphere distribution, smaller spark cores/streaks, smooth fade envelopes,
a three-unit maximum ribbon length, a 0.09-second muzzle flash and 0.12-second
contact flare. The latter two use original artwork/colours with new envelopes.
These have not been established as exact original particle behavior. Existing
projectile speed, fire cadence, damage and player movement are unchanged.
The particle pool is capped at 2,048 entries and clears on respawn/level reload.

## Theia and IRIS

`src/hig/higvfx.h` exposes the bounded registry/Blaster reader to both programs.
It rejects truncated and unrecognized table layouts. In Theia, after opening a
Size Matters level:

```json
{"cmd":"bog_info","weapon_fx":1}
```

This returns the source file/table offset, all 203 texture hashes, the local
muzzle point and glow parameters. The separate Luna fix also preserves BOG
point metadata and exposes it through `bog_info`'s `curves` option. These are
inspection facilities; this pass does not add a particle-authoring UI.

## Validation

`sm_vfx_probe` uses the real ISO to check texture mapping, colour/size fixtures,
muzzle offset, malformed input rejection, live firing/impact events,
camera-aligned billboards, contact interpolation, render-rate independence,
lifetime expiry and pool limits. Gameplay/animation/mechanism regressions and
hidden DX11 captures provide the integration checks. No emulator comparison
or physical-controller validation is claimed.

The local annotated disassemblies are under `mods/sm_native/vfx_*_20260909.asm`.
No extracted game artwork or executable is bundled with the source changes.
