"""(b) E4 union-plane/master ratio with registration-corrected plane sampling
(matching how the decoder itself reads planes), using v2 decode shifts."""
import io
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
D = REPO / 'runs/daemon/analysis/photometry_v2'

for run, tag, L in [('run8', 'r8', 80), ('run9', 'r9', 100),
                    ('run10', 'r10', 120), ('run11', 'r11', 150)]:
    dec = json.load(open(D / f'{run}_ledpos_v2.json'))
    leds = dec['leds']
    shifts = {int(k): v for k, v in dec['shifts'].items()}
    mast = np.asarray(Image.open(REPO / f'runs/daemon/runs/{run}/cwc_{tag}_master.jpg')
                      .convert('RGB'), dtype=np.float32).max(axis=2)
    H, W = mast.shape
    umax = None
    for p in range(24):
        dx, dy = shifts[p][0], shifts[p][1]
        pl = np.asarray(Image.open(REPO / f'runs/daemon/runs/{run}/cwc_{tag}_p{p:02d}.jpg')
                        .convert('RGB'), dtype=np.float32).max(axis=2)
        mx, my = np.meshgrid(np.arange(W, dtype=np.float32) + np.float32(dx),
                             np.arange(H, dtype=np.float32) + np.float32(dy))
        sh = cv2.remap(pl, mx, my, cv2.INTER_LINEAR,
                       borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        m2 = cv2.dilate(sh, np.ones((3, 3), np.float32))
        umax = m2 if umax is None else np.maximum(umax, m2)
    r = []
    for q in leds:
        cy, cx = q['cy'], q['cx']
        m3 = float(mast[cy - 1:cy + 2, cx - 1:cx + 2].max())
        if m3 < 250:
            u3 = float(umax[cy - 1:cy + 2, cx - 1:cx + 2].max())
            r.append(u3 / max(m3, 1.0))
    r = np.array(r)
    print(f'{tag} L={L}: n_unclipped {len(r)} ratio med {np.median(r):.3f} '
          f'(p10 {np.percentile(r, 10):.3f} p90 {np.percentile(r, 90):.3f})')