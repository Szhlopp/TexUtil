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

Studio and outdoor HDR previews use the same material recipes. Each output writes
its selected graph textures and `material.mtlx` beneath
`out/bottle/render/preview-materials/<preview-name>.assets/<slot>/`.
The render statistics list source graphs, slot names, projection, approximation
warnings and exported asset files. Copy each material directory with its PNGs.
The original OBJ has no material assignments; use the prepared OBJ's named slots
when assigning these materials in Maya or another DCC.
