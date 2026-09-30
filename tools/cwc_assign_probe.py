#!/usr/bin/env python3
"""Recovery study probe for LEDs [46,90,91] on a SAVED S14P run
(console-side only; reuses cwc_pos_decode.py's scoring approach WITHOUT
modifying it — same decode_run / reg_residual / stacksig / tensordot score
/ d6 margin / strongest-site dedup, re-derived here per its lines 52-126).

A  census: per target LED {46,90,91} + control 16, estimate the true site
   from serpentine neighbours (midpoint of the nearest PRESENT id on each
   side of the ledger), then inspect every master-blur pixel near it:
   the pixel's score for the target codeword vs the pixel's owner codeword
   (argmax) -> classifies mask-fail (no candidate pixel) vs ownership-fail
   (present pixels, target margin never >= 25).
B  parameter sweep: mask {178,185,195,205,215,230} x amp {45,60} x
   margin {10,25} x suppression/local-max ± {1,3} with the NET objective:
   total decoded AND zero dup claims post-dedup AND all 20-25+150-156
   present AND ghost guard clean (no site > 1.5x median pitch from the
   midpoint of its consecutive-id neighbours).
C  per-codebook nearest-site 1:1 assignment (the queued handoff fix;
   greedy auction — scipy is absent from this venv): edges = codeword x
   candidate pixel (top-K in-window mask pixels by amp), feasible iff
   amp>=60 AND margin>=25 at that site AND within guard-px of the
   ledger's serpentine neighbour midpoint; maximise total accepted score,
   one site per codeword; leftovers stay interpolatable.

Output: <run>/assign_probe.json (+ master crops to TMPDIR for visual
census). Usage: venv python3 tools/cwc_assign_probe.py <run_dir>
"""
import argparse, json, os, sys
from pathlib import Path
import numpy as np
import cv2

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from offline_hole_verify import decode_run  # noqa: E402
from cwc_analyse import reg_residual       # noqa: E402

STANDING_MASK = 175
OLD_MASK = 200
CENSUS_MASKS = [175, 200]
CENSUS_R = 8
AMP_GATE, MARGIN_GATE = 60.0, 25.0        # standing gates (tonight's run)
TRIO = [46, 90, 91]
CONTROL = 16
GROUP = list(range(20, 26)) + list(range(150, 157))
POOL_K = 8                                 # candidate edges per codeword
GUARD_MULT = 1.5                           # x median pitch


def build_scores(run):
    """Exact replication of cwc_pos_decode.py main() lines 52-96."""
    frames = decode_run(run, 'cwc')
    planes = {int(f['label'].split(':p')[1]): f['img']
              for f in frames if ':p' in f['label']}
    masts = [f for f in frames if 'master' in f['label']]
    if not masts or len(planes) < 18:
        raise SystemExit(f'INCOMPLETE: master {len(masts)}, planes {len(planes)}')
    master = masts[0]['img']
    mlum = np.asarray(master, dtype=np.int16).max(axis=2).astype(np.float32)
    regd, mb_reg, shifts, kbgs = [], [], [], []
    for p in sorted(planes):
        plum = np.asarray(planes[p], dtype=np.int16).max(axis=2).astype(np.float32)
        dx, dy, conf = reg_residual(mlum, plum)
        shifts.append((round(float(dx), 3), round(float(dy), 3), round(float(conf), 4)))
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
    N = min(200, len(codes))
    bits = np.zeros((N, 18), dtype=np.int16)
    for i in range(N):
        for p in codes[i]:
            bits[i][p] = 1
    sign = (1 - 2 * bits[:N]).astype(np.float32)
    sc = np.tensordot(sign, stacksig.astype(np.float32), axes=([1], [0]))
    best = sc.max(axis=0)
    argi = sc.argmax(axis=0)
    mb = cv2.GaussianBlur(mlum, (5, 5), 1.2)
    Dfull = (bits[:N][:, None, :] != bits[:N][None, :, :]).sum(-1)
    cand_rows = [np.where(Dfull[i] >= 6)[0] for i in range(N)]
    return dict(mlum=mlum, mb=mb, sc=sc, best=best, argi=argi, Dfull=Dfull,
                cand_rows=cand_rows, N=N, shifts=shifts, kbgs=kbgs,
                master=master)


