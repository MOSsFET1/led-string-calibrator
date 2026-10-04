#!/usr/bin/env python3
"""Independent final validation: every jpg in the 5 dirs, fresh logic."""
import hashlib
import json
from pathlib import Path

from PIL import Image

RUNS = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/runs')
WRK = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/analysis/run_repair')
DIRS = ['run0', 'run8', 'run9', 'run10', 'run11']
man = json.loads((WRK / 'manifest.json').read_text())
mf = {(r['dir'], r['file']): r for r in man['files']}
fails = []
tot = 0
per = {}
for d in DIRS:
    c = {'h': 0, 'r': 0, 'u': 0, 't': 0}
    for jpg in sorted((RUNS / d).glob('*.jpg')):
        b = jpg.read_bytes()
        m = json.loads(jpg.with_name(jpg.stem + '.meta.json').read_text())
        rec = mf.get((d, jpg.name))
        if rec is None:
            fails.append((d, jpg.name, 'no-manifest-entry'))
            continue
        v = rec['verdict']
        if hashlib.sha256(b).hexdigest() != rec['sha_after']:
            fails.append((d, jpg.name, 'sha-drift'))
        if not b.endswith(b'\xff\xd9'):
            fails.append((d, jpg.name, 'no-EOI'))
        try:
            im = Image.open(jpg)
            im.load()
            if (im.width, im.height) != (m['W'], m['H']):
                fails.append((d, jpg.name, 'W/H-mismatch'))
        except Exception as e:
            fails.append((d, jpg.name, 'decode:' + str(e)[:40]))
        c[v[0]] = c.get(v[0], 0) + 1
        c['t'] += 1
        tot += 1
    per[d] = c
print('independent sweep over', tot, 'files')
print('per-dir verdicts (healthy/repaired/unresolvable/total):',
      {d: f"{v['h']}/{v['r']}/{v['u']}/{v['t']}" for d, v in per.items()})
print('FAILURES:', len(fails), fails)
print('manifest counts block:', json.dumps(man['counts']))
print('manifest files:', len(man['files']),
      'verdicts:', {v: sum(1 for r in man['files'] if r['verdict'] == v)
                    for v in ('healthy', 'repaired', 'unresolvable')})
tot_a = sum(r['added_bytes'] for r in man['files'] if r['verdict'] == 'repaired')
print('bytes restored total:', tot_a)
print('capture sha256:', man['capture']['sha256'][:16],
      'size', man['capture']['size'])
print('incomplete groups noted:', len(man['capture_incomplete_groups']))
print('OK' if not fails and tot == 528 else 'PROBLEM')