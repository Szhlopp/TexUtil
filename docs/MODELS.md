# Models, geometry maps and material previews

TexUtil loads static **OBJ, FBX, glTF and GLB** meshes. CPU commands inspect and
unwrap UVs and bake geometry maps. Optional **Filament** support renders generated
materials on those meshes using HDR lighting, UV or triplanar projection, and
one or more object rotations.

## Build with previews

First install the normal prerequisites in [README](../README.md). Model loading,
xatlas UV generation and CPU baking are included in the ordinary C++17 build.
The first configure downloads pinned, SHA-256-checked source dependencies.

GPU previews additionally need the official Filament **1.76.1** SDK, a C++20
compiler and a working graphics driver. The helper uses Python 3.9+ only at setup:

```sh
python3 tools/setup_filament.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTEXUTIL_FILAMENT_ROOT="$PWD/build/model-deps/filament"
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

If the SDK directory already exists, use it; the helper refuses to overwrite it.
Alternatively extract the matching [official SDK](https://github.com/google/filament/releases/tag/v1.76.1)
and point `TEXUTIL_FILAMENT_ROOT` at the directory containing `bin`, `include` and
`lib`. Keep the compiler (`matc`) and libraries from the same release. Clear cached
`TEXUTIL_MATC` / `TEXUTIL_FILAMENT_*` paths or use a fresh build directory when
switching SDKs. Shader packages are compiled and embedded into the executable.

For Windows, run `py tools/setup_filament.py` and add
`-DTEXUTIL_FILAMENT_ROOT="C:/path/to/TexUtil/build/model-deps/filament"` to the
Windows CMake configure command in the README. Use a Release x64 build with the
SDK's dynamic MSVC runtime (`/MD`). macOS previews use Metal; Windows/Linux use
Vulkan. **Only macOS ARM64 has been built and GPU-tested.** Other platform paths
are provisional and may need toolchain/linker adjustments. No preview window is
opened, but GPU access is required. Ordinary texture graphs and model baking
remain CPU-only. Configure a separate build without `TEXUTIL_FILAMENT_ROOT` to
omit the renderer.

## Inspect or generate UVs

```sh
./build/texutil model --help
./build/texutil model check-uvs assets/models/preview-torus.obj --json
./build/texutil model check uvs model.fbx --size 1024 --json
./build/texutil model uv model.fbx --out out/model-uv.obj --size 1024 --padding 4 --json
```

Checks report missing, nonfinite, degenerate, mirrored and out-of-range UVs;
degenerate geometry; sampled UV overlap and coverage; material slots and bounds.
`usable_for_preview` accepts overlapping or repeated UVs. `usable_for_baking`
requires a unique 0..1 atlas with no detected overlap. The check command exits 1
when the latter is false. Overlap is sampled at pixel centers, so very small
intersections can be missed. Increase `--size` for a finer check.

`model uv` explicitly unwraps a **new copy** with xatlas. Existing OBJ or MTL output
paths are refused. Use the new OBJ for both baking and previewing. The new UVs
invalidate old UV-based textures; no automatic transfer is performed. Material
slots, static geometry and normals are retained, but animation, hierarchy and
original shader networks are not exported. `--size` is the atlas packing target,
32..4096, and `--padding` is 0..64 pixels.

## Bake maps from geometry

```sh
./build/texutil model bake assets/models/preview-torus.obj --out out/model-bake --size 512 --samples 64 --threads 8
./build/texutil model bake out/model-uv.obj --out out/object-bake --maps curvature,ao,materialids --size 1024 --distance 0.25 --json
```

Outputs are **16-bit linear PNGs**, an importable `maps.json`, and a
`bake-info.json` report. Output files are replaced when rerunning a bake. Defaults:
1024 square, 64 ray samples, 4 pixels of island padding, 1024 MiB working buffer
budget. Size is capped at 4096, samples at 4096, workers at 256. This memory option
is not a total process or GPU memory cap. Start at 256 or 512 before raising quality.

| Map | Meaning |
| --- | --- |
| `curvature` (CLI alias `curv`) | Cotangent mean curvature: 0.5 flat, brighter convex, darker concave. Mesh scale normalized, smoothed by vertex interpolation. |
| `ao` | Cosine-weighted hemisphere visibility: 1 exposed, 0 occluded. |
| `thickness` | Mean inward ray distance divided by `--distance`. Requires closed, consistently outward-facing geometry. A miss uses the maximum distance. |
| `materialids` | Original material slot index divided by (slot count minus one). The report lists exact names, indices and encoded values. |
| `position` | XYZ normalized independently to the mesh bounding box, stored as raw RGB data. |
| `object_normal` | Object-space geometric/shading normal encoded from -1..1 to 0..1. This is **not** a tangent-space detail normal map. |
| `coverage` | 1 for covered atlas texels, 0 outside islands. No padding on this map. |

All seven are generated unless `--maps` narrows the list. Curvature is computed
from the mesh, not from a texture. AO and thickness use TinyBVH ray queries and
are deterministic for a fixed mesh, resolution and sample count. Thickness is an
approximate directional average, not a minimum wall-thickness measurement.
It is unreliable on open, intersecting or inward-facing geometry; UV validity
alone does not establish a watertight solid. There is no high-poly-to-low-poly
cage baker in this version. Material IDs reflect assigned source slots and do
not infer different materials from shape or appearance.

`--distance` is the AO/thickness ray limit in mesh units; default is the bounding
box diagonal. FBX is converted to meters and Y-up; glTF uses its meter convention.
OBJ has no reliable unit metadata, so its coordinates are treated as meters.
Curvature uses `0.5 + 0.5*tanh(H*diagonal*0.1)`; fine, coarse and irregular meshes
will produce different results. Uncovered pixels receive sensible map defaults,
then border values are extended by the requested padding without wrapping.

## Reference geometry maps in a graph

```json
{
  "size": 512,
  "tile": false,
  "imports": {"mesh": "../../out/model-bake/maps.json"},
  "nodes": {
    "cavities": {"op": "invert", "input": "mesh.ao"}
  },
  "outputs": {"cavities.png": {"node": "cavities", "bits": 16}}
}
```

The import path resolves relative to the recipe. Image paths inside `maps.json`
resolve relative to that file. Use `mesh.curvature`, `mesh.ao`, `mesh.thickness`,
`mesh.materialids`, etc. like other graph references. Imported outputs are ignored;
only nodes needed by the parent execute. Bakes are reusable disk assets, not
live operations repeated on every material render. Copy the entire bake folder
when moving a recipe. See [graph imports](IMPORTS.md).

Use curvature and AO to **place** wear and dirt rather than multiplying permanent
lighting into albedo. Retain AO as a separate map for the destination shader.
Height-to-normal still generates tangent-space material detail; do not connect
`object_normal` through the MaterialX tangent-normal helper. Choose material-ID
selection ranges that account for 16-bit quantization and filtered boundaries.

## Preview output

Declare ordinary PNG exports and a MaterialX output first. A preview references
that material's **output filename**, and renders after its maps have been saved:

```json
"preview.png": {
  "type": "preview",
  "material": "material.mtlx",
  "model": "../../assets/models/preview-torus.obj",
  "environment": "studio",
  "projection": "triplanar",
  "projection_scale": 2,
  "projection_blend": 4,
  "views": [[35, 0, 0], [35, 60, 0], [60, 120, 0]],
  "size": 384
}
```

Run the complete checked-in examples:

```sh
./build/texutil samples/models/preview.json --out out/model-preview --json
./build/texutil samples/models/triplanar.json --out out/triplanar --json
```

| Setting | Default / behavior |
| --- | --- |
| `model` | Required path relative to the JSON; OBJ, FBX, glTF or GLB. |
| `material` | Default material binding, required unless `materials` supplies every used mesh slot. |
| `materials` | Map source material names or `#index` keys to local MaterialX outputs or external recipe bindings. |
| `environment` | `studio`, `outdoor`, or a custom 2:1 Radiance `.hdr` path relative to the JSON. |
| `rotation` | `[pitch, yaw, roll]` degrees, default `[0,0,0]`. Applied X then Y then Z. |
| `views` | Alternative to `rotation`: 1..12 angle triples. Multiple views produce a labeled sheet. |
| `size` | Pixels per square view, 64..2048, default 512. Independent of graph `--size`. |
| `columns` | Sheet columns, 1..8, default 3. |
| `projection` | `uv` by default, or `triplanar`. |
| `projection_scale` | Triplanar repeats per source mesh unit, 0.0001..10000, default 1. |
| `projection_blend` | Axis blend exponent, 1..16, default 4. Larger values sharpen transitions. |
| `intensity` | HDR illumination multiplier in Filament's convention, 0..1000000, default 30000. |
| `exposure` | Stops relative to the preview camera, -16..16, default 0. |
| `environment_rotation` | Panorama yaw in degrees, default 0. Lighting and visible background rotate together. |
| `background` | Opaque color, default `#24282d`. |
| `show_environment` | Display HDR background, default false. |
| `thickness` | Refraction thickness in the normalized preview scene, 0..10, default 0.1. |
| `render_order` | `default`, `center_out`, or `outside_in`. Sort material parts by maximum vertex radius from the model's bounding-box center, then assign Filament priorities. |

