#!/usr/bin/env python3
"""Console-side prototype of the S14P-1904 IN-PAGE decode (page recipe).

Validates, on a real pulled run, EXACTLY what the page will run, before the
JS port: backwards-seeded registration chain (Oliver, 30 Sep) + per-codeword
score-map position decode with on-the-fly shifted sampling (no warped-frame
copies — the page's memory budget).

Page recipe mirrored here:
- phase corr = phaseCorrJS semantics: decimate both luma frames to 128 x DH
  (nearest sampling), mean-centre, integer NCC over +/-12 px, best score.
  Returned (dx, dy) is the SAMPLE-AT offset: cur[y+dy, x+dx] aligns to ref.
- chain (backwards): tot[p17] = ncc(master, p17); for p in 16..0:
  pre = shift(p, tot[p+1]); rem = ncc(master, pre); tot[p] = tot[p+1] + rem.
  Integer pixel units everywhere (the page has no sub-px machinery).
- per-plane gain k_p = median(plane) / median(master).
- score at masked site s for LED i:
    score(i, s) = sum_p sign[i, p] * ( master[s + tot_p] - k_p * plane_p[s + tot_p] )
  sign ON = -1, OFF = +1 (stacksig recipe, cwc_pos_decode.py).
- sites: greedy accept over masked pixels, best-score desc, amp gate
  (score/9 >= cwcAmpGate 60) + d6 margin gate (>= 25) + 7 px suppression.
  MULTI-SITE PER CODEWORD ALLOWED (mirrored strings — never 1:1).

Usage: venv python3 cwc_page_decode_sim.py <run_dir> [--save-overlay]
"""
import argparse, json, sys
from pathlib import Path
import numpy as np
import cv2

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from offline_hole_verify import decode_run  # noqa: E402

DW = 128
SEARCH = 12
MASK_THR = 200.0
SUPPRESS = 7


def luma(img):
    return np.asarray(img, dtype=np.float32).max(axis=2)


def decimate(f):
    """Nearest-neighbour decimation to DW x DH, exactly like phaseCorrJS."""
    h, w = f.shape
    dh = max(2, int(round(DW * h / w / 2)) * 2)
    ys = np.minimum(h - 1, np.round(np.arange(dh) * h / dh).astype(int))
    xs = np.minimum(w - 1, np.round(np.arange(DW) * w / DW).astype(int))
    return f[np.ix_(ys, xs)], dh


def ncc(ref, cur):
    """phaseCorrJS port: returns (dx, dy, conf) with SAMPLE-AT semantics —
    sampling cur at +(dx, dy) aligns it to ref (verified numpy, S14B-5)."""
    a, dh = decimate(ref)
    b, _ = decimate(cur)
    aC = a - a.mean()
    bC = b - b.mean()
    a2 = np.sqrt((aC * aC).sum())
    best, bdx, bdy = -1e9, 0, 0
    dh2 = aC.shape[0]
    for dy in range(-SEARCH, SEARCH + 1):
        ys = slice(max(0, -dy), min(dh2, dh2 - dy))
        for dx in range(-SEARCH, SEARCH + 1):
            xs = slice(max(0, -dx), min(DW, DW - dx))
            v = bC[ys, xs]
            s = (aC[ys, xs] * v).sum()
            b2 = np.sqrt((v * v).sum())
            n = s / (a2 * b2) if (a2 * b2) > 0 else 0
            if n > best:
                best, bdx, bdy = n, dx, dy
    return bdx, bdy, max(0.0, best)


