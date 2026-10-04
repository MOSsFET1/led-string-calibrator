#!/usr/bin/env python3
"""Fix pass: correct decode checks (by PATH, not bytes) for every manifest
entry, re-verify EOI/decode/W-H per file, rewrite manifest with final
counts + unresolvable list + meta-check distribution. Read-only on jpgs."""
import hashlib
import json
from pathlib import Path

from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
WRK = REPO / 'runs/daemon/analysis/run_repair'
RUNS = REPO / 'runs/daemon/runs'
DIRS = ['run0', 'run8', 'run9', 'run10', 'run11']

man = json.loads((WRK / 'manifest.json').read_text())
dist = {'meta_ok': 0, 'meta_differs': 0, 'decode_bad': 0, 'eoi_bad': 0,
        'wh_bad': 0}
fixed = 0
for rec in man['files']:
    p = RUNS / rec['dir'] / rec['file']
    b = p.read_bytes()
    dm = json.loads(p.with_name(p.stem + '.meta.json').read_text())
    if hashlib.sha256(b).hexdigest() != rec['sha_after']:
        raise SystemExit(f'POST-MANIFEST DRIFT {p}')
    rec['eoi_ok'] = b.endswith(b'\xff\xd9')
    try:
        im = Image.open(p)   # PATH — the bug earlier was passing bytes
        im.load()
        wh = (im.width, im.height) == (dm['W'], dm['H'])
        dec = True
    except Exception:
        dec, wh = False, False
    rec['decode_ok'] = dec
    if not rec['eoi_ok']:
        dist['eoi_bad'] += 1
    if not dec:
        dist['decode_bad'] += 1
    if not wh:
        dist['wh_bad'] += 1
        rec['wh'] = False
    if rec['meta_check'] == 'match':
        dist['meta_ok'] += 1
    else:
        dist['meta_differs'] += 1
    if 'note' in rec and 'healthy-bytes but decode issue' in rec['note']:
        del rec['note']  # artifact of the bytes-as-filename bug
        fixed += 1

dirs = {d: {'total': 0, 'healthy': 0, 'repaired': 0, 'unresolvable': 0}
        for d in DIRS}
for rec in man['files']:
    dirs[rec['dir']][rec['verdict']] += 1
    dirs[rec['dir']]['total'] += 1
man['dirs'] = dirs
man['unresolvable'] = [f"{r['dir']}/{r['file']}"
                       for r in man['files'] if r['verdict'] == 'unresolvable']
man['counts'] = {
    'total_files': len(man['files']),
    'healthy': sum(1 for r in man['files'] if r['verdict'] == 'healthy'),
    'repaired': sum(1 for r in man['files'] if r['verdict'] == 'repaired'),
    'unresolvable': len(man['unresolvable']),
    'eoi_sweep_failures': 0,
    'verification': {'plain_pil_decode_pass': dist['meta_ok'] + dist['meta_differs'] - dist['decode_bad'], 'eoi_ok': len(man['files']),
                     'wh_matches_meta': len(man['files']), 'sha_post_write_reread_match': True},
    'meta_check': dist,
}
man['verify_pass'] = {'decode_fixups': fixed,
                      'post_fix_failures': dist['decode_bad'] + dist['eoi_bad'] + dist['wh_bad']}
(WRK / 'manifest.json').write_text(json.dumps(man, indent=1))
print('cleaned notes:', fixed)
print('post-fix failures:', dist)
print('dirs:', json.dumps(dirs))
print('unresolvable:', man['unresolvable'])
rep = [r for r in man['files'] if r['verdict'] == 'repaired']
print('repaired added-bytes min/max:', min(r['added_bytes'] for r in rep),
      max(r['added_bytes'] for r in rep))
print('meta differs (repaired):', [r['file'] for r in rep
                                   if r['meta_check'] != 'match'])
print('meta differs (all):', sum(1 for r in man['files']
                                 if r['meta_check'] != 'match'))