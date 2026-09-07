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
assets remained trackable. No repository was initialized in the project itself.
