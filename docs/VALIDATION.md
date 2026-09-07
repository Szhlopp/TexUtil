# Validation and performance

Measured on 2026-09-07 on Apple Silicon (arm64), macOS 26.4.1.

C++17 Release, Apple Clang 17, FastNoise2 strict floating point enabled.
Times are medians of three complete renders, including PNG encoding and writes.
Process startup and JSON parsing are outside the timer. These are local measurements,
not hardware-independent performance guarantees.

| Example | Size | Workers | Total ms | Export ms | Peak float buffers MiB |
| --- | --- | --- | --- | --- | --- |
| stone | 512 | 1 | 99.4 | 52.3 | 6.0 |
| stone | 512 | 8 | 59.9 | 52.3 | 6.0 |
| stone | 2048 | 1 | 1460.8 | 709.6 | 96.0 |
| stone | 2048 | 8 | 807.3 | 696.1 | 96.0 |
| ornament | 512 | 1 | 38.2 | 16.5 | 5.0 |
| ornament | 512 | 8 | 20.1 | 16.2 | 5.0 |
| ornament | 2048 | 1 | 589.7 | 247.1 | 80.0 |
| ornament | 2048 | 8 | 316.5 | 258.5 | 80.0 |
| tiles | 512 | 1 | 70.2 | 40.0 | 5.0 |
| tiles | 512 | 8 | 44.1 | 39.1 | 5.0 |
| tiles | 2048 | 1 | 829.4 | 358.0 | 80.0 |
| tiles | 2048 | 8 | 443.3 | 370.0 | 80.0 |

Reproduce with `python3 tools/benchmark.py`. Raw node timings are written to
`out/benchmark.json`; output images are in `out/benchmark/`. Stone writes four
outputs; ornament and tiles write three each. Buffer counts exclude codec storage.
Export is serial, so complete-render speedup is smaller than generation speedup.

## Correctness checks

`ctest --test-dir build --output-on-failure` runs a C++ regression suite, CLI help,
example validation, and end-to-end CLI integration. Coverage includes:

- Analytic blend/mask arithmetic and all ten blend modes.
- Shared-node reuse, unused-node pruning, and memory release on a 30-node chain.
- Memory budget failure, unknown fields/references, cycles and invalid settings.
- Flat normals, analytic slopes, DirectX/OpenGL flip and resolution invariance.
- Linear ramps, endpoint clamping, step interpolation and sRGB conversion.
- Seed variation and exact thread-count determinism for all six noise generators.
- All cellular metrics/outputs, fBm/ridged normalization and periodic seam continuity.
- Tiny images, odd SIMD batch sizes and extreme transform coordinates.
- Identity/repeat/transparent transforms, neutral warp and constant-field blur.
- Premultiplied-alpha blur, grid/radial/stamp locations, wrapping and grid jitter.
- PNG 16-bit precision, RGBA roundtrips, flattening and document-relative imports.
- PPM scalar promotion, PGM/PFM writing, malformed PNGs, input overwrite rejection.
- Packing, grayscale, gradients, shapes, levels and thresholds.

CLI integration independently reads a PNG header to confirm 17x9 dimensions and
16-bit grayscale output. It also checks stdin, overrides, discovery, unknown CLI
options and trailing JSON syntax errors.

## Visual review

Five examples were rendered and inspected: stone, ornament, tiles, stamps, warped.
Their labeled contact sheet is `out/preview.png`. Recreate it after rendering the
examples with `python3 tools/preview.py` (Pillow required for this optional helper).
The runtime executable does not depend on Python or Pillow.

## Sanitizers

A separate RelWithDebInfo build instruments C++ sources and FastNoise2/FastSIMD with
`-fsanitize=address,undefined`. On this Mac, use `UBSAN_OPTIONS=halt_on_error=1`
and `ASAN_OPTIONS=detect_leaks=0`. Apple's runtime does not support LeakSanitizer
here. The final test outcome is recorded below.

