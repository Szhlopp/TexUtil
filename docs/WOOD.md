# Directional warp and wood grain

Render the included wood recipe:

```sh
./build/texutil examples/wood.json --out out/wood
./build/texutil examples/wood.json --out out/wood-large --size 2048 --seed 92
```

The graph exports color, 16-bit height, DirectX normals and roughness. The default
size is 1024. It is a procedural, stylized wood example with explicit knot positions;
it is not a scanned material and it does not tile seamlessly. Changing the seed
changes the grain variation, while knot positions remain controlled by the recipe.

## Directional warp

```json
{
  "op": "directional_warp",
  "input": "lines",
  "intensity": "knot-mask",
  "angle": 0,
  "strength": 0.2,
  "midlevel": 0,
  "channel": "luminance",
  "edge": "repeat"
}
```

The control is any node reference, including an imported grayscale image:

```json
{
  "knot-mask": {"op": "image", "path": "my-knots.png", "kind": "scalar"},
  "lines": {"op": "stripes", "count": 48},
  "wood-lines": {"op": "directional_warp", "input": "lines", "intensity": "knot-mask", "strength": 0.2}
}
```

Use these entries inside the document's `nodes` object. `path` is relative to the
JSON document. Import B/W displacement images as `kind: "scalar"` so a stored gray
value of 0.5 remains 0.5 instead of being decoded as sRGB color. A colored or packed
control can select `channel: "r"`, `"g"`, `"b"` or `"a"`; the default is luminance.
For packed data, import as `kind: "normal"`, or `kind: "color", srgb: false`, to
preserve its channel values. Alpha is not multiplied into the control automatically.

The sampling rule is:

```text
amount = strength * (clamp(selected_control, 0, 1) - midlevel)
source_uv = output_uv + amount * [cos(angle), sin(angle)]
```

Angles are clockwise degrees: 0 samples to the right, 90 samples down. As with the
existing warp node, visible image features move in the opposite direction. Strength
is in UV units, independent of output resolution. Negative strength reverses the
sampling direction. Black does nothing with the default midlevel 0; white gives
full displacement. Midlevel 0.5 produces -strength/2 at black and +strength/2 at
white. Omit `intensity` for a uniform white control, which makes a uniform shift.
`strength: 0` returns the source directly. `edge` accepts repeat, clamp or transparent.
The result preserves the source's scalar/color/data kind and alpha filtering rules.

## Building the wood

1. `stripes` creates growth lines and a finer set of fiber lines.
2. Stretch cloud noise along Y using `transform`, then use it to directionally warp
   each set of lines. This introduces gentle lengthwise bends.
3. Create a feathered circular `shape` and `stamp` it at several positions with tall,
   elliptical footprints. This is the knot displacement field.
4. Apply the same directional knot warp to the coarse lines, fine lines and elongated
   fiber noise. Sufficient displacement folds their contours into closed rings.
5. Adjust the growth-line profile with `levels`, combine fibers with `blend`, and
   darken knot centers. `ramp` maps the scalar grain to warm wood tones.
6. Derive height and a subtle normal map from the same grain. Roughness here is an
   artistic remapping of grain, not a physically inferred surface measurement.

Useful edits in `examples/wood.json`:

| Node / parameter | Effect |
| --- | --- |
| `growth-lines.count` | Number of broad growth bands. |
| `fine-lines.count` | Fine grain density. |
| `wavy-lines.strength`, `fine-waves.strength`, `fiber-wave.strength` | Base waviness; keep these matched for aligned details. |
| `knots.points` | Knot positions and full UV footprints. |
| `knotted-grain.strength`, `fine-knots.strength`, `fiber-knots.strength` | Knot bending/folding; keep these matched. |
| `growth-profile.gamma` | Thickness and contrast of dark growth bands. |
| `color.stops` | Palette. Hex values are interpreted as sRGB. |
| `normal.strength` | Depth of grain relief. |

## Stripes

`stripes` supports sine, triangle, saw and square profiles, with cycle count, angle,
phase and square-wave duty fraction. At angle 0 the lines are vertical, so a warp
along X bends them. It analytically box-filters the profile across the projected
pixel footprint; subpixel lines retain average coverage instead of vanishing.
This antialiasing applies to the initial source. Strong warps can compress cached
lines above the output's pixel resolution; lower counts or render larger images
if knots show aliasing. The warp uses the existing bilinear sampler, not anisotropic
texture filtering. Integer counts at axis-aligned angles produce periodic lines;
arbitrary angles/counts are not necessarily tileable.
