#!/usr/bin/env python3
"""E4 0003E gate sweep — PHASE 3b: own-best re-scoring for the OTHER miss
classes agent-1's census classified (contest ids at codeword-lost pixels, and
never-confirmed ids), so the never-confirmed loss table carries measured
own-best amp/margin per id per epoch, not only the first-25 detail.

Also computes: never-confirmed per epoch first-gate classification, the
suppressed-class own-best check (which suppressed ids own a passing pixel
once baseline windows are removed = conflict-redesign + relaunch pool),
and the per-epoch guard-verified recovery ceilings written into
ceilings.json (consumed by summary assembly).
"""
import io
import json
import math
import sys
import time
from pathlib import Path
from collections import Counter

import numpy as np
import cv2
from PIL import Image, ImageFile
ImageFile.LOAD_TRUNCATED_IMAGES = True

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402

OUT = REPO / 'runs/daemon/analysis/gates_e4003e'
DE = REPO / 'runs/daemon/analysis/decode_e4003e'
TAGS = ['r2', 'r3', 'r4', 'r5']
AMP0, MAR0, SUPP0 = 40.0, 6.0, 7


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
    assert np.array_equal(best, np.load(OUT / f'_best_{tag}.npy')), tag
    return dict(tag=tag, sc=sc, best=best, argi=argi, mb=mb, mmed=float(mmed),
                cand=cand, H=H, W=W, mlum=mlum)


def main():
    t0 = time.time()
    base = json.loads((OUT / 'baseline.json').read_text())
    ua_int = json.loads((OUT / '_union_anchor.json').read_text())
    union_ids = {int(k) for k in ua_int}
    never = [i for i in range(600) if i not in union_ids]

    def interp(i):
        s = i // 200
        items = sorted((abs(k - i), ua_int[str(k)]) for k in range(600)
                       if k in union_ids and k // 200 == s
                       and abs(k - i) <= 12)[:12]
        if not items:
            return None
        w = np.array([1.0 / max(1, d) for d, _ in items])
        xs = np.array([p[0] for _, p in items], float)
        ys = np.array([p[1] for _, p in items], float)
        return (float((w * xs).sum() / w.sum()), float((w * ys).sum() / w.sum()))

    res = {'never_ids': never, 'epochs': {}}
    for tag in TAGS:
        ep = build_epoch(tag)
        sc, best, argi, cand = ep['sc'], ep['best'], ep['argi'], ep['cand']
        mb = ep['mb']
        thr_auto = base[tag]['thr_auto']
        mask = mb >= thr_auto
        H, W = mask.shape
        base_final = {q['led']: q for q in base[tag]['final']}
        used = np.zeros(mask.shape, bool)
        for q in base[tag]['final']:
            used[max(0, q['cy'] - SUPP0 // 2): q['cy'] + SUPP0 // 2 + 1,
                 max(0, q['cx'] - SUPP0 // 2): q['cx'] + SUPP0 // 2 + 1] = True
        d1 = json.loads((DE / f's14r0003e-{tag}_ledpos.json').read_text())
        cen = d1['census']
        miss = set(d1['missing'])
        ne = miss & set(never)
        te = miss - set(never)
        # per-id own-best on in-mask, non-used pixels for both classes
        def own_best(ids):
            rows = {}
            for i in ids:
                row = sc[i]
                ownmap = np.where(mask & (~used), row, -np.inf)
                y1, x1 = np.unravel_index(int(np.argmax(ownmap)), row.shape)
                amp_own = float(row[y1, x1]) / 12.0
                s2 = float(sc[cand[i], y1, x1].max())
                mar_own = (float(row[y1, x1]) - s2) / 12.0
                x, y = int(x1), int(y1)
                owner = int(argi[y1, x1])
                ia = interp(i)
                d_i = math.hypot(x - ia[0], y - ia[1]) if ia else None
                bq = base_final.get(i)
                d_a = math.hypot(x - bq['cx'], y - bq['cy']) if bq else None
                bpts = np.array([(q['cx'], q['cy']) for k, q in
                                 base_final.items() if k != i], float)
                d_c = float(np.hypot(bpts[:, 0] - x, bpts[:, 1] - y).min()) \
                    if len(bpts) else 9e9
                guarded = (d_i is not None and d_i <= 6) or \
                          (d_a is not None and d_a <= 6) or (d_c <= 5)
                rows[i] = {'amp': round(amp_own, 1),
                           'margin': round(mar_own, 1),
                           'owner': owner, 'site': [x, y],
                           'passes_own': bool(amp_own >= AMP0 and
                                              mar_own >= MAR0),
                           'd_interp': None if d_i is None else round(d_i, 1),
                           'd_anchor': None if d_a is None else round(d_a, 1),
                           'd_claim': round(d_c, 1),
                           'guarded': bool(guarded)}
            return rows
        ob_ne = own_best(sorted(ne))
        ob_te = own_best(sorted(te))
        # classify never-missing by first gate at own best
        cls = Counter()
        for i, v in ob_ne.items():
            if v['amp'] < 8:
                cls['no_evidence<8'] += 1
            elif not mask[v['site'][1], v['site'][0]]:
                cls['mask'] += 1   # own best off-mask pixel
            elif v['amp'] < AMP0:
                cls['amp_gate'] += 1
            elif v['margin'] < MAR0 and v['owner'] == i:
                cls['margin_gate'] += 1
            elif v['owner'] != i:
                cls['contest_at_own_best'] += 1
            else:
                cls['site_empty_walk'] += 1
        # never-missing that PASS own gates = redesign population
        passes_ne = [i for i, v in ob_ne.items() if v['passes_own']]
        guard_ne = [i for i in passes_ne if ob_ne[i]['guarded']]
        passes_te = [i for i, v in ob_te.items() if v['passes_own']]
        guard_te = [i for i in passes_te if ob_te[i]['guarded']]
        res['epochs'][tag] = {
            'L': base[tag]['L'],
            'never_missing_n': len(ne), 'this_epoch_only_n': len(te),
            'never_by_class': dict(cls),
            'never_pass_own_gates': len(passes_ne),
            'never_pass_guarded': len(guard_ne),
            'never_pass_ids': sorted(passes_ne)[:40],
            'never_pass_detail': {str(i): ob_ne[i] for i in passes_ne[:40]},
            'te_pass_own_gates': len(passes_te),
            'te_pass_guarded': len(guard_te),
            'te_pass_ids': sorted(passes_te)[:40],
            'te_pass_detail': {str(i): ob_te[i] for i in passes_te[:40]},
        }
        print(f'[{tag}] never {len(ne)} pass-own {len(passes_ne)} '
              f'(guarded {len(guard_ne)}), classes {dict(cls)}; '
              f'this-epoch-only {len(te)} pass-own {len(passes_te)} '
              f'(guarded {len(guard_te)}) | {time.time()-t0:.0f}s', flush=True)
        del ep['sc']
        import gc
        gc.collect()
    (OUT / 'phase3b_never_census.json').write_text(json.dumps(res, indent=1))
    print('PHASE 3B COMPLETE', flush=True)


if __name__ == '__main__':
    main()