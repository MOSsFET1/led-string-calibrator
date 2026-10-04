#!/usr/bin/env python3
"""Post-repair label audit: for every formerly-truncated frame, the repaired
wire bytes must carry the LABELED plane's duty structure (ON sites brighter
than OFF sites vs master). Full 24-plane matrix for margin context."""
import json
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
OUTD = REPO / 'runs/daemon/analysis/e4_union'
codes = json.load(open(REPO / 'tools/codewords_12of24.json'))['codes']
raw = json.loads((OUTD / 'e4_union_raw.json').read_text())
bad = {'r8': [14, 22], 'r9': [22], 'r10': [5, 13, 21], 'r11': [4, 21]}
res = {}
for tag, planes_ in bad.items():
    run = raw[tag]['run']
    rep = OUTD / 'repaired' / run
    ml = np.asarray(Image.open(rep / f'cwc_{tag}_master.jpg').convert('L'),
                    np.float32)
    sites = raw[tag]['leds']
    ids = [q['led'] for q in sites]
    xs = [q['cx'] for q in sites]
    ys = [q['cy'] for q in sites]
    rows = {}
    for p in range(24):
        fr = np.asarray(Image.open(rep / f'cwc_{tag}_p{p:02d}.jpg')
                        .convert('L'), np.float32)
        v = np.array([float(np.mean(fr[max(0, y-1):y+2, max(0, x-1):x+2] -
                                    ml[max(0, y-1):y+2, max(0, x-1):x+2]))
                      for x, y in zip(xs, ys)])
        D = [float(v[[i for i, ii in enumerate(ids) if t in codes[ii]]].mean()
                   - v[[i for i, ii in enumerate(ids) if t not in codes[ii]]]
                   .mean()) for t in range(24)]
        rows[p] = (round(D[p], 1), int(np.argsort(D)[-1]),
                   round(float(np.sort(D)[-1]), 1))
    flagged = {p: rows[p] for p in planes_}
    # criterion: D[labeled] > 0 AND labeled is the strict argmax
    good = all(r[0] > 5 and r[1] == p for p, r in flagged.items())
    res[tag] = {'flagged': flagged, 'all_D_labeled':
                {p: rows[p][0] for p in range(24)}}
    print(tag, 'repaired frames:', {p: rows[p] for p in planes_},
          '=> LABEL-CORRECT' if good else '=> PROBLEM')
(OUTD / 'post_repair_label_audit.json').write_text(json.dumps(res, indent=1))