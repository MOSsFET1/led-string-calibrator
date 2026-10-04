#!/usr/bin/env python3
"""S14R-0003c photometry v2 — per-frame metric pass over run0 (post-repair corpus).

All frames are wire-repaired IN PLACE and manifest-verified
(runs/daemon/analysis/run_repair/manifest.json, verify_pass true), so this
loader NEVER sets ImageFile.LOAD_TRUNCATED_IMAGES and asserts the EOI tail
+ full PIL decode per frame (fails loudly on any bad file).

One JSON object per frame -> frame_metrics.json (whole pass) or, with --cal,
a calibration subset with candidate-definition printouts used to lock the
aggregate definitions against v1's printed tables before the full pass.

Frame-level metrics (documented in the v2 report):
  luma = max(r,g,b); histMed = page integer-luma median
  (tools/cwc_pos_decode.hist_median); page quantile q(N) = cumsum>q*N -> bin+0.5
  lamp mask = union of luma>=200 over all E1_L179 k, dilate r=9, y-band 150..622
  blobs(thr) = 4-conn components >=4 px touching the lamp mask
  census(thr) = blobs with 4<=area<=2000 and peak - histMed >= 15
  per-blob: area, pixel mean/p50/p90/peak, ring_i = 3..9 px annulus median
  per-frame bg candidates: bgFM = histMed, bgRingU = union-annulus median
"""
import io
import json
import re
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402  (page-parity hist_median, constants)

RUN0 = REPO / 'runs/daemon/runs/run0'
OUTD = REPO / 'runs/daemon/analysis/photometry_v2'
OUTD.mkdir(parents=True, exist_ok=True)

CAL_SET = ([('idle', k) for k in range(0, 12)] +
           [('E1', 5), ('E1', 20), ('E1', 40), ('E1', 100), ('E1', 179),
            ('E2', 5), ('E2', 40), ('E2', 100), ('E2', 179)])


def page_q(v, q):
    h = np.bincount(np.asarray(v, dtype=np.int64).ravel(), minlength=256)
    total = int(h.sum())
    thr = q * total
    acc = 0
    for val in range(256):
        acc += int(h[val])
        if acc > thr:
            return val + 0.5
    return 255.5


def load_frame(jpg):
    raw = jpg.read_bytes()
    assert raw.endswith(b'\xff\xd9'), f'no EOI: {jpg.name}'
    img = Image.open(io.BytesIO(raw))
    img.load()
    mlum = np.asarray(img.convert('RGB'), dtype=np.uint8).max(axis=2)
    meta = json.loads(jpg.with_suffix('.meta.json').read_text())
    return mlum, meta


def blob_list(mlum, thr, lamp_mask, min_area=4):
    bw = (mlum >= thr).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, connectivity=4)
    out = []
    H, W = mlum.shape
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < min_area:
            continue
        m = lab[y:y + h, x:x + w] == i
        lm = lamp_mask[y:y + h, x:x + w]
        if not (m & lm).any():
            continue
        sub = mlum[y:y + h, x:x + w].astype(np.float32)
        vals = sub[m]
        pk = float(vals.max())
        mean = float(vals.mean())
        p50 = float(np.percentile(vals, 50))
        p90 = float(np.percentile(vals, 90))
        cy, cx = np.argwhere(m & (sub == pk))[0]
        gx, gy = x + int(cx), y + int(cy)
        out.append({'area': int(area), 'peak': pk, 'mean': mean,
                    'p50': p50, 'p90': p90, 'x': gx, 'y': gy})
    return out


