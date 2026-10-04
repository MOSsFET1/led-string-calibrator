#!/usr/bin/env python3
"""E4 S14R-0003E battery decode driver (v2-parity, run-dir loader).

Same decode machinery as the CLI tools/cwc_pos_decode.py via the proven
photometry_v2/e4_v2_decode.py pattern: register_direct + ncc_refine
(fullres rad 4), stacksig per plane (bilinear at float shift, kp = plane
histMed / master histMed), bank codewords_12of24.json N=600, gates
amp 40 / margin 6, adaptive mask, suppress 7 px, max-amp-per-codeword dedup.
All 24 planes present each epoch -> amp_den = 12 for every codeword =
CLI byte parity (v2 proved EXACT parity on 0003c run dirs).

Adds the per-epoch LOSS CENSUS on the missed ids:
  mask_stage     - own best evidence site's blur luma < eff_thr (wall-luma
                   glow strangles the candidate mask)
  contest_rival  - best evidence pixel's argmax is a rival codeword
                   (sub: contest_would_pass = own amp>=40 AND margin>=6
                   there -> the page-parity conflict-redesign population)
  amp_gate       - own pixel, passes mask, amp < 40
  margin_gate    - own pixel amp >= 40 but d8-rival margin < 6
  suppressed     - best own in-mask, codeword-owned pixel already claimed
                   by a confirmed site's 7 px window
"""
import io
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image as PILImage

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402

RUNS = REPO / 'runs/daemon/runs'
OUTD = REPO / 'runs/daemon/analysis/decode_e4003e'
OUTD.mkdir(parents=True, exist_ok=True)

RUNS_L = [('s14r0003e-r2', 'r2', 80), ('s14r0003e-r3', 'r3', 100),
          ('s14r0003e-r4', 'r4', 120), ('s14r0003e-r5', 'r5', 150)]


def load_run(run_dir, tag):
    frames = []
    for jpg in sorted(run_dir.glob(f'cwc_{tag}_*.jpg')):
        raw = jpg.read_bytes()
        assert raw.endswith(b'\xff\xd9'), f'no EOI: {jpg.name}'
        img = PILImage.open(io.BytesIO(raw)).convert('RGB')
        img.load()
        meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        frames.append({'meta': meta, 'label': meta['label'], 'img': img})
    return frames


