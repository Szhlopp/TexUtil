# Wind-packed powder snow

[samples/snow.json](../samples/snow.json) is a self-contained, tileable 2048-square
snow material. A matching demonstration lives at [examples/snow.json](../examples/snow.json).

```sh
./build/texutil samples/snow.json --out out/snow --threads 8
```

| Output | Purpose |
| --- | --- |
| `snow-color.png` | Unlit sRGB albedo, pale white with subtle cold-blue variation. |
| `snow-height.png` | Linear 16-bit grayscale height for displacement. |
| `snow-height.pfm` | The same heightfield as unclipped 32-bit float. |
| `snow-normal.png` | Linear/data RGB tangent-space normals, DirectX convention. |
| `snow-lit.png` | Illustrative diffuse-light preview using the exported normal. |
| `snow-sheet.png` | Native titled 2x2 sheet of lit preview, albedo, height and normal. |

Broad periodic fractal noise defines shallow drifts. Stretched and rotated noise
adds directional wind texture, followed by light wind redistribution and thermal
settling. Fine fractal powder and independent Gaussian crystal grain are added after
settling so small surface features remain. The near-white color is intentionally
subtle; most of the visible material detail comes from relief and normal response.

The heightfield is centered near 0.5 and is not auto-leveled per export. Normal
`strength:0.08` defines displacement scale per unit UV. When interpreting a tile as
one world-space unit wide, the corresponding displacement is
`0.08 * (height - 0.5)` world units. Adjust this to the physical scale of your material.
Import albedo as sRGB and height/normal as linear data. For OpenGL-style normals,
change the normal node's `convention` to `opengl` and update the preview lighting's
Y convention if regenerating that preview.

The lit preview decodes the DirectX normal, evaluates a fixed directional diffuse
term plus ambient light, and multiplies the albedo. It is separate from the unlit
material color and uses no baked shadows in that albedo. It is a 2D relief preview,
not a full renderer with subsurface scattering or view-dependent snow sparkle.

Seed 826 makes the material reproducible. Procedural noise uses `tile:true`, erosion
and normals use `edge:repeat`. There are no imported texture assets. The native sheet
has 448-pixel cells; changing `--size` does not change the sheet layout. Pixel-scale
crystal noise and erosion footprints can change character at another resolution.