Triplanar samples three object-space planes and blends by the surface normal.
The projection follows object rotation and does not require or modify UVs.
Normal maps are decoded with their DirectX/OpenGL convention, blended as surface
slopes and transformed into the mesh tangent frame. Neutral normal maps retain
the underlying smooth normal. Every preview material texture uses the selected
projection. Use **UV projection for geometry-baked maps** that belong to a specific
atlas. Mixing UV atlas masks with triplanar detail is not supported in this version.
Triplanar always repeats its textures; it does not make a non-tileable image seamless.
This setting affects the preview only; it does not author a triplanar MaterialX
network or bake the projection back into UV textures.

Meshes are centered and scaled uniformly to fit a fixed camera. Projection scale
uses source units before this display normalization. A single `material` applies to every slot; `materials` can assign separate recipes
to named slots and override the default. Existing model shaders, textures, cameras
and lights are not rendered. UV mode uses the first UV set. Static transforms and
instances are flattened; skinned meshes and morph targets must be exported as a
static posed mesh. glTF triangle primitives must be uncompressed (no Draco or
meshopt). The loader limits each input file to 512 MiB and the combined mesh to
2 million triangles; referenced buffers and parser allocations are additional.

Filament approximates the exported Standard Surface using base color, roughness,
metalness, tangent normal, IOR, transmission/tint/depth, emission and clear coat.
Unsupported inputs and unapplied displacement are reported as preview warnings
on stderr and in `--json` output details. This is not a full MaterialX shader
interpreter or an Arnold reference render. Opaque, thin and solid surfaces use different
Filament material packages; transmissive variants use screen-space or environment-cubemap refraction; neither performs path-traced internal scattering.
Previews use HDR image-based diffuse/specular lighting, neutral PBR tone mapping
and FXAA, and export opaque 8-bit sRGB PNGs. Other formats and render callbacks
are unsupported for preview outputs. GPU allocations are outside `--memory`.

