# Seeded sprite atlases

`type:"spritesheet"` renders an external texture recipe with a sequence of seeds,
packs its PNG outputs into matching grids and writes a portable JSON manifest.
Unlike inspection `sheet` outputs, atlases have no labels or presentation layout.

```json
{
  "size": 256,
  "seed": 42,
  "nodes": {},
  "outputs": {
    "leaves.atlas.json": {
      "type": "spritesheet",
      "source": "leaf.json",
      "columns": 4,
      "rows": 3,
      "count": 12,
      "padding": 4
    }
  }
}
```

```sh
./build/texutil samples/spritesheets/leaves.json --out out/leaf-atlas --threads 8 --json
```

The included example produces `leaves.atlas.json` and
`leaves.atlas.assets/color.png`, `normal.png` and `metallic-roughness.png`.
Each atlas is 1024 × 768. Every tile is 256 × 256 including four pixels of
padding per edge; each source variant renders at 248 × 248.

| Field | Default | Meaning |
| --- | --- | --- |
| `source` | Required | External JSON recipe, relative to the declaring JSON. |
| `columns` | 4 | Grid columns, 1..64. |
| `rows` | Enough to fit count | Grid rows, 1..64. May reserve unused cells. |
| `count` | 16 | Occupied variants, 1..256 and at most grid capacity. |
| `padding` | 4 | Extruded pixels per cell edge, 0..64. Must leave positive content dimensions. |
| `seed` | Parent's effective root seed | First variant seed; subsequent cells use seed + index. |

The parent `size` or CLI `--size` is the complete cell size including padding,
and accepts rectangular dimensions. Atlas dimensions may not exceed 16384 per
side. A 4 × 4 grid of 256-pixel cells gives a 1024 × 1024 atlas.

The source may contain 1..16 regular PNG outputs. They render with the same seed
for each cell, keeping color, alpha, normals and packed material data aligned.
Outputs retain their bit depths, color/data encoding and alpha settings. Source
sheet and MaterialX outputs are validated but skipped and listed in the manifest.
Preview, export_bake and nested spritesheet source outputs are rejected. Unused
source nodes still validate.

Seeds change stochastic nodes that inherit the source root seed. Explicit node
seeds and imported graph seeds retain their isolation. Constants and image-only
recipes repeat; a new seed does not guarantee unique pixels. The example uses
inherited seeds for pigment and outline distortion.

Padding duplicates boundary pixels without resampling or changing normal
orientation. It reduces neighboring-cell bleed during filtering, but does not
guarantee isolation throughout an arbitrarily deep mip chain. Unoccupied color
cells are transparent when alpha output is enabled; the manifest lists only
occupied cells.

## Manifest and FoliageUtil

The manifest uses `format:"texutil-spritesheet"`, `version:1`, and
`uv_origin:"top-left"`. Indices run left-to-right, then top-to-bottom.
`cell_size` includes padding; `content_size` excludes it. `image_size`, grid,
count, padding, per-cell `pixels` / `uv_rect`, and each cell's seed are explicit.
Image paths are relative to the manifest inside `<manifest-stem>.assets`.
Keep that directory with the JSON when moving an atlas.

FoliageUtil `instance.atlas` reads this manifest. `atlas_mode:"random"` selects
an occupied cell per copy, `"cycle"` enumerates them, and `"fixed"` uses
`atlas_index`. Bind the matching atlas PNGs in the foliage material. The reader
handles PNG and mesh UV origins; no manual grid numbers or vertical flip are
needed. Use the same selected UVs for every material channel.

Physical card dimensions are independent of UV bounds. Preserve the content
aspect ratio for undistorted artwork, or vary it deliberately with `scale_jitter`.

## Budgets and verification

Variants execute sequentially while atlas canvases share the graph's float-buffer
budget. Peak statistics include both retained atlas pixels and the active variant.
All variants complete before atlas images are written; the manifest is written
last. Multi-file output is not transactional. Source recipes, imported JSON and
input images are protected from overwrite, including reserved-package collisions.

The `spritesheet_export` CTest checks rectangular and partial grids, UV bounds,
edge extrusion, sRGB/alpha and 16-bit data, channel alignment, seed/thread
repeatability, explicit seed isolation, input protection and memory failure.
The full 20-test suite passes on the development macOS build.
