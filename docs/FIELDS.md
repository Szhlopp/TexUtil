# Flood fill and height/normal conversion

## Flood fill

`flood_fill` labels connected foreground regions of a thresholded channel. It is
useful for picking islands, varying individual cells, or selecting regions by area.
Use the default luminance channel for grayscale inputs and `channel:alpha` for
transparent silhouettes.

```json
{"op":"flood_fill","input":"mask","mode":"select",
 "point":[0.5,0.5],"threshold":0.5,"connectivity":4,"edge":"repeat"}
```

| Mode | Scalar output |
| --- | --- |
| `select` | White for the foreground component containing `point`, black elsewhere. |
| `random` | One seeded value in 0.1..1 per region; background zero. |
| `labels` | Region ID divided by total region count; background zero. |
| `area` | Region pixel count divided by total image pixel count. |

Connectivity is integer 4 or 8; 8 joins diagonally touching pixels. Repeat boundaries
join components across opposite edges. Clamp treats borders as closed. The seed
point uses UV coordinates, wrapped or clamped according to `edge`. A point in the
background returns an empty selection. Only random mode uses `seed`; labels are
assigned in row-major discovery order. Changing topology or connectivity can change
subsequent IDs and random values. Random values are visual variation, not unique IDs.

These are connected components of a binary threshold, not a color-tolerance paint
bucket. Feed the resulting mask into `blend`, `ramp`, or erosion's `mask` input.
For grayscale interval selection before connectivity, use `range_mask` first.

The traversal is O(pixels), deterministic and serial. Its visited and queue buffers
use uint32 indices and count against `--memory`, in addition to float images.
Normalized label output rejects more than 16,777,216 regions because float storage
cannot retain every larger integer ID exactly. PNG export further quantizes values;
use PFM when preserving scalar precision matters.

## Height to normal

Already available as `normal`:

```json
{"op":"normal","input":"height","strength":0.035,
 "convention":"directx","edge":"repeat"}
```

Central differences estimate slopes in UV units and encode a tangent-space normal
in RGB. `opengl` flips green. Strength is height scale per unit UV, not pixel radius.

## Normal to height

```json
{"op":"normal_to_height","input":"normal_map","strength":0.035,
 "convention":"directx","mean":0.5,"iterations":500,
 "tolerance":0.00001,"edge":"repeat"}
```

Decode the normal's RGB into XYZ, divide X/Y by positive Z to estimate slopes, then
solve a least-squares central-difference integration problem with conjugate gradients.
The forward derivative and its transpose use the same repeat/clamp conventions as
`normal`. Iteration stops at the requested relative residual or the iteration limit;
a limited iteration count can leave an approximate reconstruction. The mean is set
explicitly after integration and no automatic range normalization or clipping occurs.

For normal PNG inputs, specify `kind:normal` on the `image` node so the encoded
components are not decoded as sRGB color. Match the original `strength`, green
convention and edge mode when known. RGB must be finite in 0..1 and decoded Z must
be nonnegative. `min_z` limits the denominator near horizontal normals to avoid
extreme reconstructed slopes. Lower it only when the map needs those steep slopes.

A normal map loses information: absolute elevation and unknown original strength
cannot be inferred. Periodic integration cannot recover a global tilt, and central
differences lose checkerboard/Nyquist modes on even periodic grids. Nonintegrable,
quantized, or edited normal maps yield a least-squares approximation. Alpha is
ignored. Zero iterations returns the chosen flat mean field.

The solver uses float images with double-precision dot-product accumulation. Its
row reductions have a fixed summation order for worker-count determinism. Runtime
is O(pixels × iterations); memory includes the normal input and six scalar images.
At 16384 square, that is about 10 GiB before other live graph inputs and process
overhead. A full-resolution solve may require more iterations than a small image.

Tests reconstruct known smooth heightfields for DirectX/OpenGL and both boundaries,
check clamped ramps and requested mean, and cover tiny images and identical results
across one/four workers. The sample effect gallery includes source height, generated
normal, and reconstructed height alongside the connected-region demonstrations.
