# TexUtil implementation plan

## Product contract

A CPU-first C++17 command line utility. One readable JSON document describes size,
seed, export defaults, named nodes and outputs. `texutil material.json` renders;
`texutil validate material.json` checks without rendering. CLI overrides are limited
to output directory, size, seed, threads and memory budget. Node options live in JSON.

## Execution

1. Expand preset recipes into collision-safe ordinary node names, then validate every
   node, parameter, reference, output and cycle before rendering.
2. Build a topological schedule for nodes reachable from selected outputs.
3. Evaluate each node once into an immutable float image (one channel for scalar
   data, four for color and encoded normals). A persistent worker pool divides rows.
4. Use FastNoise2 SIMD generators and fractals for procedural fields. Spatial
   operations sample cached images with bilinear filtering and explicit edge rules.
5. Export outputs as soon as their source is computed, then release intermediate
   buffers after their final consumer. Track float-buffer memory and node timings.

Graph nodes are separate from FastNoise2's internal graph. The initial engine
combines each noise generator and its fractal inside FastNoise2; it does not promise
whole-document fusion or GPU execution. Strict floating point mode is enabled;
thread-count determinism is tested, cross-platform byte identity is not promised.

## First implementation

- Noise: simplex, Perlin, value, cellular/Voronoi, white; fBm and ridged fractals;
  clouds preset; optional periodic noise sampling.
- Sources: constant, linear/radial/angular gradients, shapes, checker, PNG import.
- Operations: blend and masks, invert, levels, threshold, blur, transform, warp,
  grayscale, color ramp, height-to-normal, channel packing.
- Placement: explicit stamps, rectangular arrays, radial arrays; size, rotation,
  opacity, overlap mode, deterministic grid jitter and optional edge wrapping.
- Outputs: PNG 8/16, PPM/PGM 8/16 and PFM float; color versus data encoding.
- CLI discovery: nodes, describe, validate, machine-readable node catalog, timings.

## Validation and delivery

Test analytic blend and normals, ramp interpolation, graph reuse/liveness, invalid
graphs, seed and threading determinism, transforms/placement, PNG precision and
alpha roundtrips. Render example materials and inspect a visual contact sheet.
Measure representative 512 and 2048 renders. Keep README and node reference current
as features land, including defaults, units, limits and supported formats.

## Deliberate boundaries

No editor, material renderer, mesh baking, physically inferred roughness/AO, plugin
ABI, GPU backend, or arbitrary scripting in v0.1. No CImg dependency is needed for
the implemented operations. Dependency downloads are pinned; libpng is supplied by
the platform. Export file writes are individual, not a transactional multi-file job.

## Implemented outcome

The current implementation has 40 discoverable operations, 18 preset recipes, and
nine example documents including three feature galleries.
The evaluator and exporters follow the design above. Tiled noise is also parallel:
TexUtil constructs periodic 4D coordinates and dispatches row batches through
FastNoise2's SIMD position-array API. Validation, tests and measured performance
are documented in [VALIDATION.md](VALIDATION.md). The generated node catalog is
available as both [Markdown](NODES.md) and [JSON](nodes.json).

## Advanced nodes and heightfield weathering

Implemented as additions to the existing graph contract. Ordinary primitives stay
in `src/nodes.cpp`; advanced image filters and distance transforms live in
`src/advanced.cpp`, simulations in `src/erosion.cpp`, and recipes in
`src/presets.cpp`. `src/advanced_catalog.cpp` extends the same authoritative catalog
used by validation, discovery and generated documentation.

- Samplers reuse bounded stamp rectangles and row-band indexing. Additional source
  references participate in graph validation and lifetime tracking.
- Anisotropic noise changes SIMD sample coordinates before rasterization.
- Euclidean distances use separable lower envelopes of squared-distance parabolas.
  Periodic transforms consider three copies of each row/column, then keep the center.
- Gaussian convolution and path blurs handle color in premultiplied alpha.
- Erosion uses synchronous steps with tracked full-image buffers. Wind deposition
  is serial to keep summation order deterministic; other passes parallelize by row.
- Presets expand once before scheduling. They have no separate image evaluator or
  hidden persistent cache. Timings expose every generated primitive.

Validation adds analytic filter checks, brute-force distance comparisons,
conservation and directional transport checks, seed/thread determinism, tiny
heightfields, sampler controls and renders of every preset. Labeled image galleries
are generated from checked-in JSON recipes and visually reviewed.
