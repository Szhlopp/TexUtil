# Working with TexUtil

TexUtil is a C++17 command line tool for deterministic procedural texture graphs.
Generate materials with its JSON recipes and native nodes. FastNoise2 and TexUtil's
CPU algorithms produce the float pixels; libpng writes PNGs. Rendering textures,
labeled sheets and MaterialX documents requires no GPU or Python runtime.

Keep method and function signatures on one line. Avoid em dashes in prose.

## Discover commands before authoring

Run commands from the repository root. Use `./build/texutil` on macOS/Linux or
`.\build-windows\Release\texutil.exe` with the documented Windows Release build.
See [README.md](README.md) for prerequisites and platform-specific configuration.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
ctest --test-dir build --output-on-failure

./build/texutil --help
./build/texutil nodes --json
./build/texutil describe normal
./build/texutil describe ramp
./build/texutil presets --json
./build/texutil materialx --json

./build/texutil validate samples/handled-glass.json --json
./build/texutil samples/handled-glass.json --out out/handled-glass --size 1024
./build/texutil render samples/handled-glass.json --out out/handled-glass --threads 8 --stats
```

The executable's catalog is the source of truth for parameter names, enums and
bounds. `describe OP` returns JSON. Do not invent Substance node names or assume
that a similarly named TexUtil node has identical controls.

| CLI option | Use |
| --- | --- |
| `--out DIR` | Write generated files here; use an `out/` subdirectory. Existing files are overwritten. |
| `--size N` / `--size WxH` | Override all graph dimensions, including imports. |
| `--seed N` | Override the root document seed; explicit node seeds and imported document seeds stay independent. |
| `--threads N` | Choose 1..256 workers; default is CPU concurrency capped at 16. |
| `--memory MB` | Live float-buffer budget in MiB; default 1024. This is not a total process memory cap. |
| `--stats` / `--json` | Node timings on stderr / machine-readable render statistics on stdout. |

`render` is optional. `-` reads JSON from stdin, with paths relative to the current
directory. Use separate option tokens, not `--option=value`. Success exits 0;
validation, rendering or I/O errors exit 1. Validation alone does not decode input
PNGs or prove the render fits memory. Exports are not an atomic batch.

## JSON model and execution

A document contains `size`, optional `seed` and `tile`, named `nodes`, optional
`imports`, and `outputs`. Each node is an object with `op` and direct parameters.
Inputs such as `input`, `a`, `b`, `mask` and `sources` refer to node names. Object
order is irrelevant. Every node is validated; only nodes reachable from root
outputs execute. Shared references evaluate once, and buffers are released after
their last consumer. Presets expand into ordinary nodes before execution.

Output keys are relative filenames. Values are node names or objects such as
`{"node":"height","bits":16}`. Use PNG for exchange, usually 16-bit for height
and subtle data maps; PFM preserves 32-bit linear float height, including values
outside 0..1. Integer exports clamp to 0..1. PNG is the only input image format.

For a labeled comparison, add a native sheet alongside the individual outputs:

```json
{
  "type": "sheet",
  "title": "Material maps",
  "columns": 3,
  "cell": 320,
  "items": [
    {"node":"height", "label":"Height"},
    {"node":"normal", "label":"Normal / DirectX"},
    {"node":"color", "label":"Unlit albedo"}
  ]
}
```

This is the value of an output such as `material-sheet.png`, not a graph node.
See [node controls](docs/NODES.md) and [sheet outputs](docs/SHEETS.md).

## Reuse recipes through JSON imports

Use imports instead of duplicating another recipe's nodes or rendering intermediate
PNGs. For a graph saved beside the sample files:

```json
{
  "size": 1024,
  "tile": true,
  "imports": {
    "glass": "glass.json",
    "dust": "dust.json",
    "prints": "fingerprints-scattered.json"
  },
  "nodes": {
    "dusty": {"op":"blend", "a":"glass.roughness", "b":"dust.dust", "mode":"add", "opacity":0.35},
    "handled": {"op":"blend", "a":"dusty", "b":"prints.scattered-prints", "mode":"add", "opacity":0.22},
    "roughness": {"op":"math", "input":"handled", "mode":"clamp", "range":[0,1]}
  },
  "outputs": {
    "roughness.png": {"node":"roughness", "bits":16},
    "normal.png": {"node":"glass.normal", "bits":16},
    "color.png": "glass.color"
  }
}
```

- Reference imported nodes as `alias.node`; nested imports use `outer.inner.node`.
  These are node names, not output filenames. Inspect the imported recipe first.
- Import and image paths resolve relative to the JSON that declares them. Preserve
  relative folder structure when copying recipes. Use forward slashes in JSON paths.
- Aliases are ASCII identifiers, up to 128 characters. Local names cannot equal an
  alias or start with `alias.`. Imported references stay within their own scope and
  cannot reach back into a parent graph.
- All nodes render at the parent's size. Imported nodes retain their document seed
  (default 42) and noise tiling (default false), unless explicitly overridden on
  the node. The root's `tile:true` does not make an imported graph seamless.
- Imported outputs are ignored, including sheets and MaterialX declarations. The
  parent chooses exports, color encoding, background and output settings. Unused
  imported nodes are validated but do not run.
- The same file may be imported under multiple aliases as separate instances.
  References to the same qualified node share its evaluation. Import values are
  filenames only; per-instance parameter overrides are not implemented. Add parent
  nodes to adjust results, or edit an appropriate source recipe.
- Import libraries may omit `outputs`; parents may omit `nodes` if imports supply
  them. A renderable parent still needs nonempty `outputs` and an expanded graph.
- Cycles and namespace conflicts fail validation. Limits are 16 nested levels,
  128 import instances, 8 MiB per imported file, 32 MiB unique imported JSON total,
  and 4096 nodes after imports and presets. Outputs cannot overwrite imported assets.

[samples/handled-glass.json](samples/handled-glass.json) is the complete example,
including MaterialX and a comparison sheet. See [import mechanics](docs/IMPORTS.md).

## Available recipes and useful primitives

Start with [samples/README.md](samples/README.md). Material recipes include `stone`,
`wood`, `birch`, `snow`, `cork`, `glass`, `potion` and `handled-glass`. `terrain`, `forest` and
`erosion` demonstrate heightfield weathering. `tiles`, `ornament`, `stamps` and
`warped` show placement and deformation. The node, preset and effect galleries
demonstrate the remaining controls.

`birch.json` includes bark and separate cut-end maps for one log. The optional
`tools/render_birch.py` builds an HDR-lit single-log preview in a separate Blender
background process; see [birch sample](docs/BIRCH.md). TexUtil itself still needs
no Blender installation to generate the maps or MaterialX files.

Reusable wear graphs are `dust.json` (`dust.dust` when imported as `dust`),
`fingerprint.json` (node `fingerprint`), and `fingerprints-scattered.json`
(`prints.scattered-prints` when imported as `prints`). They produce masks, not a
finished physical dirt layer. Connect masks deliberately to the channels they affect.

Built-in `preset` names are `bnw_spots`, `gaussian_spots`, `dirt`, `grunge`,
`grunge_rust`, `grunge_leaks`, `scratches`, `fibers`, `fur`, `shavings`, `brick`,
`weave`, `ornament`, `creased`, `crystal`, `plasma`, `fluid` and `liquid`.
Use `{"op":"preset","name":"dirt","config":{"scale":1,"detail":0.5,"seed":42}}`.
Config also accepts `angle`; query `describe preset` for limits. Presets are small
scalar recipes, not full material graphs or copies of commercial presets.

| Task | Nodes / documentation |
| --- | --- |
| Build structure | Noise, `clouds`, `voronoi`, `shape`, `stripes`, `blend`, `math`; [nodes](docs/NODES.md). |
| Select or compress grayscale | `range_mask` selects an interval; `invert` excludes it; `levels` sets input/output ranges and gamma; `threshold` isolates a cutoff. |
| Place details | `stamp` for explicit placements, `array` for grids, `radial` for circles, `scatter` for seeded variation and masks. |
| Bend grain | `warp`, `directional_warp`, `swirl`, `polar`; [wood](docs/WOOD.md), [effects](docs/EFFECTS.md). |
| Weather height | `erode` with `rain`, `wind` or `time`; [erosion](docs/EROSION.md). Wind supports direction. These are approximate heightfield models. |
| Convert fields | `normal`, `normal_to_height`, `flood_fill`; [fields](docs/FIELDS.md). Normal integration cannot recover absolute elevation or lost detail. |
| Map B/W to color | `ramp` maps scalar values through ordered gradient stops with `linear`, `smooth` or `constant` interpolation. |

## Build viable materials in this order

1. **Interpret the surface.** Identify material, physical feature scale, intended
   style, condition, environment, and tileability. Separate substrate from paint,
   rust, dust, moisture or other coatings. Infer reasonable defaults from the request
   and record meaningful assumptions in the recipe documentation.
2. **Start in black and white.** Usually this is the heightfield. Establish broad
   forms, then medium features, then restrained fine grain. Use separate masks for
   pores, deposits, cracks and wear so they can drive multiple outputs coherently.
   Bright means higher and dark means lower. Levels should shape meaningful relief,
   not force every surface to use the entire black-to-white range. Flat glass may
   legitimately be almost uniform. Inspect the B/W result before adding color.
3. **Derive normals from height.** Use `normal` after the heightfield's warps,
   placement and erosion are finished. Start with a modest explicit strength, often
   0.02..0.08 for these sample-scale textures, then judge the actual slopes. This is
   a starting range, not a physical requirement. Match DirectX/OpenGL to the target.
   Keep fine detail in normals; when appropriate, use a smoother height branch for
   coarse displacement. Match displacement to scene units and avoid applying the
   same relief twice at full strength through displacement and normals.
4. **Author roughness intentionally.** Begin with the material's finish, then use
   structural masks and restrained independent variation. Height and roughness may
   correlate, but roughness is not automatically height or inverted height. Refer
   to the condition table below. Add dust, scratches and fingerprints only where
   handling and environment justify them, at plausible scale and varied opacity.
   Many fingerprints belong mainly in roughness, not deep height grooves.
5. **Generate color/albedo last.** Use a solid color for uniform substances, or
   `ramp` from height, roughness or a dedicated composition mask. Select colors for
   the material rather than merely making the grayscale look attractive. Reuse
   meaningful masks without copying identical contrast into every channel. Keep
   lighting, highlights, cast shadows and presentation gradients out of base color.
6. **Set metalness and other shader properties.** Uniform dielectric: constant
   `metalness:0`. Uniform exposed metal: constant `metalness:1`. Neither needs a
   metalness texture. Use a map when metal and dielectric regions vary spatially,
   such as chipped paint, rust or dirt over metal. Set IOR, transmission, coat,
   subsurface or emission only when the surface calls for them.
7. **Export and inspect.** Save the JSON, individual maps, a useful labeled sheet,
   and a connected `.mtlx` when a material is requested. Check the actual maps as
   well as any amplified inspection view. Label display boosts and illustrations.
   Do not add fake reflection streaks to glass previews. A 2D sheet does not verify
   a physically lit material; use a suitable renderer when available and state what
   was actually tested.

| Condition | Treatment |
| --- | --- |
| New / clean | Preserve the intended finish, with slight manufacturing variation where appropriate. New cloth or chalk can still be rough. |
| Used / handled | Localize contact wear, polish, small scratches and oily prints; add modest dust where it can collect. Wear can smooth high-contact regions. |
| Old / weathered | Add coherent erosion, cracks, deposits, corrosion or peeling where the substrate and exposure support them. Age does not mean uniform noise or uniformly maximum roughness. |

Dust often increases roughness on polished surfaces; oil or moisture can smooth
some surfaces. Fingerprints may raise or lower roughness relative to the clean
finish. Choose the effect from the material and residue, not a universal rule.

For a cartoon or stylized request, favor readable forms, a deliberate palette and
often brighter or more saturated colors, with simpler fine detail. For realism,
use reference-appropriate reflectance, scale and restrained variation. Realistic
snow, paint and pigments can be bright; realism does not mean dark or desaturated.
Stylization can exaggerate shapes and palettes while preserving coherent channel
semantics. Document an intentional departure from physical behavior.

## Make the maps describe one physical surface

Choose the material's real-world coverage before tuning detail: for example, one
tile might represent 10 cm of glass or 2 m of terrain. Record that coverage and
the intended displacement depth. Keep grain, bubbles, scratches and fingerprints
consistent with it. Increasing resolution adds samples, not physical depth or a
reason to make every feature stronger. Inspect both close up and at the expected
camera distance; fine detail should survive filtering without becoming sparkling
noise or a repeated stamp pattern.

Build masks around causes. A scratch can remove a coating and expose another
substance; a fingerprint usually deposits a thin residue; a crack changes relief;
dust collects according to exposure and contact. Share placement masks between
maps, then remap their strength separately for each physical effect. Do not put
the same noise at equal contrast into height, color and roughness. A surface can
have a nearly flat heightfield and still have rich roughness variation.

| Surface feature | Coordinate the maps this way |
| --- | --- |
| Painted metal with chips | Paint remains dielectric. Exposed metal changes metalness and base color together; use the chip boundary for shallow relief and tune each finish's roughness separately. |
| Rust or opaque dirt over metal | Covered regions behave as a dielectric deposit, with their own color, roughness and possible added height. Do not keep those regions fully metallic. |
| Scratched glass | Begin with small roughness changes and shallow incisions. Preserve transmission; avoid white painted lines unless the damage actually scatters enough light to appear frosted. |
| Dust and fingerprints | Use sparse, varied masks appropriate to the object's handling. Favor roughness changes for thin residue; add color or height only when the deposit warrants them. |
| Wood, stone or cork | Establish structure and pores at multiple scales. Use structure to inform color and roughness, with variation appropriate to the finish, such as sealed versus unfinished wood. |
| Liquid or wet surfaces | Use transmission or a suitable surface layer, restrained roughness and coherent ripples. A wet film and its substrate have different optical roles; darkening albedo alone is incomplete. |

Avoid making every requested material visibly distressed. A clean product should
retain clean areas. For used objects, distinguish contact polishing from abrasion
and accumulated deposits. Surface-only graphs cannot infer an object's exposed
edges, gravity direction or hand-contact areas from arbitrary UVs. If placement
depends on geometry, use appropriate supplied masks or document the approximation.

## Preserve the material's optical behavior

PBR shaders balance reflected and transmitted energy. Let the shader calculate
view-dependent highlights and Fresnel response. Do not paint bright rims into
albedo or adjust metalness to compensate for weak lighting. Dielectrics still
reflect light; metalness zero does not mean no specular reflection. Roughness
changes the shape and clarity of reflections, so judge it under lighting that
contains both broad bright regions and smaller highlights.

Keep coating and substrate roles distinct. Colored paint over metal usually
behaves as paint at the surface; clear varnish is better represented by a coat
layer when appropriate. For glass and liquids, use a plausible IOR and distinguish
surface roughness from absorption through the volume. Transmission tint and depth
depend on object thickness and scene units. Use closed geometry with sensible
normals for solid glass or liquid; choose thin-walled behavior deliberately for
a sheet. A liquid inside a bottle generally needs separate glass and liquid
materials and suitable geometry, not one shader assigned to everything.

Height changes shape only when connected to displacement with sufficient geometry
or renderer subdivision. A normal map changes shading and cannot create a raised
silhouette. Verify a simple bump under directional light to catch inverted normal
conventions. Do not fix an inverted result by changing arbitrary color channels
until the map convention and shader interpretation have been checked.

In the destination DCC, check texture color spaces as well as filenames. Decode
sRGB base color once, and read roughness, metalness, normals and height as raw
linear data. Check the renderer's working space and display/view transform;
changing exposure or the view transform changes the displayed appearance without
changing the texture. Keep those settings fixed when comparing recipe revisions.

## Evaluate materials with HDR environment lighting

**A suitable HDR environment can dramatically improve rendered material quality
in Blender, Maya, 3ds Max and similar 3D software**, especially for glass, liquids,
polished surfaces and metals. It supplies varied illumination and recognizable
reflections that a flat background or basic default light cannot provide. Include
HDRI lighting in material-preview recommendations and DCC setup when available.

Use a genuine high-dynamic-range environment image, commonly `.hdr` or `.exr`, as
an actual environment light. A background image alone does not necessarily light
the scene, and a file extension alone does not guarantee useful dynamic range.
Blender uses an Environment Texture in the World shader; Arnold provides a
Skydome light for HDR image-based lighting. In Maya or 3ds Max, use the appropriate
environment light for the active renderer. See the official
[Blender World lighting documentation](https://docs.blender.org/manual/en/latest/render/lights/world.html)
and [Arnold Skydome documentation](https://help.autodesk.com/cloudhelp/ENU/AR-Core/files/ac-lights/arnold_core_ac_skydome_light_html.html).

Start with a neutral studio environment, rotate it to reveal form, and set sensible
exposure. Add area lights where needed to clarify roughness or surface relief.
Also inspect under a different environment, such as outdoor daylight, so the
material does not depend on one flattering setup. A good HDRI can greatly improve
presentation, but it cannot repair incorrect maps, geometry or shader connections.

Before delivery, compare a flat patch, a curved object and, where possible, the
intended mesh at its actual scale. Check grazing reflections, normal direction,
transmission thickness, seams and repeating details. Let the render converge
enough to distinguish sampling noise from texture grain. Do not add grain or
roughness to disguise render noise. Record the renderer, environment, exposure,
view transform and any display boosts used for the preview. Keep HDR lighting in
the scene, separate from the unlit exported maps; TexUtil's sheets and MaterialX
exports do not automatically create an HDR-lit 3D scene.

## Run Blender previews in a background process

Detect the current operating system, shell and installed tools before constructing
commands. Do not assume macOS, a Unix shell, a particular installation directory
or that `blender` is on PATH. Use Blender's own executable and bundled Python for
`bpy` scripts. Discover `blender`/`blender.exe` using the shell's executable lookup
(for example, `Get-Command blender` in PowerShell or `command -v blender` in a POSIX
shell), then inspect installation locations appropriate to that host if necessary.
Use the actual TexUtil build output for the selected generator and configuration.
Quote paths using the active shell's rules; PowerShell requires `&` to invoke a
quoted executable path. Blender is an optional preview dependency, not part of
TexUtil's texture-generation runtime.

From the repository root, use the discovered executables with these arguments.
The angle-bracket names below are placeholders, not literal shell commands:

```text
<texutil executable> samples/birch.json --out out/birch --threads 8
<blender executable> --background --factory-startup --python-exit-code 1 --python tools/render_birch.py -- --out out/birch --samples 96
```

Run generated preview scripts in their own background process. `--factory-startup`
gives a predictable starting scene and preferences for that process; it does not
make destructive script operations safe inside the user's interactive session.
The birch script deletes its starting scene. Do not run it in a user's open file
or save over an existing user scene. Write scripts to `tools/` and renders, logs
and `.blend` files to a dedicated ignored `out/` directory.

Blender processes arguments in order. Put `--python-exit-code 1` before `--python`
so script exceptions return failure. Arguments after `--` belong to the script.
When rendering an existing `.blend` with CLI options, load the file before setting
render overrides and put the render action last. A script that calls
`bpy.ops.render.render(write_still=True)` already initiates the render; do not add
another CLI render action. See the official
[command-line argument reference](https://docs.blender.org/manual/en/4.0/advanced/command_line/arguments.html).

For reproducible preview scripts:

- Resolve input/output paths explicitly and fail early if maps or HDRs are missing.
  Locate bundled HDRs through `bpy.utils.system_resource('DATAFILES')`, not a fixed
  Blender version folder. Accept an explicit HDR path for installations without
  those assets. Do not silently replace requested HDR lighting with a flat world.
- Set the active camera, renderer, device, resolution percentage, samples, output
  format/path, exposure and view transform explicitly. Cycles CPU is a useful
  baseline; background mode alone does not select CPU or prove GPU availability.
  Use a small draft first, then render the final image. Bound worker counts to
  avoid saturating the desktop, and record settings that affect appearance.
- Build materials with explicit texture color spaces. For TexUtil DirectX normal
  maps, read Non-Color data, flip green once (`G = 1 - G`), then use a tangent-space
  Normal Map node. Do not treat the RGB normal texture as a height/bump input.
- Save the reproducible scene and call `render(write_still=True)` to save the PNG.
  Absolute map/HDR paths work locally but require the same files later. Pack
  resources or establish portable relative paths when delivering a movable scene.
  Check external HDR licensing before bundling it with repository assets.

Capture process output and wait for completion. Verify the exit status, that the
expected image was freshly written, its dimensions, and its appearance. A saved
`.blend` or a zero exit code alone is insufficient evidence of a finished render.
Inspect the image for missing textures, framing, normal direction and sampling
noise; report DCC rendering separately from MaterialX SDK validation.

## Map semantics, ranges and color management

| Map / property | Meaning and constraints |
| --- | --- |
| Base color | sRGB color export by default. For dielectrics it describes diffuse reflectance; for exposed metal it supplies colored reflectance in the metalness workflow. Do not use a lit preview as base color. |
| Height | Linear scalar data. Integer exports are 0..1, but this is an encoding range, not a real-world height range. Define midlevel and displacement scale. Prefer 16-bit PNG or float PFM to avoid banding. |
| Normal | Raw linear RGB encoding of a direction, not a color gradient. Flat tangent-space normal is `(0.5,0.5,1)`. Use `normal`, not a ramp. Generic image transforms do not rotate or renormalize normal vectors; transform height first. |
| Roughness | Linear scalar in 0..1: 0 smooth, 1 rough. It controls the spread of reflections, not metalness or albedo brightness. No universal minimum or maximum narrower than this applies to every material. |
| Metalness | Linear scalar in 0..1, usually 0 or 1 for resolved pure regions. Intermediate values can describe filtered boundaries or unresolved coverage; do not use gray merely to make metal less shiny. Paint, rust and covering dirt are dielectric. Coordinate their color and metalness masks. |
| AO / masks | Linear scalar data. AO is separate from albedo; an inverted heightfield is not a physically computed AO map. Do not assume TexUtil has a dedicated AO node or Standard Surface AO input; query the catalogs. |
| Transmission / opacity | Separate controls in 0..1. Transparent glass generally needs transmission and suitable geometry/IOR; lowering opacity is not a substitute for refraction. Alpha does not connect either property automatically. |
| IOR / emission | IOR is a material-dependent scalar, not a normalized mask. For a common dielectric, IOR near 1.5 gives about 4% normal-incidence reflectance. Emission can be HDR and must remain separate from reflected base color. |

The supplied PBR guide suggests roughly **30..240 sRGB** for ordinary dielectric
base-color brightness on an 8-bit scale, or **50..240** as a stricter range. Treat
these as authoring checks, not universal physical bounds or a per-channel clamp
that destroys saturated pigments. Avoid unintended pure black/white diffuse
regions and baked shadows. These are sRGB-encoded brightness values, not linear
roughness, height or metalness thresholds. Use measured reflectance for a specific
metal when available; do not apply dielectric albedo limits to metal reflectance,
transmission color or emission.

TexUtil hex colors such as `#808080` are interpreted as sRGB and decoded to linear
RGB. Numeric arrays such as `[0.5,0.5,0.5]` are already linear and are therefore not
equivalent. Blending and `ramp` interpolation happen in linear RGB. Scalar, normal
and packed data bypass sRGB encoding. Import height/masks with `image.kind:scalar`
and normals with `image.kind:normal`; do not load data maps as ordinary color.
The MaterialX working space is linear Rec.709. Shader numeric inputs are not
automatically clamped to physical ranges, so validate your choices.

