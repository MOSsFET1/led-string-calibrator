#!/usr/bin/env python3
"""E4 0003E gate sweep — PHASE 2 (single full pass per epoch).

Rebuilds the epoch machinery in-process ONCE (same code path as phase 1 so
arrays are bit-identical), then sweeps every knob by re-running ONLY the
verbatim candidate-selection loop, audits every NEW id with the
position-guard discipline, and runs the conflict (eaten-rival) audit.

Combos per epoch:
  amp    30/35/40/45/50   (margin 6, sup 7px, mask auto)
  margin 4/5/6/8          (amp 40, sup 7px, mask auto)
  supp   3/5/7/9 px       (amp 40, margin 6, mask auto)
  mask   legacy 100       (vs auto at amp 40 / margin 6 / sup 7)
Output: sweep_raw.json (full per-combo data + audits).
"""
import io
import json
import math
import sys
import time
from pathlib import Path

import numpy as np
import cv2
from PIL import Image, ImageFile
ImageFile.LOAD_TRUNCATED_IMAGES = True

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402

OUT = REPO / 'runs/daemon/analysis/gates_e4003e'
OUT.mkdir(parents=True, exist_ok=True)
TAGS = ['r2', 'r3', 'r4', 'r5']
AMP0, MAR0, SUPP0 = 40.0, 6.0, 7
AMP_SWEEP = [30.0, 35.0, 40.0, 45.0, 50.0]
MAR_SWEEP = [4.0, 5.0, 6.0, 8.0]
SUPP_SWEEP = [3, 5, 7, 9]


def load_run(run_dir, tag):
    frames = []
    for jpg in sorted(run_dir.glob(f'cwc_{tag}_*.jpg')):
        meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        img = Image.open(io.BytesIO(jpg.read_bytes())).convert('RGB')
        frames.append((meta['label'], img))
    return frames


