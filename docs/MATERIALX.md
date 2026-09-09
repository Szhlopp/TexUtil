# MaterialX material export

TexUtil can export MaterialX 1.38 or 1.39 `.mtlx` documents alongside baked PNG textures.
The document uses the standard `standard_surface` shader, version 1.0.1. It has no
TexUtil-specific shader nodes and needs no TexUtil plugin in the receiving DCC.
The exporter is native C++; exporting does not require Python, the MaterialX SDK,
OpenGL or a GPU. MaterialX is an open material interchange standard, not a renderer.

## JSON and CLI

Add a `type:materialx` entry to the existing `outputs` object:

```json
"outputs": {
  "glass-color.png": "color",
  "glass-roughness.png": {"node":"roughness", "bits":16},
  "glass-normal.png": {"node":"normal", "bits":16},
  "glass.mtlx": {
    "type": "materialx",
    "name": "Glass",
    "inputs": {
      "base": 0,
      "base_color": {"texture":"glass-color.png"},
      "specular_roughness": {"texture":"glass-roughness.png"},
      "specular_IOR": 1.5,
      "transmission": 1,
      "transmission_color": "#f5fcfa",
      "thin_walled": true
    },
    "normal": {
      "texture": "glass-normal.png",
      "convention": "directx",
      "scale": 1
    }
  }
}
```

Texture bindings name **exported filenames**, including extensions, not graph node
IDs. They must refer to ordinary PNG outputs in the same graph. Sheet images,
PFM/PPM/PGM, other material outputs and arbitrary external files are rejected.
Use an `image` node plus a PNG output when bringing an external texture into a bundle.

```sh
./build/texutil samples/glass.json --out out/glass
./build/texutil validate samples/glass.json
./build/texutil materialx --json
```

The last command lists all 42 supported Standard Surface input names, types and
defaults. A checked-in copy is [materialx-inputs.json](materialx-inputs.json).
Agents can query it before constructing a material, without needing to guess names.

## Document version and host compatibility

The MaterialX output accepts `"version":"1.38"` (default) or `"version":"1.39"`.
This is separate from the root TexUtil graph version and the Standard Surface
shader version. Choose 1.38 for a 1.38 host such as Maya 2026; choose 1.39 for Maya
2027 / LookdevX 2.0 to avoid a legacy-document upgrade prompt.

```json
"potion-1.39.mtlx": {
  "type":"materialx", "version":"1.39", "name":"Potion",
  "inputs":{"base_color":{"texture":"potion-color.png"}, "metalness":0}
}
```

Both formats use the same portable PNG references and Standard Surface shader.
The normal helper omits the old `space` input: tangent space is the 1.38 default,
while 1.39 removed that parameter. A 1.38 SDK cannot validate a 1.39 document;
use an SDK at least as new as the chosen format.

## Arbitrary Standard Surface inputs

`inputs` accepts **every input of Standard Surface 1.0.1**, not just a small preset
of PBR map slots. Use its exact case-sensitive names, such as `specular_IOR`,
`transmission_depth`, `coat_IOR`, `subsurface_radius`, `emission_color` or `opacity`.

- `float`: a finite JSON number, or `{"texture":"scalar-output.png"}`.
- `color3`: an opaque hex color, three linear RGB numbers, or a texture binding.
- `vector3`: three raw numeric components or a raw RGB texture binding.
- `boolean`: `true` or `false`; boolean texture bindings are not supported.

Hex colors are decoded from sRGB to linear Rec.709. Three-component arrays are
already linear, which also lets you supply HDR values or quantities such as
subsurface radii. Numeric parameters are type-checked for finiteness but are not
clamped to the shader's UI slider ranges. Omitted inputs use the standard shader's
defaults. Unknown input names are errors so typos do not silently change a material.

For example, additional inputs for a magical liquid can be supplied directly:

```json
"inputs": {
  "specular_IOR": 1.33,
  "transmission": 0.92,
  "transmission_color": {"texture":"potion-color.png"},
  "transmission_depth": 0.1,
  "emission": 0.15,
  "emission_color": {"texture":"potion-emission.png"},
  "coat": 0.1,
  "coat_roughness": 0.06
}
```

This release targets one shader: `shader` may be omitted or set to
`"standard_surface"`. OpenPBR, renderer-specific shaders, custom nodedefs and arbitrary
MaterialX shader graphs are not exported yet. TexUtil's procedural graph is baked;
the resulting MaterialX texture connections and shader parameters remain editable.

## Normal and displacement helpers

`normal` is optional. It reads encoded tangent-space RGB through a standard
`normalmap` node. `convention` defaults to `directx`, which inserts `G = 1 - G`
before decoding; `opengl` skips the flip. `scale` defaults to 1 and adjusts the
already-baked normal's strength. It is not the height-to-normal node's UV strength.
The helper cannot be combined with `inputs.normal`. Direct `inputs.normal` and
`inputs.coat_normal` are advanced world-space vector inputs, not encoded normal maps.

Optional displacement:

```json
"displacement": {
  "texture": "height.png",
  "midlevel": 0.5,
  "scale": 0.035
}
```

This binds a standard displacement shader with `(height - midlevel) * scale`.
Defaults are midlevel 0.5 and scale 0.01. Scale is in scene units, so tune it to the
mesh and UV scale. Renderer subdivision and displacement settings still apply.
If both normal and displacement are enabled, tune their contributions to avoid
exaggerating the same detail twice; remove either helper when appropriate.

