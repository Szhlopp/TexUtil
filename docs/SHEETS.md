# Native labeled contact-sheet outputs

Sheets are an output type, separate from image-processing nodes. Add a sheet beside
ordinary exports in the same document. Items reference **node names**, including
intermediates that do not otherwise need a file export.

```json
"outputs": {
  "color.png": "color",
  "height.png": {"node":"height", "bits":16},
  "normal.png": "normal",
  "preview.png": {
    "type": "sheet",
    "title": "Snow material",
    "columns": 3,
    "cell": 320,
    "items": [
      {"node":"color", "label":"Color"},
      {"node":"height", "label":"Height"},
      {"node":"normal", "label":"Normal / DirectX"}
    ]
  }
}
```

Render with the usual command. No additional switches, Python, Pillow, installed
font files, or external image application are needed:

```sh
./build/texutil samples/snow.json --out out/snow
```

## Output fields

| Field | Default | Meaning |
| --- | --- | --- |
| `type` | `image` | Set to `sheet` for a contact sheet. |
| `items` | required | Ordered array of 1..256 node names or `{node,label}` objects. |
| `columns` | 3 | 1..32 columns; final row can have empty cells. |
| `cell` | 256 | Thumbnail box size, or `[width,height]`, each 16..2048 pixels. |
| `title` | empty | Optional title above the grid. |
| `labels` | true | Show a caption under every cell. |
| `padding` | 12 | Grid gap and outer margin, 0..128 pixels. |
| `font_scale` | 2 | Caption bitmap-font scale, 1..8. Title uses one step larger. |
| `background` | `#171b20` | Opaque background, also used to flatten transparent thumbnails. |
| `text_color` | `#ebeff4` | Opaque title/caption color. |
| `format` | `png` | Sheets currently export PNG only. |
| `bits` | 8 | PNG 8 or 16 bits. |
| `alpha` | false | Must be false; sheets are flattened presentation images. |
| `srgb` | true | Must be true for consistent mixed color/data display. |

Labels default to the node name. Empty captions are allowed. Text supports printable
ASCII up to 128 characters. The built-in original 5x7 font renders lowercase as
uppercase; long captions/title are truncated to fit, with an ellipsis when space
permits. There is no automatic wrapping or Unicode/font-file support yet.

Sheet export defaults are independent of the document's alpha, format, bits and
sRGB defaults, which still govern ordinary outputs. Explicit incompatible sheet
settings fail validation. Canvas dimensions are calculated from the grid and text;
either dimension must not exceed 16384 pixels. CLI `--size` changes source texture
resolution, while `cell` controls the sheet's thumbnail resolution independently.

## Display and execution

- Thumbnails preserve source aspect ratio and center in their cells.
- Reductions use exact box-area averages; enlargements use bilinear sampling.
- Color filtering/compositing uses linear RGB and premultiplied alpha.
- Scalar heights and encoded normals display their raw 0..1 values. Their thumbnails
  are converted to linear equivalents before the sheet's sRGB encoding, preventing
  unintended gamma brightening. Normal components are averaged for the preview,
  not renormalized or changed in the exported normal map.
- Values outside 0..1 clip for data thumbnails. Color HDR values clip at PNG export.
  There is no automatic contrast normalization. Add `levels` or `auto_levels` to a
  separate preview node if a low-contrast heightfield needs a stronger display.

A node is computed once across sheet and individual outputs. Each matching thumbnail
is copied into the sheet as soon as its node finishes. Full-resolution images are
released according to their remaining graph consumers; they are not retained until
all sheet cells are ready. The canvas is exported and released after the final cell.
Repeated nodes, multiple sheets, and sheet-only output documents are supported.
Sheet canvases count against `--memory`; many simultaneous large sheets can still
consume substantial memory. The existing output path and input-overwrite checks apply.

`--json` render statistics include `output_details` with each output's filename,
type, actual dimensions, bit depth, and format. Sheet assembly/export is included
in `export_ms`, not in a synthetic node timing.

The existing node/preset/effects/erosion/forest galleries now declare native sheet
outputs. `tools/gallery.py` only batches renders and saves timing reports; it no
longer imports Pillow. The older optional `tools/preview.py` helper remains available
for its earlier examples.
