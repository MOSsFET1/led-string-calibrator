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
            print('bit read BLOCKED: no pile-up blobs — cannot locate LED')
            return 1
        # The test LED's pile-up hole is the blob with the deepest pile-up
        # deficit (9 dark samples vs impostors' <9); pick the peak hole.
        cand = acc2 or allblobs2
        hole = max(cand, key=lambda b: b['peak'])
        hole_r = max(2.0, (hole['n'] / 3.14159) ** 0.5)
        led_xy = (hole['cx'], hole['cy'])
        print(f"LED hole: ({led_xy[0]:.0f},{led_xy[1]:.0f}) peak {hole['peak']:.0f} n={hole['n']}")

        # Backwards registration is ALREADY DONE: rows[] holds per-plane
        # (dx, dy) plane->master shifts (console-side ground truth). Sample
        # bit = plane_luma(led + shift) / (master_luma(led) x k_p)
        mlum_at_led = luma(mimg).astype(np.float32)[int(round(led_xy[1])), int(round(led_xy[0]))]
        print(f'master luma at LED: {mlum_at_led:.0f}')
        results = []
        for p in sorted(planes):
            dx, dy, conf = next(((r[1], r[2], r[3]) for r in rows if r[0] == p), (0.0, 0.0, 1.0))
            plum = luma(planes[p]['img']).astype(np.float32)
            # plane->master: sample the plane at (led + shift). An OFF plane's
            # hole is the ONLY structure in the frame — phase correlation
            # finds nothing (conf ~ 0, junk shift) — so read the raw position
            # when conf is low and say so (the s14n r6 lesson: reads against
            # junk registrations were the original failure).
            low_conf = conf < 0.3
            sx, sy = led_xy if low_conf else (led_xy[0] + dx, led_xy[1] + dy)
            sx = max(0, min(plum.shape[1]-1, sx)); sy = max(0, min(plum.shape[0]-1, sy))
            plane_lum_at_led = plum[int(round(sy)), int(round(sx))]
            # k_p: per-plane gain = median(non-LED pixels in plane / master) —
            # non-LED = pixels far from every pile-up hole; approximate with
            # the frame's median luma ratio (test string: one LED hole is
            # tiny; the median sees ~all non-LED pixels)
            k_p = float(np.median(plum)) / max(float(np.median(mlum)), 1.0)
            if k_p <= 0.05 or k_p > 20:
                print(f'  p{p:02d}: WARNING bogus gain k={k_p:.2f} (flat/black frame?) — clamped to 1.0')
                k_p = 1.0
            read = plane_lum_at_led / max(mlum_at_led * k_p, 1.0)
            exp = 'ON ' if p in expected_on_planes else 'OFF'
            results.append((p, exp, read, conf))
            reg = 'raw (conf %.2f)' % conf if low_conf else 'reg (%+.1f,%+.1f conf %.2f)' % (dx, dy, conf)
            print(f'  p{p:02d}: read={read:.2f} expected={exp} ({reg}, k={k_p:.2f})')
        # r6 normalisation: divide each read by its own top-9 mean (the 9
        # highest of the 18) — residual bloom/limb darkening divides out and
        # ON lands ~1.0, OFF ~0.3-0.5. Then gate ON/OFF at the 0.5 boundary.
        reads = np.array([r for _, _, r, _ in results])
        top9 = np.mean(np.sort(reads)[-9:]) if len(reads) >= 9 else max(reads.max(), 1e-6)
        errs = 0
        for (p, e, r, c), rnorm in zip(results, reads / max(top9, 1e-6)):
            got = 'ON ' if rnorm > 0.5 else 'OFF'
            if got.strip() != e.strip():
                errs += 1
                print(f'  BIT ERROR p{p:02d}: norm read {rnorm:.2f} -> {got}, expected {e}')
        on_n = [rn for rn, (_, e, _, _) in zip(reads / max(top9, 1e-6), results) if e.strip() == 'ON']
        off_n = [rn for rn, (_, e, _, _) in zip(reads / max(top9, 1e-6), results) if e.strip() == 'OFF']
        if on_n and off_n:
            print(f'normalised reads: ON {min(on_n):.2f}-{max(on_n):.2f}, '
                  f'OFF {min(off_n):.2f}-{max(off_n):.2f} '
                  f'(ON/OFF ratio {min(on_n)/max(max(off_n),1e-6):.2f}x)')
        else:
            print('WARNING: empty ON or OFF set — bimodal check vacuous')
        verdict = 'PASS' if errs == 0 else f'FAIL ({errs} bit errors)'
        print(f'\nTEST MODE VERDICT: {verdict} — {18-errs}/18 bits correct')

    return 0


if __name__ == '__main__':
    sys.exit(main())