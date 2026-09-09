# Standard preview lighting

These 1024x512 Radiance HDR panoramas are reduced versions of the user-supplied
4K Poly Haven assets:

- `studio.hdr`: [Studio Small 09](https://polyhaven.com/a/studio_small_09).
- `outdoor.hdr`: [Kloofendal 48d Partly Cloudy Puresky](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky).

Poly Haven releases its assets under [CC0](https://polyhaven.com/license), including
redistribution and commercial use. The source 4K files are not bundled. Custom
2:1 `.hdr` files can be selected directly in a preview output.

Conversion used Filament 1.76.1 `cmgen`, preserving HDR radiance:

```sh
cmgen --quiet --no-mirror --size=256 --type=equirect --format=hdr --extract=converted source.hdr
```

The resulting `source/skybox.hdr` was renamed to the corresponding standard name.
