"""Zoo: find the definition reproducing v1's (b)/(d) raw coreP50 ladder
(222/254/227/173/174/136/110/110/122/128) and E2 (105/107/87/112/110/104/95/
94/80/82) from the v1-kept frames."""
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
V1P50 = {'L5': 222, 'L10': 254, 'L20': 227, 'L40': 173, 'L60': 174,
         'L80': 136, 'L100': 110, 'L120': 110, 'L150': 122, 'L179': 128}


def per_blob_stats(mlum, thr=40):
    """Return per-blob (area, peak, p90, mean, topdec_mean, p75) for
    mask-touching 4-conn blobs."""
    f = mlum.astype(np.float32)
    bw = (mlum >= thr).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    out = []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < 4:
            continue
        m = lab[y:y + h, x:x + w] == i
        if not (m & MASK[y:y + h, x:x + w]).any():
            continue
        v = np.sort(f[y:y + h, x:x + w][m])
        out.append({'area': area, 'peak': float(v[-1]),
                    'p90': float(v[int(0.9 * len(v)) - 1]),
                    'mean': float(v.mean()),
                    'topdec': float(v[-max(1, len(v) // 10):].mean()),
                    'top3': float(v[-3:].mean())})
    return out


def pq_of(vals, q):
    v = np.sort(np.asarray(vals))
    k = q * len(v)
    kk = int(k)
    if kk >= len(v):
        return float(v[-1])
    frac = k - kk
    return float(v[kk - 1] + frac * (v[kk] - v[kk - 1])) if frac else float(v[kk - 1])


def rung_metric(L, ks, agg):
    """agg(frame_blobs) -> frame scalar; then median over frames."""
    vals = []
    for k in ks:
        bl = per_blob_stats(load(frame_path('E1', L, k)))
        vals.append(agg(bl))
    return float(np.median(vals))


CANDS = {
    'peakP50_allblobs': lambda bl: pq_of([b['peak'] for b in bl], .5),
    'peakP50_census': lambda bl: pq_of([b['peak'] for b in bl
                                        if 4 <= b['area'] <= 2000], .5),
    'peakP90_census': lambda bl: pq_of([b['peak'] for b in bl
                                        if 4 <= b['area'] <= 2000], .9),
    'blobP90P50': lambda bl: pq_of([b['p90'] for b in bl], .5),
    'topdecP50': lambda bl: pq_of([b['topdec'] for b in bl], .5),
    'blobP75P50': lambda bl: pq_of([b['p90'] for b in bl], .25),
}
print('L     tgt  ' + '  '.join(f'{k:>16s}' for k in CANDS))
for L in [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]:
    ks = [k for k in range(2, 19) if k not in R1.get(f'L{L}', [])]
    row = {nm: rung_metric(L, ks, fn) for nm, fn in CANDS.items()}
    print(f'L{L:4d} {V1P50[f"L{L}"]:5d} ' +
          '  '.join(f'{row[nm]:17.1f}' for nm in CANDS))