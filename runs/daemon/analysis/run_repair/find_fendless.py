#!/usr/bin/env python3
"""Find FRAME groups lacking FEND (incomplete / PHONE-LOG-cut)."""
import json
from pathlib import Path

CAP = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/capture.txt')
cur_label, cur_n, start_ln = None, 0, 0
bad = []
with open(CAP, 'rb') as f:
    for ln, raw in enumerate(f, 1):
        line = raw.decode('utf-8', errors='replace')
        if '] [PHONE] FRAME ' in line:
            if cur_label is not None:
                bad.append((cur_label, start_ln, cur_n))
            j = line.split('] [PHONE] FRAME ', 1)[1]
            try:
                meta = json.loads(j[:j.rfind('}') + 1])
                cur_label = meta.get('label', '?')
            except Exception:
                cur_label = '?'
            start_ln, cur_n = ln, 0
        elif cur_label is not None and '] [PHONE] FJPEG ' in line:
            cur_n += 1
        elif cur_label is not None and '] [PHONE] FEND' in line:
            cur_label = None
if cur_label is not None:
    bad.append((cur_label, start_ln, cur_n))
print('FEND-less groups:', bad)