Reproduce the sanitizer build:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j 8
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --timeout 120 --output-on-failure
```

### Recorded outcome

All four Release CTest targets pass. The core target contains six groups of numeric,
validation, noise, spatial, format and node-coverage checks. Five example documents
render successfully and their output contact sheet has been visually inspected.

AddressSanitizer could not be completed on this machine. With unsupported leak
detection disabled, the runtime stalled during initialization before application
output, including when invoking only `--help`. The same startup timeout occurred
outside the filesystem sandbox. The hanging runs were stopped. This is an unresolved
sanitizer environment/runtime limitation; no ASan/UBSan pass is claimed. The commands
above are retained so these checks can be rerun on a working sanitizer environment.

## Directional warp and wood addition

All five Release CTest targets pass, including the new `wood_example` render.
The core suite now has seven groups. Added coverage checks black/white/omitted
intensity, midgray neutrality, signed strength, cardinal directions, all five
control-channel modes, color/alpha preservation, thread-count determinism,
missing/invalid controls, and a 16-bit grayscale PNG as the warp control.
Stripes tests cover all four profiles, periodicity, negative phase, orientation,
invalid frequency and subpixel average coverage.

The 1024x1024 wood graph rendered its four maps in 210.7 ms
with eight workers and 28.0 MiB peak float buffers on this machine
(single measured run, including exports). Visually reviewed color, knot contours
and a process strip saved as `out/wood/preview.png`. See [WOOD.md](WOOD.md).

`.gitignore` was checked using Git in a temporary repository: build directories,
compiled objects and `out/` were ignored; source, docs, example JSON and source PNG
assets remained trackable. The project was subsequently initialized as a Git repository and connected to its GitHub remote.

## Advanced nodes, presets and erosion

All nine Release CTest targets pass. The original seven test groups remain, plus
four groups in `advanced_nodes` and render tests for each new gallery document.
New numerical coverage includes:

- Exact distance results against a brute-force oracle for all three boundary and
  side modes, uniform masks, and bevel heights.
- Gaussian impulse kernel/energy/symmetry, directional support, transparent-color
  filtering, flat-slope identity, directional slope transport, arithmetic, range
  masks and flat/ranged auto levels.
- Erosion mass conservation, finite nonnegative heights, black masks, constant
  height, zero steps, tiny periodic/closed domains, downwind motion, rainfall seeds,
  reduced thermal roughness energy and exact worker-count determinism.
- Erosion scratch-buffer memory budget enforcement.
- Sampler row offsets, directional/value/scale maps, source selection, missing
  source validation and randomized footprint limits.
- New shape coverage, Gaussian noise distribution, seeded point masks, anisotropic
  noise determinism and effective-frequency limits.
- Every preset's output, deterministic workers, config extremes, unknown config
  rejection and collision-safe expansion.

Single measured 512x512 runs, eight workers, including exports, 2026-09-07:

| Graph | Exports | Total ms | Export ms | Peak float buffers MiB |
| --- | --- | --- | --- | --- |
| nodes-gallery | 36 | 402.6 | 153.4 | 7.0 |
| presets-gallery | 18 | 197.9 | 99.5 | 4.0 |
| erosion | 16 | 850.8 | 201.1 | 11.0 |

Reproduce using `python3 tools/gallery.py`. Each output directory contains its
machine-readable `stats.json`. There are 36 primitive PNGs, 18 preset PNGs,
12 erosion PNGs, four unclipped erosion PFM files, and three labeled contact sheets.
The sheets were visually inspected. The helper explicitly scales 16-bit grayscale
PNGs to 8-bit previews before creating RGB thumbnails.

For the final erosion example, the largest relative height-sum change across wind,
rain and time was 3.94e-08, measured from the float PFM outputs. Wind piles
can exceed height 1, so the PNG/color previews can clip highlights. Rain is a local
water/sediment approximation and its visual effect depends strongly on parameters;
the final example uses 400 rain steps, 80 wind steps and 160 thermal steps.
[EROSION.md](EROSION.md) documents the simulation models and limitations.

Build products and `out/` were confirmed ignored by Git after adding the new test
executable. Source, tests, example graphs, gallery script and documentation remain
trackable. The sanitizer limitation recorded earlier remains unresolved; these
new changes are validated with the Release suites, not a new sanitizer pass.

## Effects, forest, portable samples and field conversion

All 12 Release CTest targets pass. The advanced test executable now includes six
groups, covering the earlier operations plus effects and connected fields.

- Sobel/Scharr/Laplacian unit-step and flat-field responses.
- Stroke inside/outside/center coverage, zero width, uniform masks and alpha silhouettes.
- Glow impulse energy, outer support, black input, and tinted-alpha preservation.
- Swirl zero-angle/mask identities, finite support and periodic translation behavior.
- Polar coordinate orientation and radial scaling on rectangular images.
- Exact worker-count determinism for effects, connected regions and normal integration.
- Flood-fill diagonal versus edge connectivity, seam crossing, selection, area,
  normalized labels, seeded region values and invalid connectivity.
- Height/normal reconstruction on smooth periodic and clamped fields in both green
  conventions, clamped ramps, chosen mean, tiny images and scalar-input rejection.
- Every portable sample validates; copied examples match their sample copies;
  `samples/terrain.json` also renders in the integration check.

Rendered and inspected `out/effects-gallery/gallery.png` and the stronger cumulative
`out/forest/gallery.png`. The effects gallery has 24 PNGs. The forest exports 13 PNGs
(including unlit albedo) and four float PFM heights; its sheet shows 12 stage maps.
The sample JSON and all documentation are check-in-ready; generated outputs remain
ignored by Git. No full 16384-square render was attempted.

Single measured runs with eight workers, including exports:

| Graph | Size | Total ms | Peak tracked buffers MiB |
| --- | --- | --- | --- |
| effects-gallery | 512 | 427.1 | 17.0 |
| forest | 1024 | 3406.1 | 80.0 |

Maximum relative height-sum change across the cumulative forest erosion stages: 3.20e-08, measured from the float PFM exports.

## Native sheet outputs and snow

All 14 Release CTest targets pass, including a dedicated native-sheet suite and the
snow example. The portable-sample check validates 13 JSON graphs and checks matching
example copies. New tests cover computed canvas dimensions, mixed scalar/normal/color
encoding, independent sheet export defaults, repeated/shared sources, multiple sheets,
sheet-only scheduling, area-filtered reductions, alpha composition, aspect-fit
letterboxing, caption/title bounds, memory release across a ten-node chain, and
invalid references/layouts/text/settings/output paths.

The chain test renders ten 256-square scalar sources into a sheet under a 1 MiB
budget and peaks below 0.6 MiB, demonstrating that sheet capture does not retain
all full-resolution sources. PNG writing is exercised by native gallery and snow
CTest renders. `git diff --check` passes, and generated maps, sheets and the new
sheet-test executable are correctly ignored.

The 2048-square snow material exports color, 16-bit height PNG, 32-bit float height,
DirectX normals, diffuse preview and a native 2x2 sheet. The sheet and a 2x2 repeated
lighting preview were visually inspected. Mean height discontinuity at the wrap
boundary relative to ordinary neighboring pixels was 0.975 in X and 1.028 in Y,
consistent with no unusual seam. This is a statistical seam check and visual review,
not a guarantee that repetition is unrecognizable. The height range was approximately
0.40055..0.60211. Its near-white albedo is intentionally subtle; the normal and
lighting preview carry the visible powder relief.

The measured 2048-square run with eight workers took 1527.1 ms including exports, with 271.3 MiB peak tracked image buffers (single local run).

Five existing gallery graphs now declare native sheet outputs. The Python gallery
helper only invokes the executable and saves timing reports; it no longer performs
image assembly or requires Pillow. The forest gallery was regenerated at 1024 with
the native output as an additional mixed data/color verification.

## MaterialX export

All 15 Release CTest targets pass with the new MaterialX suite. The portable-sample
check now covers 16 graphs. Export tests cover relative paths and XML escaping,
color/data encoding, DirectX green inversion, OpenGL passthrough, displacement
binding, all 42 shader inputs, constant-only materials, output ordering, callback
payloads, and rejection of invalid types, references, collisions and parameters.

The official MaterialX 1.38.10 Python SDK independently validated four test
documents and six regenerated sample materials (stone, wood, snow, cork, glass and
potion). Every texture filename resolved relative to its document, and the SDK
generated GLSL source for each material with its standard color-management system.
This validates shader graphs and shader generation, not GPU compilation or rendering.
No Maya/Arnold import or sphere render has been tested yet.

Reproduce the independent check in an environment with the MaterialX SDK installed:

```sh
python tools/validate_materialx.py build/materialx-test out/stone out/wood out/snow out/cork out/glass out/potion
```

The SDK was installed only in the ignored `build/materialx-venv` for development
verification. It is not linked into TexUtil and is not a runtime requirement.
