# Portable sample graphs

These JSON documents are intended to be checked into Git and require no external
textures. Some recipes import other samples; keep this folder together when copying
them. Generated images belong under `out/`, which is ignored along with build products.

```sh
./build/texutil samples/terrain.json --out out/terrain
./build/texutil samples/forest.json --out out/forest
./build/texutil validate samples/forest.json
```

| Sample | What it produces |
| --- | --- |
| `birch.json` | Pale birch bark, horizontal marks, healed scars and separate cut-end maps for one log. Includes MaterialX and a sheet; see [controls and optional Blender preview](../docs/BIRCH.md). |
| `handled-glass.json` | Imports glass, dust and fingerprints to make a reusable worn-glass material. See [imports](../docs/IMPORTS.md). |
| `dust.json` | Subtle tileable dust with fine particles, flecks, lint and faint buildup. See [controls](../docs/DUST.md). |
| `fingerprints-scattered.json` | Scattered fingerprint masks with varying intensity, rotation and size, plus edge wrapping. |
| `fingerprint.json` | Grayscale fingerprint-style ridges as an ink print and an inverse stamping mask. See [construction](../docs/FINGERPRINT.md). |
| `potion.json` | Fresh ruby potion with shallow swirls, bubbles, separate transmission tint, liquid maps and a labeled preview. See [controls](../docs/POTION.md). |
| `glass.json` | Gently scratched glass color, height, normals, roughness, transmission and an illustrative preview. See [controls](../docs/GLASS.md). |
| `cork.json` | Tileable pressed cork color, height, normals, roughness and a native labeled sheet. See [controls](../docs/CORK.md). |
| `snow.json` | Tileable snow albedo, height, normals, lighting preview and a native labeled sheet. |
| `terrain.json` | Compact cumulative rain, wind and thermal terrain; 16-bit PNG, float PFM, normals. |
| `forest.json` | Lush forest comparison before/after sequential erosion, plus unlit albedo. |
| `erosion.json` | Independent wind/rain/time comparison from the same starting terrain. |
| `wood.json` | Knotted wood color, height, normals and roughness. |
| `stone.json` | Procedural stone material maps. |
| `tiles.json` | Tile layout and material maps. |
| `ornament.json` | Repeated radial ornamental shapes. |
| `stamps.json` | Explicit stamping demonstration. |
| `warped.json` | Displacement demonstration. |
| `nodes-gallery.json` | Advanced primitive/filter demonstrations. |
| `presets-gallery.json` | All 18 preset recipes. |
| `effects-gallery.json` | Glow, stroke, edge detection, swirl/polar, flood fill and normal integration. |

Except for the compact `terrain.json`, these are copies of the corresponding
`examples/` graphs used by the documentation/gallery tool. Keep the corresponding
copy synchronized when changing a demonstration; the sample CTest check catches
accidental divergence. Use `samples/` as a starting point for your own materials.
`terrain.json` was created here as a real runnable sample; it was previously only
an illustrative filename in a CLI command.

CLI `--size` overrides the saved resolution. Simulation iterations, pixel distances
and noise frequencies still need tuning for another resolution. See the large
texture notes in the project README before attempting a 16384-square erosion job.

The five gallery/comparison samples, `snow.json`, `cork.json`, `glass.json`, `potion.json`, `dust.json`, `handled-glass.json` and `birch.json` include native `type:sheet` outputs.
They render their labeled graphics directly with TexUtil; no Pillow step is needed.

`stone.json`, `wood.json`, `snow.json`, `cork.json`, `glass.json`, `potion.json`, `handled-glass.json` and `birch.json`
also emit MaterialX materials. See [MaterialX export](../docs/MATERIALX.md) for
custom shader parameters and Maya usage. Copy each `.mtlx` with its referenced PNGs.
