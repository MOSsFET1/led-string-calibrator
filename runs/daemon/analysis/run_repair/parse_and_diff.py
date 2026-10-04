#!/usr/bin/env python3
"""In-place wire repair for run0 + run8..run11 jpgs (PASS 1).

Parses ALL complete FRAME/FJPEG/FEND groups in capture.txt into a
label -> list of (group_ref, payload_bytes, meta_json) map, dropping
incomplete/FEND-less trailing groups (logged). Writes capture_groups.json
(index + stats only; bytes stay in the kernel) and a disk-vs-wire diff
report for every jpg target. No writes to capture.txt; no jpg writes yet.
"""
import base64
import hashlib
import json
import pickle
import sys
from pathlib import Path

CAP = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/capture.txt')
WRK = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/analysis/run_repair')

TARGET_LABELS = None  # set below (labels appearing in run meta files, or None=all)
ALL = '--all' in sys.argv
if not ALL:
    labs = set()
    import re
    for d in ('run0', 'run8', 'run9', 'run10', 'run11'):
        for m in Path(f'/home/nellie/projects/led-display/poc_survey/runs/daemon/runs/{d}').glob('*.meta.json'):
            labs.add(json.loads(m.read_text())['label'])
    TARGET_LABELS = labs
    print(f'filtering wire parse to {len(labs)} target labels of the final battery')

groups = {}      # label -> [(grp_ref, payload_bytes, meta_dict)]
incomplete = []  # (ref, meta_start, n_fjpeg)
cur = None
with open(CAP, 'rb') as f:
    for ln, raw in enumerate(f, 1):
        line = raw.decode('utf-8', errors='replace')
        if '] [PHONE] FRAME ' in line:
            if cur is not None:
                incomplete.append(cur['ref'])
            j = line.split('] [PHONE] FRAME ', 1)[1]
            try:
                meta = json.loads(j[:j.rfind('}') + 1])
            except Exception:
                meta = {}
            cur = {'ref': f'capture.txt:{ln}', 'meta': meta, 'b64': [], 'ln': ln}
        elif cur is not None:
            if '] [PHONE] FJPEG ' in line:
                cur['b64'].append(line.split('] [PHONE] FJPEG ', 1)[1].strip())
            elif '] [PHONE] FEND' in line:
                payload = base64.b64decode(''.join(cur['b64']))
                lab = cur['meta'].get('label', '?')
                if TARGET_LABELS is None or lab in TARGET_LABELS:
                    groups.setdefault(lab, []).append(
                        (cur['ref'], payload, cur['meta']))
                cur = None
if cur is not None:
    incomplete.append(cur['ref'])

print('wire labels kept:', len(groups))
tot = sum(len(v) for v in groups.items().__iter__().__next__.__self__.values()) \
    if False else sum(len(v) for v in groups.values())
print('complete groups kept:', tot)
print('incomplete groups dropped:', len(incomplete), incomplete)

# multi-group labels (wire duplicates)
dups = {k: len(v) for k, v in groups.items() if len(v) > 1}
print('labels with >1 complete group:', len(dups))
for k in sorted(dups):
    if ':idle' in k:
        print('   dup', k, dups[k])

# EOI sanity on wire groups
noeoi = 0
for lab, lst in groups.items():
    for ref, payload, meta in lst:
        if not payload.endswith(b'\xff\xd9'):
            noeoi += 1
            print('wire group missing EOI:', ref, lab, len(payload))
print('wire groups missing EOI:', noeoi)

with open(WRK / 'wire_groups.pkl', 'wb') as f:
    pickle.dump(groups, f, protocol=pickle.HIGHEST_PROTOCOL)

# stats file
stats = {lab: [{'ref': ref, 'size': len(p), 'meta': meta} for ref, p, meta in lst]
         for lab, lst in sorted(groups.items())}
(WRK / 'capture_groups.json').write_text(json.dumps(stats, indent=1))
print('wrote wire_groups.pkl / capture_groups.json')

# ---- PASS 2: disk vs wire for every jpg
RUNS = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/runs')
DIRS = ['run0', 'run8', 'run9', 'run10', 'run11']
diffs = {}
nomatch_label_files = []
for d in DIRS:
    mismatches = []
    ok = 0
    for jpg in sorted((RUNS / d).glob('*.jpg')):
        disk = jpg.read_bytes()
        meta_f = jpg.with_name(jpg.stem + '.meta.json')
        dm = json.loads(meta_f.read_text()) if meta_f.exists() else None
        lab = dm['label'] if dm else jpg.stem.replace('_', ':')
        cands = groups.get(lab, [])
        hit = [c for c in cands if c[1] == disk]
        if hit:
            ok += 1
            continue
        prefix = [c for c in cands if len(c[1]) > len(disk) and c[1].startswith(disk)]
        mismatches.append((jpg.name, lab, len(disk), len(prefix),
                           [p[0] for p in prefix][:3], [p[0] for p in cands][:3]))
    diffs[d] = mismatches
    print(f'{d}: exact-match {ok}, mismatch/unmatched {len(mismatches)}')
    for m in mismatches:
        print('   MIS', m[0], 'wire-pfx-cands', m[4], 'all-cands', m[5])

(WRK / 'pass1_diffs.json').write_text(json.dumps(diffs, indent=1))
print('done')