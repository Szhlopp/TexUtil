# Glass with gentle scratches

[samples/glass.json](../samples/glass.json) generates 2048-square glass material
maps using wrapped, tapered scratch stamps. A matching graph lives in
[examples/glass.json](../examples/glass.json). No external assets are required.

```sh
./build/texutil samples/glass.json --out out/glass --threads 8
```

| Output | Purpose |
| --- | --- |
| `glass.mtlx` | Connected Standard Surface material; see [MaterialX export](MATERIALX.md). |
| `glass-color.png` | Nearly white sRGB base color with a slight green tint. |
| `glass-roughness.png` | 16-bit linear roughness: smooth glass with rougher scratches. |
| `glass-normal.png` | 16-bit DirectX tangent-space normals for shallow grooves. |
| `glass-height.png` | 16-bit linear height, centered at 0.5 on undamaged glass. |
| `glass-height.pfm` | The same heightfield as 32-bit float. |
| `glass-transmission.png` | Constant white transmission weight. |
| `glass-scratches.png` | Unboosted linear scratch mask for custom shader control. |
| `glass-preview.png` | Illustrative studio reflection and scratch presentation. |
| `glass-sheet.png` | Native sheet of preview, boosted mask, roughness and normal. |

Use a dielectric glass shader with transmission weight 1, metallic 0 and an initial
IOR of 1.5. Import color as sRGB; import all other material maps as linear data.
The color map's alpha is not used for glass transparency. Transmission, refraction,
thickness and environment reflections are handled by your renderer. For a thin
window pane, use its thin-glass option or appropriate pane geometry. Set normal
interpretation to DirectX, or regenerate with `normal.convention: "opengl"`.

The procedural surface maps tile. The studio preview uses a nonrepeating gradient
and softbox shape for presentation and is not a tileable material map or a physical
refraction render. Its scratch glints are illustrative, and should not be used as
base color. The sheet's scratch mask is contrast-boosted for visibility; the exported
mask and material maps retain their gentle values.

## Controls

- `fine-scratches.count` (110) and `long-scratches.count` (18) set density.
- Their `size` pairs control length and width in UV units; `rotation_jitter` and
  `angle` control direction. Both scatter nodes wrap across image boundaries.
- `value_range` controls individual scratch intensity. Noise modulates the strokes
  to avoid completely uniform lines.
- `base-roughness.value` is 0.025. `scratch-roughness.out` adds up to 0.4 times the
  scratch mask, whose actual values are softened and reduced by the stamp intensity.
- `height.out` runs from 0.5 to 0.488, cutting shallow grooves into a flat surface.
  With `normal.strength: 0.025`, matching displacement for a one-unit-wide tile is
  `0.025 * (height - 0.5)` world units. The actual grooves are shallower than the
  full range because the scratch mask stays below 1.
- Explicit scatter seeds make this recipe reproducible. Edit them to vary placement.

The saved resolution resolves fine strokes approximately one to a few pixels wide.
Lower-resolution renders can lose them. Preserve sufficient resolution and use
appropriate filtering in your renderer. The normal and roughness PNGs use 16 bits
to retain small variations; reduce the output `bits` to 8 if an importer requires it.