## MaterialX connections

Query `./build/texutil materialx --json` for supported Standard Surface names and
types. `inputs` accepts constants and PNG texture bindings; use exact names such
as `specular_roughness`, `specular_IOR`, `base_color` and `metalness`.

```json
{
  "type": "materialx",
  "name": "ExampleMaterial",
  "inputs": {
    "base_color": {"texture":"color.png"},
    "specular_roughness": {"texture":"roughness.png"},
    "metalness": 0,
    "specular_IOR": 1.5
  },
  "normal": {"texture":"normal.png", "convention":"directx"}
}
```

Place this under `outputs` as `material.mtlx`. Output `version` defaults to `"1.38"`;
use `"1.39"` for Maya 2027 / LookdevX 2.0 to avoid a legacy upgrade prompt. This is
separate from the TexUtil graph version. Bind **exported PNG filenames**, not
node references: `glass.roughness` is a graph reference, while `roughness.png` is a
MaterialX texture binding. Float inputs require scalar outputs. The normal helper
decodes tangent-space normals and handles the DirectX green flip. Its `scale` is
different from the `normal` node's height-to-slope `strength`.

For Maya 2027 Hypershade's Arnold `aiMaterialXShader`, set
`"version":"1.39"` and `"texture_paths":"absolute"` on the material output.
This workflow was confirmed in the user's shader-ball preview with
`out/potion/potion-arnold.mtlx`, material `Potion`. Absolute texture paths resolve
against `--out`; regenerate after moving the folder or changing computers.
Keep the default `"texture_paths":"relative"` for portable exports to hosts that
resolve paths against the document. The potion recipe emits both variants.

