# Bottle material study

This sample uses the user-supplied `Bottle.obj`, with separate JSON recipes for
`glass`, `cork`, `potion`, `brass`, `gold`, `bubbles` and `emerald`. The model is not
bundled. Generated geometry and maps live in the ignored `out/bottle/` directory.

## Preparation and reproduction

```sh
python3 tools/prepare_bottle_example.py /path/to/Bottle.obj --texutil ./build/texutil
./build/texutil model bake out/bottle/bottle-final-atlas.obj --out out/bottle/bake --size 1024 --samples 96 --threads 8 --distance 0.06 --padding 6
./build/texutil samples/models/bottle/bottle.json --out out/bottle/render --threads 8 --json
./build/texutil samples/models/bottle/inspection.json --out out/bottle/inspection
```

The preparation script is specific to the supplied model's `T3_*` group names.
It preserves the original file, converts its centimeter coordinates to meters,
assigns material slots, removes exact coincident duplicate faces within each
material and creates an xatlas UV copy. It refuses existing prepared outputs.
The supplied asset contained four copies of each face: 23,968 input triangles
became 5,992 unique triangles. The cleaned 2048 packing target produced a valid
atlas with roughly 52% covered texels and no overlaps at the checked resolution.
TexUtil's working-coordinate normalization avoids xatlas dropping tiny faces.

The OBJ's bottle is upside down relative to the usual Y-up presentation, so the
preview rotates it 180 degrees around Z. This is presentation only. All material
maps refer to the exact exported `bottle-final-atlas.obj` UVs.

## Material choices and map use

- Glass: IOR 1.5, subtle scratch normals, handling marks in roughness, sparse dust
  modulated by baked cavities. No painted reflections or fake white streaks.
- Cork: warm natural flakes, matte finish, triplanar projection at 30 repeats per
  meter to avoid dependence on small UV islands.
- Brass and gold accents: material-specific metal reflectance, different finishes,
  and deposits concentrated by baked AO/cavities. Covered areas reduce metalness.
- Green brew: slow color variation and shallow normal detail. Green was inferred
  from the source object's `Potion_Tier3_Green` naming.
- Bubbles and jewel: separate optical parameters and matching green-family colors.

`geometry.json` imports the reusable baked `maps.json`. AO drives the cavity mask;
curvature drives a small roughness reduction on exposed edges through `convex`. Thickness, material IDs,
position, object normals and coverage are preserved in the bake for inspection
and further edits. These geometry maps describe this specific object, not a
seamlessly repeating material tile. Do not connect `object_normal` as tangent normal.

## Preview feedback and limitations

The first draft revealed both the orientation issue and environment-only glass
refraction hiding the liquid. The renderer now uses screen-space refraction for
transmissive materials and separate opaque shader variants for cork and metal.
The final recipe uses an explicit `refraction:opaque` preview override for brew,
bubbles and the jewel: this fast renderer cannot accurately resolve nested
transmissive objects. Their exported MaterialX still contains the original
transmission/IOR settings. The override is reported in render JSON and warnings.
Opaque brew obscures bubbles beneath its surface; use a path-tracing DCC for a
physically faithful final glass/liquid volume render.

### Transmission comparison

`transmission.json` preserves a diagnostic comparison of the opaque override and
actual transmission under identical studio lighting, rotation and exposure. Each
is rendered with both a plain background and the visible HDR environment:

```sh
./build/texutil samples/models/bottle/transmission.json --out out/bottle/transmission --threads 8 --json
```

Tested with the Metal preview: enabling transmission makes the green liquid
largely disappear behind the outer glass. An isolated liquid mesh retains its
green appearance. Showing the HDR background, varying liquid absorption depth
from 0.25 to 4, and trying thin-walled glass did not resolve the combined result.
A further liquid test used white base color, full transmission and zero emission;
the inner liquid was still lost behind the glass. The original material recipes
remain unchanged. These are diagnostics, not a corrected optical preview or an
Arnold comparison under matched settings.

The current screen-space refraction path does not correctly compose this bottle's
nested transmissive surfaces. Absorption/thickness tuning alone is insufficient;
a different refraction path is needed before these previews can validate the liquid inside
the glass. The isolated tests automatically frame the smaller mesh, so their
apparent size and normalized thickness are not directly comparable to the bottle.

### Ordering and hybrid refraction study

```sh
./build/texutil samples/models/bottle/ordering.json --out out/bottle/order-study/render --threads 8 --json
./build/texutil samples/models/bottle/ordering-sheet.json --out out/bottle/order-study
./build/texutil samples/models/bottle/hybrid.json --out out/bottle/hybrid --threads 8 --json
```

The nine ordering experiments keep lighting and optical material values fixed.
Both radial orders, explicit liquid-first/glass-first priorities, a later glass
channel and back-face culling leave the screen-space liquid missing. Filament
1.76.1 captures its refraction input once before the refractive draws, so merely
reordering those draws cannot put the liquid into that captured image.

The **hybrid** retains transmission on brew, bubbles and jewel using environment
cubemap refraction; the glass then uses screen-space refraction of that rendered
scene. This keeps the liquid visible and darker than the opaque baseline. The
hybrid recipe uses center-out ordering and back-face culling, and emits studio
angles plus an outdoor view. The previous opaque recipe remains available as a
baseline. No optical inputs or MaterialX exports are changed by these controls.

This is a useful approximate preview, not a faithful nested-dielectric solution:
the liquid refracts the environment, not neighboring geometry, bubbles or the back
of the bottle. Refraction thickness remains an artist-supplied approximation in
normalized preview units. `center_out` uses the shared bounds center and maximum
vertex radius of each material part; it does not infer actual containment or
preserve a DCC's dielectric priorities. See [preview controls](../../../docs/MODELS.md).

Studio and outdoor HDR previews use the same material recipes. Each output writes
its selected graph textures and `material.mtlx` beneath
`out/bottle/render/preview-materials/<preview-name>.assets/<slot>/`.
The render statistics list source graphs, slot names, projection, approximation
warnings and exported asset files. Copy each material directory with its PNGs.
The original OBJ has no material assignments; use the prepared OBJ's named slots
when assigning these materials in Maya or another DCC.