def build_epoch(tag):
    frames = load_run(REPO / 'runs/daemon/runs' / f's14r0003e-{tag}', tag)
    planes = {int(lab.split(':p')[1]): img for lab, img in frames if ':p' in lab}
    masts = [img for lab, img in frames if 'master' in lab]
    mlum = np.asarray(masts[0], np.float32).max(axis=2).astype(np.float32)
    H, W = mlum.shape
    planeL = {p: np.asarray(img, np.float32).max(axis=2).astype(np.float32)
              for p, img in planes.items()}
    tot, surf, info = C.register_direct(mlum, planeL, C.CWC_NCC_PEAK_MARGIN,
                                        verbose=False)
    for p in sorted(planeL):
        sdx = C.round_half_up(tot[p][0]); sdy = C.round_half_up(tot[p][1])
        rdx, rdy, _n = C.ncc_refine(mlum, planeL[p], sdx, sdy, rad=C.FULLRES_RAD)
        tot[p] = (float(rdx) + (tot[p][0] - sdx),
                  float(rdy) + (tot[p][1] - sdy), tot[p][2])
    stacksig = np.empty((24, H, W), np.float32)
    mmed = C.hist_median(mlum)
    for j, p in enumerate(sorted(planeL)):
        tdx, tdy = float(tot[p][0]), float(tot[p][1])
        mx, my = np.meshgrid(np.arange(W, dtype=np.float32) + np.float32(tdx),
                             np.arange(H, dtype=np.float32) + np.float32(tdy))
        sh = cv2.remap(planeL[p], mx, my, cv2.INTER_LINEAR,
                       borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        stacksig[j] = mlum - (float(C.hist_median(planeL[p])) / mmed) * sh
    codes = json.load(open(C.BASE / 'codewords_12of24.json'))
    codes = codes['codes'] if isinstance(codes, dict) else codes
    bits = np.zeros((600, 24), dtype=np.int16)
    for i in range(600):
        for p in codes[i]:
            bits[i][p] = 1
    sign = (1 - 2 * bits[:600]).astype(np.float32)
    sc = np.tensordot(sign, stacksig, axes=([1], [0]))
    best = sc.max(axis=0)
    argi = sc.argmax(axis=0).astype(np.int32)
    mb = cv2.GaussianBlur(mlum, (5, 5), 1.2)
    Dw = (bits[:600][:, None, :] != bits[:600][None, :, :]).sum(-1)
    cand = [np.where(Dw[i] >= 8)[0] for i in range(600)]
    # verify against phase-1 stage (bit identity)
    best1 = np.load(OUT / f'_best_{tag}.npy')
    same = bool(np.array_equal(best, best1))
    return dict(tag=tag, best=best, argi=argi, sc=sc, mb=mb, mmed=float(mmed),
                cand=cand, H=H, W=W, identical_to_phase1=same)


def gate_loop(best, argi, sc, cand, mask, amp_gate, margin_gate, supp_win):
    """VERBATIM cwc_pos_decode.main candidate-selection block."""
    used = np.zeros(mask.shape, bool)
    amap = np.where(mask, best, -1e9)
    order = np.argsort(amap.ravel())[::-1]
    accepted = []
    for j in order:
        y, x = divmod(int(j), mask.shape[1])
        if not mask[y, x] or used[y, x]:
            continue
        i = int(argi[y, x])
        amp = float(best[y, x]) / 12.0
        if amp < amp_gate:
            continue
        s2 = float(sc[cand[i], y, x].max())
        margin = (float(best[y, x]) - s2) / 12.0
        if margin < margin_gate:
            continue
        used[max(0, y - supp_win // 2): y + supp_win // 2 + 1,
             max(0, x - supp_win // 2): x + supp_win // 2 + 1] = True
        accepted.append({'led': i, 'cx': int(x), 'cy': int(y),
                         'amp': round(amp, 1), 'margin': round(margin, 1)})
    byled = {}
    for q in accepted:
        cur = byled.get(q['led'])
        if cur is None or q['amp'] > cur['amp']:
            byled[q['led']] = q
    return sorted(byled.values(), key=lambda q: q['led']), used


def conflict_audit(ep, final, amp_gate, margin_gate, mask):
    best, argi, sc, cand = (ep['best'], ep['argi'], ep['sc'], ep['cand'])
    H, W = mask.shape
    eat, seen = [], set()
    for q in final:
        for yy in range(max(0, q['cy'] - 3), min(H, q['cy'] + 4)):
            for xx in range(max(0, q['cx'] - 3), min(W, q['cx'] + 4)):
                if not mask[yy, xx] or (yy, xx) in seen:
                    continue
                i = int(argi[yy, xx])
                if i == q['led']:
                    continue
                amp = float(best[yy, xx]) / 12.0
                if amp < amp_gate:
                    continue
                s2 = float(sc[cand[i], yy, xx].max())
                margin = (float(best[yy, xx]) - s2) / 12.0
                if margin < margin_gate:
                    continue
                seen.add((yy, xx))
                d = math.hypot(xx - q['cx'], yy - q['cy'])
                same_str = (i // 200) == (q['led'] // 200)
                if same_str and d <= 1.5:
                    cls = 'page_suppresses_too'
                elif same_str and d < 6:
                    cls = 'page_conflict_keep_both'
                elif same_str:
                    cls = 'page_never_suppresses'
                else:
                    cls = 'cross_string_page_confirms_both'
                eat.append({'led': i, 'amp': round(amp, 1),
                            'margin': round(margin, 1), 'd': round(d, 1),
                            'same_string': bool(same_str),
                            'near_led': q['led'], 'page_class': cls})
    return eat


def audit_new(final, base_ids, anchor_pos, interp_fn):
    site = {q['led']: (q['cx'], q['cy']) for q in final}
    amp = {q['led']: q['amp'] for q in final}
    pts = np.array([(q['cx'], q['cy']) for q in final], float)
    idxs = {q['led']: k for k, q in enumerate(final)}
    out = {}
    for i, (x, y) in site.items():
        if i in base_ids or i < 0:
            continue
        d_a = math.hypot(x - anchor_pos[i][0], y - anchor_pos[i][1]) \
            if i in anchor_pos else None
        ia = interp_fn(i)
        d_i = math.hypot(x - ia[0], y - ia[1]) if ia else None
        dd = np.hypot(pts[:, 0] - x, pts[:, 1] - y) if len(pts) else np.array([9e9])
        dd[idxs[i]] = 9e9
        nears = [int(k) for k in np.where(dd < 6)[0]]
        imp = any(final[k]['amp'] > amp[i] for k in nears)
        j = int(dd.argmin()); d_c = float(dd[j]); cid = final[j]['led']
        guarded = (d_a is not None and d_a <= 6) or \
                  (d_i is not None and d_i <= 6) or \
                  (d_c <= 5 and cid != i)
        cls = 'anchor' if (d_a is not None and d_a <= 6) else \
              'interp' if (d_i is not None and d_i <= 6) else \
              'claim_adj' if (d_c <= 5 and cid != i) else 'orphan'
        out[i] = {'amp': amp[i], 'cx': int(x), 'cy': int(y),
                  'd_anchor': None if d_a is None else round(d_a, 1),
                  'd_interp': None if d_i is None else round(d_i, 1),
                  'd_claim': round(d_c, 1), 'near_claim': int(cid),
                  'impostor_proxy': bool(imp), 'guarded': bool(guarded),
                  'cls': cls}
    return out


def main():
    t0 = time.time()
    base = json.loads((OUT / 'baseline.json').read_text())
    ua_int = json.loads((OUT / '_union_anchor.json').read_text())
    union_anchor = {int(k): v for k, v in ua_int.items()}

    def interp(i):
        s = i // 200
        items = sorted((abs(k - i), union_anchor[k]) for k in union_anchor
                       if k // 200 == s and abs(k - i) <= 12)[:12]
        if not items:
            return None
        w = np.array([1.0 / max(1, d) for d, _ in items])
        xs = np.array([p[0] for _, p in items], float)
        ys = np.array([p[1] for _, p in items], float)
        return (float((w * xs).sum() / w.sum()), float((w * ys).sum() / w.sum()))

    raw = {'meta': {'corpus': 's14r0003e-r2..r5 (L 80/100/120/150)',
                    'baseline': 'amp40 margin6 sup7px mask-auto',
                    'amp_sweep': AMP_SWEEP, 'margin_sweep': MAR_SWEEP,
                    'supp_sweep_px': SUPP_SWEEP},
           'epochs': {}}
    for tag in TAGS:
        ep = build_epoch(tag)
        thr_auto = base[tag]['thr_auto']
        mask_auto = ep['mb'] >= thr_auto
        mask_leg = ep['mb'] >= 100.0
        anchor_pos = {q['led']: (q['cx'], q['cy']) for q in base[tag]['final']}
        base_ids = set(anchor_pos)
        E = raw['epochs'][tag] = {'L': base[tag]['L'],
                                  'histMed_cli': base[tag]['histMed_cli'],
                                  'thr_auto': thr_auto,
                                  'baseline': len(base[tag]['final']),
                                  'identical_to_phase1': ep['identical_to_phase1'],
                                  'combos': {}}
        combos = [('amp', a, a, MAR0, SUPP0, mask_auto) for a in AMP_SWEEP]
        combos += [('margin', m, AMP0, m, SUPP0, mask_auto)
                   for m in MAR_SWEEP if m != MAR0]
        combos += [('supp', s, AMP0, MAR0, s, mask_auto)
                   for s in SUPP_SWEEP if s != SUPP0]
        combos.append(('mask_legacy100', 100.0, AMP0, MAR0, SUPP0, mask_leg))
        for name, knob, a, m, sw, mask in combos:
            final, used = gate_loop(ep['best'], ep['argi'], ep['sc'],
                                    ep['cand'], mask, a, m, sw)
            key = f'{name}={knob}'
            combo_out = {'confirmed': len(final)}
            new_ids = [q['led'] for q in final if q['led'] not in base_ids]
            if new_ids:
                au = audit_new(final, base_ids, anchor_pos, interp)
                combo_out['new_ids'] = len(new_ids)
                combo_out['audit'] = au
                combo_out['guarded_new'] = sum(
                    1 for v in au.values() if v['guarded'])
                combo_out['orphan_new'] = sum(
                    1 for v in au.values() if v['cls'] == 'orphan')
                combo_out['impostor_proxy'] = sum(
                    1 for v in au.values() if v['impostor_proxy'])
                combo_out['lost_base_ids'] = sorted(
                    q['led'] for q in base[tag]['final']
                    if q['led'] not in {x['led'] for x in final})
            else:
                combo_out['new_ids'] = 0
                combo_out['lost_base_ids'] = []
            if name != 'amp':   # conflict audit once per non-amp knob too
                pass
            E['combos'][key] = combo_out
            print(f'[{tag}] {key}: {len(final)} (+{combo_out["new_ids"]} new, '
                  f'{combo_out.get("guarded_new", 0)} guarded, '
                  f'{combo_out.get("orphan_new", 0)} orphan, '
                  f'lost {len(combo_out["lost_base_ids"])})', flush=True)
        # conflict audits: baseline + the amp30 + legacy-mask + supp variants
        for label, a, m, sw, mask in [
                ('baseline', AMP0, MAR0, SUPP0, mask_auto),
                ('amp30', 30.0, MAR0, SUPP0, mask_auto),
                ('mask_legacy100', AMP0, MAR0, SUPP0, mask_leg)]:
            final, _u = gate_loop(ep['best'], ep['argi'], ep['sc'], ep['cand'],
                                  mask, a, m, sw)
            eat = conflict_audit(ep, final, a, m, mask)
            from collections import Counter
            E[f'conflict_audit_{label}'] = {
                'n_claims': len(final),
                'n_eaten_rivals': len(eat),
                'unique_eaten_ids': len({e['led'] for e in eat}),
                'by_page_class': dict(Counter(e['page_class'] for e in eat)),
                'by_d_band': dict(Counter(
                    'd<=1.5' if e['d'] <= 1.5 else '1.5<d<3' if e['d'] < 3
                    else '3<=d<6' if e['d'] < 6 else 'd>=6'
                    for e in eat)),
                'eaten': eat[:200],
            }
            print(f'[{tag}] conflicts[{label}]: {len(eat)} claims '
                  f'({len({e["led"] for e in eat})} uniq ids)', flush=True)
        del ep['sc']
        import gc
        gc.collect()
        print(f'[{tag}] done {time.time()-t0:.0f}s', flush=True)
    (OUT / 'sweep_raw.json').write_text(json.dumps(raw, indent=1))
    print('PHASE 2 COMPLETE -> sweep_raw.json', flush=True)


if __name__ == '__main__':
    main()