## Packaging, color and validation

The output name must be relative to `--out`; `.mtlx` is appended if omitted.
`name` defaults to `Material` and must be an ASCII identifier with at most 128
characters: letters, digits and underscores, starting with a letter or underscore.
By default, texture paths inside the XML are relative to the `.mtlx` file, including when it is
in a subdirectory. Copy the material file **and its referenced PNGs**, preserving
their relative locations. Paths are XML-escaped. No standard-library files are
copied into the bundle; the host supplies its MaterialX standard libraries.

Set `"texture_paths":"absolute"` on a MaterialX output for hosts that do not resolve
relative image filenames against the document, including the tested Maya 2027
Arnold `aiMaterialXShader` workflow. The exporter resolves each texture against
`--out` and writes its full disk path. Texture bindings in the recipe still name
exported PNG files. The default is `"texture_paths":"relative"` for portable bundles.
Regenerate absolute-path exports after moving the output folder or copying it to
another computer.

The document working space is `lin_rec709`. Color image nodes use `srgb_texture`
when the actual color output was sRGB-encoded, and `lin_rec709` for linear exports.
Scalar and normal/vector maps stay raw. Float inputs require scalar texture nodes;
normal/vector textures require raw RGB. Alpha is not implicitly connected to opacity
or transmission. Supply those inputs explicitly. Addressing is `periodic` for
`tile:true` and `clamp` otherwise; this does not make a nonperiodic source seamless.

Unknown fields, bad values, missing references, invalid output types and path
collisions fail graph validation before rendering. Pixel-kind checks run as the
referenced images are produced; those errors can leave earlier image exports on
disk. Material documents are written only after all image exports succeed. They
add no float image buffers or extra procedural-node evaluations.

`--json` statistics list materials in `files` and `output_details` with
`type:"materialx"`, `format:"mtlx"` and `shader:"standard_surface"`; they have no
pixel dimensions or bit depth. For C++ callers, a material invokes the existing
render sink with a null image, `Output.material` set and XML in `Output.text`.

## Maya and preview rendering

Use LookdevX's **Load MaterialX** or **Import MaterialX** document command,
then assign the material to geometry. Loading retains the external document and
its relative texture-path anchor. Importing embeds a copy; the tested Maya builds
converted the texture paths to absolute paths. Keep the PNGs beside the exported
material when moving the bundle. Reimport or reload after regenerating exports;
an embedded copy is not automatically refreshed from the source file.

For Hypershade's Arnold **aiMaterialXShader**, use `potion-arnold.mtlx` in
**MaterialX Filename** and select **Potion** under **Materials**. This sample uses
version 1.39 and absolute texture paths. Its shader graph stays inside the MaterialX
document, so separate Maya file nodes are not expected in Hypershade.

The potion sample also exports portable `potion.mtlx` (1.38) and `potion-1.39.mtlx` (1.39).
Its five image nodes reference body color, emission color, roughness, transmission
tint and normal. Base and emission weights are zero by default, so those two
connected color maps do not contribute until their lobes are enabled. Height is
exported separately but is not connected as displacement. These are recipe choices,
not missing texture references.

An isolated Maya 2027 / LookdevX 2.0 test successfully loaded and imported the 1.39
potion document without the previous normal-input or legacy-version errors. Both
routes exposed all five expected texture paths. SDK validation covers 1.38.10 and
1.39.4, including GLSL generation. The user also confirmed the absolute-path Arnold
export renders as a red transmissive material in Maya 2027's Hypershade shader-ball
preview. A bottle scene render has not been verified. Renderer support for displacement, volumes and individual shader
features can differ. A material sphere renderer is a separate future feature.

macOS Preview is an image/PDF viewer, not a MaterialX shader host. Open the generated
`potion-sheet.png` there to inspect maps; use a MaterialX-capable DCC for `.mtlx`.
Material files do not embed their PNGs or a rendered preview.

Stone, wood, snow, cork, glass and potion samples now emit matching `.mtlx` files.

Optional developer verification, using a Python environment with a sufficiently recent MaterialX SDK installed (1.39+ for the complete potion folder):

```sh
python tools/validate_materialx.py out/glass out/potion
```

The helper checks XML/standard-library validity, resolves texture paths and generates
GLSL source. It does not create a graphics context, compile GPU shaders or render.

Sources: [Standard Surface definition](https://github.com/AcademySoftwareFoundation/MaterialX/blob/v1.38.10/libraries/bxdf/standard_surface.mtlx),
[standard node definitions](https://github.com/AcademySoftwareFoundation/MaterialX/blob/v1.38.10/libraries/stdlib/stdlib_defs.mtlx),
[Maya LookdevX introduction](https://help.autodesk.com/view/MAYAUL/2026/ENU/?guid=GUID-139850AD-F5B8-4F7C-87A5-214C787423FC).

Host references: [Autodesk import versus load](https://help.autodesk.com/cloudhelp/2026/CHS/LookdevX/files/LookdevX_Developer_Examples/MaterialX/BBC7E488-F3AE-4699-BB0E-B9030EF9B07F.html), [Maya 2027 MaterialX update](https://help.autodesk.com/view/MAYAUL/2027/ENU/?guid=GUID-6DA3C115-C004-48DB-9D84-90D19E42E358).
