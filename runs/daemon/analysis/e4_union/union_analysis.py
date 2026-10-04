#!/usr/bin/env python3
"""E4 union(L) objective: per-L confirmed sets -> pairwise + 4-way unions,
growth across L (in the L order and best-added order), and per-L miss-side
failure-axis classification. Anchors: tripod-fixed rig (cross-run site
identity measured med L1 0.0 on shared confirmed ids) so coordinate transfer
between bursts is direct. Classification per missed-with-anchor id:
  mask    : blur(anchor) < eff_thr              (site never a candidate)
  wall    : predicted amp k*(255-wall) < 40     (amp gate unreachable)
  suppress: another confirmed site within the 7 px SUPPRESS window
  contest : candidate + predicted-reachable but not confirmed (lost argmax/
            margin/suppression adjudication; CLI gate-blocked)
  noanchor: never confirmed in any burst (hidden/colocated/undecodable)
k per L fit from that L's confirmed (amp vs 255-wall) — exposure-specific law.
"""
import itertools
import json
from pathlib import Path

import numpy as np

OUTD = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/'
            'analysis/e4_union')
raw = json.loads((OUTD / 'e4_union_raw.json').read_text())

TAGS = ['r8', 'r9', 'r10', 'r11']
LS = {'r8': 80, 'r9': 100, 'r10': 120, 'r11': 150}
sets = {}
for t in TAGS:
    sets[t] = {q['led']: (q['cx'], q['cy'], q['amp'])
               for q in raw[t]['leds']}
eff = {t: raw[t]['eff_thr'] for t in TAGS}
wall = {t: raw[t]['wall_at_site'] for t in TAGS}
N = 600

print('=== per-L confirmed')
for t in TAGS:
    amps = [v[2] for v in sets[t].values()]
    print(f'L={LS[t]:3d} ({raw[t]["run"]}): {len(sets[t]):3d}/600 '
          f'amp med {np.median(amps):5.1f} p90 {np.percentile(amps,90):5.1f}')

print('\n=== pairwise + 4-way unions (by led id)')
keys = list(sets)
pw = {}
for a, b in itertools.combinations(keys, 2):
    u = len(set(sets[a]) | set(sets[b]))
    pw[f'{a}+{b}'] = u
    print(f'{a}(L{LS[a]})∪{b}(L{LS[b]}): {u}')
U4 = set.union(*[set(sets[t]) for t in keys])
print('4-burst union:', len(U4))

print('\n=== union growth in L order 80->100->120->150')
acc = set()
for t in keys:
    prev = len(acc)
    acc |= set(sets[t])
    print(f'+L{LS[t]:3d}: +{len(acc)-prev:3d} -> {len(acc)}')
print('\n=== cumulative union if L added in other orders')
import itertools as it
best_order, best_len = None, 0
for order in it.permutations(keys):
    acc, vals = set(), []
    for t in order:
        acc |= set(sets[t])
        vals.append(len(acc))
    if vals[-1] > best_len:
        best_len, best_order = vals[-1], (order, vals)
print('best order:', [f'L{LS[t]}' for t in best_order[0]], best_order[1])

print('\n=== failure axes per L')
fail = {}
import cv2
from PIL import Image
for t in TAGS:
    rep = OUTD / 'repaired' / raw[t]['run']
    ml = np.asarray(Image.open(rep / f"cwc_{t}_master.jpg").convert('L'),
                    np.float32)
    mb = cv2.GaussianBlur(ml, (5, 5), 1.2)
    S = sets[t]
    aw = [(v[2], 255.0 - wall[t][str(i)]) for i, v in S.items()]
    k = float(np.median([a / w for a, w in aw if w > 5]))
    missing = [i for i in range(N) if i not in S]
    cls = {'mask': [], 'wall': [], 'suppress': [], 'contest': [], 'noanchor': []}
    sites_t = [(v[0], v[1]) for v in S.values()]
    st = np.array(sites_t) if sites_t else np.zeros((0, 2))
    for i in missing:
        anchor = None
        for u in TAGS:
            if u != t and i in sets[u]:
                anchor = sets[u][i][:2]
                break
        if anchor is None:
            cls['noanchor'].append(i)
            continue
        ax, ay = anchor
        blur = float(mb[int(ay), int(ax)])
        if blur < eff[t]:
            cls['mask'].append(i)
        elif k * (255.0 - blur) < 40:
            cls['wall'].append(i)
        else:
            # suppression: another id's confirmed site within 7 px (L1 window
            # approximates the CLI's 3 px half-window box)
            sx = sy = None
            if len(st):
                d = np.abs(st - np.array([ax, ay])).sum(axis=1)
                j = int(np.argmin(d))
                if d[j] <= 6:
                    sx = int(st[j][0]); sy = int(st[j][1])
            if sx is not None and (sx, sy) != (ax, ay):
                cls['suppress'].append(i)
            else:
                cls['contest'].append(i)
    fail[t] = {kk: len(vv) for kk, vv in cls.items()}
    fail[t]['k_fit'] = round(k, 4)
    print(f'L={LS[t]:3d} misses {len(missing):3d}: ' +
          ' '.join(f'{kk}={len(cls[kk])}' for kk in
                   ['mask', 'wall', 'suppress', 'contest', 'noanchor']) +
          f'  (k {k:.3f})')

summary = {
    'per_L': {t: {'L': LS[t], 'confirmed': len(sets[t]),
                  'amp_med': round(float(np.median([v[2] for v in
                                                    sets[t].values()])), 1),
                  'histMed': raw[t]['histMed'], 'eff_thr': eff[t]}
              for t in TAGS},
    'pairwise_unions': pw,
    'union4': len(U4),
    'union4_ids': sorted(U4),
    'failure_axes': {t: fail[t] for t in TAGS},
}
(OUTD / 'union_summary.json').write_text(json.dumps(summary, indent=1))
print('\nwrote union_summary.json')