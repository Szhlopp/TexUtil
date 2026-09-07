# Heightfield erosion

`erode` interprets scalar input as height, or converts RGB to linear luminance.
The result is a scalar field. Alpha does not contribute. Input heights must be
finite and within 0..10000; typical inputs use 0..1. Negative heights are rejected,
so use `levels` or `math` to shift signed terrain first.

```json
{"op":"erode", "input":"height", "mode":"rain", "iterations":120,
 "rate":0.55, "rainfall":0.025, "capacity":12, "seed":42, "edge":"repeat"}
```

Modes are artistic heightfield simulations, not calibrated geology, fluid dynamics
or real elapsed years. They operate at pixel scale: rendering at another resolution
can change the erosion footprint. Start around 256 or 512 pixels and tune there.
No automatic normalization occurs. This preserves material accounting and lets
intermediate fields exceed white when sediment piles up. PNG/PGM clip outside 0..1;
use PFM to preserve unclipped float heights, or explicitly remap before display.

## Shared controls

| Control | Default | Meaning |
| --- | --- | --- |
| `mode` | `time` | `wind`, `rain`, or `time`. |
| `iterations` | 32 | 0..512 synchronous simulation steps. |
| `rate` | 0.3 | 0..1 removal/relaxation fraction. |
| `mask` | omitted | Optional clamped luminance mobility map. |
| `edge` | `repeat` | Periodic domain, or `clamp` for closed borders. |

Both edge modes retain material. Closed borders are walls, not a drain. A periodic
rain surface retains and recirculates its water until evaporation. No external
sediment is added. Heights sum to their original total within floating-point error.
Rain temporarily stores some material as suspended sediment, then deposits all
remaining sediment before returning the final heightfield.

Zero iterations or zero rate returns the grayscale input without erosion. A black
mask prevents erosion everywhere. For time, mobility is symmetric across each
neighbor pair. For rain and wind, protected pixels can still receive deposition
from elsewhere; a mask is not a wall or a final output blend.

## Time: thermal relaxation

Material moves from higher pixels to their four lower neighbors when the height
drop exceeds `talus` (default 0.005 height units per pixel). Pairwise flux is
`rate * 0.25 * max(abs(height_difference) - talus, 0)` multiplied by the lesser
mobility of the pair. This conserves material and relaxes steep slopes.

Low talus smooths small features; higher talus retains ledges. More iterations
spread the relaxation farther. This is a thermal/weathering approximation, not an
explicit calendar-time model. The `seed` has no effect.

## Wind: directional entrainment and deposition

The wind compares each pixel to a bilinear sample one pixel upwind. Exposed material
above the upwind height plus `talus` is removed at `rate * 0.25`, limited by available
height and mobility. It is deposited `distance` pixels downwind with bilinear weights.

- `angle`: clockwise direction, 0 right, 90 down; default 0.
- `distance`: transport distance in pixels, default 2; zero gives no change.
- `talus`: exposure threshold, default 0.005.

Repeated steps can form drifting ridges and accumulate piles above height 1.
There is no air-flow solver, fetch-length model or grain-size distribution. Seed
has no effect. Deposition uses a deterministic serial accumulation order; extraction
is parallelized. This avoids races at pixels that receive multiple contributions.

## Rain: water and sediment transport

Each step adds water with seeded pixel variation, computes four-neighbor flow from
surface height (terrain + water), and limits outgoing flux to available water.
Sediment capacity depends on outgoing flow and downhill terrain drops. Underloaded
water erodes; overloaded water deposits. Removal per step is additionally capped
at one quarter of the greatest local downhill height drop to limit new pits.
Water carries suspended material along the same fluxes. Evaporation deposits a
fraction of suspended sediment locally.

| Control | Default | Meaning |
| --- | --- | --- |
| `rainfall` | 0.01 | Water height added each step, multiplied by seeded 0.5..1.5 variation. |
| `capacity` | 8 | Sediment carrying coefficient. Higher generally removes more material. |
| `deposition` | 0.3 | Fraction of sediment above capacity deposited each step. |
| `evaporation` | 0.1 | Fraction of water evaporated, and sediment deposited, per step. |
| `seed` | document seed | Reproducible rainfall pattern. |

Zero rainfall or zero capacity produces no change. `talus`, `angle` and `distance`
are unused in rain mode. Rain may look subtle with small slopes or low rainfall;
use more steps or higher capacity to increase weathering. This local model primarily
wears peaks and redistributes sediment into lower regions. It does not guarantee
branching river networks, overhangs, undercutting, splashes or natural-looking gullies
on every input. Four-neighbor flow can introduce grid bias.

## Reproduce the comparison

```sh
./build/texutil examples/erosion.json --out out/erosion --threads 8 --stats
python3 tools/gallery.py
```

The JSON saves before/wind/rain/time heights in both 16-bit PNG and float PFM,
plus normal and color maps. The sheet in `out/erosion/gallery.png` uses the same
height display range, normal strength and color ramp for all four columns, with
correct 16-bit-to-preview conversion. Strong accumulations can still clip in the
preview. Normal/color maps are illustrative; there is no 3D terrain renderer.

Tests cover material conservation, nonnegative finite output, constant fields,
black masks, zero steps, 1x1/2x2 images, both boundaries, wind direction, rain seed
variation, thermal smoothing, and exact results across one and four workers.
