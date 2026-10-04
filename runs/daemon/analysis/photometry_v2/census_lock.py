#!/usr/bin/env idl
"""Census variants for (c): lock the v1 reproduction before extending.

v1 definitions (report): census = 4-conn thr blobs (>=4 px) filtered to the
lamp mask, 4 <= area <= 2000, contrast = peak - frame-median bg >= 15,
bg = the SETTLED ROOM-IDLE MEDIAN (67-ish) not the per-frame histMed.
v1 targets: idle00-11 n40 10-16 (median 13.0); E1 L5 per-frame
[16,0,71,71,56,51,52,56,59,54,45,8,60,69,64,71,65,51] median 56; L20 95
(1-105); L40 57.5; L60 32.5; L80 25; L120 20; L179 20.
Candidate definitions tested here on idle00-11 + E1 L5/L20/L40/L60/L80/L179.
"""
import io
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
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
    ker = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
    return cv2.dilate((acc & band).astype(np.uint8), ker) > 0


MASK = get_mask()


def census_n(mlum, thr, bg_refs):
    """bg_refs: dict name -> reference value; returns {name: count}."""
    f = mlum.astype(np.float32)
    bw = (mlum >= thr).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    cnt = {name: 0 for name in bg_refs}
    peaks = []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < 4 or area > 2000:
            continue
        m = lab[y:y + h, x:x + w] == i
        if not (m & MASK[y:y + h, x:x + w]).any():
            continue
        pk = float(f[y:y + h, x:x + w][m].max())
        peaks.append(pk)
        for name, ref in bg_refs.items():
            if pk - ref >= 15.0:
                cnt[name] += 1
    return cnt, peaks


def frame_path(arm, L, k):
    if arm == 'idle':
        return RUN0 / f'cal_idle_{k:02d}.jpg'
    return RUN0 / f'cal_{arm}_L{L}_{k}.jpg'


def main():
    sys.path.insert(0, str(REPO / 'tools'))
    import cwc_pos_decode as C
    # settled idle reference (v1 used 67 = the k00-11 state)
    idle_ml = [load(RUN0 / f'cal_idle_{k:02d}.jpg') for k in range(12)]
    hm_idles = [C.hist_median(ml) for ml in idle_ml]
    idle_bg = float(np.median(hm_idles))
    print('idle bg (median of histMed k00-11):', idle_bg,
          'per-frame:', hm_idles)

    sets = ([('idle', 'idle', 0)]
            + [(f'E1 L{L}', 'E1', L) for L in [5, 20, 40, 60, 80, 179]])
    variants = {
        'ref67': lambda hm: idle_bg,
        'max67': lambda hm: max(hm, idle_bg),
        'fmh': lambda hm: hm,
    }
    for Lname, arm, L in sets:
        rows = {v: [] for v in variants}
        kk = range(12) if arm == 'idle' else range(1, 19)
        for k in kk:
            ml = load(frame_path(arm, L, k))
            hm = C.hist_median(ml)
            cnt, _ = census_n(ml, 40, {vn: vf(hm) for vn, vf in variants.items()})
            for vn in variants:
                rows[vn].append(cnt[vn])
        print(f'{Lname:8s} ' + ' | '.join(
            f'{vn}: med {np.median(rows[vn]):5.1f} min {min(rows[vn]):3d} '
            f'max {max(rows[vn]):3d}' for vn in variants))


if __name__ == '__main__':
    main()