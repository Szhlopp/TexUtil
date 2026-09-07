# TexUtil

Generate textures from small JSON graphs. C++17, CPU SIMD noise, parallel image
operations, floating-point intermediates, and a command line designed for agents.

## Build on macOS

Apple Command Line Tools, CMake, Git, Ninja and libpng are needed. On this machine
they are already installed. On a fresh Mac:

```sh
xcode-select --install
brew install cmake ninja libpng
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

The first configure downloads pinned FastNoise2 1.1.1 and nlohmann/json 3.12.0.
FastNoise2 also fetches a pinned FastSIMD revision. Subsequent builds use the local
build cache. No GUI or GPU is required. CImg is unnecessary for this implementation.
On Linux, install a C++17 compiler, CMake, Ninja, Git, pkg-config and libpng development
headers using your distribution's package manager. Only macOS ARM64 has been tested.

## Run

```sh
./build/texutil examples/stone.json --out out/stone
./build/texutil examples/ornament.json --out out/ornament --size 1024
./build/texutil examples/tiles.json --out out/tiles --stats
./build/texutil examples/wood.json --out out/wood
./build/texutil validate examples/stone.json
./build/texutil nodes
./build/texutil describe radial
```

`render` is optional: `texutil render material.json` and `texutil material.json` are
identical. `--help` prints usage, `--version` prints the version. Pass `-` instead of
a filename to read stdin. Image input paths are relative to the document, or the
current working directory for stdin. Outputs are relative to `--out` (default `.`).
Existing output files are overwritten. Parent directories are created automatically.
Each file is written independently; a failure can leave earlier outputs or a partial
file. Output paths that would overwrite an imported image are rejected.

| Option | Meaning |
| --- | --- |
| `--out DIR` | Output directory. |
| `--size N` or `--size WxH` | Override document size, for example `2048` or `1024x512`. |
| `--seed N` | Override document seed; explicit per-node seeds still win. |
| `--threads N` | 1..256 workers; default is CPU concurrency capped at 16. |
| `--memory MB` | Float-buffer budget, default 1024 MiB. |
| `--stats` | Per-node milliseconds on stderr. |
| `--json` | Machine-readable result on stdout; supported by render, validate, nodes. |

Successful commands exit 0; invalid input, rendering and I/O failures exit 1 with a
message on stderr. Render normally prints generated paths on stdout and a timing
summary on stderr. `describe OP` always emits JSON. CLI options use separate tokens,
not `--option=value`. Node settings stay in the graph.

## Small JSON graph

```json
{
  "size": 512,
  "seed": 42,
  "tile": true,
  "nodes": {
    "cells": {"op": "voronoi", "scale": 8},
    "clouds": {"op": "clouds", "scale": 4},
    "height": {"op": "blend", "a": "cells", "b": "clouds", "mode": "multiply", "opacity": 0.8},
    "normal": {"op": "normal", "input": "height", "strength": 0.03, "convention": "directx"}
  },
  "outputs": {
    "height.png": {"node": "height", "bits": 16},
    "normal.png": "normal"
  }
}
```

A node is a named object with an `op` and its parameters. Inputs reference other
node names. Names and object order do not control execution. There are no connection
objects, IDs separate from names, editor positions, or nested parameter wrappers.
A shared node is computed once even when several nodes/outputs reference it.

Document fields:

| Field | Default | Meaning |
| --- | --- | --- |
| `version` | `1` | Document format version. |
| `size` | `512` | Square size or `[width,height]`; dimensions 1..16384. |
| `seed` | `42` | Signed 32-bit seed. |
| `tile` | `false` | Default periodic sampling for noise nodes. |
| `background` | `"#000000"` | Opaque color used when flattening color outputs. |
| `alpha` | `false` | Preserve output alpha; requires PNG. |
| `format` | `"png"` | Encoder for output names with no extension. |
| `bits` | `8` (`32` for PFM default format) | Default integer output depth. |
| `srgb` | `true` | Encode color outputs as sRGB; data outputs ignore this. |
| `nodes` | required | Named node objects, maximum 4096. |
| `outputs` | required | Filenames mapped to node names or output settings, maximum 1024. |

Output settings can override `node`, `format`, `bits`, `alpha`, `srgb`. A filename
extension selects its encoder; an explicit format must agree. Extensionless names
get the format appended. Paths must be relative and cannot contain `..`.

Supported exports: PNG (8/16 bit, scalar/RGB/alpha), PPM (8/16 RGB), PGM (8/16 gray),
and PFM (32-bit float, linear gray/RGB, no alpha). PFM keeps out-of-range values;
integer outputs clamp to 0..1 at export. PNG compression level is fixed at 3 for
speed. PNG is the only import format in v0.1. 16-bit PNG imports preserve precision.

## Coordinates, data and color

Coordinates use UV units: `[0,0]` is top-left, `[1,1]` bottom-right. Samples are at
pixel centers. Positive angles rotate clockwise. Width and height are independent
UV axes, so a UV circle becomes an ellipse in a rectangular image. `scale` on noise
means approximate frequency across the image; larger values give smaller features.
Transform scale enlarges the source. Stamp size is its full footprint in UV units.
Blur radius is in pixels. Normal strength is height per unit UV, independent of
resolution, so small values such as 0.02..0.08 are useful for textured surfaces.

Scalar fields use one float per pixel. Color and normal/packed data use four.
Scalar-to-RGB promotion repeats the scalar in RGB with alpha 1. Operators needing
one number use linear Rec.709 luminance. Grayscale and ramp discard source alpha;
ramps can introduce their own alpha through stop colors.

Hex colors (`#RRGGBB` / `#RRGGBBAA`) are interpreted as sRGB and converted to linear
RGB. Numeric colors (`[r,g,b]` or `[r,g,b,a]`) are already linear; a number creates a
scalar. Blends and ramps work in linear RGB. Alpha is always linear coverage.
Image nodes decode color PNGs as sRGB by default, regardless of embedded profiles;
set `srgb:false` for already-linear pixels. ICC/profile conversion is not supported.
Normal, height, mask and packed data bypass sRGB conversion. Normal/packed images
are tagged internally as data; downstream generic transforms do not rotate or
renormalize vectors. Generate normals after changing a heightfield's geometry.

