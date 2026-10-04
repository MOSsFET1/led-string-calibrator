"""Lock v1's per-blob definitions on idle frames (directly quotable numbers).

v1 (c) idle row: bgMed 67 | census n40 13.0 (10-16) | peakP10 85 peakP50 110
| n30 5.0 | thr30 peakP10/P50 96/112.  Also: 'idle lamp-adjacent  pixels sit
at coreP50 ~71-79 (thr-40 blob cores), coreP90(thr-30) ~108, coreP90(thr-40)
~134-168, 24% of idle anchor sites >=100 from spill'.
"""
import io
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
RUN0 = REPO / 'runs/daemon/runs/run0'
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402


def load(jpg):
    raw = jpg.read_bytes()
    assert raw.endswith(b'\xff\xd9')
    img = Image.open(io.BytesIO(raw))
    img.load()
    return np.asarray(img.convert('RGB'), dtype=np.uint8).max(axis=2)


def get_mask():
    acc = None
    for k in range(1, 19):
        ml = load(RUN0 / f'cal_E1_L179_{k}.jpg')
        a = (ml >= 200)
        acc = a if acc is None else (acc | a)
    H, W = acc.shape
    yy, xx = np.mgrid[0:H, 0:W]
    band = (yy >= 150) & (yy <= 622)
    ker = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
    m = cv2.dilate((acc & band).astype(np.uint8), ker) > 0
    return m, acc & band


MASK, SEEDS = get_mask()


def blobs(mlum, thr, mask, cap=None):
    f = mlum.astype(np.float32)
    bw = (mlum >= thr).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    out = []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < 4:
            continue
        if cap and area > cap:
            continue
        m = lab[y:y + h, x:x + w] == i
        if mask is not None and not (m & mask[y:y + h, x:x + w]).any():
            continue
        vals = f[y:y + h, x:x + w][m]
        out.append({'area': area, 'peak': float(vals.max()),
                    'mean': float(vals.mean()),
                    'p50': float(np.percentile(vals, 50)),
                    'p90': float(np.percentile(vals, 90))})
    return out


def pq(v, q):
    v = np.sort(np.asarray(v))
    k = q * len(v)
    kk = int(k)
    if kk >= len(v):
        return float(v[-1])
    frac = k - kk
    return float(v[kk - 1] + frac * (v[kk] - v[kk - 1])) if frac else float(v[kk - 1])


def main():
    frames = [load(RUN0 / f'cal_idle_{k:02d}.jpg') for k in range(12)]
    bg = 67.0  # v1's idle bgMed
    for thr, cap, tag, mask in [(40, 2000, 'census40', MASK), (30, 2000, 'census30', MASK),
                                (40, None, 'blob40', MASK), (30, None, 'blob30', MASK),
                                (40, None, 'noblob', None)]:
        cnts, peaks = [], []
        for ml in frames:
            bl = blobs(ml, thr, mask, cap)
            bl = [b for b in bl if b['peak'] - (C.hist_median(ml) if tag == 'census40' or tag == 'census30' else bg) >= 15.0] \
                if tag.startswith('census') else bl
            cnts.append(len(bl))
            peaks += [b['peak'] for b in bl]
        print(f'{tag}: count med {np.median(cnts):5.1f} ({min(cnts)}-{max(cnts)}) '
              f'pooled peaks P10 {pq(peaks, .10):6.1f} P50 {pq(peaks, .50):6.1f} '
              f'P90 {pq(peaks, .90):6.1f}')
    # per-frame mean count and 24% sites >=100 check (anchor sites = seeds CCs)
    nseed, lab = cv2.connectedComponents(SEEDS.astype(np.uint8), connectivity=8)
    print('seed sites (8-conn):', nseed - 1)


if __name__ == '__main__':
    main()