Imported MaterialX declarations are not copied into the parent. Declare the
parent's own PNG exports and material connections. Copy the resulting `.mtlx` and
all referenced PNGs together, preserving relative paths. See
[MaterialX documentation](docs/MATERIALX.md) for displacement, transmission and
other inputs. The exporter does not render a sphere; SDK validation and shader
generation do not prove a host application renders the material correctly.

## Model operations and GPU previews

See [model documentation](docs/MODELS.md) before authoring a preview. CPU commands:
`model check-uvs FILE --json`, `model uv FILE --out new.obj`, and
`model bake FILE --out out/bake --maps curvature,ao,thickness,materialids`.
UV unwrap writes a new OBJ/MTL and refuses existing paths. Use that copy consistently
for baking and previewing. Thickness requires a closed, outward-facing mesh.

Import the bake's `maps.json` through normal graph `imports`; paths to its PNGs
resolve beside that JSON. Bakes are reusable disk assets. `object_normal` is an
object-space geometry map, not a tangent-space MaterialX detail normal.

Filament-enabled builds accept `type:preview` PNG outputs referencing a MaterialX
output filename plus a `model` path. Choose `environment:studio`, `outdoor`, or a
custom HDR path; `rotation:[pitch,yaw,roll]` or `views` for a labeled angle sheet.
`projection:triplanar` works without UVs; `projection_scale` controls repeats per
mesh unit and `projection_blend` controls axis transitions. It affects preview
sampling only, not exported MaterialX. UV-baked masks must use UV projection.
Use saved recipes in `samples/models/`; `geometry-maps.json` requires running its
bake command first. Report unsupported-input preview warnings and remember that
Filament's PBR approximation does not establish identical Arnold/Maya rendering.