For opaque inputs, multiply at 80% is `a + 0.8 * (a*b - a)`. `mask` multiplies the
opacity. Color blend modes use source-over alpha compositing with the mode applied
to overlapping colors; `mix` interpolates premultiplied RGBA. Scalar modes interpolate
between `a` and the mode result. `over` and `mix` are equivalent for scalar inputs.
Filtering uses premultiplied alpha for color to avoid black fringes.

Normals use central height differences in UV units. Red encodes `-dH/du`; DirectX
green encodes `+dH/dv` where image v increases downward. OpenGL flips green. Blue is
positive Z. Flat height produces `(0.5,0.5,1)`. Verify the convention against your
engine's importer if it flips the green channel itself.

## Tiling and placement

Noise `tile:true` maps coordinates onto a 4D torus and samples FastNoise2 in SIMD
batches across workers; this is periodic generation,
not blending opposite borders. Tileable noise differs from non-tileable noise with
the same seed. Adjacent first/last pixel values need not be identical: their centers
are one pixel apart across the seam. Spatial operations support `repeat`, `clamp`,
and `transparent` sampling. These edge rules do not make nonperiodic inputs seamless.

`array` places source stamps on a grid; `radial` places them around a circle; `stamp`
accepts explicit positions with per-placement size, angle and opacity overrides.
They start with zero for scalar fields or transparency for color. Use a separate
blend to place them over a background. Default overlap mode is `max`; `over` is useful
for colored stamps. `wrap:true` wraps footprints across the output edges. Rotated
wrapped stamps may overlap their own copies. Arrays do not automatically resize
stamps to fit their cells; set `size` explicitly.

```json
{
  "nodes": {
    "dot": {"op": "shape", "type": "circle", "softness": 0.2},
    "ring": {"op": "radial", "input": "dot", "count": 16, "radius": 0.35, "size": 0.08},
    "color": {"op": "ramp", "input": "ring", "stops": [[0,"#141820"],[0.4,"#735231"],[1,"#ffe5a0"]]}
  },
  "outputs": {"ornament.png":"color"}
}
```

