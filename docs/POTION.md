# Ruby health potion brew

[samples/potion.json](../samples/potion.json) produces a stylized 2048-square red
liquid material inspired by the ruby brew in the bottle reference. Its matching
example is [examples/potion.json](../examples/potion.json). It uses existing nodes
and needs no imported images.

```sh
./build/texutil samples/potion.json --out out/potion --threads 8
```

| Output | Purpose |
| --- | --- |
| `potion.mtlx` | Connected Standard Surface material; see [MaterialX export](MATERIALX.md). |
| `potion-color.png` | Unlit sRGB ruby color with crimson flowing ribbons. |
| `potion-height.png` | 16-bit linear height with shallow ripples and bubble domes. |
| `potion-height.pfm` | The same heightfield as 32-bit float. |
| `potion-normal.png` | 16-bit DirectX tangent-space normals. |
| `potion-roughness.png` | 16-bit linear roughness, ranging from 0.055 to 0.11. |
| `potion-transmission.png` | Constant transmission weight of 0.92. |
| `potion-emission.png` | Optional subtle red emission color, stored as 16-bit sRGB. |
| `potion-bubbles.png` | Linear bubble mask for custom shader effects. |
| `potion-preview.png` | Illustrative surface lighting, bubble glints and faint emission. |
| `potion-sheet.png` | Native labeled preview, color, boosted height and normal. |

Stretched fractal noise is displaced by two independent flow fields, then twisted
around a broad central swirl and a smaller swirl crossing a tile corner. A color
ramp supplies deep wine-red through bright crimson. A narrow grayscale selection
adds fine red ribbons. Sparse wrapped Gaussian stamps create small bubble domes;
matching ring stamps supply illustrative highlights in the preview only.

The material maps tile, including the wrapped swirls and bubbles. The height has
small variations around 0.5; only the sheet's height view expands its contrast.
Normal strength is 0.035. For a tile one world-space unit wide, matching displacement
is `0.035 * (height - 0.5)` world units.

## Using it inside a bottle

Apply these maps to separate liquid geometry inside the glass. Use a transmissive
dielectric shader, metallic 0 and an initial liquid IOR of about 1.33. Import color
and emission as sRGB, and the other maps as linear data. Match the normal convention
to your renderer. Start with emission disabled for ordinary liquid, then enable it
at low intensity for a magical brew.

The bottle reference combines colored liquid, glass reflections and depth-dependent
light absorption. These textures supply surface detail and stylized color; volume
absorption and refraction are renderer settings. Use a red absorption/transmission
tint and tune its distance to your bottle's actual size. The preview is a 2D surface
illustration, not a bottle or volume render. Its rings represent surface bubbles;
floating bubbles inside the liquid need separate geometry or a volume effect.

## Controls

- Edit `ruby.stops` to change the brew color.
- `main-swirl.angle` (420) and `flow.angle` (-220) control the twists. Reduce their
  magnitudes for a calmer potion, or adjust centers and radii to move the eddies.
- `flow-warp.strength` (0.16) controls the irregular flow. `flow-base.stretch`
  controls ribbon proportions.
- Change `bubbles` and `bubble-rims` together: their count, size, jitter, seed and
  value range must match so the preview rings align with the surface domes.
- `bubble-height.out[1]` controls dome height; `ripple-height.out` controls ripples.
- `emission-mask.out[1]` scales optional emission. `silk-ribbons.range` chooses the
  flow band that emits.

Explicit node seeds keep the pattern reproducible. Edit those seeds for variations.
All generated files belong under the ignored `out/potion/` directory; the sample,
example and this documentation are intended to be checked into Git.
