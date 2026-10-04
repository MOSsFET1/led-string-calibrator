"""Impostor-proxy audit for EVERY gain: is the gain site dominated by a
STRONGER claim within 6 px? Recompute the new sets (in-process) and check
each gain against the FULL new claim set + the anchors of other ids.
"""
import gc
import io
import json
import math
import sys
import time
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402
from offline_hole_verify import parse_planes  # noqa: E402

RUNS = REPO / 'runs/daemon/analysis/e4_union/repaired'
OUT = REPO / 'runs/daemon/analysis/parity'
N = 600


def load_run(run_dir, tag):
    frames = []
    for jpg in sorted(run_dir.glob(f'cwc_{tag}_*.jpg')):
        meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        img = Image.open(io.BytesIO(jpg.read_bytes())).convert('RGB')
        img.load()
        frames.append((meta['label'], img))
    return frames


def build_epoch(run_dir, tag):
    frames = load_run(run_dir, tag)
    planes = parse_planes([{'label': lab, 'img': im} for lab, im in frames])
    masts = [im for lab, im in frames if 'master' in lab]
    mlum = np.asarray(masts[0], np.float32).max(axis=2).astype(np.float32)
    H, W = mlum.shape
    planeL = {p: np.asarray(im, np.float32).max(axis=2).astype(np.float32)
              for p, im in planes.items()}
    tot, _s, _i = C.register_direct(mlum, planeL, C.CWC_NCC_PEAK_MARGIN,
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
    thr = min(100.0, max(45.0, 1.12 * mmed))
    return dict(best=best, argi=argi, sc=sc, mb=mb, cand=cand, thr=thr)


def loop_new(ep, mask, nPs=200):
    sc, best, argi, cand = ep['sc'], ep['best'], ep['argi'], ep['cand']
    H, W = mask.shape
    used = np.zeros(mask.shape, bool)
    amap = np.where(mask, best, -1e9)
    order = np.argsort(amap.ravel())[::-1]
    accepted = []
    for j in order:
        y, x = divmod(int(j), W)
        if not mask[y, x] or used[y, x]:
            continue
        i = int(argi[y, x])
        amp = float(best[y, x]) / 12.0
        if amp < 40.0:
            continue
        s2 = float(sc[cand[i], y, x].max())
        margin = (float(best[y, x]) - s2) / 12.0
        if margin < 6.0:
            continue
        used[y, x] = True
        s_own = i // nPs
        for yy in range(max(0, y - 1), min(H, y + 2)):
            for xx in range(max(0, x - 1), min(W, x + 2)):
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


def main():
    out = {}
    for run, tag in [('run8', 'r8'), ('run9', 'r9'),
                     ('run10', 'r10'), ('run11', 'r11')]:
        ep = build_epoch(RUNS / run, tag)
        mask = ep['mb'] >= ep['thr']
        new = loop_new(ep, mask)
        # per-gain impostor proxy: another claim within 6 px with amp higher
        pts = np.array([(q['cx'], q['cy']) for q in new], float)
        amps = np.array([q['amp'] for q in new])
        imps = []
        for q in new:
            d = np.hypot(pts[:, 0] - q['cx'], pts[:, 1] - q['cy'])
            d_qidx = {k: i for i, k in enumerate(
                (qq['led'] for qq in new))}
            near = (d < 6.0) & (amps > q['amp'] + 1.0)
            if near.any():
                imps.append(q['led'])
        # amp sanity of the new set + amp distribution of new-only gains
        val = json.loads((OUT / 'e4_parity_validation.json').read_text())
        gains = {int(k): v for k, v in val['epochs'][run]['gain_detail'].items()}
        gimp = [i for i in gains if i in set(imps)]
        out[run] = {
            'new_confirmed': len(new),
            'n_stronger_neighbour_within6px': len(imps),
            'of_which_are_gains': sorted(gimp),
            'gain_amp_min': min(v['amp'] for v in gains.values()),
            'note': 'impostor proxy = claim within 6 px of a >+1 amp stronger '
                    'claim (17h43 audit convention)',
        }
        print(run, json.dumps(out[run]), flush=True)
        del ep['sc']
        gc.collect()
    (OUT / 'e4_impostor_audit.json').write_text(json.dumps(out, indent=1))
    print('done')


if __name__ == '__main__':
    main()