# Glow, stroke, edges and polar effects

The authoritative options are available with `texutil describe NODE` and in
[NODES.md](NODES.md). Render all five new operations with:

```sh
./build/texutil samples/effects-gallery.json --out out/effects-gallery
python3 tools/gallery.py --only effects-gallery
```

## Glow

```json
{"op":"glow","input":"logo","channel":"alpha","radius":12,
 "strength":3,"color":"#169aff","mode":"outer","include_source":true}
```

`radius` is Gaussian sigma in pixels, using a normalized kernel truncated at 3σ.
The selected channel is clamped to 0..1, then `threshold` is subtracted with a floor
of zero. That emission mask is blurred. Outer glow multiplies the blur by one minus
the source control; inner glow uses emission × (1 - blur); both uses the full blur.
`strength` scales the result. A color tint is optional and defaults to white.

An untinted isolated glow is scalar. Tinted glows, or glows that include a color
source, produce RGBA. Color glow adds premultiplied RGB in linear light and combines
alpha coverage. Bright glow values can exceed 1; PNG clips them, while PFM retains
float range. For opaque black/white masks use the default `channel:luminance`.
For transparent shapes use `channel:alpha`, including black-colored silhouettes.
There is no automatic preservation of the source hue in the halo; supply `color`.

## Stroke

```json
{"op":"stroke","input":"mask","width":6,"position":"center",
 "color":"#ffb947","include_source":true}
```

Threshold the selected channel (`threshold:0.5` by default), then draw an `inside`,
`outside`, or `center` outline. Width is total pixels, split equally between both
sides for a centered stroke. Pixel-center Euclidean distances are offset by half a
pixel to estimate the boundary. The edge has built-in one-pixel antialiasing;
`softness` adds an outward fade. There are no vector paths or configurable joins:
these are raster silhouette strokes, with rounded outer distance contours.

Without `color`, output is a scalar outline. With color it is a transparent tinted
outline. `include_source:true` composites the outline over a color source, or takes
the maximum with a scalar source. Width zero produces no stroke. Uniform masks with
repeat/clamp boundaries have no outline; transparent boundaries supply background
outside the canvas and can outline a silhouette touching the image border.

## Edge detection

```json
{"op":"edge_detect","input":"height","method":"sobel","strength":2}
```

Methods are `sobel`, `scharr`, and `laplacian`. Sobel/Scharr output gradient magnitude
normalized so an axis-aligned unit step has response 1. Laplacian is the absolute
four-neighbor Laplacian. `strength` scales the result; `clamp:false` preserves
responses above 1. Output is scalar. Available channels are luminance, alpha, r, g,
and b. This detects local image variation, while stroke follows a binary silhouette.

## Swirl

```json
{"op":"swirl","input":"weave","center":[1,0.5],"radius":0.42,
 "angle":320,"falloff":2,"wrap":true,"edge":"repeat"}
```

Positive angles visibly twist clockwise, negative angles counterclockwise. The
maximum angle is at the center and fades to zero at the radius as
`(1 - radius_fraction)^falloff`. Radius is relative to the shorter image dimension,
so the effect is circular in pixels on rectangular images. Pixels beyond the
radius remain unchanged. An optional scalar `mask` multiplies the angle.

`wrap:true` repeats the center across boundaries. This requires `radius <= 0.5`
and `edge:repeat`, which lets the effect return to identity before the nearest
periodic center changes. With a seamless source and mask, it preserves seamlessness.
Without wrap, an off-center swirl crossing a border can introduce a seam.
Zero angle and black masks are exact identities.

## Polar coordinate conversion

```json
{"op":"polar","input":"stripes","mode":"from_polar","radius":0.45}
```

- `from_polar`: source X is angle and source Y is radius; wrap the strip into a disk.
- `to_polar`: unwrap a Cartesian disk into an angle/radius strip.

At `angle:0`, angular zero points right and increases clockwise. Center is in UV
coordinates; radius uses the shorter image dimension. Outside the generated disk
is black for scalar data and transparent for color, with one-pixel edge coverage.
The angular strip coordinate always wraps, including bilinear interpolation across
its seam. `edge` controls radial strip sampling or Cartesian source sampling;
its default is clamp for this node.

Polar transforms resample and cannot promise an exact round trip. The disk center
is a coordinate singularity, so dense angular patterns can alias near it. Polar
conversion changes topology and does not automatically produce a seamlessly tiling
rectangle. Swirl is the appropriate local effect for twisting an existing tile.

Glow, stroke and edge detection support repeat/clamp/transparent boundaries.
Repeat samples across the tile seam; it preserves a seamless input without repairing
an arbitrary nonperiodic source. RGBA gallery previews composite against a dark
background so transparent halos and outlines are displayed correctly.
