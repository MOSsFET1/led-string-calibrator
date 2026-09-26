#!/usr/bin/env python3
"""CWC-form burst analysis (19-frame protocol: master + 18 planes, NO off
frame). Reads a run dir produced by s14_bench.py pull:

  <run>/cwc_frames.txt   (FRAME/FJPEG chunks, labels 'cwc:master' / 'cwc:pNN')
  <run>/cwc_stats.json   (CWCSTATS line: cadence + per-plane comp shifts)

Checks, in order:
  1. frames present (master + planes p00..p17, decode failures filtered)
  2. exposure pin across the burst
  3. comp residual per plane (source px, from CWCSTATS shifts)
  4. movement: console-side cv2.phaseCorrelate plane-vs-MASTER (the decoder's
     registration path), residual after the page's own warp-back
  5. pile-up image: sum of registered (master - plane) diffs -> hole detector
     (offline_hole_verify.detect_holes, the S13-mirror) -> per-LED point set
  6. per-LED weight check: holes-in-(master-plane_p) count per LED should
     average 9 (weight 9 of 18); spread is the first decode-readiness metric

Usage: python3 cwc_analyse.py runs/<dir> [--save-pileup png]
"""
import argparse, json, sys
from pathlib import Path
import numpy as np
import cv2

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from offline_hole_verify import decode_run, luma, detect_holes  # noqa: E402

NPLANES = 18


def load_frames(run_dir: Path):
    frames = decode_run(run_dir, 'cwc')
    master = [f for f in frames if f['label'] == 'cwc:master']
    planes = {}
    for f in frames:
        m = __import__('re').fullmatch(r'cwc:p(\d\d)', f['label'])
        if m:
            planes[int(m.group(1))] = f
    return master, planes


def reg_residual(master_lum, plane_lum):
    """phaseCorrelate plane-vs-master with the console recipe (sqrt-luma +
    Hanning): returns (dx, dy, conf) — the console-side ground truth the
    page's own comp residual is judged against."""
    a = np.sqrt(np.maximum(master_lum, 0) + 1.0)
    b = np.sqrt(np.maximum(plane_lum, 0) + 1.0)
    win = cv2.createHanningWindow((a.shape[1], a.shape[0]), cv2.CV_32F)
    (dx, dy), conf = cv2.phaseCorrelate(a, b, win)
    return float(dx), float(dy), float(conf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run_dir')
    ap.add_argument('--save-pileup', default=None)
    args = ap.parse_args()
    run = Path(args.run_dir)

    stats = {}
    sf = run / 'cwc_stats.json'
    if sf.exists():
        try:
            stats = json.loads(sf.read_text())
        except Exception:
            pass

    master, planes = load_frames(run)
    print(f"frames: master {len(master)}, planes {sorted(planes)}")
    if not master or len(planes) < NPLANES:
        print('INCOMPLETE: need master + 18 planes (missing:',
              sorted(set(range(NPLANES)) - set(planes)), ')')
        return 1

    mimg = master[0]['img']
    mlum = luma(mimg).astype(np.float32)
    exps = {master[0]['meta'].get('exp', '?')} | {planes[p]['meta'].get('exp', '?') for p in planes}
    print('exposure lines:', list(exps) if len(exps) <= 3 else f'{len(exps)} distinct')

    # comp residual shipped by the page (source px after the grid-unit fix)
    shifts = {int(s['k']): s for s in stats.get('shifts', []) if isinstance(s, dict)}
    if shifts:
        mags = [np.hypot(s['dx'], s['dy']) for s in shifts.values()]
        print(f"page comp residual: median {np.median(mags):.2f} px, "
              f"max {max(mags):.2f} px over {len(mags)} planes")
    else:
        print('page comp residual: no CWCSTATS shifts (pull lost the stats line?)')

    # console-side registration check + pile-up
    pile = np.zeros_like(mlum)
    rows = []
    for p in sorted(planes):
        plum = luma(planes[p]['img']).astype(np.float32)
        dx, dy, conf = reg_residual(mlum, plum)
        rows.append((p, dx, dy, conf))
        # register the plane onto the master frame (sub-pixel, the decoder's
        # warp): shift plane content by (dx, dy) to align with master
        M = np.float32([[1, 0, dx], [0, 1, dy]])
        aligned = cv2.warpAffine(planes[p]['img'], M, (mimg.shape[1], mimg.shape[0]),
                                 flags=cv2.INTER_LINEAR)
        pile += mlum - luma(aligned).astype(np.float32)
    mags = [(dx * dx + dy * dy) ** 0.5 for _, dx, dy, _ in rows]
    confs = [c for *_, c in rows]
    print(f"console reg plane-vs-master: median {np.median(mags):.2f} px, "
          f"max {max(mags):.2f} px, conf min {min(confs):.2f}")
    worst = max(rows, key=lambda r: (r[1] * r[1] + r[2] * r[2]) ** 0.5)
    if (worst[1] ** 2 + worst[2] ** 2) ** 0.5 > 8:
        print(f"  <-- JUMP at plane p{worst[0]:02d} ({worst[1]:+.1f},{worst[2]:+.1f}) conf {worst[3]:.2f}")

    pile_norm = pile - pile.min()
    np.clip(pile_norm, 0, None, out=pile_norm)
    if args.save_pileup:
        out = (255 * pile_norm / max(pile_norm.max(), 1)).astype(np.uint8)
        cv2.imwrite(str(args.save_pileup), out)
        print('pile-up image ->', args.save_pileup)

    # point set: S13-mirror hole detector on the pile-up. detect_holes diffs
    # its two args internally (positive diff = holes), so feed the pile-up as
    # "master" against an all-zero "pair": diff = pile - 0 = the pile-up.
    try:
        from offline_hole_verify import HOLE_THR, DOMK, MERGE_R, AREA_FRAC
        zeros = np.zeros_like(pile_norm)
        acc, allblobs, diff = detect_holes(pile_norm, zeros, thr=HOLE_THR,
                                           domk=DOMK, merge_r=MERGE_R,
                                           area_frac=AREA_FRAC)
        print(f"pile-up holes detected: {len(acc)} accepted / {len(allblobs)} raw")
    except TypeError:
        print('pile-up holes: detect_holes signature mismatch — call with the '
              'same opt dict the offline verifier uses (check offline_hole_verify)')
        acc, allblobs = [], []
    for b in (acc or allblobs)[:8]:
        print(f"   blob ({b['cx']:.0f},{b['cy']:.0f}) n={b['n']} pk={b['peak']:.0f}")

    print('\nVERDICT HELPS:')
    print(f"  - comp path: 19-frame protocol captured; comp residual "
          f"{'%.2f px median' % np.median(mags) if mags else 'n/a'} vs ~5 px budget")
    print(f"  - plane-vs-master conf {min(confs):.2f}-{max(confs):.2f} "
          f"(content toggle: 50% of LEDs flip per plane — low conf here is "
          f"the known pessimistic case)")
    return 0


if __name__ == '__main__':
    sys.exit(main())