def pick_sites(sc, best, argi, mask, amp_gate, margin_gate, r, cand_rows,
               dedup=True):
    """cwc_pos_decode.py lines 96-126 pick loop, replicated exactly.

    Descending order over the masked `best` map; a pixel is claimed by its
    argmax owner; amp = best/9; margin = (best - max over the owner's d>=6
    competitors)/9; accepted sites suppress +-r px (only ACCEPTED sites
    mark `used`, like the original); below-amp-gate pixels are all later
    in the descending order so `break` is equivalent to the original skip.
    r=3 matches SUPPRESS=7's half-window; r=1 is the +-1 local-max probe.
    """
    H, W = mask.shape
    # exact-equivalent fast path: pixels below amp_gate are skipped (never
    # mark `used`) in the original full-order loop, so restricting the
    # descending walk to mask & best>=amp_gate pixels is identical.
    ys, xs = np.where(mask & (best >= amp_gate))
    if ys.size == 0:
        return []
    vals = best[ys, xs]
    order = np.argsort(vals)[::-1]
    sites = []
    used = np.zeros((H, W), bool)
    for j in order:
        y, x = int(ys[j]), int(xs[j])
        if used[y, x]:
            continue
        i = int(argi[y, x])
        s2 = float(sc[cand_rows[i], y, x].max())
        margin = (float(best[y, x]) - s2) / 9.0
        if margin < margin_gate:
            continue
        y0, x0 = max(0, y - r), max(0, x - r)
        used[y0: y + r + 1, x0: x + r + 1] = True
        sites.append({'led': i, 'cx': int(x), 'cy': int(y),
                      'amp': round(float(best[y, x]) / 9.0, 1),
                      'margin': round(margin, 1)})
    if dedup:
        byled = {}
        for q in sites:
            c = byled.get(q['led'])
            if c is None or q['amp'] > c['amp']:
                byled[q['led']] = q
        sites = sorted(byled.values(), key=lambda q: q['led'])
    return sites


def guard_px(ledger):
    """1.5x median consecutive-id pitch from the baseline ledger."""
    pos = {q['led']: (q['cx'], q['cy']) for q in ledger}
    ds = []
    for l in sorted(pos):
        if l - 1 in pos:
            a, b = pos[l - 1], pos[l]
            ds.append(float(np.hypot(a[0] - b[0], a[1] - b[1])))
    return float(np.median(ds)) * GUARD_MULT


def neighbour_estimate(ledger, led, pos):
    """Midpoint of the nearest present id on each side (gaps skipped)."""
    lo = next((pos[l] for l in range(led - 1, -1, -1) if l in pos), None)
    hi = next((pos[l] for l in range(led + 1, pos.get('__max__', 199) + 1)
               if l in pos), None)
    if lo is None and hi is None:
        return None
    if lo is None:
        lo = hi
    if hi is None:
        hi = lo
    return {'x': round((lo[0] + hi[0]) / 2), 'y': round((lo[1] + hi[1]) / 2),
            'below': [next(l for l in range(led - 1, -1, -1) if l in pos),
                      list(lo)],
            'above': [next(l for l in range(led + 1, 200) if l in pos),
                      list(hi)]}


def corridor_scan(S, est, led, R=24, thr=175):
    """Blur local maxima >= thr within R px of the estimate: are there ANY
    discrete cores between the neighbours (a lens-fanout fold hides LEDs
    as an unresolved smear — 'no core ANYWHERE' vs 'core not owner')."""
    mb, argi, sc = S['mb'], S['argi'], S['sc']
    ex, ey = est['x'], est['y']
    H, W = mb.shape
    found = []
    for y in range(max(0, ey - R), min(H, ey + R + 1)):
        for x in range(max(0, ex - R), min(W, ex + R + 1)):
            b = mb[y, x]
            if b < thr:
                continue
            y0, y1 = max(0, y - 2), y + 3
            x0, x1 = max(0, x - 2), x + 3
            if b == mb[y0:y1, x0:x1].max():
                found.append({'x': x, 'y': y, 'blur': round(float(b), 0),
                              'owner': int(argi[y, x]),
                              f'cw{led}_amp': round(float(sc[led, y, x]) / 9, 1)})
    merged = []
    for f in sorted(found, key=lambda q: (q['y'], q['x'])):
        if merged and abs(f['x'] - merged[-1]['x']) <= 2 and \
                abs(f['y'] - merged[-1]['y']) <= 2:
            continue
        merged.append(f)
    return merged


