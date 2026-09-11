# Leaf sprite variations

`leaf.json` is a stylized procedural leaf stencil with seeded pigment and outline
distortion. Its diagonal pattern demonstrates aligned color/normal detail; it is
not a botanical branching-vein simulation.

`leaves.json` renders twelve variants into matching color, normal and packed
metallic/roughness atlases. Run from the TexUtil root:

```sh
./build/texutil samples/spritesheets/leaves.json --out out/leaf-atlas --threads 8 --json
```

See [SPRITESHEETS.md](../../docs/SPRITESHEETS.md) for grid, padding, seeds,
encoding and FoliageUtil integration.
