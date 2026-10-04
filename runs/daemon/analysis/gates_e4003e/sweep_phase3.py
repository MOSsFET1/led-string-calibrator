#!/usr/bin/env python3
"""E4 0003E gate sweep — PHASE 3 (final): refine the eaten-rival population
per id (own-best re-scoring with stacksig resident in-process), guard-verify
the amp30/supp3 recoveries that phase 2 already audited (cross-check), build
the guard-verified union sets, and write summary.json + report.md data.

Own-best refinement (task-2 precision): a baseline-eaten rival id counts as a
conflict-redesign RECOVERY only if it still passes amp+margin at its best own
in-mask, non-baseline-site pixel. Requires each id's score row -> stacksig
resident (rebuilt here exactly as phase 1/2 did; verified vs _best .npy).
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
    raw = json.loads((OUT / 'sweep_raw.json').read_text())
    base = json.loads((OUT / 'baseline.json').read_text())
    ua_int = json.loads((OUT / '_union_anchor.json').read_text())
    union_ids = {int(k) for k in ua_int}
    never = [i for i in range(600) if i not in union_ids]

    def interp(i):
        s = i // 200
        items = sorted((abs(k - i), ua_int[str(k)]) for k in range(600)
                       if k in union_ids and k // 200 == s and abs(k - i) <= 12)[:12]
        if not items:
            return None
        w = np.array([1.0 / max(1, d) for d, _ in items])
        xs = np.array([p[0] for _, p in items], float)
        ys = np.array([p[1] for _, p in items], float)
        return (float((w * xs).sum() / w.sum()), float((w * ys).sum() / w.sum()))

    out3 = {'never_confirmed_ids': never, 'n_never': len(never), 'epochs': {},
            'union': {}}
    for tag in TAGS:
        ep = build_epoch(tag)
        sc, best, argi, cand = ep['sc'], ep['best'], ep['argi'], ep['cand']
        mb = ep['mb']
        thr_auto = base[tag]['thr_auto']
        mask = mb >= thr_auto
        H, W = mask.shape
        base_final = {q['led']: q for q in base[tag]['final']}
        # baseline used-map (CLI 7px window)
        used = np.zeros(mask.shape, bool)
        for q in base[tag]['final']:
            used[max(0, q['cy'] - SUPP0 // 2): q['cy'] + SUPP0 // 2 + 1,
                 max(0, q['cx'] - SUPP0 // 2): q['cx'] + SUPP0 // 2 + 1] = True
        # ---- eaten-rival per-id refinement -----------------------------
        eatraw = raw['epochs'][tag]['conflict_audit_baseline']['eaten']
        eaten_ids = sorted({e['led'] for e in eatraw})
        refin = {}
        for i in eaten_ids:
            row = sc[i]
            ownmap = np.where(mask & (~used), row, -np.inf)
            y1, x1 = np.unravel_index(int(np.argmax(ownmap)), row.shape)
            amp_own = float(row[y1, x1]) / 12.0
            s2 = float(sc[cand[i], y1, x1].max())
            mar_own = (float(row[y1, x1]) - s2) / 12.0
            passes = bool(amp_own >= AMP0 and mar_own >= MAR0)
            # owner of that pixel
            owner = int(argi[y1, x1])
            cls = None; d_anchor = None; d_interp = None; d_claim = None
            x, y = int(x1), int(y1)
            if i in base_final:
                bq = base_final[i]
                d_anchor = math.hypot(x - bq['cx'], y - bq['cy'])
            ia = interp(i)
            if ia:
                d_interp = math.hypot(x - ia[0], y - ia[1])
            # claim adjacency vs baseline claims (exclude own)
            bpts = np.array([(q['cx'], q['cy']) for k, q in base_final.items()
                             if k != i], float)
            if len(bpts):
                d_claim = float(np.hypot(bpts[:, 0] - x, bpts[:, 1] - y).min())
            guarded = (d_anchor is not None and d_anchor <= 6) or \
                      (d_interp is not None and d_interp <= 6) or \
                      (d_claim is not None and d_claim <= 5)
            refin[i] = {'own_best_amp': round(amp_own, 1),
                        'own_best_margin': round(mar_own, 1),
                        'site': [x, y], 'owner_at_site': owner,
                        'passes': passes,
                        'd_anchor': None if d_anchor is None else round(d_anchor, 1),
                        'd_interp': None if d_interp is None else round(d_interp, 1),
                        'd_claim': None if d_claim is None else round(d_claim, 1),
                        'guarded': bool(guarded)}
        recov = {i: v for i, v in refin.items() if v['passes']}
        recov_g = {i: v for i, v in recov.items() if v['guarded']}
        out3['epochs'][tag] = {
            'eaten_unique': len(eaten_ids),
            'eaten_pass_own_best': len(recov),
            'eaten_pass_guarded': sum(1 for v in recov.values() if v['guarded']),
            'eaten_orphans': sum(1 for v in recov.values() if not v['guarded']),
            'recov_detail': {str(k): v for k, v in sorted(recov.items())},
            'refin_detail': {str(k): v for k, v in sorted(refin.items())},
        }
        print(f'[{tag}] eaten {len(eaten_ids)} -> own-best pass {len(recov)} '
              f'(guarded {out3["epochs"][tag]["eaten_pass_guarded"]})',
              flush=True)
        # ---- amp30 recovery guard cross-check (from phase-2 audit) -----
        a30 = raw['epochs'][tag]['combos']['amp=30.0']
        au = a30.get('audit', {})
        cross = {'n_new': len(au),
                 'by_cls': dict(Counter(v['cls'] for v in au.values())),
                 'impostor': sum(1 for v in au.values() if v['impostor_proxy'])}
        out3['epochs'][tag]['amp30_new_by_cls'] = cross['by_cls']
        out3['epochs'][tag]['amp30_impostor'] = cross['impostor']
        del ep['sc']
        import gc
        gc.collect()
        print(f'[{tag}] phase3 done {time.time()-t0:.0f}s', flush=True)
    # ---- guard-verified unions ---------------------------------------
    sets = {}
    for tag in TAGS:
        ids = {q['led'] for q in base[tag]['final']}
        sets[tag] = ids
    out3['union']['baseline_single'] = {t: len(s) for t, s in sets.items()}
    def uset(*ts):
        u = set()
        for t in ts:
            u |= sets[t]
        return len(u)
    import itertools
    pairs = {f'{a}+{b}': uset(a, b) for a, b in itertools.combinations(TAGS, 2)}
    trips = {f'{a}+{b}+{c}': uset(a, b, c)
             for a, b, c in itertools.combinations(TAGS, 3)}
    out3['union']['pairs'] = pairs
    out3['union']['triples'] = trips
    out3['union']['union4'] = uset(*TAGS)
    (OUT / 'phase3_eaten_refinement.json').write_text(json.dumps(out3, indent=1))
    print('PHASE 3 COMPLETE', flush=True)


if __name__ == '__main__':
    main()