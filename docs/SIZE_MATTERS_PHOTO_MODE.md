# Native Size Matters photo mode

Open a loaded level and press **F2**, or select **Photo mode** in the pause menu.
**F2 / Esc** returns; XInput **Start** also returns. Entering from pause returns
to pause. Gameplay position, combat, triggers, projectiles, environment time and
audio remain frozen while the photo camera and explicitly playing photo actors
can move. The gameplay camera is restored on exit.

## Making a photo

- **Characters:** choose a model from the searchable disc library and spawn it
  beside Ratchet, in front of the camera or at the selected photo character.
  Select, duplicate, remove or hide instances. There is a 64-instance limit.
  Gameplay Ratchet and level enemies have separate visibility switches.
- **Pose:** edit position, rotation and scale; snap to ground or frame the
  current posed geometry. Choose any clip by its index and original stored name.
  Play, pause, loop, change speed, scrub time or step by 1/30 second. A paused
  final frame stays on the final pose. Every instance has its own playback state.
- **Ratchet attachments:** show/hide Clank, select wrench/blaster, and either
  follow the hero's swing/glide pose or manually set attachment clips and time.
  Original back/right-hand joints and the wrench's own deformation bank are used.
- **Camera:** RMB looks; WASD moves; Q/E lowers/raises; Shift moves faster.
  XInput sticks move/look and bumpers lower/raise. Field of view, roll, speed and
  camera position are editable. Frame-selected uses the model's posed bounds,
  including its rotation and scale, so large characters can be framed too.
- **F12 / Take photo:** writes a clean BMP before UI compositing. Select current
  render size, **1920×1080** or **3840×2160**; a high-resolution capture does not
  change saved graphics preferences. Images go to `screenshots/SizeMatters/`
  beside the launcher. The saved path appears in the panel. **F1** hides/restores
  the interface and F12 continues to work while it is hidden.

The original Ratchet becomes a separate, paused photo instance on first entry;
the gameplay Ratchet is hidden until the mode ends. Photo instances are retained
when re-entering the same level, and cleared when a different level is loaded.
They do not enter the campaign, save data or collision/AI systems. Staged scenes
are currently in-memory; persistent pose-scene files are not implemented.

## Original assets and Theia spillover

The catalog reads **GLOBAL.WAD, SP.WAD and all 24 level WADs**, matching skinned
GDE paths to their own `.anim` bank. The default hero skin comes from
`HEROSKIN.PAK` entry 29. The USA image yields **306 entries**: the default Ratchet
plus **305 WAD model/bank pairs**. Repeated characters remain scoped to their
archive because animations and texture variants may differ. The catalog also
includes rigged gadgets/props. Clip names are the original eight-byte labels;
indices distinguish names that share the same truncated prefix.

Catalog work uses a background worker with its own read-only ISO handle. Source
WADs are decompressed and checksum-checked. Spawning loads/caches the selected
model and complete bank, validates bone references, and resolves texture hashes
against its source WAD and the shared archives. No game assets are modified.

The all-character check exposed two older shared-reader restrictions:

1. Display command **0x9b000000/1** blocked weapons, Skyboarders and Dozer.
   The original resident `SKIN_DrawMesh` dispatcher, file offsets
   **0x28028 → 0x283d8**, consumes it without changing skin rendering state.
   The shared decoder now accepts that command explicitly.
2. TrampSproutLG stores several strips in one DMA packet. The original draw
   reads only packet extent/address at **0x282b0**; each GIF tag owns its vertex
   count. The shared decoder no longer treats the packet's aggregate count as
   a single strip capped at 256 vertices. Individual GIF/VIF counts and bounds
   remain validated.

Both fixes are in `hig::decode_actor`, used by Theia as well as the native game.
`hig::catalog_actors` is a shared CPU catalog API. Photo transforms, instances,
playback and texture binding are in `sm::PhotoSession`, independent of DX11/UI.
Full secondary retail material passes and class-specific procedural effects are
still separate rendering work; loading every bank does not establish visual
equivalence for every effect in the original game.

## Headless use

```
run_size_matters.bat --photo --photo-clip 12 --photo-time 0.6
run_size_matters.bat --photo-model DozerMaster --photo-clip 0 --photo-time 0.4
run_size_matters.bat --photo --hidden --frames 2 --photo-shot NEW.bmp --photo-resolution 2160
run_size_matters.bat --gameplay-script throw:10,idle:20 --hidden --frames 2 --capture THROW.bmp
run_size_matters.bat --gameplay-script jump:5,hyper:10 --hidden --frames 2 --capture STRIKE.bmp
```

`--photo-model` can be repeated. It matches the original model basename and scans
the library synchronously for deterministic CLI selection. `--photo-hide-ui`
also permits clean captures through the ordinary `--capture` frame-limit path.

`sm_photo_wrench_probe` loads every catalog entry, samples every clip, checks
finite rendered geometry, and exercises independent pose state, duplication,
removal, pause, loop, final-frame hold and posed bounds. On the USA image,
**all 306 entries, including the default Ratchet and its 100 clips, loaded and
their complete clip banks rendered without an unsupported-model failure**.
Hidden DX11 captures verify the UI and clean high-resolution capture path.
No physical controller or emulator was used.

Final evidence is recorded in [the validation report](SIZE_MATTERS_PHOTO_VALIDATION.md).