## Tiling, performance and delivery checks

- Use `tile:true` for periodic noise, `edge:"repeat"` for appropriate sampling,
  and `wrap:true` for stamps crossing an edge. A half-offscreen wrapped stamp
  appears on the opposite side. These controls do not repair nonperiodic sources.
- Inspect at least a 2x2 repeat when promising tileability. Rotations and transforms
  can break periodicity. Adjacent first/last pixel centers need not have equal
  values; judge the transition across the wrap against ordinary neighboring pixels.
- Work at a modest preview size, then inspect the requested final resolution. UV
  feature sizes and pixel blur radii scale differently. Fine scratches can disappear
  at low resolutions; erosion and filters may need retuning.
- Dimensions are limited to 16384 per axis. One 16384-square scalar float image is
  1024 MiB; an RGBA/normal float buffer is 4096 MiB. A graph needs additional live
  inputs and scratch buffers. Increase `--memory` only within available RAM; this
  engine is not a streaming or out-of-core terrain renderer.
- Save reusable recipes in `samples/`, with corresponding `examples/` copies kept
  synchronized where they already exist. `terrain.json` is the documented exception.
  Keep generated renders in ignored `out/`. Put only intentional documentation
  images in `docs/images/`. Preserve other work in the checkout.
- Validate the recipe, render it, and inspect height, normals, roughness and color.
  Check ranges, clipping, seams, scale and material consistency. Save useful controls
  and the reproduction command in Markdown. A successful JSON parse is not visual QA.
- For code changes, rebuild and run the relevant tests. Run the full CTest suite
  for graph/runtime changes. Add meaningful regression coverage for new behavior,
  update catalogs and docs, and use `tools/update_docs.py` for generated node docs.
  Recipe/documentation edits need relevant validation rather than new test code.
- If the optional MaterialX Python SDK is available, run
  `python tools/validate_materialx.py out/handled-glass` (substitute your output
  directory). Report SDK checks separately from actual DCC rendering.

## PBR reference

Material-authoring guidance above draws on **Wes McDermott, The PBR Guide,
Allegorithmic, third edition, February 2018**, supplied by the user. Useful sections
are linear space rendering (pp. 38-41), base color (pp. 51-53), metallic
(pp. 54-59), roughness (pp. 59-60), and AO, height and normals (pp. 74-79).
The PDF is an external reference, not a bundled repository dependency. These are
paraphrased principles adapted to TexUtil's metalness/roughness workflow; the guide's
Substance-specific tools and historical renderer behavior are not TexUtil features.
