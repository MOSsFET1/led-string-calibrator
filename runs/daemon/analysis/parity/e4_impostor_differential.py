"""Impostor-proxy DIFFERENTIAL: impostor rate among GAINS minus the base rate
among legacy ids, matched by string. If gains' proxy rate ~= the legacy base
rate (same colocated-pair geometry), the gain class is the SAME kind of
claims as the baseline ones — not a new impostor mechanism.

Also count the pair-grid consistency of the same-string straddle gains:
  |Δled| to the partner vs the geometric pitch — are the partner ids the
  plausible grid neighbours (gap 6-9 for the d=3.0/2.8 claims)?
Print the per-burst gain-impostor rate vs base rate.
"""
import gc
import json
import math
import sys
from pathlib import Path

import numpy as np

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'runs/daemon/analysis/parity'))
sys.path.insert(0, str(REPO / 'tools'))
from e4_impostor_audit import build_epoch, loop_new  # noqa: E402

RUNS = REPO / 'runs/daemon/analysis/e4_union/repaired'
OUT = REPO / 'runs/daemon/analysis/parity'

legacy_finals = {}
for run in ['run8', 'run9', 'run10', 'run11']:
    leds = json.loads((REPO / f'runs/daemon/analysis/e4_union/{run}_ledpos.json'
                       ).read_text())['leds']
    legacy_finals[run] = {q['led']: (q['cx'], q['cy'], q['amp']) for q in leds}

out = {}
for run, tag in [('run8', 'r8'), ('run9', 'r9'),
                 ('run10', 'r10'), ('run11', 'r11')]:
    ep = build_epoch(RUNS / run, tag)
    mask = ep['mb'] >= ep['thr']
    new = loop_new(ep, mask)
    nsite = {q['led']: (q['cx'], q['cy'], q['amp']) for q in new}
    val = json.loads((OUT / 'e4_parity_validation.json').read_text())
    gains = {int(k): v for k, v in val['epochs'][run]['gain_detail'].items()}
    pts = np.array([(v[0], v[1]) for v in nsite.values()], float)
    amps = np.array([v[2] for v in nsite.values()], float)
    keys = list(nsite)
    gimp = 0
    for qi, i in enumerate(keys):
        if i not in gains:
            continue
        x, y, a = nsite[i]
        d2 = np.hypot(pts[:, 0] - x, pts[:, 1] - y)
        d2[qi] = 9e9
        near = (d2 < 6.0) & (amps > a + 1.0)
        if near.any():
            gimp += 1
    # base rate
    lf = legacy_finals[run]
    lpts = np.array([(v[0], v[1]) for v in lf.values()], float)
    lamps = np.array([v[2] for v in lf.values()], float)
    lb = 0
    for qi, (i, v) in enumerate(lf.items()):
        d2 = np.hypot(lpts[:, 0] - v[0], lpts[:, 1] - v[1])
        d2[qi] = 9e9
        near = (d2 < 6.0) & (lamps > v[2] + 1.0)
        if near.any():
            lb += 1
    out[run] = {'gain_impostor': gimp, 'n_gains': len(gains),
                'gain_rate': round(100.0 * gimp / len(gains), 1),
                'legacy_base_rate': round(100.0 * lb / len(lf), 1),
                'differential': round(100.0 * gimp / len(gains) -
                                      100.0 * lb / len(lf), 1)}
    print(run, json.dumps(out[run]), flush=True)
    del ep['sc']; gc.collect()
(OUT / 'e4_impostor_differential.json').write_text(json.dumps(out, indent=1))
print('wrote e4_impostor_differential.json')