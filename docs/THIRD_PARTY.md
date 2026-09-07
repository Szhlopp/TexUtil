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

Project sources and license texts:

- [FastNoise2](https://github.com/Auburn/FastNoise2/tree/903c1f2d2f9d53ddce94cd223f32727d9ab3aeaa)
- [FastSIMD](https://github.com/Auburn/FastSIMD/tree/16450dae9528727e500e7254f635a671f9c7ee2d)
- [nlohmann/json](https://github.com/nlohmann/json/tree/v3.12.0)
- [libpng license](http://www.libpng.org/pub/png/src/libpng-LICENSE.txt)
- [zlib license](https://zlib.net/zlib_license.html)

This is an engineering dependency inventory. No license has been selected for
TexUtil's own source yet.
