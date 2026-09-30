#!/usr/bin/env python3
"""CWC position+identity decode (S14P): score every LED's codeword against
every lit site. S14P-1911: registration = DIRECT per-plane — every plane
registers vs the master independently (page agent decision S14P-1911: chain
totals sat 1.25-2.28 px off GT on handheld runs, and that 1-2 px total-stack
bias corrupts crowded d6 pairs at 20 px pitch; the plan's own §3 already
found plane-vs-master NEVER degrades with raw shift). Structure = the same
backwards-chain code minus the accumulation: for each plane p the NCC runs
vs the master with the integer pre-shift DISABLED (pre-shift = 0), i.e.
shift[p] = ncc(mD, dec(plane_p)); the §10d parabolic sub-peak (S14 plan
§10d, page cwcNccPeakMargin) is unchanged — formula, clamps, interior/
boundary and conditioning-guard behaviour identical to 1910.

Inputs: a pulled CWC run dir (master + 18 planes).
Outputs: <run>/ledpos.json (per confirmed LED: cx, cy, amp, amp_margin) and
         <run>/led_overlay.png (12 px box + id on the master frame).

Registration recipe (S14 plan §3/§10d, page cwcChain structure):
  - decimate master+plane to 128-wide grid (nearest-sample, W/128 = K px/step);
  - EVERY plane measures DIRECT vs the decimated master (no pre-shift, no
    carry: the dead chain remainder/pre-shift/total-carry code is removed);
  - integer NCC, ±12 steps both axes, mean-centred (ref global-mean cross
    term; cur global mean for b2) — sample-at convention: cur[y+dy, x+dx]
    aligns ref[y,x] (verified against the shipped CWCSTATS chain: 1908 dev
    <= 0.006 px on all 18 planes, tripod dev 0.000).
  - §10d sub-peak: 3-point parabola through the NCC peak along each axis on
    the 25x25 surface, delta = (y- − y+)/(2(y+ + y- − 2y0)) (the vertex of
    the parabola through (−1,y-),(0,y0),(+1,y+)), clamped to (−0.5,+0.5);
    applied ONLY when the peak stands over its shoulders (per-axis margin
    peak − max(side) >= cwcNccPeakMargin conf units, default 0.05 — measured:
    tripod per-axis margins 0.17–0.25 min, handheld degraded 0.001–0.13);
    integer pick kept at ±12 boundary peaks. Float shifts, no carry.
Read recipe (S14 plan §1/§3, r6 lesson): per-plane stacksig = master − k_p·plane
sampled at the plane's integer direct shift (k_p = per-plane median gain,
integer sample-at like the page — NO warpAffine; the old float-warp path was
sign-inverted vs the page convention and doubled handheld residuals).
Signed profile: positive = LED OFF in that plane at that site; per-LED score
= Σ_p sign[i,p] × stacksig[p] (sign +1 OFF, −1 ON); amp = score/9.
Codeword bank prefix property: first-150 d_min = 6 → the identity gate
requires a d6 margin (score minus best non-confusable competitor > gate).

Usage: venv python3 cwc_pos_decode.py <run_dir> [--amp-gate 60]
       [--margin-gate 10] [--peak-margin 0.05] [--save-overlay]
       [--save-json]
"""
import argparse, json, math, sys
from pathlib import Path
import numpy as np
import cv2

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from offline_hole_verify import decode_run  # noqa: E402

# Final S14P-1911 default set (mirrors the page CFG knobs, same NAMES):
CWC_MASK_THR = 150       # cwcMaskThr  — master blur luma = candidate LED site;
                         # 175 -> 150 decided on run s14p-1910-handheld-3
                         # (edge-on cores 50/155/179-class sit 143-172 under
                         # mask 175; mask 150 is what took handheld r3 to
                         # 197/200 = the tripod gate). Phantom watch below.
CWC_AMP_GATE = 60        # cwcAmpGate   — unchanged (per-plane mean score)
CWC_MARGIN_GATE = 10     # cwcMarginGate — 25 -> 10 on run-3 evidence (LED27
                         # margin 11.8 = the last fold-crowded true site)