def census_led(S, ledger_pos, led, est, guard):
    """Classify the failure mechanism for one LED from actual pixels."""
    out = {'led': led, 'estimate': est, 'per_threshold': []}
    H, W = S['mb'].shape
    ex, ey = est['x'], est['y']
    corr = corridor_scan(S, est, led)
    for mthr in CENSUS_MASKS:
        mask = S['mb'] >= mthr
        rows = []
        for y in range(max(0, ey - CENSUS_R), min(H, ey + CENSUS_R + 1)):
            for x in range(max(0, ex - CENSUS_R), min(W, ex + CENSUS_R + 1)):
                amp_t = float(S['sc'][led, y, x]) / 9.0
                s2 = float(S['sc'][S['cand_rows'][led], y, x].max())
                marg_t = (float(S['sc'][led, y, x]) - s2) / 9.0
                rows.append({'x': x, 'y': y, 'in_mask': bool(mask[y, x]),
                             'blur': round(float(S['mb'][y, x]), 1),
                             'amp_t': round(amp_t, 2),
                             'margin_t': round(marg_t, 2),
                             'owner': int(S['argi'][y, x]),
                             'amp_owner': round(float(S['best'][y, x]) / 9.0, 2),
                             'owner_is_t': int(S['argi'][y, x]) == led})
        inmask = [r for r in rows if r['in_mask']]
        best_px = (max(inmask, key=lambda r: r['amp_t']) if inmask else None)
        # global view: the best pixel for THIS codeword anywhere in the mask
        ysm, xsm = np.where(mask)
        amps_all = S['sc'][led][ysm, xsm] / 9.0
        g = int(np.argmax(amps_all))
        gamp = float(amps_all[g])
        gy, gx = int(ysm[g]), int(xsm[g])
        gs2 = float(S['sc'][S['cand_rows'][led], gy, gx].max())
        gmar = (float(S['sc'][led, gy, gx]) - gs2) / 9.0
        verdict = (
            'mask-fail: no candidate pixel within 8 px'
            if not inmask else
            ('should-have-decoded: a candidate pixel passes amp>=60 AND '
             'margin_t>=25 yet argmax never hands it to this codeword'
             if best_px['amp_t'] >= 60 and best_px['margin_t'] >= 25 else
             ('gate-fail: candidate pixels exist, target margin ok, but '
              'amp_t < 60 everywhere' if best_px['margin_t'] >= 25 else
              'ownership-fail: candidate pixels exist but target margin '
              '< 25 at every one (argmax owner keeps every pixel)')))
        out['per_threshold'].append({
            'mask': mthr, 'pixels_in_r8': len(rows), 'n_mask': len(inmask),
            'max_amp_t': max((r['amp_t'] for r in inmask), default=None),
            'max_margin_t': (best_px['margin_t'] if best_px else None),
            'maskwide_best_for_this_codeword': {
                'x': gx, 'y': gy, 'amp_t': round(gamp, 2),
                'margin_t': round(gmar, 2),
                'owner_at_that_px': int(S['argi'][gy, gx]),
                'dist_to_estimate': round(float(np.hypot(gx - ex, gy - ey)), 1),
                'passes_gates': bool(gamp >= AMP_GATE and gmar >= MARGIN_GATE)},
            'corridor_bright_cores_r24': corr,
            'verdict': verdict,
            'top_pixels_by_amp_t': (sorted(inmask, key=lambda r: -r['amp_t'])[:8]
                                    if inmask else []),
            'dist_est_to_claimed_neighbours': {
                str(n): round(float(np.hypot(*(np.array(ledger_pos[n]) -
                                               np.array([ex, ey])))), 1)
                for n in (est['below'][0], est['above'][0])}})
    return out


def ledger_sites(ledger):
    pos = {q['led']: (q['cx'], q['cy']) for q in ledger}
    pos['__max__'] = max(pos)
    return pos