## Multiple materials and external JSON recipes

A model can bind a different graph to each existing material slot:

```json
"preview.png": {
  "type": "preview",
  "model": "bottle.obj",
  "materials": {
    "glass": {"graph": "glass.json", "material": "glass.mtlx"},
    "cork": {"graph": "cork.json", "projection": "triplanar", "projection_scale": 30},
    "brew": {"graph": "potion.json", "refraction": "opaque"},
    "brass": "brass.json"
  },
  "environment": "studio",
  "rotation": [0, 30, 0]
}
```

The binding keys match material slots reported by `model check-uvs`; `#1`, `#2`,
etc. select explicit zero-based indices when names are ambiguous. These are
material assignments, not arbitrary OBJ group names. Assign slots in the source
DCC or prepare a working copy first. An unknown key or an unbound used slot fails at render time;
an optional top-level `material` acts as a fallback.

A string ending in `.json` is shorthand for `{"graph":"path.json"}`. Other strings
refer to MaterialX output filenames in the current graph. An object with `graph`
may select a MaterialX output using `material`; selection is automatic only when
the external recipe has exactly one MaterialX output. Graph paths resolve beside
the preview JSON, while each graph's own imports resolve beside that graph.
Per-binding `projection`, `projection_scale`, `projection_blend` and `thickness`
override preview defaults. Graphs containing only preview outputs may use empty
`nodes`, so a scene recipe needs no dummy texture node.

Validation checks binding syntax and recipe-file existence; external graph contents
and mesh-slot matching are checked when rendering. External recipes render their selected MaterialX output and its referenced image
outputs into `preview-materials/<preview-output>.assets/<slot>/` under `--out`.
Unrelated image/sheet/preview outputs are pruned, preventing recursive preview
execution. Graph resolution and tiling come from the external recipe. These
exports are listed in render statistics; each preview also reports its resolved
slot bindings, asset directories, projections and warnings. GPU texture storage
is additional to the graph's CPU float-buffer budget.