def ring_i(mlum, blob, rad_out=9, rad_in=3):
    """median of the 3..9 px annulus around one blob (bbox window)."""
    x0 = max(0, blob['x'] - blob['area'] // 60 - rad_out)
    raise SystemExit('unused')


def annulus_median(mlum, blob):
    """Per-blob ring: pixels whose Chebyshev distance from the blob's own
    pixel set is in (3, 9]; computed in a padded bbox window."""
    rad_out, rad_in = 9, 3
    bwmask = (mlum >= 0)  # placeholder
    return None


def blob_ring_median(mlum_f, lab, idx, stats, rad_out=9, rad_in=3):
    x, y, w, h, area = stats[idx]
    pad = rad_out + 2
    X0, Y0 = max(0, x - pad), max(0, y - pad)
    X1, Y1 = min(mlum_f.shape[1], x + w + pad), min(mlum_f.shape[0], y + h + pad)
    m = (lab[Y0:Y1, X0:X1] == idx)
    ker_out = cv2.getStructuringElement(cv2.MORPH_RECT, (2 * rad_out + 1,) * 2)
    ker_in = cv2.getStructuringElement(cv2.MORPH_RECT, (2 * rad_in + 1,) * 2)
    out = cv2.dilate(m.astype(np.uint8), ker_out) > 0
    inn = cv2.erode(m.astype(np.uint8), ker_in) > 0
    ring = out & ~inn & ~m
    if not ring.any():
        return None
    return float(np.median(mlum_f[Y0:Y1, X0:X1][ring]))


def frame_metrics(mlum, lamp_mask):
    f = mlum.astype(np.float32)
    hm = C.hist_median(mlum)
    q90, q95, q99 = page_q(mlum, 0.90), page_q(mlum, 0.95), page_q(mlum, 0.99)
    bw40 = (mlum >= 40).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw40, connectivity=4)
    blobs, blob_rings = [], []
    ring_pts_global = []
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < 4:
            continue
        m = lab[y:y + h, x:x + w] == i
        lm = lamp_mask[y:y + h, x:x + w] if lamp_mask is not None else None
        if lm is not None and not (m & lm).any():
            continue
        vals = f[y:y + h, x:x + w][m]
        pk = float(vals.max())
        mean = float(vals.mean())
        p50 = float(np.percentile(vals, 50))
        p90 = float(np.percentile(vals, 90))
        cy, cx = np.argwhere(m & (f[y:y + h, x:x + w] == pk))[0]
        blobs.append({'area': int(area), 'peak': pk, 'mean': mean,
                      'p50': p50, 'p90': p90, 'x': x + int(cx), 'y': y + int(cy)})
        rm = blob_ring_median(f, lab, i, stats)
        blob_rings.append(rm)
        # global ring pixels (union annulus) accumulated via bbox rings is
        # approximated below by a separate whole-frame pass (cheaper, exact):
    # union annulus: dilate(union,9) & ~erode(union,3) & ~union
    ker_out = cv2.getStructuringElement(cv2.MORPH_RECT, (19, 19))
    ker_in3 = cv2.getStructuringElement(cv2.MORPH_RECT, (7, 7))
    u = (lab > 0).astype(np.uint8)
    outd = cv2.dilate(u, ker_out) > 0
    inn = cv2.erode(u, ker_in3) > 0
    ring = outd & ~inn & ~u.astype(bool)
    bgRingU = float(np.median(f[ring])) if ring.any() else None
    ker_in9 = cv2.getStructuringElement(cv2.MORPH_RECT, (19, 19))
    inn9 = cv2.erode(u, ker_in9) > 0
    ring39 = outd & ~inn9
    bgRing39 = float(np.median(f[ring39])) if ring39.any() else None

    cens40 = [b for b in blobs if 4 <= b['area'] <= 2000 and b['peak'] - hm >= 15.0]
    thr30 = blob_list(mlum, 30, lamp_mask) if lamp_mask is not None else []

    # POOLED core stats (this is v1's 'core' aggregate, decoding-locked:
    # v1 cM-bg = pooled-mean(union thr-40 blob px) - ring bg, all 10 E1 rungs
    # within 1.8 LSB; idle pooled P90 137 = v1 coreP90(thr-40) 134-168)
    bpix = (lab > 0) & (lamp_mask if lamp_mask is not None else True)
    # restrict to mask-touching components only (lab already filtered? no:
    # lab holds ALL >=4px comps; rebuild union of kept comps)
    kept = np.zeros_like(bpix)
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if area < 4:
            continue
        m = lab[y:y + h, x:x + w] == i
        if lamp_mask is not None and not (m & lamp_mask[y:y + h, x:x + w]).any():
            continue
        kept[y:y + h, x:x + w] |= m
    bvals = f[kept]
    pooledN = int(bvals.size)
    if pooledN:
        pm = float(bvals.mean())
        pp50 = page_q(bvals, 0.50)
        pp90 = page_q(bvals, 0.90)
    else:
        pm = pp50 = pp90 = None

    res = {
        'histMed': hm, 'q90': q90, 'q95': q95, 'q99': q99,
        'bgFM': hm, 'bgRingU': bgRingU, 'bgRing39': bgRing39,
        'nB40': len(blobs), 'nB30': len(thr30), 'nCens40': len(cens40),
        # aggregate candidates over the thr-40 blob set
        'blob_mean_med': float(np.median([b['mean'] for b in blobs])) if blobs else None,
        'blob_mean_avg': float(np.mean([b['mean'] for b in blobs])) if blobs else None,
        'blob_p50_med': float(np.median([b['p50'] for b in blobs])) if blobs else None,
        'blob_p90_med': float(np.median([b['p90'] for b in blobs])) if blobs else None,
        'blob_p90_p90': float(np.percentile([b['p90'] for b in blobs], 90)) if blobs else None,
        'blob_peak_med': float(np.median([b['peak'] for b in blobs])) if blobs else None,
        'blob_peak_p90': float(np.percentile([b['peak'] for b in blobs], 90)) if blobs else None,
        'corr_mean_ring_med': (float(np.median([b['mean'] - (r if r is not None else b['mean'])
                                                for b, r in zip(blobs, blob_rings)]))
                               if blobs else None),
        'corr_p90_ring_med': (float(np.median([b['p90'] - (r if r is not None else b['p90'])
                                               for b, r in zip(blobs, blob_rings)]))
                              if blobs else None),
        'corr_p90_ring_p90': (float(np.percentile([b['p90'] - (r if r is not None else b['p90'])
                                                   for b, r in zip(blobs, blob_rings)], 90))
                              if blobs else None),
        'corr_p90_ring_mean': (float(np.mean([b['p90'] - (r if r is not None else b['p90'])
                                              for b, r in zip(blobs, blob_rings)]))
                               if blobs else None),
        'p90ring': None,
        'pooled_mean': pm, 'pooled_p50': pp50, 'pooled_p90': pp90,
        'pooled_N': pooledN,
        'cens40_peaks': [round(b['peak'], 1) for b in cens40],
        'cens40_areas': [b['area'] for b in cens40],
        'thr30_peaks': [round(b['peak'], 1) for b in thr30],
        'blobs_n': len(blobs),
    }
    return res


