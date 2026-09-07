# Portable sample graphs

These JSON documents are intended to be checked into Git. They are self-contained
and require no external textures. Generated images belong under `out/`, which is
ignored along with build products.

```sh
./build/texutil samples/terrain.json --out out/terrain
./build/texutil samples/forest.json --out out/forest
./build/texutil validate samples/forest.json
```

| Sample | What it produces |
| --- | --- |
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
| `effects-gallery.json` | Glow, stroke, edge detection, swirl and polar transforms. |

Except for the compact `terrain.json`, these are copies of the corresponding
`examples/` graphs used by the documentation/gallery tool. Keep the corresponding
copy synchronized when changing a demonstration; the sample CTest check catches
accidental divergence. Use `samples/` as a starting point for your own materials.
`terrain.json` was created here as a real runnable sample; it was previously only
an illustrative filename in a CLI command.

CLI `--size` overrides the saved resolution. Simulation iterations, pixel distances
and noise frequencies still need tuning for another resolution. See the large
texture notes in the project README before attempting a 16384-square erosion job.
