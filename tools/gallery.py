#!/usr/bin/env python3
"""Render new nodes, all presets, and erosion comparisons. Requires Pillow for sheets."""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, default=ROOT / 'build/texutil')
p.add_argument('--size', type=int, default=512)
p.add_argument('--threads', type=int, default=8)
p.add_argument('--only', choices=['nodes-gallery', 'presets-gallery', 'erosion', 'effects-gallery', 'forest'])
args = p.parse_args()
font = ImageFont.load_default()
for name in ([args.only] if args.only else ['nodes-gallery', 'presets-gallery', 'erosion', 'effects-gallery', 'forest']):
    document = ROOT / 'examples' / (name + '.json')
    destination = ROOT / 'out' / name
    destination.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(args.binary.resolve()), str(document), '--out', str(destination), '--size', str(args.size), '--threads', str(args.threads), '--json'], check=True, text=True, capture_output=True)
    stats = json.loads(result.stdout)
    (destination / 'stats.json').write_text(json.dumps(stats, indent=2) + '\n')
    names = [f for f in json.loads(document.read_text())['outputs'] if f.endswith('.png')]
    if name == 'erosion': names = [mode + suffix + '.png' for suffix in ['', '-normal', '-color'] for mode in ['before', 'wind', 'rain', 'time']]
    if name == 'forest': names = [stage + suffix + '.png' for suffix in ['', '-normal', '-forest'] for stage in ['before', 'after-rain', 'after-wind', 'after-time']]
    columns = 4 if name != 'presets-gallery' else 6
    thumb, label, margin = 224, 30, 12
    sheet = Image.new('RGB', (columns * (thumb + margin) + margin, ((len(names) + columns - 1) // columns) * (thumb + label + margin) + margin), '#171b20')
    draw = ImageDraw.Draw(sheet)
    for i, filename in enumerate(names):
        x, y = margin + (i % columns) * (thumb + margin), margin + (i // columns) * (thumb + label + margin)
        with Image.open(destination / filename) as image:
            if image.mode in ('I', 'I;16', 'I;16B', 'I;16L'):
                image = image.point(lambda value: value / 257).convert('L')
            if 'A' in image.getbands():
                background = Image.new('RGBA', image.size, '#0b1018')
                image = Image.alpha_composite(background, image.convert('RGBA'))
            image = image.convert('RGB').resize((thumb, thumb), Image.Resampling.LANCZOS)
            sheet.paste(image, (x, y))
        draw.text((x + 4, y + thumb + 9), Path(filename).stem.replace('-', ' '), fill='#ebeff4', font=font)
    sheet.save(destination / 'gallery.png')
    print(f'{name}: {len(names)} PNGs, {stats["total_ms"]:.0f} ms, {stats["peak_buffer_mb"]:.1f} MiB float buffers; {destination / "gallery.png"}')
