#!/usr/bin/env python3
"""E4 run8..run11 parity validation driver (proposal §4 instrument).

Baseline vs NEW candidate-selection on the SAME in-process score arrays
(machinery = cwc_pos_decode's own register_direct/ncc_refine/hist_median),
then:
  - legacy loop  = verbatim OLD CLI block (SUPPRESS=7 global ±3 window)
  - new loop     = proposal §3.2 (±1 px same-string-only, claim-1px,
                   same-string conflict ledger)
  - guards       = id anchor (union position over the OTHER epochs' confirms)
                   or id-kNN <=12 same-string 1/dist interpolation; <=6 px
  - cross-check  = real subprocess cwc_pos_decode.py --save-json on run9
Deliverable: validation JSON for validation_0003.md.
"""
import gc
import io
import json
import math
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

import cv2
import numpy as np
from PIL import Image  # no LOAD_TRUNCATED: repaired corpus decodes plainly

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402

RUNS = REPO / 'runs/daemon/analysis/e4_union/repaired'
OUT = REPO / 'runs/daemon/analysis/parity'
OUT.mkdir(parents=True, exist_ok=True)
PYV = '/home/nellie/.hermes/hermes-agent/venv/bin/python3'
N = 600
AMP0, MAR0, LEGACY_SUPP, NEW_SUPP = 40.0, 6.0, 7, 1
RUNS_L = [('run8', 'r8', 80), ('run9', 'r9', 100),
          ('run10', 'r10', 120), ('run11', 'r11', 150)]
PREDICT = {'run8': (429, 432), 'run9': (471, 474),
           'run10': (407, 410), 'run11': (428, 431)}
BASELINE = {'run8': 412, 'run9': 454, 'run10': 390, 'run11': 411}


def load_run(run_dir, tag):
    frames = []
    for jpg in sorted(run_dir.glob(f'cwc_{tag}_*.jpg')):
        # repaired-corpus meta naming: cwc_<tag>_pNN.meta.json (no .jpg stem)
        meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        raw = jpg.read_bytes()
        assert raw[:2] == b'\xff\xd8' and raw[-2:] == b'\xff\xd9', jpg.name
        img = Image.open(io.BytesIO(raw)).convert('RGB')
        img.load()
        frames.append((meta['label'], img))
    return frames


def build_epoch(run_dir, tag):
    from offline_hole_verify import parse_planes
    frames = load_run(run_dir, tag)
    planes = parse_planes([{'label': lab, 'img': im} for lab, im in frames])
    masts = [im for lab, im in frames if 'master' in lab]
    assert masts and len(planes) == 24, (tag, len(planes), len(masts))
    mlum = np.asarray(masts[0], np.float32).max(axis=2).astype(np.float32)
    H, W = mlum.shape
    planeL = {p: np.asarray(im, np.float32).max(axis=2).astype(np.float32)
              for p, im in planes.items()}
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
    with open(C.BASE / 'codewords_12of24.json') as fh:
        codes = json.load(fh)
    codes = codes['codes'] if isinstance(codes, dict) else codes
    bits = np.zeros((N, 24), dtype=np.int16)
    for i in range(N):
        for p in codes[i]:
            bits[i][p] = 1
    sign = (1 - 2 * bits[:N]).astype(np.float32)
    sc = np.tensordot(sign, stacksig, axes=([1], [0]))
    best = sc.max(axis=0)
    argi = sc.argmax(axis=0).astype(np.int32)
    mb = cv2.GaussianBlur(mlum, (5, 5), 1.2)
    Dw = (bits[:N][:, None, :] != bits[:N][None, :, :]).sum(-1)
    cand = [np.where(Dw[i] >= 8)[0] for i in range(N)]
    mmed_hm = C.hist_median(mlum)     # == mmed
    eff_thr = min(100.0, max(45.0, 1.12 * mmed_hm))
    return dict(tag=tag, best=best, argi=argi, sc=sc, mb=mb, mmed=float(mmed),
                cand=cand, H=H, W=W, thr=eff_thr)


