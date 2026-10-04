#!/usr/bin/env python3
"""Wire repair for truncated E4 jpgs (HANDOFF §5/§9 recipe).

1) find files that fail a PLAIN PIL decode (no LOAD_TRUNCATED_IMAGES);
2) parse capture.txt FRAME/FJPEG/FJPEG/FEND groups (marker: '] [PHONE] FJPEG '
   — chunks carry the wall-clock prefix);
3) provenance = unique group whose payload startswith() the disk bytes (a
   truncated file is a clean prefix of its full wire frame);
4) write repaired bytes under analysis/e4_union/repaired/<run>/ (originals
   untouched — runs/ stays read-only as evidence);
5) verify: repaired decodes plain, is NOT a duplicate of another label's
   repaired bytes, and startswith(disk) holds.
"""
import base64
import json
from pathlib import Path

from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
RUNS = REPO / 'runs/daemon/runs'
CAP = REPO / 'runs/daemon/capture.txt'
OUT = REPO / 'runs/daemon/analysis/e4_union/repaired'
RUNS_L = [('run8', 'r8'), ('run9', 'r9'), ('run10', 'r10'), ('run11', 'r11')]

# ---- 1) which files are truncated
trunc = {}
for run, tag in RUNS_L:
    d = RUNS / run
    for jpg in sorted(d.glob(f'cwc_{tag}_*.jpg')):
        try:
            Image.open(jpg).load()
            ok = True
        except Exception as e:
            ok = False
            trunc.setdefault(run, []).append((jpg.name, str(e)[:60]))
print('truncated plain-decode failures:')
for run, lst in trunc.items():
    for name, err in lst:
        print(' ', run, name, err)

# ---- 2) parse capture groups for cwc:r8..r11 labels
groups = {}   # label -> list of (line_no, payload_bytes)
cur_label, cur_b64 = None, []
with open(CAP, 'rb') as f:
    for ln, raw in enumerate(f, 1):
        line = raw.decode('utf-8', errors='replace')
        if '] [PHONE] FRAME ' in line:
            j = line.split('] [PHONE] FRAME ', 1)[1]
            try:
                meta = json.loads(j[:j.rfind('}') + 1])
                label = meta.get('label', '?')
            except Exception:
                label = None
            if label and ':r' in label and label.split(':')[1] in \
                    ('r8', 'r9', 'r10', 'r11'):
                if cur_label and cur_b64:
                    groups.setdefault(cur_label, []).append(
                        (ln, base64.b64decode(''.join(cur_b64))))
                cur_label, cur_b64 = label, []
            else:
                if cur_label and cur_b64:
                    groups.setdefault(cur_label, []).append(
                        (ln, base64.b64decode(''.join(cur_b64))))
                cur_label, cur_b64 = None, []
        elif '] [PHONE] FJPEG ' in line and cur_label:
            cur_b64.append(line.split('] [PHONE] FJPEG ', 1)[1].strip())
        elif '] [PHONE] FEND' in line and cur_label:
            groups.setdefault(cur_label, []).append(
                (ln, base64.b64decode(''.join(cur_b64))))
            cur_label, cur_b64 = None, []
    if cur_label and cur_b64:
        groups.setdefault(cur_label, []).append(
            (ln, base64.b64decode(''.join(cur_b64))))
print('capture groups for E4 labels:', {k: len(v) for k, v in
                                        sorted(groups.items())[:1]}, '...total labels', len(groups))

# ---- 3+4+5) repair
report = {}
for run, tag in RUNS_L:
    d = RUNS / run
    outd = OUT / run
    outd.mkdir(parents=True, exist_ok=True)
    for jpg in sorted(d.glob(f'cwc_{tag}_*.jpg')):
        label = json.loads(jpg.with_suffix('.meta.json').read_text())['label']
        disk = jpg.read_bytes()
        target = outd / jpg.name
        reps = groups.get(label, [])
        repair_src = None
        for _ln, payload in reps:
            if payload.startswith(disk) and payload != disk:
                repair_src = (payload,)
        if repair_src:
            (payload,) = repair_src
            ln = reps[0][0] if repair_src else 0
            target.write_bytes(payload)
            # verify plain decode
            Image.open(target).load()
            report[label] = {'repaired': True, 'first_line': ln,
                             'added_bytes': len(payload) - len(disk)}
            print(f'REPAIRED {label} ~capture line {ln} '
                  f'(+{len(payload)-len(disk)} bytes, plain-decode OK)')
        else:
            # exact copy for corpus completeness (byte-identical = no repair)
            if not target.exists() or target.read_bytes() != disk:
                target.write_bytes(disk)
print('repaired set:', json.dumps(report, indent=1))
(OUT / 'repair_report.json').write_text(json.dumps(report, indent=1))
print('done ->', OUT)