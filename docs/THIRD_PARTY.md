# Third-party dependencies

Dependencies are downloaded into the build tree or supplied by the platform.
Their original license files must accompany redistribution where required.

| Dependency | Pin / source | License |
| --- | --- | --- |
| FastNoise2 | 1.1.1, commit `903c1f2d2f9d53ddce94cd223f32727d9ab3aeaa` | MIT |
| FastSIMD | Commit `16450dae9528727e500e7254f635a671f9c7ee2d`, selected by FastNoise2 | MIT |
| JSON for Modern C++ | 3.12.0 archive, SHA-256 in CMakeLists.txt | MIT |
| libpng | Installed development package (1.6.58 on development machine) | libpng license |
| zlib | libpng dependency from platform | zlib license |
| MaterialX input metadata / optional validation SDK | Standard Surface 1.0.1 in MaterialX 1.38.10; no runtime linkage | Apache-2.0 |

Project sources and license texts:

- [FastNoise2](https://github.com/Auburn/FastNoise2/tree/903c1f2d2f9d53ddce94cd223f32727d9ab3aeaa)
- [FastSIMD](https://github.com/Auburn/FastSIMD/tree/16450dae9528727e500e7254f635a671f9c7ee2d)
- [nlohmann/json](https://github.com/nlohmann/json/tree/v3.12.0)
- [libpng license](http://www.libpng.org/pub/png/src/libpng-LICENSE.txt)
- [zlib license](https://zlib.net/zlib_license.html)

MaterialX input names, types and defaults in `src/materialx_inputs.inc` and
`docs/materialx-inputs.json` follow the [upstream Standard Surface definition](https://github.com/AcademySoftwareFoundation/MaterialX/blob/v1.38.10/libraries/bxdf/standard_surface.mtlx).
The exporter and XML writer are TexUtil code. The optional developer validation
script uses the upstream SDK; distributing TexUtil does not require that SDK.
The upstream [MaterialX license](licenses/MaterialX.txt) is retained with this metadata.

TexUtil's own source uses the MIT license in [LICENSE](../LICENSE).

## Model operations and optional previews

Pinned source files and SHA-256 hashes are recorded in `cmake/Models.cmake`.
License notices are retained under `docs/licenses/`.

| Dependency | Pin | Selected license |
| --- | --- | --- |
| [ufbx](https://github.com/ufbx/ufbx) | `fcc5d6ba444cfd3eb80677dba5e37e493941abe5` | MIT option |
| [xatlas](https://github.com/jpcy/xatlas) | `f700c7790aaa030e794b52ba7791a05c085faf0c` | MIT |
| [cgltf](https://github.com/jkuhlmann/cgltf) | `85cd62382dfea638278962690cf515023f33ed00` | MIT |
| [TinyBVH](https://github.com/jbikker/tinybvh) | `0e4584287823252cf83f0e9cd072848bec5f79c5` | MIT |
| [Filament](https://github.com/google/filament/tree/v1.76.1) | 1.76.1 official desktop SDK; optional | Apache-2.0 |
| stb_image HDR decoder | Copy shipped in Filament 1.76.1 | MIT option |

Curvature evaluation, baking integration and projection logic are TexUtil code.
Neither libigl nor Open3D is included. The two bundled HDRIs are CC0 Poly Haven
assets; their [provenance](../assets/hdri/README.md) is included. The demo torus and
its format-converted test fixtures are original project assets under MIT.

The optional Filament SDK incorporates other components, including Abseil
(Apache-2.0), MikkTSpace (zlib-style), smol-v (MIT option) and Zstandard (BSD option).
Relevant notices are retained as `Filament-*-LICENSE.txt`; shader compiler notices
from the pinned SDK's `matc --license` are in `Filament-shader-tools-NOTICES.txt`.
TexUtil does not redistribute the SDK's sample applications or link its Assimp,
Draco, Basis Universal, WebGPU/Dawn or SDL libraries. If distributing those tools
or choosing additional SDK components, include their applicable notices too.

The selected licenses permit proprietary applications; preserve copyright,
license and attribution notices where required. Installing TexUtil copies the
retained notices beside its shared assets. Also include notices for the actual
libpng/zlib binaries supplied with any packaged executable.