def decode_one(run_dir, tag, L, n=600, amp_gate=40, margin_gate=6):
    frames = load_run(run_dir, tag)
    planes = {int(f['label'].split(':p')[1]): f['img']
              for f in frames if ':p' in f['label']}
    masts = [f for f in frames if 'master' in f['label']]
    assert masts and len(planes) == 24, (tag, len(planes), len(masts))
    master = masts[0]['img']
    mlum = np.asarray(master, dtype=np.float32).max(axis=2).astype(np.float32)
    H, W = mlum.shape

    planeL = {p: np.asarray(planes[p], dtype=np.float32).max(axis=2)
              .astype(np.float32) for p in planes}
    tot, surf, info = C.register_direct(mlum, planeL,
                                        C.CWC_NCC_PEAK_MARGIN, verbose=False)
    for p in sorted(planeL):
        sdx = C.round_half_up(tot[p][0])
        sdy = C.round_half_up(tot[p][1])
        rdx, rdy, _n = C.ncc_refine(mlum, planeL[p], sdx, sdy,
                                    rad=C.FULLRES_RAD)
        tot[p] = (float(rdx) + (tot[p][0] - sdx),
                  float(rdy) + (tot[p][1] - sdy), tot[p][2])

    stacksig = np.empty((24, H, W), np.float32)
    kbgs = []
    mmed = C.hist_median(mlum)
    for j, p in enumerate(sorted(planeL)):
        tdx, tdy = float(tot[p][0]), float(tot[p][1])
        mx, my = np.meshgrid(
            np.arange(W, dtype=np.float32) + np.float32(tdx),
            np.arange(H, dtype=np.float32) + np.float32(tdy))
        sh = cv2.remap(planeL[p], mx, my, cv2.INTER_LINEAR,
                       borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        kp = float(C.hist_median(planeL[p])) / mmed
        kbgs.append((p, round(kp, 3)))
        stacksig[j] = mlum - kp * sh

    codes = json.load(open(C.BASE / 'codewords_12of24.json'))
    codes = codes['codes'] if isinstance(codes, dict) else codes
    N = min(n, len(codes))
    assert N == 600
    bits = np.zeros((N, 24), dtype=np.int16)
    for i in range(N):
        for p in codes[i]:
            bits[i][p] = 1
    sign = (1 - 2 * bits[:N]).astype(np.float32)      # ON=-1, OFF=+1
    sc = np.tensordot(sign, stacksig.astype(np.float32), axes=([1], [0]))
    best = sc.max(axis=0)
    argi = sc.argmax(axis=0)

    mb = cv2.GaussianBlur(mlum, (5, 5), 1.2)
    mmed_hm = C.hist_median(mlum)
    eff_thr = min(float(100), max(float(45), 1.12 * mmed_hm))
    mask = mb >= eff_thr
    Dfull = (bits[:N][:, None, :] != bits[:N][None, :, :]).sum(-1)

    ledpos = []
    used = np.zeros(mask.shape, bool)
    contested = 0
    walk_seen = 0
    amap = np.where(mask, best, -1e9)
    order = np.argsort(amap.ravel())[::-1]
    for j in order:
        y, x = divmod(int(j), mask.shape[1])
        if not mask[y, x] or used[y, x]:
            continue
        walk_seen += 1
        i = int(argi[y, x])
        amp = float(best[y, x]) / 12.0
        row = Dfull[i]
        cand = np.where(row >= 8)[0]
        s2 = float(sc[cand, y, x].max())
        margin = (float(best[y, x]) - s2) / 12.0
        if amp < amp_gate or margin < margin_gate:
            contested += 1
            continue
        used[max(0, y - C.SUPPRESS // 2): y + C.SUPPRESS // 2 + 1,
             max(0, x - C.SUPPRESS // 2): x + C.SUPPRESS // 2 + 1] = True
        ledpos.append({'led': i, 'cx': x, 'cy': y,
                       'amp': round(amp, 1), 'margin': round(margin, 1)})
    byled = {}
    for q in ledpos:
        cur = byled.get(q['led'])
        if cur is None or q['amp'] > cur['amp']:
            byled[q['led']] = q
    leds = sorted(byled.values(), key=lambda q: q['led'])

    # ---- LOSS CENSUS on missed ids -------------------------------------
    missing = [i for i in range(N) if i not in byled]
    owns = {i: (argi == i) for i in range(N)}  # lazy: build only for missing
    census = {'mask_stage': [], 'contest_rival': [], 'amp_gate': [],
              'margin_gate': [], 'suppressed': []}
    census_detail = {'mask_stage': [], 'contest_rival': [], 'amp_gate': [],
                     'margin_gate': [], 'suppressed': []}
    for i in missing:
        row = sc[i]
        y0, x0 = np.unravel_index(int(np.argmax(row)), row.shape)
        amp_i = float(row[y0, x0]) / 12.0
        luma = float(mb[y0, x0])
        in_mask = bool(mask[y0, x0])
        owner = int(argi[y0, x0])
        # margin of i's OWN evidence at (y0,x0) vs its d8 rivals
        cand = np.where(Dfull[i] >= 8)[0]
        s2 = float(sc[cand, y0, x0].max())
        marg_i = (float(row[y0, x0]) - s2) / 12.0
        rec = {'led': i, 'amp': round(amp_i, 1), 'margin': round(marg_i, 1),
               'luma': round(luma, 1), 'eff_thr': round(eff_thr, 1),
               'owner': owner, 'in_mask': in_mask}
        if not in_mask:
            census['mask_stage'].append(i)
            if len(census_detail['mask_stage']) < 25:
                census_detail['mask_stage'].append(rec)
            continue
        if owner != i:
            rec['would_pass'] = bool(amp_i >= amp_gate and marg_i >= margin_gate)
            census['contest_rival'].append(i)
            if len(census_detail['contest_rival']) < 25:
                census_detail['contest_rival'].append(rec)
            continue
        # i owns (y0,x0), in mask: was it used-suppressed, or a gate loss?
        if bool(used[y0, x0]):
            census['suppressed'].append(i)
            if len(census_detail['suppressed']) < 25:
                census_detail['suppressed'].append(rec)
            continue
        # best in-mask, codeword-owned, non-used own-pixel
        own = owns[i]
        ownmap = np.where(own & mask & (~used), row, -np.inf)
        y1, x1 = np.unravel_index(int(np.argmax(ownmap)), row.shape)
        amp_own = float(row[y1, x1]) / 12.0
        cand = np.where(Dfull[i] >= 8)[0]
        s2 = float(sc[cand, y1, x1].max())
        marg_own = (float(row[y1, x1]) - s2) / 12.0
        rec2 = {'led': i, 'amp': round(amp_own, 1),
                'margin': round(marg_own, 1), 'cx': int(x1), 'cy': int(y1)}
        if amp_own < amp_gate:
            census['amp_gate'].append(i)
            if len(census_detail['amp_gate']) < 25:
                census_detail['amp_gate'].append(rec2)
        elif marg_own < margin_gate:
            census['margin_gate'].append(i)
            if len(census_detail['margin_gate']) < 25:
                census_detail['margin_gate'].append(rec2)
        else:
            # passes all gates at a non-used in-mask own pixel -> this is a
            # walk-order artifact (site processed but suppressed earlier);
            # count as suppressed-adjacent, should be near empty
            census['suppressed'].append(i)
            rec2['note'] = 'gate-pass-not-reached'
            if len(census_detail['suppressed']) < 25:
                census_detail['suppressed'].append(rec2)

    amps = [p['amp'] for p in leds]
    mars = [p['margin'] for p in leds]
    wall_probe = {}
    for q in leds:
        wall_probe[q['led']] = float(mb[q['cy'], q['cx']])
    wl = [wall_probe[q['led']] for q in leds]
    wall_law_k = [a / (255.0 - w) for a, w in zip(amps, wl) if w < 250]

    res = {'tag': tag, 'run': run_dir.name, 'L': L, 'N': N,
           'confirmed': len(leds), 'leds': leds,
           'frames_loaded': len(frames),
           'histMed': float(mmed_hm), 'eff_thr': eff_thr,
           'exp': masts[0]['meta'].get('exp', ''),
           'wall_at_site': wall_probe,
           'mask_pixels': int(mask.sum()),
           'walk_sites': walk_seen, 'conf_pre_gate': contested,
           'kbgs': kbgs,
           'shifts': {str(p): [round(float(tot[p][0]), 2),
                               round(float(tot[p][1]), 2),
                               round(float(tot[p][2]), 3)]
                      for p in sorted(tot)},
           'stats': {
               'amp_med': float(sorted(amps)[len(amps) // 2]) if amps else 0,
               'amp_min': float(min(amps)) if amps else 0,
               'amp_max': float(max(amps)) if amps else 0,
               'margin_med': float(sorted(mars)[len(mars) // 2]) if mars else 0,
               'margin_min': float(min(mars)) if mars else 0,
               'wall_med': float(sorted(wl)[len(wl) // 2]) if wl else 0,
               'wall_med_255minus': float(255 - sorted(wl)[len(wl) // 2]) if wl else 0,
               'k_a_over_255minus_wall_med': float(np.median(wall_law_k)) if wall_law_k else 0,
           },
           'census': census,
           'census_detail': census_detail,
           'missing': missing}
    return res


def main():
    summary = {}
    for run, tag, L in RUNS_L:
        res = decode_one(RUNS / run, tag, L)
        keep = {k: v for k, v in res.items() if k != 'leds'}
        amps = [p['amp'] for p in res['leds']]
        st = res['stats']
        cen = res['census']
        print(f"{res['run']} L={L}: confirmed {res['confirmed']}/{res['N']} "
              f"amp med {st['amp_med']:.1f} min {st['amp_min']:.1f} "
              f"margin med {st['margin_med']:.1f} histMed {res['histMed']:.1f} "
              f"eff_thr {res['eff_thr']:.1f} wall {st['wall_med']:.0f} "
              f"k {st['k_a_over_255minus_wall_med']:.3f}")
        print(f"  census: mask_stage {len(cen['mask_stage'])} "
              f"contest_rival {len(cen['contest_rival'])} "
              f"amp_gate {len(cen['amp_gate'])} "
              f"margin_gate {len(cen['margin_gate'])} "
              f"suppressed {len(cen['suppressed'])}")
        (OUTD / f'{run}_ledpos.json').write_text(json.dumps(res, indent=1))
        (OUTD / f'{run}_leds.json').write_text(
            json.dumps({'run': res['run'], 'L': L,
                        'confirmed': res['confirmed'],
                        'leds': [q['led'] for q in res['leds']]}, indent=1))
        summary[tag] = {'run': run, 'L': L, 'confirmed': res['confirmed'],
                        'stats': st, 'exp': res['exp'],
                        'histMed': res['histMed'], 'eff_thr': res['eff_thr'],
                        'mask_pixels': res['mask_pixels'],
                        'walk_sites': res['walk_sites'],
                        'conf_pre_gate': res['conf_pre_gate'],
                        'census': {k: v for k, v in res['census'].items()},
                        'census_detail': res['census_detail'],
                        'L_set': sorted(set(q['led'] for q in res['leds']))}
        sys.stdout.flush()


if __name__ == '__main__':
    main()