# Lush forest terrain example

The saved graph is [samples/forest.json](../samples/forest.json), with a matching
copy at [examples/forest.json](../examples/forest.json). The 1024-square graph uses
seed 731 and periodic sampling. It compares cumulative stages:

1. Original multiscale terrain, remapped once to heights 0.12..0.85.
2. Rain: 400 steps, rate 0.8, rainfall 0.04, capacity 40, deposition 0.4,
   evaporation 0.12.
3. Wind applied to the rain result: 96 steps, rate 0.55, angle 35 degrees,
   distance 3 pixels, talus 0.0006. A soft height-range mask (0.4..1 with
   softness 0.18) exposes a broader area of the terrain to wind.
4. Thermal settling applied after wind: 160 steps, rate 0.65, talus 0.0006.

All erosion boundaries repeat. These settings are deliberately overdriven from the
initial forest example so drifting wind ridges and thermal smoothing are evident.
Rain remains less visually dramatic on these broad slopes. Every column uses the
same display range; no per-stage auto-leveling exaggerates the differences.
They remain artistic models, not physically calibrated geology or a vegetation
simulation. See [EROSION.md](EROSION.md).

The forest palette moves from deep green valleys to lighter green uplands.
Fine cellular and fractal noise modulate canopy-like color variation. This is a
top-down procedural terrain/material illustration; it does not place individual
3D trees. A finite-difference light term supplies directional relief in the forest
preview. `forest-albedo.png` is exported separately without that baked lighting.
All stages use the same palette, shading, normal strength and display range.

```sh
./build/texutil samples/forest.json --out out/forest --threads 8
python3 tools/gallery.py --only forest --size 1024
```

Outputs include each stage's 16-bit height PNG, 32-bit height PFM, normal map and
shaded forest preview, plus the final unlit albedo. The contact sheet is
`out/forest/gallery.png`, ordered original, rain, rain + wind, rain + wind + time.
Final preview: `out/forest/after-time-forest.png`.

For a compact terrain graph without comparison branches or color variations, use
[samples/terrain.json](../samples/terrain.json). Generated outputs remain in ignored
`out/` directories; the recipes, documentation and gallery helper belong in Git.