def shift_at(f, dx, dy):
    """out[y, x] = f[y + dy, x + dx] (sample-at convention, integer)."""
    h, w = f.shape
    out = np.zeros_like(f)
    ys_src = slice(max(0, dy), min(h, h + dy))
    xs_src = slice(max(0, dx), min(w, w + dx))
    ys_dst = slice(max(0, -dy), min(h, h - dy))
    xs_dst = slice(max(0, -dx), min(w, w - dx))
    out[ys_dst, xs_dst] = f[ys_src, xs_src]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run_dir')
    ap.add_argument('--n', type=int, default=200)
    ap.add_argument('--amp-gate', type=float, default=60.0)
    ap.add_argument('--margin-gate', type=float, default=25.0)
    ap.add_argument('--save-overlay', action='store_true')
    args = ap.parse_args()
    run = Path(args.run_dir)

    frames = decode_run(run, 'cwc')
    planes = {int(f['label'].split(':p')[1]): f['img'] for f in frames if ':p' in f['label']}
    masts = [f for f in frames if 'master' in f['label']]
    if not masts or len(planes) < 24:
        print(f'INCOMPLETE: master {len(masts)}, planes {len(planes)}')
        return 1
    masterL = luma(masts[0]['img'])
    planeL = {}
    for p in sorted(planes):
        plum = np.asarray(planes[p].convert('RGB'), dtype=np.float32)
        planeL[p] = plum.max(axis=2)
    H, W = masterL.shape

    # ---- backwards chain (page recipe) ----
    tot = {}
    dx, dy, conf = ncc(masterL, planeL[17])
    tot[17] = (dx, dy, conf)
    for p in range(16, -1, -1):
        pre = shift_at(planeL[p], tot[p + 1][0], tot[p + 1][1])
        dx, dy, conf = ncc(masterL, pre)
        tot[p] = (tot[p + 1][0] + dx, tot[p + 1][1] + dy, conf)
    print('chain totals (plane -> sample-at shift vs master):')
    for p in sorted(tot):
        dx, dy, c = tot[p]
        print(f'  p{p:02d}: ({dx:+d},{dy:+d}) conf {c:.2f}')

    # ---- gains + mask ----
    mmed = float(np.median(masterL))
    k = {p: float(np.median(planeL[p])) / max(mmed, 1.0) for p in planeL}
    mblur = cv2.GaussianBlur(masterL, (5, 5), 1.2)
    mask = mblur >= MASK_THR
    mids = np.argwhere(mask)
    print(f'site mask: {int(mask.sum())} px (of {H*W})')

    # ---- bank + signs ----
    codes = json.load(open(BASE / 'codewords_12of24.json'))   # S14R-0000
    codes = codes['codes'] if isinstance(codes, dict) else codes
    N = min(args.n, len(codes))
    bits = np.zeros((N, 24), dtype=np.int8)
    for i in range(N):
        for p in codes[i]:
            bits[i][p] = 1
    sign = (1 - 2 * bits).astype(np.float32)      # ON=-1, OFF=+1
    Dfull = (bits[:, None, :] != bits[None, :, :]).sum(-1)

    # ---- score every LED at every masked site (on-the-fly sampling) ----
    sy, sx = mids[:, 0], mids[:, 1]
    best = np.full(len(mids), -1e9, np.float32)
    argi = np.zeros(len(mids), np.int32)
    for i in range(N):
        sc = np.zeros(len(mids), np.float32)
        for p in range(24):
            tdx, tdy, _ = tot[p]
            # master sampled at s + tot_p  (site + shift), plane likewise
            yy = np.clip(sy + tdy, 0, H - 1)
            xx = np.clip(sx + tdx, 0, W - 1)
            mb = masterL[yy, xx]
            pl = planeL[p][yy, xx]
            sc += sign[i, p] * (mb - k[p] * pl)
        upd = sc > best
        best[upd] = sc[upd]
        argi[upd] = i

    # ---- local-max filter: bloom skirts decay monotonically from a core, so
    # a true LED site is a LOCAL MAX of bestS among masked pixels within ±3 px
    idx_of = {}
    for jj, ij in enumerate(mids):
        idx_of[ij[0] * W + ij[1]] = jj
    lmax = np.zeros(len(mids), bool)
    for jj, ij in enumerate(mids):
        y, x = int(ij[0]), int(ij[1])
        v = best[jj]
        ok = True
        for dyy in (-3, -2, -1, 0, 1, 2, 3):
            for dxx in (-3, -2, -1, 0, 1, 2, 3):
                if not dyy and not dxx:
                    continue
                nb = idx_of.get((y + dyy) * W + (x + dxx))
                if nb is not None and best[nb] > v:
                    ok = False
                    break
            if not ok:
                break
        lmax[jj] = ok

    # ---- greedy accept: amp + d6 margin + suppression (multi-site OK) ----
    order = np.argsort(best)[::-1]
    used = np.zeros(len(mids), bool)
    sites = []
    Hm, Wm = mask.shape
    for j in order:
        if used[j] or not lmax[j]:
            continue
        i = int(argi[j])
        amp = float(best[j]) / 12.0
        cand = np.where(Dfull[i] >= 8)[0]
        y, x = int(sy[j]), int(sx[j])
        # competitor score AT THIS SITE (recompute per candidate)
        sc_site = np.zeros(N, np.float32)
        for c in cand:
            s = 0.0
            for p in range(24):
                tdx, tdy, _ = tot[p]
                yyc = min(H - 1, max(0, y + tdy)); xxc = min(W - 1, max(0, x + tdx))
                s += sign[c, p] * (masterL[yyc, xxc] - k[p] * planeL[p][yyc, xxc])
            sc_site[c] = s
        margin = (float(best[j]) - float(sc_site[cand].max())) / 12.0
        if amp < args.amp_gate or margin < args.margin_gate:
            continue
        used[j] = True
        y0, x0 = max(0, y - SUPPRESS // 2), max(0, x - SUPPRESS // 2)
        blk = (sy >= y0) & (sy <= y + SUPPRESS // 2) & (sx >= x0) & (sx <= x + SUPPRESS // 2)
        used |= blk
        sites.append({'led': i, 'cx': x, 'cy': y,
                      'amp': round(amp, 1), 'margin': round(margin, 1)})

    leds = sorted(sites, key=lambda q: q['led'])
    print(f'\nsites confirmed: {len(sites)} (LEDs {len(set(s["led"] for s in sites))} distinct of {N})')
    mult = {}
    for s in sites:
        mult[s['led']] = mult.get(s['led'], 0) + 1
    hist = {}
    for v in mult.values():
        hist[v] = hist.get(v, 0) + 1
    print('site-multiplicity histogram (led -> n sites):', dict(sorted(hist.items())))
    amps = [s['amp'] for s in leds]
    if amps:
        print(f'amp: min {min(amps)}, med {sorted(amps)[len(amps)//2]}, max {max(amps)}')

    out = run / 'page_decode_sim.json'
    out.write_text(json.dumps(leds, indent=1))
    print('sites ->', out)
    # console cwc_pos_decode comparison when present
    ref = run / 'ledpos.json'
    if ref.exists():
        rp = json.loads(ref.read_text())
        rmap = {(p['cx'], p['cy']): p['led'] for p in rp}
        near = 0
        for s in leds:
            for (x, y) in rmap:
                if abs(x - s['cx']) <= 2 and abs(y - s['cy']) <= 2:
                    near += 1
                    break
        print(f'vs console ledpos.json ({len(rp)} sites): {near}/{len(leds)} sim sites have a console site within 2 px')

    if args.save_overlay:
        img = np.asarray(masts[0]['img']).copy()
        for s in leds:
            x, y = s['cx'], s['cy']
            cv2.rectangle(img, (x - 6, y - 6), (x + 6, y + 6), (0, 255, 0), 1)
            cv2.putText(img, str(s['led']), (x - 10, y - 9),
                        cv2.FONT_HERSHEY_PLAIN, 0.7, (0, 255, 160), 1)
        cv2.imwrite(str(run / 'page_decode_sim.png'), cv2.cvtColor(img, cv2.COLOR_RGB2BGR))
        print('overlay ->', run / 'page_decode_sim.png')
    return 0


if __name__ == '__main__':
    sys.exit(main())