CWC_NCC_PEAK_MARGIN = 0.05   # cwcNccPeakMargin — §10d conditioning floor,
                         # CONF-UNITS form, IDENTICAL to 1910 (tripod
                         # per-axis margins min 0.17/0.25; the 1908
                         # degraded burst 0.001-0.13 → refused there).
MARGIN_GATE = CWC_MARGIN_GATE   # legacy alias for older wrappers
AMP_GATE = CWC_AMP_GATE
MASK_THR = CWC_MASK_THR
SUPPRESS = 7          # per-LED site suppression window (px)
DW = 128              # decimated grid width (page cwcChain DW2)
SEARCH = 12           # ±12 steps both axes (page cwcChain)
FULLRES_RAD = 4        # ±px window of the score-time full-res NCC around the
                       # decimated seed (r3 GT error max 2.19; K/2 = 2.8)
FULLRES_STRIDE = 2     # correlation pixel stride; shift grid stays 1 px


def round_half_up(x):
    """JS Math.round (all halves away from zero at .5 exactly: floor(x+0.5));
    Python round() is banker's — NOT page-parity at exact .5 ties."""
    return int(math.floor(x + 0.5))


def hist_median(v):
    """Page histMedian (survey.html, S14B): histogram of INTEGER luma
    values, walk the cumsum past half the pixel count, return v + 0.5.
    Page-parity for per-plane gains (np.median averages the two middle
    values on even counts — a different number)."""
    h = np.bincount(np.asarray(v, dtype=np.int64).ravel(), minlength=256)
    half = h.sum() // 2
    acc = 0
    for val in range(256):
        acc += h[val]
        if acc > half:
            return val + 0.5
    return 255


def decimate(f):
    """Nearest-sample decimation to DW wide, page cwcChain `dec`."""
    h, w = f.shape
    dh = max(2, int(round(DW * h / w / 2)) * 2)
    ys = np.minimum(h - 1, np.round(np.arange(dh) * h / dh).astype(int))
    xs = np.minimum(w - 1, np.round(np.arange(DW) * w / DW).astype(int))
    return np.ascontiguousarray(f[np.ix_(ys, xs)]), dh


def shift_at(f, dx, dy):
    """out[y,x] = f[y+dy, x+dx] (page cwcChain shiftAt, decimated units)."""
    out = np.zeros_like(f)
    h, w = f.shape
    out[max(0, -dy):min(h, h - dy), max(0, -dx):min(w, w - dx)] = \
        f[max(0, dy):min(h, h + dy), max(0, dx):min(w, w + dx)]
    return out


def shift_at_full(f, dx, dy, H, W):
    """out[y, x] = f[y+dy, x+dx] at FULL resolution (page cwcDecode
    pre-sample of the raw plane at the plane's own shift, sample-at)."""
    out = np.zeros((H, W), np.float32)
    ys0, ys1 = max(0, -dy), min(H, H - dy)
    xs0, xs1 = max(0, -dx), min(W, W - dx)
    if ys1 > ys0 and xs1 > xs0:
        out[ys0:ys1, xs0:xs1] = f[ys0 + dy:ys1 + dy, xs0 + dx:xs1 + dx]
    return out


def ncc_search(ref_dec, cur_dec, want_surface=False):
    """Page cwcChain `ncc`: integer NCC over ±12 steps, sample-at (cur[y+dy,
    x+dx] aligns ref[y,x]); mean-centred — ref mean is global, cur mean is
    global (page ports both as whole-array means); conf = max(0, ncc)."""
    aC = ref_dec - ref_dec.mean()
    a2 = np.sqrt(float((aC * aC).sum()))
    mb = cur_dec.mean()
    dh_, w_ = aC.shape
    best, bdx, bdy = -1e9, 0, 0
    surf = np.zeros((2 * SEARCH + 1, 2 * SEARCH + 1), np.float64)
    for dy in range(-SEARCH, SEARCH + 1):
        y0, y1 = max(0, -dy), min(dh_, dh_ - dy)
        for dx in range(-SEARCH, SEARCH + 1):
            x0, x1 = max(0, -dx), min(w_, w_ - dx)
            v = cur_dec[y0 + dy:y1 + dy, x0 + dx:x1 + dx] - mb
            s = float((aC[y0:y1, x0:x1] * v).sum())
            b2 = np.sqrt(float((v * v).sum()))
            n = s / (a2 * b2) if (a2 * b2) > 0 else 0.0
            surf[dy + SEARCH, dx + SEARCH] = n
            if n > best:
                best, bdx, bdy = n, dx, dy
    if want_surface:
        return int(bdx), int(bdy), max(0.0, best), surf
    return int(bdx), int(bdy), max(0.0, best)