`refraction` is a per-binding preview option: `auto` is the default and chooses
opaque or screen-space refraction from the shader's transmission input. Explicit
`opaque` disables transmission **only in the preview** and emits a warning for a
transmissive material. It is useful for inspecting dense liquid behind glass:
screen-space refraction can see the opaque scene but cannot accurately resolve
multiple nested transmissive layers. Exported MaterialX remains unchanged. Use a
path tracer for final glass/liquid volume evaluation.

`refraction: "cubemap"` keeps transmission, IOR and absorption active but refracts
only the HDR environment. These materials render in Filament's color pass, so a
later screen-space glass material can see their rendered appearance. This hybrid
is useful for the bottle preview and does not force the liquid opaque. It cannot
refract other scene objects or accurately reproduce light crossing nested liquid,
bubbles and glass. The approximation is reported in preview warnings. It does not
alter the exported MaterialX. Solid and thin-walled cubemap variants are supported.

Each material slot has a separate renderable. The preview-level `render_order`
uses a shared bounding-box center, not a preserved DCC pivot or a containment
test. It maps ascending or descending radii to eight priority levels; more than
eight parts share levels. This is a heuristic for roughly concentric shells.
Per-binding integer `render_order: 0..7` overrides that priority (lower draws
earlier within the same pass/channel). It is draw priority, not dielectric-medium
priority for a path tracer. Sorting cannot refresh the refraction buffer.

Additional per-binding diagnostic controls are `render_channel: 2..7` (default
2, higher channels draw later) and `culling: "none" | "back" | "front"` (default
`none`). Back/front culling disables double-sided shading for that material.
Use back culling only with consistently oriented geometry. Changing channels
does not create another refraction capture; prefer the default channel. Render
JSON includes the effective priority, channel, radius in source units and culling.

The bottle ordering study tested both radial orders, explicit liquid-first and
glass-first priorities, a later glass channel and back-face culling. None restored
the nested liquid with both layers using screen-space refraction. The hybrid
cubemap-liquid/screen-space-glass preview did retain the green fill.

See [multi-material torus](../samples/models/multi-material.json) for a self-contained
example and [the bottle study](../samples/models/bottle/README.md) for real geometry
preparation, baked masks and separate glass/cork/potion/brass recipes.

## Lighting assets and redistribution

Bundled `studio` and `outdoor` HDRIs are compact CC0 Poly Haven assets; see
[asset provenance](../assets/hdri/README.md). Custom HDRs must be 2:1, no more than
8192 pixels wide and 128 MiB compressed. They are filtered for lighting at runtime.
Keep custom HDR files with the recipe or use absolute paths.

`cmake --install build --prefix install` copies the executable, standard HDRIs and
retained notices. Copy those directories together. Standard lighting is located
via `TEXUTIL_HDRI_DIR`, the build's source asset directory, or the installed
`share/texutil/hdri` relative to the executable. Platform libpng/zlib and graphics
drivers must also be available. Filament libraries and shader packages are linked
into the executable; `matc`, Python and the SDK are not runtime requirements.

The dependency set uses permissive licenses: Filament Apache-2.0; xatlas, ufbx,
cgltf and TinyBVH MIT; stb's MIT option; and the retained permissive licenses of
Filament's linked components. **libigl and Open3D are not dependencies.** See
[third-party notices](THIRD_PARTY.md) and retain the required notices when shipping.

## Validation recorded 2026-09-09

- macOS ARM64 Release builds, with and without Filament: all 18 CTests passed.
- Automated model tests cover OBJ/FBX/glTF/GLB geometry, UV conversion and material
  slots; missing/invalid/overlapping UVs; xatlas export and overwrite refusal;
  plane/torus AO, thickness and curvature; threaded bake determinism; bake imports;
  and preview reference, rotation and projection validation.
- Actual Metal renders covered all four file formats, missing-UV triplanar mapping,
  normal-mapped wood at three rotations, outdoor lighting, the supplied 4K studio
  HDR, and thin/solid glass. Neutral-normal UV/triplanar comparison differed by at
  most 2/255 per channel, with mean difference 0.0023/255, consistent with the
  shader's extra tangent-frame transforms and output quantization.
- The three-view 384-pixel sample completed in approximately 230 ms on an Apple
  M5 Max after shader caches were warm. Cold starts, other GPUs, larger textures
  and complex meshes can differ substantially.
- The pinned macOS SDK archive passed its SHA-256/extraction check. A local install
  included the executable, standard HDRIs and notices. Windows/Linux builds and
  GPU rendering remain unverified. CTest does not require GPU access; actual
  render checks above were performed separately.
