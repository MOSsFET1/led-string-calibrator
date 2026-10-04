"""Test: v1's (b)/(d) 'raw coreP50' = pooled page-p50 over CENSUS blob pixels
(thr-40, 4-2000 px, contrast>=15 vs frame histMed, lamp-masked)."""
import io
import json
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
R2 = {'L10': [8], 'L100': [8, 16], 'L120': [11], 'L179': [10, 17],
      'L20': [2], 'L40': [11], 'L5': [14], 'L60': [8], 'L80': [4]}
V1 = {'E1': {'L5': 222, 'L10': 254, 'L20': 227, 'L40': 173, 'L60': 174,
             'L80': 136, 'L100': 110, 'L120': 110, 'L150': 122, 'L179': 128},
      'E2': {'L5': 105, 'L10': 107, 'L20': 87, 'L40': 112, 'L60': 110,
             'L80': 104, 'L100': 95, 'L120': 94, 'L150': 80, 'L179': 82}}


def census_pooled(mlum):
    hm = C.hist_median(mlum)
    f = mlum.astype(np.float32)
    bw = (mlum >= 40).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    keep = np.zeros_like(bw, bool)
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < 4 or area > 2000:
            continue
        m = lab[y:y + h, x:x + w] == i
        if not (m & MASK[y:y + h, x:x + w]).any():
            continue
        if float(f[y:y + h, x:x + w][m].max()) - hm < 15.0:
            continue
        keep[y:y + h, x:x + w] |= m
    v = f[keep]
    if v.size == 0:
        return None, None
    # pooled page quantiles
    def pq(q):
        h2 = np.bincount(v.astype(np.int64).ravel(), minlength=256)
        tot = int(h2.sum())
        thr = q * tot
        acc = 0
        for val in range(256):
            acc += int(h2[val])
            if acc > thr:
                return val + 0.5
        return 255.5
    return pq(0.50), float(v.mean())


def med(v):
    v = [x for x in v if x is not None]
    return float(np.median(v))


out = {}
for arm, rep in [('E1', R1), ('E2', R2)]:
    for L in [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]:
        kept, full = [], []
        for k in range(2, 19):
            p50, m = census_pooled(load(frame_path(arm, L, k)))
            isrep = k in rep.get(f'L{L}', [])
            if not isrep:
                kept.append(p50)
            full.append(p50)
        v1med = V1[arm][f'L{L}']
        print(f'{arm} L{L:4d}: v1-kept {med(kept):6.1f} (v1 {v1med:4d}, '
              f'd {med(kept) - v1med:+6.1f}) | full-n {med(full):6.1f}')
        out[f'{arm}_L{L}'] = {'kept': med(kept), 'full': med(full)}
json.dump(out, open(REPO / 'runs/daemon/analysis/photometry_v2/census_p50_lock.json', 'w'), indent=1)