def parabolic_refine(surf, bdx, bdy, floor):
    """§10d sub-peak: 3-point parabola through the integer peak along each
    axis of the captured NCC surface; delta = (y- − y+)/(2(y+ + y- − 2y0)) —
    the vertex of the parabola through (−1,y-),(0,y0),(+1,y+) — per-axis
    guard margin = y0 − max(in-axis shoulders) >= floor (page knob
    cwcNccPeakMargin), clamp (−0.5,+0.5), integer pick kept when either
    shoulder would index outside the 25x25 surface (the ±12-boundary rule —
    also the corner-safe form of it). Fits in DECIMATED steps."""
    S = SEARCH
    cy, cx = bdy + S, bdx + S
    y0 = float(surf[cy, cx])
    fits, info = {}, {}
    for axis, b in (('x', bdx), ('y', bdy)):
        ly, lx = (cy, cx - 1) if axis == 'x' else (cy - 1, cx)
        ry, rx = (cy, cx + 1) if axis == 'x' else (cy + 1, cx)
        inb = (0 <= ly < surf.shape[0] and 0 <= lx < surf.shape[1]
               and 0 <= ry < surf.shape[0] and 0 <= rx < surf.shape[1])
        if abs(b) >= SEARCH or not inb:
            # boundary/corner peak: keep the integer pick (§10d). margin vs
            # the in-bounds shoulder(s) where available, else vs y0 itself.
            l = float(surf[ly, lx]) if 0 <= ly < surf.shape[0] and 0 <= lx < surf.shape[1] else y0
            r = float(surf[ry, rx]) if 0 <= ry < surf.shape[0] and 0 <= rx < surf.shape[1] else y0
            fits[axis] = 0.0
            info[axis] = {'applied': False, 'why': 'boundary', 'delta': 0.0,
                          'margin': float(y0 - max(l, r)) if inb else None}
            continue
        l = float(surf[ly, lx]); r = float(surf[ry, rx])
        margin = y0 - max(l, r)
        denom = 2.0 * (l + r - 2.0 * y0)
        if abs(denom) < 1e-12 or margin < floor:
            fits[axis] = 0.0
            info[axis] = {'applied': False,
                          'why': 'degenerate' if abs(denom) < 1e-12 else 'margin',
                          'delta': 0.0, 'margin': margin}
            continue
        d = (l - r) / denom
        if not np.isfinite(d):
            fits[axis] = 0.0
            info[axis] = {'applied': False, 'why': 'degenerate', 'delta': 0.0,
                          'margin': margin}
            continue
        d_clamped = max(-0.5, min(0.5, float(d) + 0.0))   # +0.0 kills -0.0
        fits[axis] = d_clamped
        info[axis] = {'applied': True, 'why': '', 'delta': float(d),
                      'clamped': bool(d != d_clamped), 'margin': margin}
    return fits, info


