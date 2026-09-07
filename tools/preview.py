#!/usr/bin/env python3
"""Assemble existing TexUtil renders into a labeled QA contact sheet (Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw
root = Path(__file__).resolve().parents[1]
items = [
    ('Stone / color', 'stone/stone-albedo.png'), ('Stone / height', 'stone/stone-height.png'), ('Stone / normal', 'stone/stone-normal.png'),
    ('Radial ornament / color', 'ornament/ornament-color.png'), ('Radial ornament / height', 'ornament/ornament-height.png'), ('Radial ornament / normal', 'ornament/ornament-normal.png'),
    ('Grid / color', 'tiles/tiles-color.png'), ('Grid / height', 'tiles/tiles-height.png'), ('Grid / normal', 'tiles/tiles-normal.png'),
    ('Warp / color', 'warped/warped-color.png'), ('Warp / normal', 'warped/warped-normal.png'), ('Explicit stamps / alpha', 'stamps/stamps.png')
]
cell, label = 256, 32
sheet = Image.new('RGB', (3 * cell, 4 * (cell + label)), '#171b24')
draw = ImageDraw.Draw(sheet)
for i, (title, path) in enumerate(items):
    im = Image.open(root / 'out' / path)
    if im.mode.startswith('I'):
        im = im.point(lambda x: x / 257).convert('L')
    im.thumbnail((cell, cell))
    x, y = (i % 3) * cell, (i // 3) * (cell + label)
    if im.mode == 'RGBA':
        background = Image.new('RGB', im.size, '#d7dce4')
        background.paste(im, mask=im.getchannel('A')); im = background
    sheet.paste(im.convert('RGB'), (x + (cell - im.width) // 2, y + (cell - im.height) // 2))
    draw.text((x + 8, y + cell + 9), title, fill='#eeeeee')
sheet.save(root / 'out/preview.png')
print(root / 'out/preview.png')