def main():
    cal = '--cal' in sys.argv
    only = None
    for _a in sys.argv[1:]:
        if not _a.startswith('--'):
            only = _a
            break
    # ---- lamp mask from settled L=179 snapshots ----
    mask_acc = None
    for k in range(1, 19):
        jpg = RUN0 / f'cal_E1_L179_{k}.jpg'
        mlum, _ = load_frame(jpg)
        acc = (mlum >= 200)
        mask_acc = acc if mask_acc is None else (mask_acc | acc)
    H, W = mask_acc.shape
    yy, xx = np.mgrid[0:H, 0:W]
    band = (yy >= 150) & (yy <= 622)
    # LOCKED mask (reproduces v1's 33.6k px): thr-200 union over the 18 settled
    # L=179 snapshots, y-band 150-622, 9x9 ELLIPSE dilation = 34373 px (57.3/site)
    ker = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
    mask = cv2.dilate((mask_acc & band).astype(np.uint8), ker) > 0
    print(f'lamp mask px = {int(mask.sum())} ({int(mask.sum()) / 600:.1f}/site)')

    frames = []
    if cal:
        for arm, L in CAL_SET:
            kk = range(0, 12) if arm == 'idle' else range(1, 19)
            for k in kk:
                frames.append(RUN0 / (f'cal_idle_{k:02d}.jpg' if arm == 'idle'
                                      else f'cal_{arm}_L{L}_{k}.jpg'))
    else:
        frames = sorted(RUN0.glob('*.jpg'))
    if only:
        pat = re.compile(only)
        frames = [p for p in frames if pat.match(p.name)]

    out_path = OUTD / ('frame_metrics_cal.json' if cal else 'frame_metrics.json')
    done = []
    for jpg in frames:
        name = jpg.stem
        mlum, meta = load_frame(jpg)
        res = frame_metrics(mlum, mask)
        res['name'] = name
        res['label'] = meta['label']
        res['t'] = meta['t']
        done.append(res)
        print(f'{name} hm={res["histMed"]} nB40={res["nB40"]} nC40={res["nCens40"]} '
              f'ringU={None if res["bgRingU"] is None else round(res["bgRingU"],1)} '
              f'cM={None if res["blob_mean_med"] is None else round(res["blob_mean_med"],1)} '
              f'cP50={None if res["blob_p50_med"] is None else round(res["blob_p50_med"],1)} '
              f'crR={None if res["corr_mean_ring_med"] is None else round(res["corr_mean_ring_med"],1)} '
              f'c9R={None if res["corr_p90_ring_med"] is None else round(res["corr_p90_ring_med"],1)}')
        sys.stdout.flush()
    out_path.write_text(json.dumps(done))
    print(f'wrote {len(done)} frames -> {out_path}')


if __name__ == '__main__':
    main()