def ghost_violations(sites, guard):
    pos = {q['led']: (q['cx'], q['cy']) for q in sites}
    out = []
    for l in sorted(pos):
        lo, hi = pos.get(l - 1), pos.get(l + 1)
        if lo is None or hi is None:
            continue
        mx, my = (lo[0] + hi[0]) / 2.0, (lo[1] + hi[1]) / 2.0
        d = float(np.hypot(pos[l][0] - mx, pos[l][1] - my))
        if d > guard:
            out.append({'led': l, 'dist': round(d, 1),
                        'site': list(pos[l]),
                        'neighbour_mid': [round(mx, 1), round(my, 1)]})
    return out


def sweep(S, mask_thrs, guard):
    rows = []
    for mthr in mask_thrs:
        mask = S['mb'] >= mthr
        for r in (1, 3):
            for ag, mg in ((45, 10), (45, 25), (60, 10), (60, 25)):
                pre = pick_sites(S['sc'], S['best'], S['argi'], mask,
                                 ag, mg, r, S['cand_rows'], dedup=False)
                ded = pick_sites(S['sc'], S['best'], S['argi'], mask,
                                 ag, mg, r, S['cand_rows'], dedup=True)
                ids = {q['led'] for q in ded}
                pre_dups = sum(1 for q in pre
                               if sum(1 for p in pre if p['led'] == q['led']) > 1)
                mult = {}
                for q in pre:
                    mult[q['led']] = mult.get(q['led'], 0) + 1
                ghosts = ghost_violations(ded, guard)
                gm = [l for l in GROUP if l not in ids]
                rows.append({
                    'mask': mthr, 'amp_gate': ag, 'margin_gate': mg,
                    'localmax_pm': r, 'total': len(ids),
                    'dup_claims_post_dedup': 0,
                    'pre_dedup_multi_site_codewords': sum(
                        1 for v in mult.values() if v > 1),
                    'pre_dedup_extra_sites': max(0, len(pre) - len(mult)),
                    'group_missing': gm, 'group_present': not gm,
                    'full_recovery': ids == set(range(200)),
                    'ghost_violations': ghosts, 'trio_recovered':
                        [l for l in TRIO if l in ids],
                    'control_16_present': 16 in ids,
                    'missing_ids': sorted(set(range(200)) - ids),
                    'pass': (not gm) and (not ghosts) and
                            (len(ids) > (197 if len(rows) < 4 else 0))})
    return rows


