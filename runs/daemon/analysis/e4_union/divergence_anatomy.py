#!/usr/bin/env python3
"""Divergence anatomy for r8 p22/p14 and r9 p22: WHERE does the repaired
frame disagree with its labeled duty signature? Uses union-confirmed sites,
per plane p: mean(frame-master) at ON sites minus at OFF sites, printed as 24
D-values + the top contention structure. If D_q ~ D_labeled for q != labeled
(the 'adjacent-plane blur'), the disagreement is localized to a subset of
sites; we report per-site delta at labeled plane vs the best other plane to
localize (string rows? blob region?)."""
import json
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
OUTD = REPO / 'runs/daemon/analysis/e4_union'
codes = json.load(open(REPO / 'tools/codewords_12of24.json'))['codes']
raw = json.loads((OUTD / 'e4_union_raw.json').read_text())

for tag, p in [('r8', 22), ('r8', 14), ('r9', 22)]:
    rep = OUTD / 'repaired' / raw[tag]['run']
    ml = np.asarray(Image.open(rep / f'cwc_{tag}_master.jpg').convert('L'),
                    np.float32)
    sites = raw[tag]['leds']
    ids = [q['led'] for q in sites]
    xs = np.array([q['cx'] for q in sites])
    ys = np.array([q['cy'] for q in sites])
    fr = np.asarray(Image.open(rep / f'cwc_{tag}_p{p:02d}.jpg')
                    .convert('L'), np.float32)
    v = np.array([float(np.mean(fr[max(0, y-1):y+2, max(0, x-1):x+2] -
                                ml[max(0, y-1):y+2, max(0, x-1):x+2]))
                  for x, y in zip(xs, ys)])
    onmask = np.array([codes[i__].__contains__(p) for i__ in ids])
    delta_on_off = v[onmask].mean() - v[~onmask].mean()
    # per-site signed residual vs the codeword prediction, sign = +1 OFF/-1 ON
    # predicted sign of v at site i in plane p: ON -> negative (dimmer),
    # OFF -> ~0. wrongsign = ON sites that came out BRIGHTER than OFF median.
    off_med = float(np.median(v[~onmask]))
    wrong = int((v[onmask] > off_med).sum())
    # geometry of wrong-sign sites: y-histogram thirds (string rows are along
    # x per the rig 3x200; check x thirds too)
    wx = xs[onmask][v[onmask] > off_med]
    wy = ys[onmask][v[onmask] > off_med]
    xhis = np.histogram(wx, bins=3, range=(0, ml.shape[1]))[0] if len(wx) else [0,0,0]
    yhis = np.histogram(wy, bins=3, range=(0, ml.shape[0]))[0] if len(wy) else [0,0,0]
    print(f'{tag} p{p}: D_on-off {delta_on_off:.1f}, wrong-sign ON sites '
          f'{wrong}/{onmask.sum()}, wrong x-thirds {list(xhis)}, '
          f'wrong y-thirds {list(yhis)}, OFF med {off_med:.1f}')
    # is the frame maybe better explained as plane q for some q? count sites
    # whose v is consistent with q's code (ON -> < off_med)
    best = []
    for q in range(24):
        om = np.array([codes[i__].__contains__(q) for i__ in ids])
        consistent = float(((v[om] < off_med).mean() + (v[~om] >= off_med).mean())
                           / 2) if om.sum() and (~om).sum() else 0
        best.append((round(consistent, 3), q))
    best.sort(reverse=True)
    print('   best-explaining planes:', best[:5],
          '| labeled p consistency:', [b for b in best if b[1] == p])