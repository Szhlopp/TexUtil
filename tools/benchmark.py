#!/usr/bin/env python3
"""Measure end-to-end renders; preserve raw statistics and report median runs."""
import json
import pathlib
import statistics
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
binary = root / 'build/texutil'
results = []
for example in ['stone', 'ornament', 'tiles']:
    for size in [512, 2048]:
        for threads in [1, 8]:
            runs = []
            for repeat in range(3):
                command = [str(binary), str(root / 'examples' / (example + '.json')), '--out', str(root / 'out/benchmark' / example), '--size', str(size), '--threads', str(threads), '--json']
                runs.append(json.loads(subprocess.check_output(command)))
            record = {'example': example, 'size': size, 'threads': threads,
                      'median_ms': statistics.median(r['total_ms'] for r in runs),
                      'median_export_ms': statistics.median(r['export_ms'] for r in runs),
                      'peak_buffer_mb': max(r['peak_buffer_mb'] for r in runs), 'runs': runs}
            results.append(record)
            print(example, size, threads, round(record['median_ms'], 1), 'ms', flush=True)
(root / 'out/benchmark.json').write_text(json.dumps(results, indent=2) + '\n')
