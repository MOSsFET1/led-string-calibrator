"""Full-n (b) table with the LOCKED coreP50 definition (pooled per-blob peak
P50, thr-40 >=4px mask-touching, no cap) + ratios + updated report table."""
import io
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
RUN0 = REPO / 'runs/daemon/runs/run0'
D = REPO / 'runs/daemon/analysis/photometry_v2'
sys.path.insert(0, str(REPO / 'tools'))
sys.path.insert(0, str(D))
import cwc_pos_decode as C  # noqa: E402
from census_lock import MASK  # noqa: E402

fm = json.load(open(D / 'frame_metrics.json'))
by = {f['name']: f for f in fm}
LS = [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]
V1 = {'E1': {'L5': 222, 'L10': 254, 'L20': 227, 'L40': 173, 'L60': 174,
             'L80': 136, 'L100': 110, 'L120': 110, 'L150': 122, 'L179': 128},
      'E2': {'L5': 105, 'L10': 107, 'L20': 87, 'L40': 112, 'L60': 110,
             'L80': 104, 'L100': 95, 'L120': 94, 'L150': 80, 'L179': 82}}

KER3 = np.ones((3, 3), np.float32)


def blob_peaks_cached(mlum):
    f = mlum.astype(np.float32)
    bw = (mlum >= 40).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    out = []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < 4:
            continue
        m = lab[y:y + h, x:x + w] == i
        if not (m & MASK[y:y + h, x:x + w]).any():
            continue
        out.append(float(f[y:y + h, x:x + w][m].max()))
    return out


def pq50(vals):
    v = np.sort(np.asarray(vals, dtype=np.float64))
    k = 0.5 * len(v)
    kk = int(k)
    if kk >= len(v):
        return float(v[-1])
    frac = k - kk
    return float(v[kk - 1] + frac * (v[kk] - v[kk - 1])) if frac else float(v[kk - 1])


def cMbg(r):
    return r['pooled_mean'] - r['bgRingU']


rows = {}
for arm in ['E1', 'E2']:
    for L in LS:
        for k in range(1, 19):
            nm = f'cal_{arm}_L{L}_{k}'
            r = by[nm]
            jp = RUN0 / (nm + '.jpg')
            raw = jp.read_bytes()
            assert raw.endswith(b'\xff\xd9')
            img = Image.open(io.BytesIO(raw))
            img.load()
            ml = np.asarray(img.convert('RGB'), dtype=np.uint8).max(axis=2)
            rows[(arm, L, k)] = {'fm': r, 'peaks': blob_peaks_cached(ml)}


def win(arm, L, w):
    return [rows[(arm, L, k)] for k in range(*w)]


print('== (b) FULL-n table, locked defs ==')
res = {}
for arm in ['E1', 'E2']:
    W = (2, 19) if arm == 'E1' else (2, 16)
    t = []
    for L in LS:
        rs = win(arm, L, W)
        allp = []
        for r in rs:
            allp += r['peaks']
        row = {'L': L, 'n': len(rs),
               'bg': float(np.median([r['fm']['histMed'] for r in rs])),
               'cMbg': float(np.median([cMbg(r['fm']) for r in rs])),
               'cp50': pq50(allp),
               'nB40': float(np.median([r['fm']['nB40'] for r in rs]))}
        t.append(row)
        print(f"{arm} L{L:4d} n={row['n']:2d} bg {row['bg']:5.1f} cMbg "
              f"{row['cMbg']:6.1f} cp50 {row['cp50']:6.1f} "
              f"(v1 {V1[arm][f'L{L}']}) nB {row['nB40']:5.1f}")
    res[arm] = t
print('== ratios ==')
for i, L in enumerate(LS):
    e1, e2 = res['E1'][i], res['E2'][i]
    print(f"L{L:4d} p50 {e2['cp50'] / e1['cp50']:5.2f}  cMbg "
          f"{e2['cMbg'] / e1['cMbg']:5.2f}")
json.dump(res, open(D / 'v2_b_table_locked.json', 'w'), indent=1)
print('wrote v2_b_table_locked.json')