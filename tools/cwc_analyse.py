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
  7. TEST MODE (cwcTestMode=1): single-LED bit read via master×gain,
     backwards registration (plane->master), zero-error target

Usage: python3 cwc_analyse.py runs/<dir> [--save-pileup png] [--test-led N]
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
    # Labels ship run-tagged since S14L (cwc:rN:master / cwc:rN:pNN); the
    # bare cwc:master / cwc:pNN shapes predate it. A mixed pull keeps the
    # newest of each label (dict overwrite; master list takes the last).
    master, planes = [], {}
    for f in frames:
        m = __import__('re').fullmatch(
            r'cwc:(?:r(\d+):)?(master|p(\d\d))', f['label'])
        if not m:
            continue
        pno = m.group(3)
        if pno is not None:
            planes[int(pno)] = f
        else:
            master.append(f)
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
    ap.add_argument('--test-led', type=int, default=None, help='LED index to analyze in test mode (default: from cwc_stats.json)')
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
        pimg = np.asarray(planes[p]['img'])
        aligned = cv2.warpAffine(pimg, M, (mlum.shape[1], mlum.shape[0]),
                                 flags=cv2.INTER_LINEAR)
        pile += mlum - luma(aligned).astype(np.float32)
    mags = [(dx * dx + dy * dy) ** 0.5 for _, dx, dy, _ in rows]
    confs = [c for *_, c in rows]
    print(f"console reg plane-vs-master: median {np.median(mags):.2f} px, "
          f"max {max(mags):.2f} px, conf min {min(confs):.2f}")
    worst = max(rows, key=lambda r: (r[1] * r[1] + r[2] * r[2]) ** 0.5)
    if (worst[1] ** 2 + worst[2] ** 2) ** 0.5 > 8:
        print(f"  <-- JUMP at plane p{worst[0]:02d} ({worst[1]:+.1f},{worst[2]:+.1f}) conf {worst[3]:.2f}")

    pile_norm = np.clip(pile, 0, None)   # NOT (pile - pile.min()): one negative
                                         # outlier lifted the whole background
                                         # over thr -> full-frame mask -> a
                                         # giant junk blob (synth-caught 30 Sep)
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

    # TEST MODE: single-LED bit read via master×gain (backwards registration)
    if stats.get('testMode') == 1:
        test_led = args.test_led if args.test_led is not None else stats.get('testLed', 0)
        test_bits = stats.get('testBits', [])
        print(f'\n=== TEST MODE: LED {test_led} bit read ===')
        print(f'Expected bits (9 of 18): {test_bits}')

        # Codeword -> expected ON/OFF planes. Bank format: dict with
        # 'codes' = list of int-lists (tools/codewords_9of18.json); the page's
        # embedded CWC_CODES_9OF18 is the same codes as "p,p,.." strings.
        codewords_path = BASE / 'codewords_9of18.json'
        expected_on_planes = set()
        if codewords_path.exists():
            with open(codewords_path) as f:
                bank = json.load(f)
            codes_l = bank['codes'] if isinstance(bank, dict) else bank
            if test_led < len(codes_l):
                expected_on_planes = {int(p) for p in codes_l[test_led]}
        if not expected_on_planes:
            print('WARNING: no codeword for test LED', test_led, '- ON/OFF check is vacuous')

        # Locate the test LED: its hole in the pile-up = every OFF-plane minus
        # 0 = the pile-up already has it (Σ master-plane; OFF planes contribute
        # 9 dark samples). Take the accepted blob nearest the expected hole
        # (test mode: the LED's hole is the pile-up's strongest local minimum
        # among detected blobs; with one test LED lit in pattern, the blob set
        # is dominated by its 9 OFF-plane contributions).
        pile_np = pile_norm.copy()
        # Re-run the detector on the already-normalised pile (detect_holes
        # diffs its two args internally — same trick as above)
        zeros2 = np.zeros_like(pile_np)
        try:
            acc2, allblobs2, _ = detect_holes(pile_np, zeros2, thr=HOLE_THR,
                                              domk=DOMK, merge_r=MERGE_R,
                                              area_frac=AREA_FRAC)
        except TypeError:
            acc2, allblobs2 = acc, allblobs
        if not acc2 and not allblobs2:
            pass  # candidate selection no longer uses pile-up blobs (below)
        # A2 FIX (30 Sep): the test LED is identified by its READ, not by
        # blob depth. On a live string EVERY LED makes a pile-up hole (all
        # LEDs toggle in the plan), so 'deepest hole' picked an arbitrary
        # LED and read some other codeword. Every candidate site is scored
        # against the test LED's codeword; the best site's read is the
        # verdict, and on the mirrored-strings rig BOTH twin sites of the
        # test LED reading it exactly (18/18) is the expected PASS shape.
        # Registration per plane: the 1904 page's chained totals when
        # shipped (backwards seed chain), else the direct per-plane
        # phase correlate (rows[]) — pre-1904 captures stay analysable.
        chain = stats.get('chain') or []
        if len(chain) == 18:
            print('registration: page chained totals (S14P-1904 backwards chain)')
            rowmap = {int(s['k']): (s['dx'], s['dy'], s['conf']) for s in chain
                      if isinstance(s, dict)}
        else:
            print('registration: per-plane direct phase correlate (pre-1904 shape)')
            rowmap = {r[0]: (r[1], r[2], r[3]) for r in rows}
        # Site candidates: threshold ladder on the blurred master (bloom-
        # robust): cores at 254/250 + bounded mid-blobs at 224/200. GATE-RUN
        # LESSON (30 Sep): a single 200 threshold merged SKIRTS into giant
        # components — "best site" sat mid-skirt at master luma 21 and read
        # a coin-flip 9/18; but a flat 254 misses LED cores that sit in a
        # near-saturated zone (good sites read 18/18 with blurred luma only
        # 231/236). Ladder = both: big comps only from the top thresholds,
        # small comps (bounded area) also from the low thresholds.
        mlum_img = luma(mimg).astype(np.float32)
        mbA = cv2.blur(cv2.blur(mlum_img, (3, 3)), (3, 3))
        cand = set()
        for thr, amax in ((254, 10**9), (250, 10**9), (224, 60), (200, 40)):
            cmask = (mbA >= thr).astype(np.uint8)
            ncc_, lab_, stats_, cents_ = cv2.connectedComponentsWithStats(
                cmask, connectivity=4)
            for i in range(1, ncc_):
                a = int(stats_[i, cv2.CC_STAT_AREA])
                if 2 <= a <= amax:
                    cand.add((int(round(float(cents_[i][0]))),
                              int(round(float(cents_[i][1])))))
        cand = sorted(cand)
        if not cand:
            print('bit read BLOCKED: no site candidates on the master (paint too dim?)')
            return 1
        mmed = max(float(np.median(mlum_img)), 1.0)
        plist = sorted(planes)
        expv = [p in expected_on_planes for p in plist]
        scored = []
        for b in cand:
            led_xy = b
            mlum_at = float(mlum_img[int(round(led_xy[1])), int(round(led_xy[0]))])
            rr = []
            for p in plist:
                dx, dy, conf = rowmap.get(p, (0.0, 0.0, 1.0))
                plum = luma(planes[p]['img']).astype(np.float32)
                # plane->master: sample the plane at (led + shift). An OFF plane's
                # hole is the ONLY structure in the frame — phase correlation
                # finds nothing (conf ~ 0, junk shift) — so read the raw position
                # when conf is low (the s14n r6 lesson: reads against junk
                # registrations were the original failure).
                low_conf = conf < 0.3
                sx, sy = led_xy if low_conf else (led_xy[0] + dx, led_xy[1] + dy)
                sx = max(0, min(plum.shape[1] - 1, int(round(sx))))
                sy = max(0, min(plum.shape[0] - 1, int(round(sy))))
                k_p = float(np.median(plum)) / mmed     # per-plane median gain
                if k_p <= 0.05 or k_p > 20:
                    k_p = 1.0
                rr.append(float(plum[sy, sx]) / max(mlum_at * k_p, 1.0))
            rr = np.array(rr)
            top9 = np.mean(np.sort(rr)[-9:]) if len(rr) >= 9 else max(rr.max(), 1e-6)
            rn = rr / max(top9, 1e-6)                   # r6 normalisation
            got = rn > 0.5
            errs_n = int(sum(1 for g, e in zip(got, expv) if bool(g) != e))
            scored.append((errs_n, led_xy, rn))
        scored.sort(key=lambda t: t[0])
        print(f'sites scored with LED {test_led} codeword: {len(scored)}')
        for e_n, xy, _ in scored[:4]:
            print(f'  site ({xy[0]:.0f},{xy[1]:.0f}): {18-e_n}/18')
        errs, led_xy, rn = scored[0]
        print(f'best site ({led_xy[0]:.0f},{led_xy[1]:.0f}); '
              f'master luma there {mlum_img[int(round(led_xy[1])), int(round(led_xy[0]))]:.0f}')
        for p, e, v in zip(plist, expv, rn):
            dx, dy, conf = rowmap.get(p, (0.0, 0.0, 1.0))
            reg = ('raw (conf %.2f)' % conf) if conf < 0.3 else \
                  ('reg (%+.1f,%+.1f conf %.2f)' % (dx, dy, conf))
            flag = '' if (v > 0.5) == e else '  <-- MISMATCH'
            print(f'  p{p:02d}: norm={v:.2f} -> {"ON " if v > 0.5 else "OFF"} '
                  f'expected={"ON " if e else "OFF"} ({reg}){flag}')
        on_n = [v for v, e in zip(rn, expv) if e]
        off_n = [v for v, e in zip(rn, expv) if not e]
        if on_n and off_n:
            print(f'normalised reads: ON {min(on_n):.2f}-{max(on_n):.2f}, '
                  f'OFF {min(off_n):.2f}-{max(off_n):.2f} '
                  f'(ON/OFF ratio {min(on_n)/max(max(off_n),1e-6):.2f}x)')
        else:
            print('WARNING: empty ON or OFF set — bimodal check vacuous')
        verdict = 'PASS' if errs == 0 else f'FAIL ({errs} bit errors)'
        print(f'\nTEST MODE VERDICT: {verdict} — {18-errs}/18 bits correct')
        twins = [(xy, e_n) for e_n, xy, _ in scored if e_n == 0]
        if errs == 0 and len(twins) > 1:
            print(f'  twin-site PASS: {len(twins)} sites read LED {test_led} exactly '
                  f'(mirrored-string expectation)')

    return 0


if __name__ == '__main__':
    sys.exit(main())