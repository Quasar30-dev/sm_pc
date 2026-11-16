# Size Matters: original HUD and audio recovery

Target: PS2 USA, SCUS-97615. All offsets below refer to unrelocated disc
files. The native engine reads data; it does not execute the original code.

## HUD

`src/hig/highud.*` recovers `/HUD/MAINHUD.W3D`: 73 screens and 1,415 elements.
The format includes signed 12-bit coordinates, layout rectangles, references
with translated origins, flat-colour triangle strips, and strips with a colour
per vertex. References are bounded and cycles rejected. These readers live in
Theia's shared `hig_core`, independently of the native graphics backend.

`/GLOBAL.WAD` supplies `HUD_ICONS.PAK` (109 images) and
`hud_typeface01_16col.gim`. The resident's `FontTableNormal` at `0x7b6a0`
supplies 256 four-byte glyph metrics. Glyph width and signed baseline offset
are bytes 2 and 3; atlas coordinates follow `LEVEL_01.REL:0xa9540`.
Magenta transparency is handled only for these HUD images.

The native HUD draws the original bolt, weapon/ammo and nanotech panels
(screens 7, 17 and 15). Native counters drive the text. The development
controls remain available through F1. The frontend and graphics menus still
use the existing ImGui implementation.

### Verified health colour and bolt icon correction

`LEVEL_01.REL:0xb826c..0xb82c4` constructs the health gradient colours and
`0xb82c8` calls the rectangle gradient renderer at `0xac540`:

| Edge | Packed RGBA word | RGB |
| --- | --- | --- |
| Upper | `0xff09a5d0` | `#D0A509` |
| Lower | `0xff1c35ff` | `#FF351C` |

This is a continuous warm gradient ending in red, without the provisional
blue fill or segment dividers. Bounds 72 position the health fill; bounds 73
position the health text. The nanotech XP track uses bounds 71.

The bolt draw at `0xb76b4` selects HUD icon **7**, centred at `(458,18)`
with normal dimensions `28 x 22`. The loader at `0xb0994..0xb09f8` starts
its icon table at **one**. Therefore icon 7 means zero-based PAK entry **6**,
the silver bolt. Treating the HUD identifier as a zero-based array index
incorrectly displayed the next image (a bag).

The blaster tier data at `0x1f1018` supplies ammo capacity 40 at `+0x10`
and HUD icon identifier 94 at `+0x18`. Weapon identifiers use the same
one-based image lookup.

Verification: `mods/sm_native/hud_verified_colours.bmp` is a hidden native
DX11 capture with the corrected health bar and silver bolt. The associated
log reports zero DX11 validation errors.

## Audio

`src/hig/higaudio.*` reads Sony PS ADPCM, VAG streams, version 3 Scream
`SBlk` sound banks, and the game's sound bindings. Linked HIG asset headers
provide bounded payloads and source names; source paths are never opened
on the host.

`/VAGS.WAD` contains 3,276 `{signature,size,offset}` entries. Signatures are
CRC32 of uppercase filenames. `SND_AtracOpen` in the resident at `0x303e8`
resolves separate `_l.vag` and `_r.vag` channels. Each stream uses a VAGp
header and 16-byte PS ADPCM blocks yielding 28 samples.

Pokitaru's first `LevelMusic` object (`0xf2cdb47f`) selects **01A**.
Its properties select the current level and the letter A, matching the
`%02i%c` construction at `LEVEL_01.REL:0x18cfe0`. Channel signatures are
`2afed640` and `f52effa3`. Each channel decodes to 11,718,364 samples at
44,100 Hz. The native mixer plays these in stereo and honours looping.

Sound assets are loaded from GLOBAL, TR, SP and the level BWD. A `sounds.sig`
asset's hash plus one identifies its bank; the signature array gives sound
indices, also verified against GDE sound field `+0x30`. The earlier fields
in GDE sounds are not substitutes for this binding.

| Action | Sound signature | Bank | Sound index |
| --- | --- | --- | --- |
| Jump | `f4f95945` | `9c2a5efa` | 3 |
| Double jump | `090979ac` | `9c2a5efa` | 5 |
| Heli-Pack loop | `1e930d2b` | `9c2a5efa` | 14 |
| Wrench swing | `09b077b5` | `7a0a3a7b` | 0 |
| Lacerator/blaster fire | `b78c792f` | `432c61a6` | 0 |
| Lacerator/blaster impact | `be624601` | `432c61a6` | 1 |

Jump references occur in the original initializer at `0x3757c` and
`0x3759c`. Ratchet's original GLIDE animation also contains the event name
`helipack` and signature `1e930d2b`; DOUBLE_J contains `090979ac`.
The current native player emits action sounds on its simulation transitions,
not on each presentation frame. Heli-Pack playback stops on leaving glide.
Recovering and dispatching every authored animation-event timestamp remains
future work; sound selection is recovered, but timing is not yet a complete
retail sequencer reproduction.

The native Windows output uses a buffered 48 kHz stereo PCM mixer with
independent music/effects volume, pause/focus handling, and bounded voices.
The same mixer supports deterministic, device-free WAV capture:

```text
run_size_matters.bat --gameplay-script idle:30,fire:15,idle:30,jump:10,idle:1,glide:40,idle:30,wrench:1,idle:45 --audio-capture NEW.wav
```

Use a new output filename. Hidden runs do not open a sound device.
`sm_media_probe` checks the real disc's HUD, music, mapped sounds, pause,
and malformed/cyclic input. The mixed capture
`mods/sm_native/pokitaru_actions_first.wav` contains 323,200 stereo frames
and ten mapped action events.

The Scream format investigation also used the primary implementation in
[OpenGOAL's sound loader](https://github.com/open-goal/jak-project/blob/master/game/sound/989snd/loader.cpp)
and its accompanying tone/grain definitions as a reference.

## Remaining limits

Health currently represents native bring-up state (10/10); retail damage,
nanotech progression, pickups and saved counters are not implemented.
Music trigger transitions, material-dependent footsteps, the complete Scream
grain instruction set, SPU ADSR/reverb and full spatial sound behaviour remain
unfinished. Unsupported sound programs are skipped rather than combining all
their branches. Windows device output has a separate path from the headless
media checks; those checks alone do not establish listening parity with PS2.