See [the node reference](docs/NODES.md) for every field, default and allowed value,
and [the execution plan](docs/PLAN.md) for architectural boundaries. The executable
currently supports 40 operations and 18 built-in preset recipes. [Validation and benchmarks](docs/VALIDATION.md)
record correctness checks, measured timings and visual review.

## Directional warp and wood

`directional_warp` bends a source along a chosen angle, optionally driven by a B/W
image or selected channel. `stripes` supplies antialiased periodic lines. The
[wood recipe](docs/WOOD.md) explains how these combine with shapes, stamps and noise
to create knotted grain. `examples/wood.json` exports color, height, normal and
roughness maps.

## Presets and erosion

```json
{
  "size": 512,
  "tile": true,
  "nodes": {
    "base": {"op": "preset", "name": "brick", "config": {"scale": 1, "detail": 0.7, "seed": 42}},
    "worn": {"op": "erode", "input": "base", "mode": "time", "iterations": 32},
    "normal": {"op": "normal", "input": "worn", "strength": 0.02}
  },
  "outputs": {"height.png": {"node": "worn", "bits": 16}, "normal.png": "normal"}
}
```

`texutil presets` lists recipes; `texutil presets --json` returns a JSON array.
[Presets](docs/PRESETS.md) describes each recipe and its `config` object.
[Advanced nodes](docs/ADVANCED.md) covers distance/bevel, filters, math,
noise-domain anisotropy and sampling maps. [Erosion](docs/EROSION.md) explains wind,
rain and time controls, conservation, boundary behavior and simulation limits.

Render all new features, all presets and an erosion comparison with:

```sh
python3 tools/gallery.py
```

This optional Pillow helper saves individual PNGs and labeled `gallery.png` sheets
under `out/nodes-gallery/`, `out/presets-gallery/`, and `out/erosion/`. Erosion also
exports unclipped float PFM heights. Every graph is checked in under `examples/`;
the C++ tool can render them directly without Python.

## Performance and limits

Only reachable nodes execute. The evaluator uses a topological schedule, keeps each
result immutable, exports it when available, and releases buffers after their last
consumer. Noise and its fractal run together inside FastNoise2 SIMD; the whole JSON
graph is not fused. Noise and image operations use a persistent row worker pool.
Export is serial. Stamps use bounds and row
bands rather than searching all stamps at each pixel. Box blur uses sliding sums,
so per-pixel work does not grow with radius (initial row/column sums still do).

The memory budget tracks live float images, including blur intermediates. PNG decode
also checks temporary raw storage against available budget. It is not a hard process
RSS cap: codecs, thread stacks, row buffers and JSON have additional overhead.
Graphs exceeding the image-buffer budget fail with a useful error. Stamps/arrays
are limited to 10000 placements and 4 million row-band references. Noise supports
up to 12 octaves, with effective frequency including stretch capped at 10 million.
Gaussian blur costs O(pixels × sigma); line/slope blur costs O(pixels × samples).
Distance/bevel use linear-time separable distance transforms. Erosion costs
O(pixels × iterations), up to 512 steps; all full-image working buffers count
toward the memory limit. Blue-noise point selection costs O(candidates × count²). Very large stamp footprints/counts can still be expensive.

Validation checks fields, ranges, references, cycles (including unused nodes),
output settings and input file existence. PNG contents and actual memory needs are
checked at render time. Deterministic thread-count behavior is tested; exact output
identity across platforms/compiler versions is not guaranteed. FastNoise2 strict
floating-point mode is enabled.

## Dependencies

- FastNoise2 1.1.1: MIT, fetched by immutable commit.
- FastSIMD: MIT, revision pinned by FastNoise2.
- nlohmann/json 3.12.0: MIT, archive verified by SHA-256.
- libpng: platform library; libpng license, with zlib dependency.

Their license texts remain in the fetched sources or installed packages. See
[third-party notices](docs/THIRD_PARTY.md). TexUtil itself uses the [MIT license](LICENSE).
