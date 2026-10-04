#!/usr/bin/env python3
"""S14 LED-position detection (console-side, tunes on captured frames ONLY).

S14P findings this encodes (29 Sep, runs/s14p-b2-led0 r4+r5):
  - The stored "master" is plane-content, not all-on (fast-grab latency):
    a true all-on master is EQUALLY far from every plane; detect the dip
    (min/med mean-abs < 0.8) and REPLACE it with a pseudo-master =
    per-pixel max over the 18 registered planes (every LED ON in 9).
  - LED positions = bright blobs in the (pseudo-)master, classified by the
    blink stack: C = coherent hole in 7..11 planes (single LED),
    M = >= 12 (merged multi-LED blob), S = <= 6 (static clutter).
  - The hole pile-up (Σ plane diffs) is kept as a diagnostic, not the
    position source: JPEG ringing piles coherently too, so hole blobs
    point at both LEDs and their halos.

Usage: venv python3 tools/s14_detect.py runs/<dir> [--tag cwc]
       [--save-annot PNG] [--save-pile PNG] [--save-blobs JSON]
"""
import argparse, json, sys
from collections import deque, Counter
from pathlib import Path
import numpy as np
import cv2

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from offline_hole_verify import decode_run, luma, plane_index  # noqa: E402
from cwc_analyse import reg_residual  # noqa: E402

PM_THR_PCT = 97      # pseudo-master blob threshold (percentile)
MIN_PIX = 4          # min blob area (px)
MERGE_R = 2.0        # blob merge radius (x radius)
AREA_FRAC = 0.01     # max blob area (fraction of frame)
COH_THR = 25.0       # per-plane hole-depth threshold for blink membership
COH_PATCH = 3        # blink patch radius (px) around blob centre


def build(run_dir: Path, tag='cwc'):
    """Register 18 planes, choose master (dip test / pseudo-master), build
    the per-plane diff stack. Returns dict with mimg (RGB uint8 for
    annotation), mlum, stack (18,H,W), planes_used, shifts, master_used."""
    frames = decode_run(run_dir, tag)
    masts = [f for f in frames if f['label'].endswith('master')]
    plane_fs = sorted((plane_index(f['label']), f)
                      for f in frames if ':p' in f['label'])
    ref = luma(masts[0]['img']).astype(np.float32) if masts else None

    reg = []   # (p, registered_plane_lum) — base = stored master or plane 0
    shifts = {}
    for p, f in plane_fs:
        plum = luma(f['img']).astype(np.float32)
        base = ref if ref is not None else (reg[0][1] if reg else plum)
        dx, dy, conf = reg_residual(base, plum)
        shifts[p] = (dx, dy, conf)
        M = np.float32([[1, 0, dx], [0, 1, dy]])
        reg.append((p, cv2.warpAffine(plum, M, (plum.shape[1], plum.shape[0]),
                                      flags=cv2.INTER_LINEAR)))

    # master-dip test: all-on master is EQUALLY far from every plane
    master_used = 'pseudo'
    if ref is not None:
        mads = [np.mean(np.abs(ref - pw)) for _, pw in reg]
        ratio = min(mads) / max(float(np.median(mads)), 1e-9)
        dip_p = reg[int(np.argmin(mads))][0]
        master_used = 'stored' if ratio >= 0.8 else 'pseudo'
        print(f'master dip: min/med {min(mads):.2f}/{np.median(mads):.2f} '
              f'(ratio {ratio:.2f}, dip at p{dip_p:02d}) -> {master_used} master')
    if master_used == 'stored':
        mlum = ref
        mimg = np.asarray(masts[0]['img'])
    else:
        mlum = np.max([pw for _, pw in reg], axis=0)
        lo, hi = np.percentile(mlum, 1), np.percentile(mlum, 99.9)
        g8 = np.clip((mlum - lo) / max(hi - lo, 1) * 255, 0, 255).astype(np.uint8)
        mimg = np.stack([g8] * 3, axis=-1)

    planes_used, stack = [], []
    med_m = float(np.median(mlum))
    for p, pw in reg:
        k = float(np.median(pw)) / max(med_m, 1.0)
        if not (0.8 < k < 1.25):
            print(f'  p{p:02d}: gain k={k:.3f} out of band — plane SKIPPED')
            continue
        stack.append(cv2.GaussianBlur(mlum, (5, 5), 1.2) -
                     cv2.GaussianBlur(pw * k, (5, 5), 1.2))
        planes_used.append(p)
    return {'mimg': mimg, 'mlum': mlum, 'stack': np.stack(stack),
            'planes_used': planes_used, 'shifts': shifts,
            'master_used': master_used}


