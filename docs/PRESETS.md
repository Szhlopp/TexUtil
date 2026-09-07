# Preset nodes

A preset is a compact recipe that produces a scalar texture. It expands into normal
nodes before execution, so it works with every filter, erosion mode, ramp and normal
conversion. No separate material engine or downloaded texture assets are involved.

```json
{"op":"preset", "name":"brick", "config":{"scale":1.5, "detail":0.7, "seed":42}}
```

Run `texutil presets` or `texutil presets --json` for available names.
`texutil describe preset` includes the list and config contract. The preset name and
all config keys/values are checked even if the node is unreachable.

| Config | Default | Bounds | Meaning |
| --- | --- | --- | --- |
| `scale` | 1 | 0.25..4 | Pattern density/frequency multiplier; larger gives smaller features. |
| `detail` | 0.5 | 0..1 | Recipe-specific roughness, variation, density or distortion. |
| `angle` | 0 | -36000..36000 | Clockwise orientation in degrees. |
| `seed` | document seed | signed 32-bit integer | Override random inputs used by this recipe. |

The document's `tile` controls recipe noise. Stamp layouts wrap. Arbitrarily rotating
a brick, weave, cell or spot raster can break seamless edges even with `tile:true`;
leave angle zero for those recipes when tiling matters. Noise-domain rotation uses
the periodic 4D sampling described in [ADVANCED.md](ADVANCED.md).
Some filter radii use pixels, so changing output resolution can change their look.
Config intentionally stays small; edit a graph directly for detailed control.

| Name | Recipe / intended use |
| --- | --- |
| `bnw_spots` | Random circular stamps with size/value variation. |
| `gaussian_spots` | Random Gaussian bells, useful for soft masks. |
| `dirt` | Multiscale noise and scattered spots. |
| `grunge` | Multiplicative coarse/fine noise, general surface breakup. |
| `grunge_rust` | Selected ranges of dirt noise and spots for corrosion masks. |
| `grunge_leaks` | Long anisotropic streaks modulated by coarse patches. |
| `scratches` | Thin capsule stamps with directional variation. |
| `fibers` | Anisotropic fractal noise bent by a low-frequency field. |
| `fur` | More strongly bent fibrous noise. |
| `shavings` | Scattered elongated thin rings. |
| `brick` | Offset rows of beveled box stamps with grain and per-brick values. |
| `weave` | Orthogonal periodic yarn profiles interleaved by a checker mask. |
| `ornament` | Radial petal motifs repeated on a grid. |
| `creased` | Ridged fractal noise with power remapping. |
| `crystal` | Cell values modulated by cellular distance features. |
| `plasma` | Multiscale domain-warped noise. |
| `fluid` | Warped noise followed along a slope field. |
| `liquid` | Soft contour bands through warped noise. |

These are TexUtil's procedural interpretations, not pixel-identical copies of any
commercial preset library. Named variants can be added by composing the same nodes.
The full preset gallery is `examples/presets-gallery.json`, which renders all 18.

```sh
./build/texutil examples/presets-gallery.json --out out/presets-gallery
python3 tools/gallery.py
```

The second command also generates labeled sheets for new primitives and erosion.
Generated images are ignored by Git; recipes and the gallery script are tracked.
Expanded node names begin with `__preset_` and avoid existing graph names. Execution
statistics report the expanded primitives individually. The 4096-node graph limit
applies after expansion as well as before it.
