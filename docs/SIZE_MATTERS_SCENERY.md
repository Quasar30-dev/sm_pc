# Size Matters scenery UV and texture corrections

Verified against the USA ISO on 9 September 2026. Both corrections live in
`src/hig/higlevel.cpp`, shared by Theia, IRIS asset decoding and the native
Size Matters host. Original meshes, placements and ISO bytes are preserved.

## Cut stones and incomplete plants

The GDE reader incorrectly treated the render-header float at shrub +0x0C
as a UV divisor. Pokitaru's shrubs store 2 there. Dividing their coordinates
by `1024 * 2` selected only half the texture in each direction. Transparent
atlas regions then removed parts of the stones and flower/grass surfaces.

The original resident VU programs supply fixed scales:

| Program | USA SCUS file offset of UV immediate | Scale |
|---|---|---|
| staticmesh_vu_renderer_fast | 0xA0D00 | 1/256 |
| tie_vu_renderer | 0xA4DF8 | 1/1024 |
| shrub_vu_renderer_fast | 0xA4548 | 1/1024 |

The immediate `0x3A800000` loads I; the following upper instruction consumes
the previous I value, including when its lower instruction loads the next
immediate. In `shrub_vu_renderer_fast`, the saved factor multiplies the
converted UV components. The original rigid renderer also sets TexScale
to (1,1), at VA 0x01E8671C–0x01E86724. It does not apply the header float
as another divisor. The static simple renderer sets (1,1) at 0x01E86A40.
The meaning of the legacy header floats outside these PS2 paths remains
unconfirmed; the reader no longer treats them as UV scales.

Retail examples:

| Asset | Hash | Raw U range | Correct U range |
|---|---|---|---|
| stones.mb.ps2.gde | 89CA349E | 4096–5120 | 4–5 |
| grass01.mb.ps2.gde | BB0D6B0A | 4096–5120 | 4–5 |
| rockshrub.mb.ps2.gde | E640F109 | 4096–5120 | 4–5 |

The V range of stones/grass is 3073–4096. Preserve these authored UVs,
including their repeat tiles; do not normalize each mesh to an invented
0–1 bounding box. Flowers also use subregions of their atlas for stems,
leaves and petals.

## Cropped and shifted texture rows

The old GIM reader assumed tightly packed pixels and a fixed +0x40 pixel
offset. GIM separates visible dimensions, row alignment and frame offsets.
The decoder now:

- Uses the chunk's data offset and the first entry in the header +0x18
  frame-offset table for mip 0 / frame 0.
- Computes the byte pitch from bit depth and header +0x0E row alignment.
- Deswizzles the padded surface before extracting visible pixels, including
  odd-width INDEX4 rows and partial final tile rows.
- Bounds image and palette reads to their own chunk/data end. Palettes may
  omit padding after the last row.
- Accepts the retail INDEX4 convention where a nominal 256-colour palette
  contains only the 16 addressable entries (e.g. level06 tube_01.gim).

`sceGimPictureGetImagePixels`, VA 0x01E81620 (file 0x1F1A0), and
`sceGimPictureGetPalettePixels`, VA 0x01E81690 (file 0x1F210), establish the
frame table lookup. This is a first-frame decoder, not animated-GIM playback.

Level06 `r_wallTrim.gim` (hash 30BF8288) is INDEX8, **35×17**, with **48-byte
rows** and a compact 239-entry RGBA palette. Pixel (12,8) is (146,107,52,255).
Reading 35-byte rows instead reads row padding as colour indices and shifts
the edges. Other affected assets include LargePumpTurbine (8×16 INDEX8),
narrow blooms, glow/forcefield textures and GLOBAL white (16×16 INDEX4,
16-byte rows rather than 8).

## Validation

- `hig_probe`: all 24 retail level WADs, all **2,296 embedded GIM frames**,
  original stone/grass/rock UV spans and wall-trim pixel checks pass.
- Synthetic fixtures cover signed UVs, non-default frame offsets, row padding,
  odd-width indexed images, direct colour, swizzled tile boundaries, compact
  palettes, the INDEX4 legacy palette width and truncated frames.
- Matching native Pokitaru captures before/after show restored stones and
  complete plant silhouettes. A 4K capture and a Ryllus capture use the
  DX11 debug device. Source rendering evidence and logs are under
  `mods/sm_native/scenery_*` and `mods/hig_re/scenery_verified_20260909.log`.
- Theia and the native host are rebuilt from the corrected shared reader.

These checks establish the decoder corrections, not completion of every
game material. Secondary material passes, per-instance lighting and some
external asset references remain separately tracked limitations. The level
audit still reports unsupported non-static GDE types and unresolved links.
