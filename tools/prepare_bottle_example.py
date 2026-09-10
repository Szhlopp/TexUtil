#!/usr/bin/env python3
"""Prepare the supplied T3 potion Bottle.obj for the checked-in bottle recipes."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--texutil', type=Path, default=Path('build/texutil'))
    parser.add_argument('--out', type=Path, default=Path('out/bottle'))
    args = parser.parse_args()
    source = args.source.resolve()
    out = args.out.resolve()
    intermediate = out / 'bottle-materials.obj'
    atlas = out / 'bottle-final-atlas.obj'
    for path in [intermediate, intermediate.with_suffix('.mtl'), atlas, atlas.with_suffix('.mtl')]:
        if path.exists():
            parser.error(f'Prepared output already exists: {path}; choose a fresh --out directory')
    lines = ['# User Bottle.obj working copy. Centimeters converted to meters.', 'mtllib bottle-materials.mtl']
    positions, seen, counts = [], set(), Counter()
    current, removed = 'glass', 0
    for line in source.read_text().splitlines():
        if line.startswith(('#', 'mtllib ', 'usemtl ')):
            continue
        if line.startswith('v '):
            coords = tuple(float(v) * .01 for v in line.split()[1:])
            positions.append(coords)
            line = 'v ' + ' '.join(format(v, '.9g') for v in coords)
        if line.startswith('g '):
            if 'T3_Cork' in line:
                current = 'cork'
            elif 'T3_Collar_Jewel' in line:
                current = 'emerald'
            elif 'T3_Bronze_Collar' in line:
                current = 'bronze'
            elif 'T3_Trim' in line:
                current = 'gold'
            elif 'T3_Bubble' in line:
                current = 'bubble'
            elif 'T3_Liquid' in line:
                current = 'brew'
            else:
                current = 'glass'
            lines.extend([line, 'usemtl ' + current])
            continue
        if line.startswith('f '):
            key = (current, tuple(sorted(positions[int(v.split('/')[0]) - 1] for v in line.split()[1:])))
            if key in seen:
                removed += 1
                continue
            seen.add(key)
            counts[current] += 1
        lines.append(line)
    required = {'glass', 'brew', 'bubble', 'cork', 'bronze', 'gold', 'emerald'}
    if not required.issubset(counts):
        parser.error('This script requires the supplied Bottle.obj with its T3_* group names')
    out.mkdir(parents=True, exist_ok=True)
    intermediate.write_text('\n'.join(lines) + '\n')
    intermediate.with_suffix('.mtl').write_text('\n'.join('newmtl ' + name + '\nKd 0.5 0.5 0.5\n' for name in counts))
    command = [str(args.texutil.resolve()), 'model', 'uv', str(intermediate), '--out', str(atlas), '--size', '2048', '--padding', '12', '--json']
    report = json.loads(subprocess.check_output(command, text=True))
    names = report['materials']
    # The UV exporter uses safe numbered tokens; restore these known simple names.
    for path in [atlas, atlas.with_suffix('.mtl')]:
        text = re.sub(r'(?m)^(usemtl|newmtl) material_(\d+)$', lambda m: m[1] + ' ' + names[int(m[2])], path.read_text())
        path.write_text(text)
    report.update(source=str(source), duplicate_faces_removed=removed, faces_by_material=dict(counts))
    (out / 'preparation-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