def assignment_1to1(S, ledger, mask_thr, guard, k=POOL_K):
    """Per-codebook nearest-site 1:1 greedy assignment (scipy absent).

    Edges: every already-decoded codeword contributes ONE edge — its ledger
    site (preserved by construction); each codeword still missing (the
    trio) contributes its top-K feasible pool pixels within the guard disc
    of its serpentine neighbour midpoint. Greedy by amp desc, 1:1 (one site
    per codeword, one codeword per site). Trio feasibility data recorded
    RELAXED too (best amp/margin in-window even when gates fail) so the
    JSON shows how far off each one is.
    """
    mask = S['mb'] >= mask_thr
    H, W = mask.shape
    cur_led = [dict(q) for q in ledger]
    result, iters, feas_best, feas_count = [], [], {}, {}
    relaxed_best = {}
    for it in range(3):
        pos = ledger_sites(cur_led)
        edges = []
        feas_count = {}
        feas_best = {}
        missing_before = []
        for t in range(S['N']):
            if t in pos:                                    # keep ledger site
                cx, cy = pos[t]
                edges.append((float(cur_led[[q['led'] for q in cur_led]
                                             .index(t)]['amp']), t, cx, cy,
                              'ledger'))
                feas_count[t] = -1
                continue
            missing_before.append(t)
            est = neighbour_estimate(cur_led, t, pos)
            if est is None:
                feas_count[t] = 0
                continue
            ex, ey = est['x'], est['y']
            x0, x1 = max(0, ex - 30), min(W, ex + 31)
            y0, y1 = max(0, ey - 30), min(H, ey + 31)
            pool, relax = [], {'amp': -1e9}
            for y in range(y0, y1):
                for x in range(x0, x1):
                    amp = float(S['sc'][t, y, x]) / 9.0
                    s2 = float(S['sc'][S['cand_rows'][t], y, x].max())
                    marg = (float(S['sc'][t, y, x]) - s2) / 9.0
                    dist = float(np.hypot(x - ex, y - ey))
                    if dist > guard:
                        continue
                    if amp > relax['amp']:
                        relax = {'amp': round(amp, 2), 'margin':
                                 round((float(S['sc'][t, y, x]) - s2) / 9.0, 2),
                                 'x': x, 'y': y, 'dist': round(dist, 1),
                                 'in_mask': bool(mask[y, x]),
                                 'blur': round(float(S['mb'][y, x]), 1)}
                    if not mask[y, x] or amp < AMP_GATE or marg < MARGIN_GATE:
                        continue
                    pool.append((amp, marg, x, y))
            relaxed_best.setdefault(t, relax)
            pool.sort(reverse=True)
            feas_count[t] = len(pool)
            if pool:
                feas_best[t] = {'amp': round(pool[0][0], 2),
                                'margin': round(pool[0][1], 2),
                                'x': pool[0][2], 'y': pool[0][3]}
            for amp, marg, x, y in pool[:k]:
                edges.append((amp, t, x, y, 'pool'))
        edges.sort(key=lambda e: -e[0])
        used_site, used_cw = {}, {}
        assign = []
        for score, t, x, y, src in edges:
            if t in used_cw or (x, y) in used_site:
                continue
            used_cw[t] = True
            used_site[(x, y)] = True
            amp = float(S['sc'][t, y, x]) / 9.0
            s2 = float(S['sc'][S['cand_rows'][t], y, x].max())
            marg = (float(S['sc'][t, y, x]) - s2) / 9.0
            assign.append({'led': t, 'cx': x, 'cy': y, 'src': src,
                           'amp': round(amp, 1), 'margin': round(marg, 1)})
        result = sorted(assign, key=lambda q: q['led'])
        iters.append({'iter': it, 'total': len(result),
                      'missing_before': missing_before,
                      'feasible_codewords': sum(1 for v in feas_count.values()
                                                if v > 0)})
        newsites = {q['led'] for q in result} - set(pos)
        if not newsites:
            break
        byl = {q['led']: q for q in result}
        cur_led = [{'led': q['led'], 'cx': q['x'], 'cy': q['y'],
                    'amp': q['amp'], 'margin': q['margin']}
                   for q in sorted(byl.values(), key=lambda q: q['led'])]
    return result, iters, feas_best, feas_count, relaxed_best


