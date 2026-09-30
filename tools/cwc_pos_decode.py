#!/usr/bin/env python3
"""CWC position+identity decode (S14P): score every LED's codeword against
every lit site, per-LED local-peak site pick, d6 margin gate.

Inputs: a pulled CWC run dir (master + 18 planes).
Outputs: <run>/ledpos.json (per confirmed LED: cx, cy, amp, amp_margin) and
         <run>/led_overlay.png (12 px box + id on the master frame).

Read recipe (S14 plan §1/§3, r6 lesson): plane registered to MASTER
(phaseCorrelate sqrt-luma); signed profile stacksig = mb - k*rw
(positive = LED OFF in that plane at that site); per-LED score =
Σ_p sign[i,p] × stacksig[p] (sign +1 OFF, −1 ON); amp = score/9.
Codeword bank prefix property: first-150 d_min = 6 → the identity gate
requires a d6 margin (score minus best non-confusable competitor > gate).

Usage: venv python3 cwc_pos_decode.py <run_dir> [--amp-gate 90]
       [--margin-gate 30] [--save-overlay] [--save-json]
"""
import argparse, json, sys
from pathlib import Path
import numpy as np
import cv2

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from offline_hole_verify import decode_run, luma  # noqa: E402
from cwc_analyse import reg_residual              # noqa: E402

MASK_THR = 200        # master blur luma = candidate LED site
AMP_GATE = 90         # per-plane mean amplitude (of the 18-plane score)
MARGIN_GATE = 30      # d6-margin (same units)
SUPPRESS = 7          # per-LED site suppression window (px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run_dir')
    ap.add_argument('--tag', default='cwc')
    ap.add_argument('--n', type=int, default=200)
    ap.add_argument('--amp-gate', type=float, default=AMP_GATE)
    ap.add_argument('--margin-gate', type=float, default=MARGIN_GATE)
    ap.add_argument('--save-overlay', action='store_true')
    ap.add_argument('--save-json', action='store_true')
    args = ap.parse_args()
    run = Path(args.run_dir)

    frames = decode_run(run, args.tag)
    planes = {int(f['label'].split(':p')[1]): f['img'] for f in frames if ':p' in f['label']}
    masts = [f for f in frames if 'master' in f['label']]
    if not masts or len(planes) < 18:
        print(f'INCOMPLETE: master {len(masts)}, planes {len(planes)}')
        return 1
    master = masts[0]['img']
    mlum = np.asarray(master, dtype=np.int16).max(axis=2).astype(np.float32)

    regd, mb_reg, shifts = [], [], []
    for p in sorted(planes):
        plum = np.asarray(planes[p], dtype=np.int16).max(axis=2).astype(np.float32)
        dx, dy, conf = reg_residual(mlum, plum)
        shifts.append((dx, dy, conf))
        M = np.float32([[1, 0, dx], [0, 1, dy]])
        regd.append(cv2.warpAffine(plum, M, (mlum.shape[1], mlum.shape[0]),
                                   flags=cv2.INTER_LINEAR))
        mb_reg.append(cv2.warpAffine(mlum, M, (mlum.shape[1], mlum.shape[0]),
                                     flags=cv2.INTER_LINEAR))
    kbgs = [float(np.median(mw)) / max(float(np.median(rw)), 1.0)
            for rw, mw in zip(regd, mb_reg)]
    stacksig = np.stack([mw - k * rw for rw, mw, k in zip(regd, mb_reg, kbgs)])

    codes = json.load(open(BASE / 'codewords_9of18.json'))
    codes = codes['codes'] if isinstance(codes, dict) else codes
    N = min(args.n, len(codes))
    bits = np.zeros((N, 18), dtype=np.int16)
    for i in range(N):
        for p in codes[i]:
            bits[i][p] = 1
    sign = (1 - 2 * bits[:N]).astype(np.float32)      # ON=-1, OFF=+1

    # score[i](x,y) = Σ_p sign[i,p] × stacksig[p](x,y)  (vectorised)
    Sg = stacksig.astype(np.float32)
    sc = np.tensordot(sign, Sg, axes=([1], [0]))       # (N, H, W)
    best = sc.max(axis=0)
    argi = sc.argmax(axis=0)

    mb = cv2.GaussianBlur(mlum, (5, 5), 1.2)
    mask = mb >= MASK_THR
    # pairwise distance for the d6 margin
    D = (bits[:N, None, :] != bits[:N, None, :]).any(-1) if False else None
    Dfull = (bits[:N][:, None, :] != bits[:N][None, :, :]).sum(-1)

    ledpos = []
    used = np.zeros(mask.shape, bool)
    amap = np.where(mask, best, -1e9)
    # per-LED peak site with suppression (high-score wins, 1 site per LED)
    order = np.argsort(amap.ravel())[::-1]
    for j in order:
        y, x = divmod(int(j), mask.shape[1])
        if not mask[y, x] or used[y, x]:
            continue
        i = int(argi[y, x])
        amp = float(best[y, x]) / 9.0
        row = Dfull[i]
        cand = np.where(row >= 6)[0]
        s2 = float(sc[cand, y, x].max())
        margin = (float(best[y, x]) - s2) / 9.0
        if amp < args.amp_gate or margin < args.margin_gate:
            continue
        used[max(0, y - SUPPRESS // 2): y + SUPPRESS // 2 + 1,
             max(0, x - SUPPRESS // 2): x + SUPPRESS // 2 + 1] = True
        ledpos.append({'led': i, 'cx': x, 'cy': y,
                       'amp': round(amp, 1), 'margin': round(margin, 1)})
    leds = sorted(ledpos, key=lambda q: q['led'])
    print(f'sites {int(mask.sum() // 10)}-ish; LEDs confirmed: {len(leds)} / {N}')
    amps = [p['amp'] for p in leds]
    if amps:
        print(f'amp: min {min(amps)}, med {sorted(amps)[len(amps)//2]}, max {max(amps)}')
        pts = np.array([(p['cx'], p['cy']) for p in leds])
        ds = np.hypot(np.diff(pts[:, 0]), np.diff(pts[:, 1]))
        if len(ds):
            print(f'consecutive-id pitch: median {np.median(ds):.1f} px, '
                  f'min {ds.min():.1f}, max {ds.max():.1f}')
    if args.save_json:
        out = run / 'ledpos.json'
        out.write_text(json.dumps(leds, indent=1))
        print('positions ->', out)
    if args.save_overlay:
        img = np.asarray(master).copy()
        for p in leds:
            x, y = p['cx'], p['cy']
            cv2.rectangle(img, (x - 6, y - 6), (x + 6, y + 6), (0, 255, 0), 1)
            cv2.putText(img, str(p['led']), (x - 10, y - 9),
                        cv2.FONT_HERSHEY_PLAIN, 0.7, (0, 255, 160), 1)
        cv2.imwrite(str(run / 'led_overlay.png'),
                    cv2.cvtColor(img, cv2.COLOR_RGB2BGR))
        print('overlay ->', run / 'led_overlay.png')
    return 0


if __name__ == '__main__':
    sys.exit(main())