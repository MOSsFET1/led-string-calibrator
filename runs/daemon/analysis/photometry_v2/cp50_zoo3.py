"""Zoo 3: pooled-over-window peak-P50 variants (contrast filter, area cap,
k1 included/excluded) for v1's cP50 ladder, E1+E2, v1-kept frames."""
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
R2 = {'L10': [8], 'L100': [8, 16], 'L120': [11], 'L179': [10, 17],
      'L20': [2], 'L40': [11], 'L5': [14], 'L60': [8], 'L80': [4]}
V1 = {'E1': {'L5': 222, 'L10': 254, 'L20': 227, 'L40': 173, 'L60': 174,
             'L80': 136, 'L100': 110, 'L120': 110, 'L150': 122, 'L179': 128},
      'E2': {'L5': 105, 'L10': 107, 'L20': 87, 'L40': 112, 'L60': 110,
             'L80': 104, 'L100': 95, 'L120': 94, 'L150': 80, 'L179': 82}}


def blob_peaks(mlum, thr=40, cap=None, contrast=None, min_area=4):
    f = mlum.astype(np.float32)
    hm = C.hist_median(mlum)
    bw = (mlum >= thr).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    out = []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < min_area:
            continue
        if cap is not None and area > cap:
            continue
        m = lab[y:y + h, x:x + w] == i
        if not (m & MASK[y:y + h, x:x + w]).any():
            continue
        pk = float(f[y:y + h, x:x + w][m].max())
        if contrast is not None and pk - hm < contrast:
            continue
        out.append(pk)
    return out


def pq_of(vals, q):
    v = np.sort(np.asarray(vals, dtype=np.float64))
    k = q * len(v)
    kk = int(k)
    if kk >= len(v):
        return float(v[-1])
    frac = k - kk
    return float(v[kk - 1] + frac * (v[kk] - v[kk - 1])) if frac else float(v[kk - 1])


VARIANTS = {
    'plain': dict(),
    'contrast15': dict(contrast=15.0),
    'cap2000': dict(cap=2000),
    'contrast15_cap2000': dict(contrast=15.0, cap=2000),
}
for arm, rep in [('E1', R1), ('E2', R2)]:
    print(f'== {arm}')
    for L in [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]:
        ks = [k for k in range(2, 19) if k not in rep.get(f'L{L}', [])]
        peaks_all = {nm: [] for nm in VARIANTS}
        for k in ks:
            ml = load(frame_path(arm, L, k))
            for nm, kw in VARIANTS.items():
                peaks_all[nm] += blob_peaks(ml, **kw)
        line = f'L{L:4d} tgt {V1[arm][f"L{L}"]:4d}: '
        line += '  '.join(f'{nm} {pq_of(peaks_all[nm], .5):6.1f}' for nm in VARIANTS)
        print(line)