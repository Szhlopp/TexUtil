# ExportBake: material textures in model UVs

`type:"export_bake"` samples material recipes onto an existing model's unique UV
atlas. It runs on the CPU without Filament, a GPU, Blender or Python. This is
material baking, separate from `model bake`, which calculates geometry maps such
as AO, curvature and thickness.

## Quick start

The included torus has suitable UVs. Bake cork and wood, then optionally preview
only the resulting UV textures:

```sh
./build/texutil samples/models/export-bake.json --out out/export-bake --threads 8 --json
./build/texutil out/export-bake/manifest.assets/preview.json --out out/export-bake/preview
```

Only the second command requires the optional Filament build. The source mesh is
not modified, copied, automatically unwrapped or displaced. Keep that mesh with
its existing material assignments and apply the exported material for each slot.

For matched original-versus-baked torus renders, run
`./build/texutil samples/models/export-bake-preview.json --out out/export-bake/compare`
after the bake. This uses the same camera and studio lighting for both outputs.

## JSON output

```json
{
  "nodes": {},
  "outputs": {
    "baked/manifest.json": {
      "type": "export_bake",
      "model": "model-uv.obj",
      "size": 2048,
      "padding": 16,
      "normal_convention": "directx",
      "maps": ["color", "roughness", "metalness", "height", "normal"],
      "materials": {
        "glass": {
          "graph": "glass.json",
          "material": "glass.mtlx",
          "projection": "uv",
          "height": "glass-height.png"
        },
        "cork": {
          "graph": "cork.json",
          "projection": "triplanar",
          "projection_scale": 30
        }
      }
    }
  }
}
```

`materials` keys match exact model material names or zero-based `#index` slots.
`material` may instead provide a default binding for all otherwise-unassigned
slots. Every used slot must be covered, and unmatched bindings are rejected.
Bindings are external JSON recipes, optionally selecting one of their MaterialX
output filenames. `"cork":"cork.json"` is shorthand. Selection is automatic only
when the recipe contains one MaterialX output. Local parent MaterialX outputs and
arbitrary `.mtlx` input files are not supported by ExportBake in this version.

All paths resolve relative to their declaring JSON. External recipes retain their
own dimensions, imports and seed. `--size` and `--seed` affect the parent texture
graph, not the bake resolution or external material recipes. Set the bake's `size`
and edit the material recipe's size/seed deliberately.

| Setting | Behavior |
| --- | --- |
| `model` | Required static OBJ, FBX, glTF or GLB with a unique, non-overlapping 0..1 UV atlas. |
| `size` | Square bake resolution, 32..4096, default 1024. |
| `padding` | Border expansion in output texels, 0..64, default 8. |
| `maps` | Requested maps; defaults to all five shown above. |
| `normal_convention` | `directx` (default) or `opengl`. |
| `projection` | Default sampling mode for bindings: `uv` (default) or `triplanar`. |
| `projection_scale` | Triplanar repeats per source mesh unit, 0.0001..10000, default 1. |
| `projection_blend` | Triplanar normal-weight exponent, 1..16, default 4. |

Bindings can override projection controls and supply `height`, `height_scale` and
`height_midlevel`. Preview controls `refraction`, `thickness`, `render_order`,
`render_channel` and `culling` are accepted and carried into the generated preview
recipe, but do not affect baked pixels. Camera, lighting and refraction appearance
are never baked into these material channels.

## Channels and material preservation

| Map | Source | Output |
| --- | --- | --- |
| `color` | `inputs.base_color`, constant or texture | 8-bit sRGB PNG |
| `roughness` | `inputs.specular_roughness` | 16-bit linear PNG |
| `metalness` | `inputs.metalness` | 16-bit linear PNG |
| `height` | Explicit `height` output filename, otherwise displacement texture | 16-bit linear PNG |
| `normal` | Material's tangent-normal helper | 16-bit raw RGB PNG |

Default material values come from TexUtil's Standard Surface catalog. Missing
normal detail gives a flat normal. Missing height gives the midlevel (default
0.5), recorded as `source_present:false` in the manifest. Height cannot be recovered
from a normal map automatically. `height` names a scalar PNG output in the source
recipe, not a node name; it is retained even when not connected to displacement.

Existing displacement scale and midlevel are preserved. Binding `height_scale`
and `height_midlevel` override them. An explicit height texture without a source
displacement connection is exported for inspection/use, but does not create a
new displacement connection unless `height_scale` is explicitly supplied. Its
recorded scale defaults to zero. The baker does not change mesh vertices.

