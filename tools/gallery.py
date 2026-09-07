#!/usr/bin/env python3
"""Render native TexUtil contact sheets and save timing reports. No Pillow required."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GRAPHS = ['nodes-gallery', 'presets-gallery', 'erosion', 'effects-gallery', 'forest', 'snow']
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, default=ROOT / 'build/texutil')
p.add_argument('--size', type=int, default=512)
p.add_argument('--threads', type=int, default=8)
p.add_argument('--only', choices=GRAPHS)
args = p.parse_args()
for name in ([args.only] if args.only else GRAPHS):
    destination = ROOT / 'out' / name
    destination.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(args.binary.resolve()), str(ROOT / 'examples' / (name + '.json')), '--out', str(destination), '--size', str(args.size), '--threads', str(args.threads), '--json'], check=True, text=True, capture_output=True)
    stats = json.loads(result.stdout)
    (destination / 'stats.json').write_text(json.dumps(stats, indent=2) + '\n')
    sheets = [item['file'] for item in stats['output_details'] if item['type'] == 'sheet']
    print(f'{name}: {len(stats["files"])} exports, {stats["total_ms"]:.0f} ms, {stats["peak_buffer_mb"]:.1f} MiB buffers; sheets: {", ".join(sheets)}')
