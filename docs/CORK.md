# Pressed cork material

[samples/cork.json](../samples/cork.json) generates a tileable 2048-square cork
material using existing nodes, with no imported images. The matching example is
[examples/cork.json](../examples/cork.json).

```sh
./build/texutil samples/cork.json --out out/cork --threads 8
```

| Output | Purpose |
| --- | --- |
| `cork.mtlx` | Connected Standard Surface material; see [MaterialX export](MATERIALX.md). |
| `cork-color.png` | Warm tan and brown unlit sRGB albedo. |
| `cork-height.png` | Linear 16-bit grayscale displacement. |
| `cork-height.pfm` | The same heightfield as 32-bit float. |
| `cork-normal.png` | DirectX tangent-space normals, imported as linear data. |
| `cork-roughness.png` | Linear roughness, with rougher pore interiors. |
| `cork-lit.png` | Diffuse lighting preview using the exported normal. |
| `cork-sheet.png` | Native labeled preview of lighting, color, height and normal. |

Warped Voronoi cells create irregular pressed granules. A second, finer cell layer
and fractal grain break up their interiors. Matching warped cell edges create
broken fissures; a separate noise range selection adds scattered pores. The same
cavity mask darkens the color, cuts the height and increases roughness. Shallow
height differences between granules keep the result closer to cork board than bark.

Useful controls:

- Change `raw-flakes.scale` and `raw-seams.scale` together for granule size. Larger
  values make smaller granules; keep their seeds matched as well.
- `flakes.strength` and `seams.strength` control boundary distortion. Keep them
  matched so the fissures follow the color granules.
- Increase the upper end of `pores.range` for more pores, or `pore-depth.out[1]`
  for deeper cavities.
- Edit `cork-tone.stops` for the tan/brown palette.
- `normal.strength` is 0.018. For a tile one world-space unit wide, matching
  displacement is `0.018 * (height - 0.5)` world units.

Noise is periodic and sampling, blur and normals use repeating edges. The 0.65-pixel
height blur is tuned for the saved 2048 resolution. Most seeds are explicitly fixed
to preserve matching cell boundaries and independent grain layers; edit those node
seeds to create variations. A CLI seed override only changes nodes without explicit
seeds. The lit image is an illustrative diffuse preview, separate from the albedo.
