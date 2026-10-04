#!/usr/bin/env python3
"""Candidate-aggregate probe: reproduce v1's (b)/(d) settled numbers on the
v1-kept subset so the v2 aggregation is locked to v1's definitions.

v1 targets (reports/s14r-0003c-photometry-settle-duty-raw-metric-transfer.md):
  E1 settled (bg=frame median): L5..179 cM-bg 83.7/84.9/79.9/91.1/97.0/99.7/
    102.2/104.3/105.6/105.9 ; cP50 222/254/227/173/174/136/110/110/122/128
  E2 settled (k2-15): cM-bg 75.8/82.1/89.5/81.0/86.6/91.5/94.6/98.0/100.6/101.6
    cP50 105/107/87/112/110/104/95/94/80/82
"""
import io
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402

RUN0 = REPO / 'runs/daemon/runs/run0'


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
    ker = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (19, 19))
    m = cv2.dilate((acc & band).astype(np.uint8), ker) > 0
    return m


MASK = get_mask()


def frame_stats(mlum):
    f = mlum.astype(np.float32)
    hm = C.hist_median(mlum)
    bw = (mlum >= 40).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    means, p50s, peaks, rings = [], [], [], []
    keep = np.zeros_like(bw, bool)
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < 4:
            continue
        m = lab[y:y + h, x:x + w] == i
        lm = MASK[y:y + h, x:x + w]
        if not (m & lm).any():
            continue
        keep[y:y+h, x:x+w] |= m
        vals = f[y:y + h, x:x + w][m]
        means.append(float(vals.mean()))
        p50s.append(float(np.percentile(vals, 50)))
        peaks.append(float(vals.max()))
        ker9 = cv2.getStructuringElement(cv2.MORPH_RECT, (19, 19))
        ker3 = cv2.getStructuringElement(cv2.MORPH_RECT, (7, 7))
        X0, Y0 = max(0, x - 11), max(0, y - 11)
        X1, Y1 = min(mlum.shape[1], x + w + 11), min(mlum.shape[0], y + h + 11)
        mm = (lab[Y0:Y1, X0:X1] == i)
        out = cv2.dilate(mm.astype(np.uint8), ker9) > 0
        inn3 = cv2.erode(mm.astype(np.uint8), ker3) > 0
        inn9 = cv2.erode(mm.astype(np.uint8), ker9) > 0
        ring39 = out & ~inn9 & ~mm
        ring3 = out & ~inn3 & ~mm
        rings.append(float(np.median(f[Y0:Y1, X0:X1][ring39]))
                     if ring39.any() else None)
    u = keep.astype(np.uint8)
    ker_out = cv2.getStructuringElement(cv2.MORPH_RECT, (19, 19))
    ker_in3 = cv2.getStructuringElement(cv2.MORPH_RECT, (7, 7))
    outd = cv2.dilate(u, ker_out) > 0
    inn = cv2.erode(u, ker_in3) > 0
    ringU = outd & ~inn & ~keep
    bgRingU = float(np.median(f[ringU])) if ringU.any() else None
    return {'hm': hm, 'bgRingU': bgRingU,
            'blob_mean_med': float(np.median(means)),
            'blob_mean_avg': float(np.mean(means)),
            'blob_p50_med': float(np.median(p50s)),
            'blob_peak_med': float(np.median(peaks)),
            'bgRing_med39': float(np.nanmedian([r for r in rings if r is not None])),
            'maskpix_in_blob_mean': float(f[keep].mean()),
            'nblob': len(means)}


TARGETS = {
    'E1': {'L5': (83.7, 222), 'L10': (84.9, 254), 'L20': (79.9, 227),
           'L40': (91.1, 173), 'L60': (97.0, 174), 'L80': (99.7, 136),
           'L100': (102.2, 110), 'L120': (104.3, 110), 'L150': (105.6, 122),
           'L179': (105.9, 128)},
    'E2': {'L5': (75.8, 105), 'L10': (82.1, 107), 'L20': (89.5, 87),
           'L40': (81.0, 112), 'L60': (86.6, 110), 'L80': (91.5, 104),
           'L100': (94.6, 95), 'L120': (98.0, 94), 'L150': (100.6, 80),
           'L179': (101.6, 82)},
}
REPAIRED = {  # the 24 excluded from v1 aggregates
    'E1': {'L10': [4, 5, 11, 18], 'L150': [10], 'L179': [13], 'L20': [6, 14],
           'L5': [3, 10], 'L60': [14], 'L80': [10, 18]},
    'E2': {'L10': [8], 'L100': [8, 16], 'L120': [11], 'L179': [10, 17],
           'L20': [2], 'L40': [11], 'L5': [14], 'L60': [8], 'L80': [4]},
}


def main():
    for arm in ['E1', 'E2']:
        print(f'=== {arm}')
        for Ls in [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]:
            tgt = TARGETS[arm][f'L{Ls}']
            rows = []
            for k in range(1, 19):
                if k in REPAIRED[arm].get(f'L{Ls}', []):
                    continue  # v1-kept only
                rows.append(frame_stats(load(RUN0 / f'cal_{arm}_L{Ls}_{k}.jpg')))
            def med(f):
                return float(np.median([r[f] for r in rows]))
            bg = med('hm')
            print(f'L{Ls:4d} bgFM={bg:5.1f} bgRingU={med("bgRingU"):5.1f} '
                  f'ring39med={med("bgRing_med39"):5.1f} nblob={med("nblob"):5.1f} '
                  f'| cands: meanmed-bg={med("blob_mean_med")-bg:6.1f} '
                  f'meanavg-bg={med("blob_mean_avg")-bg:6.1f} '
                  f'meanmed-ring={med("blob_mean_med")-med("bgRing_med39"):6.1f} '
                  f'keepmean-bg={med("maskpix_in_blob_mean")-bg:6.1f} '
                  f'p50med-bg={med("blob_p50_med")-bg:6.1f} '
                  f'| p50med={med("blob_p50_med"):6.1f} peakmed={med("blob_peak_med"):6.1f} '
                  f'| TARGET cM-bg={tgt[0]:6.1f} cP50={tgt[1]:6.1f}')


if __name__ == '__main__':
    main()