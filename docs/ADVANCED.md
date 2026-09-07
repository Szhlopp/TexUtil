# Advanced nodes

All defaults and limits are in [NODES.md](NODES.md) and `texutil describe OP`.
`examples/nodes-gallery.json` exports 36 demonstrations; run
`texutil examples/nodes-gallery.json --out out/nodes-gallery` or `python3 tools/gallery.py`.

## Sampling and noise

`array` now accepts `row_offset`, `sources`, `mask`, `direction`, `scale_map`,
`value_map`, `size_jitter`, and `value_range`. `scatter` shares these controls except
for grid count, row offset and grid jitter. Its `count` is random candidate attempts,
so density masks can produce fewer stamps. Placement randomness is seeded and
independent of worker count. Maps are sampled at each candidate's center.

```json
{"op":"scatter", "input":"chip", "sources":["scratch","fleck"],
 "mask":"dirt_mask", "count":1000, "size":[0.04,0.02],
 "size_jitter":0.5, "rotation_jitter":180, "value_range":[0.3,1], "wrap":true}
```

The primary `input` and each additional source have equal selection probability;
repeated names can weight selection. Density is a probability, direction adds
360 × clamped map value in degrees, scale multiplies stamp size, and value
multiplies RGB/scalar values. Black scale skips a stamp. Value does not change alpha;
use `opacity` for transparency. A missing map supplies its neutral value.
`row_offset:0.5` offsets odd rows by half a cell, useful for brick layouts.
Wrapping requires size × (1 + size_jitter) <= 1 UV on both axes.
Mixed scalar/color sources promote to color; source alpha remains meaningful.

All six FastNoise2 generators accept `stretch:[x,y]` and `angle`. A larger stretch
makes features longer along that domain axis. Nonperiodic generation applies the
inverse rotation/scale to sample positions. Periodic generation rotates and scales
the embedded 4D torus domain; it remains periodic, but is not identical to rotating
a flat image. Use `tile:false` for literal planar orientation when seamless edges
are not required. White noise remains uncorrelated even when its domain stretches.
Combined frequency, including stretch and all octaves, must not exceed 10 million.

`gaussian_noise` is independent normally distributed pixel noise using Box-Muller,
with explicit mean and standard deviation, then clipped to 0..1. Clipping changes
the distribution near the limits. `blue_noise` produces a periodic best-candidate
point mask with a minimum-spacing tendency. It is **not** a ranked blue-noise dither
texture or a guaranteed Poisson-disk sampler. Increase `candidates` for more even
spacing; reduce `count` for faster generation. Dot radius uses pixels.

## Shapes and distances

`shape.type` adds `polygon` (`sides`), `star` (`sides`, `inner_radius`), `capsule`,
and `gaussian` (`sigma`). Use a thin capsule size for a line stamp. Gaussian sigma
is relative to shape half-size, and the bell is cropped at the shape's outer radius.
Polygon/star masks use nearest-edge distances and an inside/outside test.
All types support rotation, size, feather and optional color.

`distance` thresholds luminance and measures exact Euclidean distance to pixel
centers of the target class. `side:inside` measures foreground distance to background;
`outside` does the reverse; `signed` maps interior positive distances above 0.5 and
exterior distances below 0.5. `radius` is the pixel distance that saturates the range.
This raster convention gives an inside edge pixel distance 1, not 0 or 0.5.

- `repeat`: nearest site can wrap around either axis.
- `clamp`: only sites in the image count; a missing target class saturates distance.
- `transparent`: adds background pixel centers immediately outside the image.

`bevel` uses inside distance with `linear`, `smooth`, or quarter-circle `round`
profiles, multiplied by `height`. Feed its result into `normal` or a color ramp.

The distance implementation uses the separable parabola-envelope approach described
by [Felzenszwalb and Huttenlocher](https://cs.brown.edu/people/pfelzens/dt/).
It is independently implemented here; no source code from their download is included.

## Filtering and remapping

| Node | Behavior |
| --- | --- |
| `gaussian_blur` | True discrete Gaussian kernel, normalized and truncated at ceil(3σ), separable. |
| `directional_blur` | Uniform line taps centered on the pixel; length in pixels, clockwise angle. |
| `slope_blur` | Walk downhill along a slope map's normalized gradient; average, min or max source samples. |
| `auto_levels` | Map observed RGB extrema to an output range; optional independent channel ranges. |
| `range_mask` | Select a luminance interval; softness feathers outward from the two bounds. |
| `math` | Add, subtract, multiply, divide, min, max, pow, abs, clamp, fract or quantize. |

Gaussian, line and average slope blurs use premultiplied alpha. Slope min/max modes
reduce RGBA components separately; use scalar inputs for morphological height/mask
work. Slope `strength` is total travel in pixels, split across `samples` steps.
Flat slopes stop motion. More samples give smoother paths and cost more CPU time.
For directional blur, use enough taps for the line length to avoid visible gaps.

Auto levels preserves alpha and includes RGB values of transparent pixels when
measuring extrema. A constant channel maps to the lower output bound.
Math preserves the first input's alpha; `b` is an optional second node, otherwise
`value` is used. Arithmetic can produce heights outside 0..1, and PNG clips these
at export. PFM preserves float values. `fract(x)` is x-floor(x), including negative x.
Quantization clamps to `range` and includes both endpoints in `steps` levels.
A zero-width quantization range returns its sole value. Divide by |b| < 1e-8 returns
zero. Power clamps negative bases to zero and maps zero raised to negative powers
to zero. Nonfinite math results fail with a node-specific error.
