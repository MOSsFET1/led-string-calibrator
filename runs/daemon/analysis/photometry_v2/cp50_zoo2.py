"""Zoo 2: v1 cP50 residual hunting at L5 (tgt 222), L100 (110), L179 (128)."""
import io
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
RUN0 = REPO / 'runs/daemon/runs/run0'
sys.path.insert(0, str(REPO / 'tools'))
sys.path.insert(0, str(REPO / 'runs/daemon/analysis/photometry_v2'))
import cwc_pos_decode as C  # noqa: E402
from census_lock import MASK, load, frame_path  # noqa: E402

R1 = {'L10': [4, 5, 11, 18], 'L150': [10], 'L179': [13], 'L20': [6, 14],
      'L5': [3, 10], 'L60': [14], 'L80': [10, 18]}

KER3 = np.ones((3, 3), np.float32)


def comps(mlum, thr, min_area, mask_mode):
    """mask_mode: 0 none, 1 touching, 2 inside"""
    f = mlum.astype(np.float32)
    bw = (mlum >= thr).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    peaks, core3 = [], []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < min_area:
            continue
        m = lab[y:y + h, x:x + w] == i
        if mask_mode == 1 and not (m & MASK[y:y + h, x:x + w]).any():
            continue
        if mask_mode == 2:
            lm = MASK[y:y + h, x:x + w]
            if not (m & lm).any():
                continue
            mm = m & lm
        else:
            mm = m
        v = f[y:y + h, x:x + w][mm]
        pk = float(v.max())
        peaks.append(pk)
        cy, cx = np.argwhere(mm & (f[y:y + h, x:x + w] == pk))[0]
        gy, gx = y + int(cy), x + int(cx)
        core3.append(float(mlum[max(0, gy - 1):gy + 2, max(0, gx - 1):gx + 2].max()))
    return peaks, core3


def pq_of(vals, q):
    v = np.sort(np.asarray(vals, dtype=np.float64))
    k = q * len(v)
    kk = int(k)
    if kk >= len(v):
        return float(v[-1])
    frac = k - kk
    return float(v[kk - 1] + frac * (v[kk] - v[kk - 1])) if frac else float(v[kk - 1])


VARIANTS = {
    'touch_a4_ncap': lambda ml: pq_of(comps(ml, 40, 4, 1)[0], .5),
    'touch_a1_ncap': lambda ml: pq_of(comps(ml, 40, 1, 1)[0], .5),
    'inside_a4_ncap': lambda ml: pq_of(comps(ml, 40, 4, 2)[0], .5),
    'none_a4_ncap': lambda ml: pq_of(comps(ml, 40, 4, 0)[0], .5),
    'touch_a4_core3': lambda ml: pq_of(comps(ml, 40, 4, 1)[1], .5),
    'thr30_touch': lambda ml: pq_of(comps(ml, 30, 4, 1)[0], .5),
    'thr50_touch': lambda ml: pq_of(comps(ml, 50, 4, 1)[0], .5),
}

for L, tgt in [(5, 222), (20, 227), (100, 110), (179, 128)]:
    ks = [k for k in range(2, 19) if k not in R1.get(f'L{L}', [])]
    row = {}
    for nm, fn in VARIANTS.items():
        row[nm] = float(np.median([fn(load(frame_path('E1', L, k))) for k in ks]))
    print(f'L{L:4d} tgt {tgt:4d}: ' +
          '  '.join(f'{nm}={row[nm]:.1f}' for nm in VARIANTS))

# pooled-over-frames variants at the same Ls
print()
for L, tgt in [(5, 222), (100, 110), (179, 128)]:
    ks = [k for k in range(2, 19) if k not in R1.get(f'L{L}', [])]
    allp = []
    for k in ks:
        p, _ = comps(load(frame_path('E1', L, k)), 40, 4, 1)
        allp += p
    print(f'L{L:4d} tgt {tgt:4d}: pooled-touch-a4 P50 {pq_of(allp, .5):.1f} '
          f'P40 {pq_of(allp, .4):.1f} P60 {pq_of(allp, .6):.1f} '
          f'np.median {float(np.median(allp)):.1f} n {len(allp)}')