def ncc_refine(ref, cur, sdx, sdy, rad=FULLRES_RAD, stride=FULLRES_STRIDE):
    """Full-res integer NCC in a window around the decimated seed.

    Sample-at, same as the page: cur[y+dy, x+dx] aligns ref[y, x].
    Whole-array means (page ncc). Stride thins the correlation pixels only;
    the shift grid is 1 px. Returns (dx, dy, ncc) in source px.
    """
    H, W = ref.shape
    ma = float(ref.mean())
    mb = float(cur.mean())
    aC = ref - ma
    best, bdx, bdy = -1e9, int(sdx), int(sdy)
    for dy in range(int(sdy) - rad, int(sdy) + rad + 1):
        y0, y1 = max(0, -dy), min(H, H - dy)
        if y1 - y0 < 16:
            continue
        for dx in range(int(sdx) - rad, int(sdx) + rad + 1):
            x0, x1 = max(0, -dx), min(W, W - dx)
            if x1 - x0 < 16:
                continue
            a = aC[y0:y1:stride, x0:x1:stride]
            v = cur[y0 + dy:y1 + dy:stride, x0 + dx:x1 + dx:stride] - mb
            h = min(a.shape[0], v.shape[0])
            w = min(a.shape[1], v.shape[1])
            a = a[:h, :w]
            v = v[:h, :w]
            s = float((a * v).sum())
            a2 = float((a * a).sum()) ** 0.5
            b2 = float((v * v).sum()) ** 0.5
            n = s / (a2 * b2) if (a2 * b2) > 0 else 0.0
            if n > best:
                best, bdx, bdy = n, dx, dy
    return bdx, bdy, max(0.0, best)


