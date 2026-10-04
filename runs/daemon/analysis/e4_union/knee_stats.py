#!/usr/bin/env python3
"""Knee-band test: per-L lamp-core luma stats (master = all-ON frame, so the
site 3x3 max at confirmed sites is the lamp core at THIS L and exposure).
Compare against the S14R-0002 probe band (core P90 235-250) and against the
confirmed/union counts."""
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

OUTD = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/'
            'analysis/e4_union')
raw = json.loads((OUTD / 'e4_union_raw.json').read_text())
TAGS = ['r8', 'r9', 'r10', 'r11']
LS = {'r8': 80, 'r9': 100, 'r10': 120, 'r11': 150}
out = {}
for t in TAGS:
    rep = OUTD / 'repaired' / raw[t]['run']
    ml = np.asarray(Image.open(rep / f"cwc_{t}_master.jpg").convert('L'),
                    np.float32)
    cores = []
    for q in raw[t]['leds']:
        x, y = q['cx'], q['cy']
        cores.append(float(ml[max(0, y-1):y+2, max(0, x-1):x+2].max()))
    cores = np.array(cores)
    # wall percentile on the master (background)
    p90 = float(np.percentile(cores, 90))
    clip = float((cores >= 254).mean())
    row = {'L': LS[t], 'coreP90': round(p90, 1),
           'coreMed': round(float(np.median(cores)), 1),
           'clipFrac': round(clip, 4), 'confirmed': len(cores)}
    out[t] = row
    print(f"L={LS[t]:3d}: coreP90 {p90:6.1f} coreMed {np.median(cores):5.1f} "
          f"clipFrac {clip:.3f} confirmed {len(cores)}")
(OUTD / 'knee_stats.json').write_text(json.dumps(out, indent=1))