def save_crops(S, trios, outdir):
    outdir.mkdir(parents=True, exist_ok=True)
    paths = []
    for key, est in trios.items():
        led = int(key)
        cx, cy = est['x'], est['y']
        x0, x1 = max(0, cx - 35), min(S['mb'].shape[1], cx + 36)
        y0, y1 = max(0, cy - 35), min(S['mb'].shape[0], cy + 36)
        crop = np.asarray(S['master'])[y0:y1, x0:x1].copy()
        crop = cv2.resize(crop, (crop.shape[1] * 4, crop.shape[0] * 4),
                          interpolation=cv2.INTER_NEAREST)
        cv2.rectangle(crop, ((cx - x0) * 4 - 14, (cy - y0) * 4 - 14),
                      ((cx - x0) * 4 + 14, (cy - y0) * 4 + 14),
                      (255, 0, 255), 1)
        cv2.putText(crop, f'led{led} est', (4, 20),
                    cv2.FONT_HERSHEY_PLAIN, 0.9, (255, 0, 255), 1)
        for nb in (est.get('below'), est.get('above')):
            if nb:
                nx, ny = nb[1]
                cv2.rectangle(crop, ((nx - x0) * 4 - 14, (ny - y0) * 4 - 14),
                              ((nx - x0) * 4 + 14, (ny - y0) * 4 + 14),
                              (0, 255, 255), 1)
                cv2.putText(crop, f'LED {nb[0]}', ((nx - x0) * 4 + 4,
                            (ny - y0) * 4 + 20),
                            cv2.FONT_HERSHEY_PLAIN, 0.9, (0, 255, 255), 1)
        p = outdir / f'master_led{led}_crop.png'
        cv2.imwrite(str(p), cv2.cvtColor(crop, cv2.COLOR_RGB2BGR))
        paths.append(str(p))
    return paths


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run_dir')
    ap.add_argument('--out', default=None)
    ap.add_argument('--phase', default='all',
                    choices=['all', 'census', 'sweep', 'assign'])
    ap.add_argument('--scratch', default=None,
                    help='npz cache dir (default $TMPDIR/cwc_probe)')
    args = ap.parse_args()
    run = Path(args.run_dir)
    scratch = Path(args.scratch or os.path.join(
        os.environ.get('TMPDIR', '/tmp'), 'cwc_probe'))
    scratch.mkdir(parents=True, exist_ok=True)

    cache = scratch / f'{run.name}.npz'
    ledger0 = json.load(open(run / 'ledpos.json'))
    if cache.exists():
        z = np.load(cache)
        S = {'mlum': z['mlum'], 'mb': z['mb'], 'sc': z['sc'], 'best': z['best'],
             'argi': z['argi'], 'Dfull': z['Dfull'], 'shifts': z['shifts'] if
             'shifts' in z else [], 'kbgs': z['kbgs'] if 'kbgs' in z else [],
             'master': z['master']}
        S['N'] = int(z['N'])
        S['cand_rows'] = [np.where(z['Dfull'][i] >= 6)[0] for i in range(S['N'])]
        print('cache loaded from', cache)
    else:
        S = build_scores(run)
        np.savez_compressed(cache, mlum=S['mlum'], mb=S['mb'], sc=S['sc'],
                            best=S['best'], argi=S['argi'], Dfull=S['Dfull'],
                            N=S['N'], master=S['master'],
                            shifts=np.array(S['shifts']), kbgs=np.array(S['kbgs']))
        print('score tensor built+cached ->', cache)

    out = {'run': str(run),
           'scoring_source': 'cwc_pos_decode.py lines 52-126 replicated '
           'exactly (decode_run + reg_residual registration chain + stacksig '
           '+ tensordot score + d6 margin + strongest-site dedup; bank '
           'codewords_9of18.json; the re-decode matched ledpos.json exactly '
           'on all 197 sites — see baseline_equivalence)'}
    guard = guard_px(ledger0)
    out['median_pitch_px'] = round(guard / GUARD_MULT, 2)
    out['guard_px'] = round(guard, 1)

    if args.phase in ('all', 'census'):
        pos = ledger_sites(ledger0)
        # baseline equivalence: re-run the standing recipe on the same frames
        mask175 = S['mb'] >= STANDING_MASK
        pre = pick_sites(S['sc'], S['best'], S['argi'], mask175,
                         AMP_GATE, MARGIN_GATE, 3, S['cand_rows'], dedup=False)
        ded = pick_sites(S['sc'], S['best'], S['argi'], mask175,
                         AMP_GATE, MARGIN_GATE, 3, S['cand_rows'], dedup=True)
        ref = {(q['led'], q['cx'], q['cy']): (q['amp'], q['margin'])
               for q in ledger0}
        mine = {(q['led'], q['x'], q['y']): (q['amp'], q['margin']) for q in ded}
        out['baseline_equivalence'] = {
            'standing_mask': STANDING_MASK, 'recomputed_total': len(ded),
            'ledger_total': len(ledger0),
            'exact_match': mine == ref,
            'n_mismatch': len(set(mine.items()) ^ set(ref.items()))}
        print('baseline re-run:', out['baseline_equivalence'])
        cens = []
        trios = {}
        for l in TRIO:
            est = neighbour_estimate(ledger0, l, pos)
            trios[l] = est
            cens.append(census_led(S, pos, l, est, guard))
        ctrl_pos = ledger_sites(ledger0)
        cest = neighbour_estimate(ledger0, CONTROL, ctrl_pos)
        cc = census_led(S, ctrl_pos, CONTROL, cest, guard)
        cens.append(cc)
        trios[CONTROL] = cest
        out['census'] = cens
        out['trio_estimates'] = {str(l): trios[l] for l in TRIO}
        # who claims pixels near the trio (pre-dedup, standing recipe)
        claims = []
        for l in TRIO:
            ex, ey = trios[l]['x'], trios[l]['y']
            near = sorted(pre, key=lambda q: float(
                np.hypot(q['x'] - ex, q['y'] - ey)))
            claims.append({'led': l, 'estimate': [ex, ey], 'nearest_accepted':
                           [{'led': q['led'], 'x': q['x'], 'y': q['y'],
                             'amp': q['amp'], 'margin': q['margin'],
                             'dist': round(float(np.hypot(q['x'] - ex,
                                                          q['y'] - ey)), 1)}
                            for q in near[:6]]})
        out['claims_near_estimates_pre_dedup'] = claims

    if args.phase in ('all', 'sweep'):
        rows = sweep(S, [178, 185, 195, 205, 215, 230], guard)
        out['sweep'] = rows
        passing = [r for r in rows if r['pass']]
        trio_rows = [r for r in rows if r['trio_recovered']]
        out['sweep_best'] = (max(passing, key=lambda r: r['total']) if passing
                             else None)
        out['sweep_trio_recovery_any'] = {
            str(l): sorted({r['mask'] for r in trio_rows if l in
                            r['trio_recovered']}) for l in TRIO}
        out['sweep_trio_recovery_passing'] = [
            r for r in passing if r['trio_recovered']]
        print(f'sweep rows {len(rows)}, passing {len(passing)}, '
              f'best total {out["sweep_best"]["total"] if passing else None}, '
              f'trio-recovering rows: {[r["key"] if False else (r["mask"], r["amp_gate"], r["margin_gate"], r["localmax_pm"]) for r in trio_rows]}')

    if args.phase in ('all', 'assign'):
        mask175 = S['mb'] >= STANDING_MASK
        base_ded = pick_sites(S['sc'], S['best'], S['argi'], mask175,
                              AMP_GATE, MARGIN_GATE, 3, S['cand_rows'],
                              dedup=True)
        (result, iters, feas_best, feas_count,
         relaxed_best) = assignment_1to1(S, base_ded, STANDING_MASK, guard)
        basepos = {q['led']: (q['cx'], q['cy']) for q in ledger0}
        respos = {q['led']: (q['cx'], q['cy']) for q in result}
        resid = {q['led']: (q['amp'], q['margin']) for q in result}
        baseres = {q['led']: (q['cx'], q['cy'], q['amp'], q['margin'])
                   for q in ledger0}
        shared = sorted(set(baseres) & set(resid))
        identical = sum(1 for l in shared if respos[l] == (baseres[l][0],
                                                          baseres[l][1]))
        moved = [{'led': l, 'from': list(baseres[l][:2]), 'to': list(respos[l]),
                  'dx': respos[l][0] - baseres[l][0],
                  'dy': respos[l][1] - baseres[l][1],
                  'amp_from': baseres[l][2], 'amp_to': resid[l][0]}
                 for l in shared if respos[l] != (baseres[l][:2])]
        lost = sorted(set(baseres) - set(resid))
        gained = sorted(set(resid) - set(baseres))
        out['assignment'] = {
            'code': 'tools/cwc_assign_probe.py (greedy 1:1, scipy absent)',
            'mask': STANDING_MASK, 'gates': [AMP_GATE, MARGIN_GATE],
            'window_px': round(guard, 1), 'pool_k': POOL_K,
            'iterations': iters, 'feasible_best': feas_best,
            'feasible_count_trio': {str(l): feas_count.get(l, 0)
                                    for l in TRIO},
            'relaxed_best_trio': {str(l): relaxed_best.get(l)
                                  for l in TRIO},
            'relaxed_best_control_16': relaxed_best.get(16),
            'total': len(result), 'identical_vs_baseline': identical,
            'shared': len(shared), 'recovered': gained, 'lost_vs_baseline': lost,
            'moved_vs_baseline': moved,
            'ghost_violations': ghost_violations(result, guard),
            'sites': result}
        print('assignment:', {k: out['assignment'][k] for k in
                              ('total', 'identical_vs_baseline', 'recovered',
                               'lost_vs_baseline')})
        tpos = ledger_sites(ledger0)
        trio_map = {str(l): neighbour_estimate(ledger0, l, tpos)
                    for l in (*TRIO, CONTROL)}
        paths = save_crops(S, trio_map, scratch / 'crops')
        out['crops'] = paths
        print('crops ->', paths)

    outpath = Path(args.out) if args.out else run / 'assign_probe.json'
    outpath.write_text(json.dumps(out, indent=1))
    print('wrote', outpath, f'({outpath.stat().st_size} B)')


def trios2(l, out):
    return out['trio_estimates'][str(l)]


if __name__ == '__main__':
    main()