def flood_blobs(mask, pm, min_n=MIN_PIX):
    """Bright-region blobs: flood fill, centroid, peak. (int16 mask)."""
    H, W = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    blobs = []
    for sy in range(H):
        for sx in range(W):
            if mask[sy, sx] and not seen[sy, sx]:
                q = deque([(sy, sx)]); seen[sy, sx] = True; pix = []
                while q:
                    y, x = q.popleft(); pix.append((y, x))
                    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < H and 0 <= xx < W and mask[yy, xx] \
                                and not seen[yy, xx]:
                            seen[yy, xx] = True; q.append((yy, xx))
                if len(pix) < min_n:
                    continue
                ys = np.array([a[0] for a in pix]); xs = np.array([a[1] for a in pix])
                blobs.append({'n': int(len(pix)), 'cx': float(xs.mean()),
                              'cy': float(ys.mean()),
                              'peak': float(pm[ys, xs].max())})
    merged = True
    while merged and len(blobs) > 1:
        merged = False
        for i in range(len(blobs)):
            for j in range(i + 1, len(blobs)):
                a, b = blobs[i], blobs[j]
                ra = (a['n'] / np.pi) ** 0.5; rb = (b['n'] / np.pi) ** 0.5
                if np.hypot(a['cx'] - b['cx'], a['cy'] - b['cy']) < MERGE_R * max(ra, rb):
                    na, nb = a['n'], b['n']
                    a['cx'] = (a['cx'] * na + b['cx'] * nb) / (na + nb)
                    a['cy'] = (a['cy'] * na + b['cy'] * nb) / (na + nb)
                    a['n'] = na + nb; a['peak'] = max(a['peak'], b['peak'])
                    blobs.pop(j); merged = True; break
            if merged: break
    return blobs


def position_pass(bd):
    """Bright blobs in the pseudo-master + blink classification."""
    pm = cv2.GaussianBlur(bd['mlum'], (5, 5), 1.2)
    mask = (pm >= np.percentile(pm, PM_THR_PCT)).astype(np.int16)
    pos = flood_blobs(mask, pm)
    cap = AREA_FRAC * mask.size
    pos = [b for b in pos if b['n'] <= cap]
    st = bd['stack']
    for b in pos:
        x, y = int(round(b['cx'])), int(round(b['cy']))
        r = COH_PATCH
        ys, xs = max(0, y - r), max(0, x - r)
        prof = np.median(st[:, ys:ys + 2 * r + 1, xs:xs + 2 * r + 1], axis=(1, 2))
        b['coh'] = int((prof >= COH_THR).sum())
        b['cls'] = 'C' if 7 <= b['coh'] <= 11 else ('M' if b['coh'] >= 12 else 'S')
    print(f'positions: {len(pos)} bright blobs -> '
          f'{dict(Counter(b["cls"] for b in pos))} (C=single LED, M=merged, S=clutter)')
    return pos


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run_dir')
    ap.add_argument('--tag', default='cwc')
    ap.add_argument('--save-annot', default=None)
    ap.add_argument('--save-pile', default=None)
    ap.add_argument('--save-blobs', default=None)
    args = ap.parse_args()
    run = Path(args.run_dir)

    bd = build(run, args.tag)
    mags = [np.hypot(*bd['shifts'][p][:2]) for p in sorted(bd['shifts'])]
    confs = [bd['shifts'][p][2] for p in sorted(bd['shifts'])]
    print(f'{len(bd["planes_used"])} planes used; reg: median residual '
          f'{np.median(mags):.2f} px, max {max(mags):.2f} px, conf min {min(confs):.2f}')

    pile = bd['stack'].sum(axis=0)
    if args.save_pile:
        pos_ = np.clip(pile - float(np.median(pile)), 0, None)
        sc = max(float(np.percentile(pos_, 99.9)), 1.0)
        out = (255 * np.clip(pos_ / sc, 0, 1)).astype(np.uint8)
        cv2.imwrite(args.save_pile, out)
        print('pile-up ->', args.save_pile)

    posblobs = position_pass(bd)
    if args.save_blobs:
        json.dump(posblobs, open(args.save_blobs, 'w'), indent=1)

    if args.save_annot:
        img = bd['mimg'].copy()
        col = {'C': (0, 255, 0), 'M': (0, 200, 255), 'S': (0, 0, 255)}
        for b in posblobs:
            x, y = int(round(b['cx'])), int(round(b['cy']))
            cv2.rectangle(img, (x - 6, y - 6), (x + 6, y + 6), col[b['cls']], 1)
        cv2.imwrite(args.save_annot, cv2.cvtColor(img, cv2.COLOR_RGB2BGR))
        print('annotated master ->', args.save_annot)

    pts = np.array([(b['cx'], b['cy']) for b in posblobs])
    cc = [b for b in posblobs if b['cls'] == 'C']
    if len(cc) > 1:
        p2 = np.array([(b['cx'], b['cy']) for b in cc])
        d2m = ((p2[:, None, :] - p2[None, :, :]) ** 2).sum(-1)
        np.fill_diagonal(d2m, np.inf)
        ds = np.sqrt(d2m.min(axis=1))
        print(f'single-LED (C) count {len(cc)}; C-to-C NN pitch: '
              f'median {np.median(ds):.1f} px, p10 {np.percentile(ds, 10):.1f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())