def loop(ep, mask, amp_gate, margin_gate, mode, nPs=200):
    """mode 'legacy': ±(supp//2) px GLOBAL square at claim (old CLI).
       mode 'new':   claim only the pixel, then ±1 px same-string-only."""
    sc, best, argi, cand = ep['sc'], ep['best'], ep['argi'], ep['cand']
    H, W = mask.shape
    used = np.zeros(mask.shape, bool)
    amap = np.where(mask, best, -1e9)
    order = np.argsort(amap.ravel())[::-1]
    accepted = []
    R = LEGACY_SUPP // 2 if mode == 'legacy' else NEW_SUPP
    for j in order:
        y, x = divmod(int(j), W)
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
        if mode == 'legacy':
            used[max(0, y - LEGACY_SUPP // 2): y + LEGACY_SUPP // 2 + 1,
                 max(0, x - LEGACY_SUPP // 2): x + LEGACY_SUPP // 2 + 1] = True
        else:
            used[y, x] = True
            s_own = i // nPs
            for yy in range(max(0, y - R), min(H, y + R + 1)):
                for xx in range(max(0, x - R), min(W, x + R + 1)):
                    if mask[yy, xx] and not used[yy, xx] and \
                            (int(argi[yy, xx]) // nPs) == s_own:
                        used[yy, xx] = True
        accepted.append({'led': i, 'cx': int(x), 'cy': int(y),
                         'amp': round(amp, 1), 'margin': round(margin, 1)})
    byled = {}
    for q in accepted:
        cur = byled.get(q['led'])
        if cur is None or q['amp'] > cur['amp']:
            byled[q['led']] = q
    return sorted(byled.values(), key=lambda q: q['led'])


def conflict_ledger(final, nPs=200):
    out = []
    for a in range(len(final)):
        for b in range(a + 1, len(final)):
            sa, sb = final[a], final[b]
            if (sa['led'] // nPs) != (sb['led'] // nPs):
                continue
            d = math.hypot(sa['cx'] - sb['cx'], sa['cy'] - sb['cy'])
            if d < 6.0 and abs(sa['led'] - sb['led']) > 5:
                out.append({'a': sa['led'], 'b': sb['led'],
                            'd': round(d, 2), 'kind': 'same-string'})
    return out


def main():
    t0 = time.time()
    epochs = {}
    for run, tag, _L in RUNS_L:
        epochs[run] = build_epoch(RUNS / run, tag)
        print(f'[{run}] built {time.time()-t0:.0f}s', flush=True)

    # union anchors over the four legacy runs (same battery/tripod):
    anchor = defaultdict(list)
    for tag, ep in epochs.items():
        thr = ep['thr']
        final = loop(ep, ep['mb'] >= thr, AMP0, MAR0, 'legacy')
        ep['legacy_final'] = final
        for q in final:
            anchor[q['led']].append((q['cx'], q['cy']))
    anchor_pos = {i: (float(np.mean([p[0] for p in v])),
                      float(np.mean([p[1] for p in v])))
                  for i, v in anchor.items()}

    out = {'meta': {'pred_counts': PREDICT, 'gates': '40/6 mask-auto',
                    'legacy_supp': LEGACY_SUPP, 'new_supp': NEW_SUPP},
           'epochs': {}}
    for tag, ep in epochs.items():
        mask = ep['mb'] >= ep['thr']
        legacy = ep['legacy_final']
        new = loop(ep, mask, AMP0, MAR0, 'new')
        legacy_ids = {q['led'] for q in legacy}
        new_ids = {q['led'] for q in new}
        gained = new_ids - legacy_ids
        lost = legacy_ids - new_ids
        site = {q['led']: (q['cx'], q['cy']) for q in new}
        amp = {q['led']: q['amp'] for q in new}
        pts = np.array([(q['cx'], q['cy']) for q in new], float)
        idxs = {q['led']: k for k, q in enumerate(new)}
        # guards: anchor (mean over other-burst legacy confirms) OR id-kNN
        ga = {}
        for i in sorted(gained):
            x, y = site[i]
            d_a = math.hypot(x - anchor_pos[i][0], y - anchor_pos[i][1]) \
                if i in anchor_pos else None
            s = i // 200
            items = sorted((abs(k - i), anchor_pos[k]) for k in anchor_pos
                           if k // 200 == s and abs(k - i) <= 12)[:12]
            d_i = None
            if items:
                w = np.array([1.0 / max(1, d) for d, _ in items])
                xs = np.array([p[0] for _, p in items], float)
                ys = np.array([p[1] for _, p in items], float)
                d_i = math.hypot(x - float((w * xs).sum() / w.sum()),
                                 y - float((w * ys).sum() / w.sum()))
            dd = np.hypot(pts[:, 0] - x, pts[:, 1] - y) if len(pts) else np.array([9e9])
            if len(dd):
                dd[idxs[i]] = 9e9
                j = int(dd.argmin()); d_c = float(dd[j]); cid = new[j]['led']
            else:
                d_c, cid = 9e9, None
            guarded = (d_a is not None and d_a <= 6) or \
                      (d_i is not None and d_i <= 6) or \
                      (d_c <= 5 and cid != i)
            ga[i] = {'d_anchor': None if d_a is None else round(d_a, 1),
                     'd_interp': None if d_i is None else round(d_i, 1),
                     'd_claim': round(d_c, 1), 'guarded': bool(guarded),
                     'amp': amp[i]}
        # lost ids: page-suppresses-too audit (<=1.5 px same-string eat)
        lost_detail = []
        nsite = {q['led']: (q['cx'], q['cy']) for q in legacy}
        for i in sorted(lost):
            x, y = nsite[i]
            best_d, best_id = 9e9, None
            for q in new:
                if (q['led'] // 200) != (i // 200):
                    continue
                d = math.hypot(q['cx'] - x, q['cy'] - y)
                if d < best_d:
                    best_d, best_id = d, q['led']
            lost_detail.append({'led': i, 'near_led': best_id,
                                'd': round(best_d, 1) if best_id else None,
                                'page_suppresses_too': bool(
                                    best_id is not None and best_d <= 1.5)})
        # conflicts in the NEW set
        conf = conflict_ledger(new)
        amps = [q['amp'] for q in new]
        e_out = {
            'baseline_published': BASELINE[tag],
            'legacy_confirmed': len(legacy),
            'new_confirmed': len(new),
            'delta': len(new) - len(legacy),
            'predicted': PREDICT[tag],
            'in_prediction': PREDICT[tag][0] <= len(new) <= PREDICT[tag][1],
            'gained': len(gained), 'lost': len(lost),
            'guarded_gains': sum(1 for v in ga.values() if v['guarded']),
            'orphan_gains': [i for i, v in ga.items() if not v['guarded']],
            'gain_detail': {str(k): v for k, v in sorted(ga.items())},
            'lost_detail': lost_detail,
            'conflicts': len(conf),
            'amp_min': min(amps) if amps else None,
            'margin_min': min(q['margin'] for q in new) if new else None,
            'max_id': max(q['led'] for q in new) if new else None,
            'union_will_grow': True,
        }
        out['epochs'][tag] = e_out
        print(f"[{tag}] legacy {len(legacy)} -> new {len(new)} "
              f"(+{e_out['delta']}, pred {PREDICT[tag]}, gained {len(gained)} "
              f"guarded {e_out['guarded_gains']}, lost {len(lost)}, "
              f"conflicts {len(conf)}) {time.time()-t0:.0f}s", flush=True)
    # unions
    def uset(mode_key):
        u = set()
        for tag, ep in epochs.items():
            final = ep['legacy_final'] if mode_key == 'legacy' else None
            u |= {q['led'] for q in final}
        return u
    ul = uset('legacy')
    un = set()
    for tag, ep in epochs.items():
        un |= {q['led'] for q in loop(ep, ep['mb'] >= ep['thr'], AMP0, MAR0, 'new')}
    out['union4_legacy'] = len(ul)
    out['union4_new'] = len(un)
    print(f'union4: legacy {len(ul)} -> new {len(un)}', flush=True)

    # subprocess certification on one epoch (run9): real CLI end to end
    tag = 'run9'
    ep = epochs[tag]
    tmp = Path('/home/nellie/.hermes/cache/scratch/e4_cli_parity')
    tmp.mkdir(parents=True, exist_ok=True)
    import base64
    lines = []
    for jpg in sorted((RUNS / tag).glob('cwc_r9_*.jpg')):
        meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        b64 = base64.b64encode(jpg.read_bytes()).decode()
        lines.append('FRAME ' + json.dumps(meta, separators=(',', ':')))
        for i2 in range(0, len(b64), 76):
            lines.append('FJPEG ' + b64[i2:i2 + 76])
        lines.append('FEND')
    (tmp / 'r9_frames.txt').write_text('\n'.join(lines) + '\n')
    # the CLI reads jpgs from the run dir; stage copy? decode_run only uses
    # frames.txt, not the jpgs. Keep the scratch frames.txt authoritative.
    r = subprocess.run([PYV, str(REPO / 'tools/cwc_pos_decode.py'), str(tmp),
                        '--tag', 'r9', '--n', '600', '--save-json'],
                       capture_output=True, text=True, timeout=580)
    cli = json.loads((tmp / 'ledpos.json').read_text())
    mine = ep_new = loop(ep, ep['mb'] >= ep['thr'], AMP0, MAR0, 'new')
    amine = {q['led']: q for q in mine}
    diffs = []
    for q in cli:
        m = amine.get(q['led'])
        if m is None:
            diffs.append((q['led'], 'missing'))
        elif (m['cx'], m['cy']) != (q['cx'], q['cy']) or \
                abs(m['amp'] - q['amp']) > 1e-6 or \
                abs(m['margin'] - q['margin']) > 1e-6:
            diffs.append((q['led'], (m['cx'], m['cy'], m['amp'], m['margin']),
                          (q['cx'], q['cy'], q['amp'], q['margin'])))
    extra = [k for k in amine if k not in {q['led'] for q in cli}]
    out['subprocess_run9'] = {
        'rc': r.returncode, 'n_cli': len(cli), 'n_mine': len(mine),
        'diffs': diffs[:20], 'extra': extra[:20],
        'pass': not diffs and not extra and len(cli) == len(mine)}
    print('subprocess run9 parity:', out['subprocess_run9']['pass'],
          f'(rc {r.returncode}, cli {len(cli)}, mine {len(mine)})', flush=True)

    (OUT / 'e4_parity_validation.json').write_text(json.dumps(out, indent=1))
    print('wrote', OUT / 'e4_parity_validation.json', flush=True)


if __name__ == '__main__':
    main()