def register_direct(mlum, planeL, floor, refine=True, verbose=True, tag=''):
    """DIRECT per-plane registration (S14P-1911): every plane runs the
    remainder NCC vs the master with the integer pre-shift DISABLED
    (pre-shift 0) — the 1910 backwards-chain code minus the accumulation.
    Returns (sh{}, surf{}, info{}): sh[p] = (dx, dy, conf) source-px FLOAT
    shifts (sample-at), surfaced per-plane for the refinement + the JSON."""
    mD, _ = decimate(mlum)
    pD = {p: decimate(planeL[p])[0] for p in planeL}
    sh, surf, info = {}, {}, {}
    noref = ({'x': 0.0, 'y': 0.0},
             {'x': {'applied': False, 'why': 'refine-off', 'delta': 0.0,
                    'margin': None},
              'y': {'applied': False, 'why': 'refine-off', 'delta': 0.0,
                    'margin': None}})
    for p in sorted(planeL, reverse=True):
        res = ncc_search(mD, pD[p], want_surface=True)
        rdx, rdy, conf = int(res[0]), int(res[1]), float(res[2])
        surf[p] = res[3]
        fits, info[p] = parabolic_refine(res[3], rdx, rdy, floor)
        sh[p] = ((rdx + fits['x']) * (mlum.shape[1] / DW),
                 (rdy + fits['y']) * (mlum.shape[1] / DW), conf)
    if verbose:
        mag = np.array([math.hypot(*sh[p][:2]) for p in sorted(sh)])
        nx = sum(1 for p in info if info[p]['x']['applied'])
        ny = sum(1 for p in info if info[p]['y']['applied'])
        print(f'registration {"direct per-plane" if not tag else tag}: '
              f'refine {"on" if refine else "OFF"} (floor {floor:.2f}); '
              f'axes refined x {nx}/{len(sh)} y {ny}/{len(sh)}; '
              f'shift mag med {np.median(mag):.2f} px, max {mag.max():.2f} px')
    return sh, surf, info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run_dir')
    ap.add_argument('--tag', default='cwc')
    ap.add_argument('--n', type=int, default=200)
    ap.add_argument('--amp-gate', type=float, default=CWC_AMP_GATE,
                    help='page knob cwcAmpGate (final set: 60)')
    ap.add_argument('--margin-gate', type=float, default=CWC_MARGIN_GATE,
                    help='page knob cwcMarginGate (final set: 10)')
    ap.add_argument('--mask-thr', type=float, default=CWC_MASK_THR,
                    help='page knob cwcMaskThr (final set: 150)')
    ap.add_argument('--peak-margin', type=float, default=CWC_NCC_PEAK_MARGIN,
                    help='§10d sub-peak conditioning floor, conf units '
                         '(page knob cwcNccPeakMargin)')
    ap.add_argument('--save-overlay', action='store_true')
    ap.add_argument('--save-json', action='store_true')
    ap.add_argument('--save-shifts', action='store_true',
                    help='dump per-plane direct shifts + refine info to JSON')
    ap.add_argument('--fullres-rad', type=int, default=FULLRES_RAD,
                    help='score-time full-res NCC window around the decimated '
                         'seed, px. 0 disables (1911 decimated shifts only).')
    args = ap.parse_args()
    run = Path(args.run_dir)

    frames = decode_run(run, args.tag)
    planes = {int(f['label'].split(':p')[1]): f['img'] for f in frames if ':p' in f['label']}
    masts = [f for f in frames if 'master' in f['label']]
    if not masts or len(planes) < 18:
        print(f'INCOMPLETE: master {len(masts)}, planes {len(planes)}')
        return 1
    master = masts[0]['img']
    mlum = np.asarray(master, dtype=np.float32).max(axis=2).astype(np.float32)
    H, W = mlum.shape
    K = W / DW

    planeL = {p: np.asarray(planes[p], dtype=np.float32).max(axis=2).astype(np.float32)
              for p in planes}
    tot, surf, info = register_direct(mlum, planeL, args.peak_margin)
    if args.fullres_rad > 0:
        for p in sorted(planeL):
            sdx = round_half_up(tot[p][0])
            sdy = round_half_up(tot[p][1])
            rdx, rdy, _n = ncc_refine(mlum, planeL[p], sdx, sdy,
                                      rad=args.fullres_rad)
            # keep the decimated parabolic sub-pixel fraction for bilinear stacksig
            tot[p] = (float(rdx) + (tot[p][0] - sdx),
                      float(rdy) + (tot[p][1] - sdy),
                      tot[p][2])
        print(f'full-res refine: rad {args.fullres_rad} stride {FULLRES_STRIDE}')
    shifts = [tot[p] for p in sorted(tot)]

    # per-plane stacksig EXACTLY like the page cwcDecode: bilinear resampling
    # of the RAW plane at the plane's DIRECT float shift (no warp copies);
    # k_p from the page's histMedian (page-parity per-plane gain)
    stacksig = np.empty((18, H, W), np.float32)
    kbgs = []
    mmed = hist_median(mlum)
    for j, p in enumerate(sorted(planeL)):
        tdx = float(tot[p][0])
        tdy = float(tot[p][1])
        # bilinear resample plane at float shift (x+dx, y+dy) -> out[y,x]
        mx, my = np.meshgrid(
            np.arange(W, dtype=np.float32) + np.float32(tdx),
            np.arange(H, dtype=np.float32) + np.float32(tdy))
        sh = cv2.remap(planeL[p], mx, my, cv2.INTER_LINEAR,
                       borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        kp = float(hist_median(planeL[p])) / mmed
        kbgs.append(kp)
        stacksig[j] = mlum - kp * sh

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
    mask = mb >= args.mask_thr
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
    # strongest-site-per-codeword dedup (30 Sep): with only ONE physical
    # string active, a codeword claiming 2+ sites is a bloom-skirt/ghost;
    # keep the max-amp site. Zero cost to true sites (the recipe's dup
    # anatomy: all 20-25/150-156 kept, 230 raw sites dropped).
    byled = {}
    for q in ledpos:
        cur = byled.get(q['led'])
        if cur is None or q['amp'] > cur['amp']:
            byled[q['led']] = q
    ledpos = list(byled.values())
    leds = sorted(ledpos, key=lambda q: q['led'])
    miss = [i for i in range(N) if i not in byled]
    print(f'sites {int(mask.sum() // 10)}-ish; LEDs confirmed: {len(leds)} / {N}')
    print('missing:', miss)
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
    if args.save_shifts:
        outd = run / 'direct_shifts.json'
        outd.write_text(json.dumps(
            [{'plane': int(p),
              'dx': round(tot[p][0], 3), 'dy': round(tot[p][1], 3),
              'conf': round(tot[p][2], 4),
              'refinedX': bool(info[p]['x']['applied']),
              'refinedY': bool(info[p]['y']['applied']),
              'marginX': None if info[p]['x']['margin'] is None else round(info[p]['x']['margin'], 4),
              'marginY': None if info[p]['y']['margin'] is None else round(info[p]['y']['margin'], 4),
              } for p in sorted(tot)], indent=1))
        print('shifts ->', outd)
    return 0


if __name__ == '__main__':
    sys.exit(main())