Optical constants such as IOR, transmission, coat and subsurface parameters remain
in each material's MaterialX. Texture-bound additional scalar/color parameters
are reprojected to `extra_<parameter>.png` files, including transmission tint.
These extra files preserve existing material behavior; there are no specialized
coat or subsurface baking models. Direct shader normal/tangent inputs are rejected;
use TexUtil's `normal` helper for tangent-space detail.

`maps` requests files for constant/default channels as well as textured ones.
Texture files required by the exported material are still generated even when
not requested explicitly, so selecting fewer maps does not leave broken bindings.
All integer textures clamp to 0..1, including height and HDR texture values.
Constants retained in MaterialX keep their numeric values. Float texture output
and high-dynamic-range texture preservation are not implemented here.

## Projection, normals and padding

UV sampling uses the model's existing UVs and the source recipe's repeat/clamp
policy. Triplanar sampling follows the preview's object-centered coordinates,
projection scale, signs and axis weights. The shared model bounding-box center is
recorded in the manifest. Individual material parts are not recentered separately.

The baker evaluates source graph images in float precision, then samples them with
bilinear filtering. It does not resize the source graph to the atlas resolution.
Tiny features can alias or disappear if the source/atlas is too small; this first
version has no supersampling or footprint-aware anisotropic filtering. GPU mipmap
filtering means a procedural preview and baked preview will not be pixel-identical.

Normals are decoded, source strength is applied once, and triplanar surface slopes
are blended before conversion into the target UV tangent basis. The output helper
has scale 1. DirectX/OpenGL green-channel conversion occurs once. The CPU basis uses
per-indexed-vertex Lengyel tangents, matching the preview's tangent-generation
method, not MikkTSpace. Mirrored UV orientation is respected. Other renderers may
use different tangent bases; verify normal-map shading in the target application.

Each material receives a separate full-atlas texture set. Padding extends that
material's border colors/normals independently, including into UV space used by
other materials because those use different textures. It does not wrap atlas
edges. Close islands of the same material can still compete for padding, so leave
adequate packing gutters. Padding protects seams at nearby mip levels; it cannot
prevent all bleed at arbitrarily distant mip levels.

## Output package

For output `baked/manifest.json`, files go beneath `baked/manifest.assets/`:

```text
baked/manifest.json
baked/manifest.assets/1/color.png
baked/manifest.assets/1/roughness.png
baked/manifest.assets/1/metalness.png
baked/manifest.assets/1/height.png
baked/manifest.assets/1/normal.png
baked/manifest.assets/1/material.mtlx
baked/manifest.assets/1/material.json
baked/manifest.assets/2/...
baked/manifest.assets/preview.json
```

Numeric folders identify original material slots. The manifest maps slots/names
to recipes and records encodings, height scale/midlevel, UV diagnostics, projection
origin, tangent basis and a conservative peak working-memory estimate. Map filenames
inside each material record resolve beside that material's recipe. The top-level
file list and recipe paths resolve beside the manifest.

Each `material.json` imports its neighboring PNGs and can be used as an external
UV material recipe. `preview.json` assigns them to the original model, retaining
supported per-binding preview overrides. Its model reference is absolute and the
model is not packaged: update it when moving the package. Preserve material-slot
ordering, geometry and UVs. Edit the generated preview's rotation/views if needed.
Copy the `.mtlx` and its neighboring PNGs together for a DCC.

The manifest name and its asset directory are reserved; colliding graph outputs
and overlaps with source recipe/image/model paths are rejected. Existing generated
files are replaced on rerun. Exports are not an atomic transaction. The CPU memory
budget includes estimates for mesh, raster, padding, source images and one output
map at a time; parser and allocator overhead is additional.

## Bottle example and verification

First follow the [bottle preparation and geometry-bake steps](../samples/models/bottle/README.md).
Then:

```sh
./build/texutil samples/models/bottle/export-bake.json --out out/bottle/baked --threads 8 --json
./build/texutil samples/models/bottle/baked-preview.json --out out/bottle/baked-preview --threads 8 --json
```

The second recipe preserves the bottle's upright orientation, studio angles,
outdoor lighting and hybrid refraction settings. Bake output does not remove the
hybrid renderer's nested-refraction limitations.

Automated CPU tests cover color encoding, scalar height, normal strength and
conventions, mirrored UVs, multiple materials, padding, retained optical inputs,
constant-only materials, absent height, thread determinism, invalid inputs and
memory rejection. A separate GPU regression
checks an asymmetric four-color atlas so a duplicate V flip cannot pass unnoticed:

```sh
./build/texutil_export_bake_tests build/export-bake-gpu-test "$PWD"
```

That last command requires a Filament build and GPU access. Render original and
baked recipes with identical geometry, camera and lighting for visual comparison.
