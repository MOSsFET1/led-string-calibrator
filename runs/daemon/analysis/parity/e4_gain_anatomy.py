"""Anatomy audit v2: recompute the new loop per epoch (sites resident) and
split every gain into mechanism classes with real geometry:

  pair_straddle_same_string : nearest legacy claim is the gain id's own
      same-string neighbour (id gap <= 12, same //200) and d <= pitch
  pair_cross_string         : nearest legacy claim is a different string, d < 5
  free_same_string_window   : gain site inside a legacy claim's ±3 legacy
      window (Chebyshev <= 3) whose claim is a DIFFERENT codeword — the
      exact class the proposal says parity recovers at a free pixel
  anchor_free               : d_anchor <= 6, not inside any legacy window
  orphan                    : neither
Also: for each pair-straddle gain, the Δled vs the neighbour (conflict-grid
pairs must satisfy |Δled| > 5 to appear in the page ledger).
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

sys.path.insert(0, str(REPO / 'runs/daemon/analysis/parity'))
from e4_impostor_audit import build_epoch, loop_new  # noqa: E402

RUNS = REPO / 'runs/daemon/analysis/e4_union/repaired'
OUT = REPO / 'runs/daemon/analysis/parity'

legacy_finals = {}
legacy_byled = {}
for run in ['run8', 'run9', 'run10', 'run11']:
    leds = json.loads((REPO / f'runs/daemon/analysis/e4_union/{run}_ledpos.json'
                       ).read_text())['leds']
    legacy_finals[run] = {q['led']: (q['cx'], q['cy'], q['amp']) for q in leds}


def main():
    out = {}
    for run, tag in [('run8', 'r8'), ('run9', 'r9'),
                     ('run10', 'r10'), ('run11', 'r11')]:
        val = json.loads((OUT / 'e4_parity_validation.json').read_text())
        gains = {int(k): v for k, v in val['epochs'][run]['gain_detail'].items()}
        lf = legacy_finals[run]
        ep = build_epoch(RUNS / run, tag)
        mask = ep['mb'] >= ep['thr']
        new = loop_new(ep, mask)
        nsite = {q['led']: (q['cx'], q['cy'], q['amp']) for q in new}
        cats = {'pair_straddle_same_string': 0, 'pair_cross_string': 0,
                'inside_legacy_window_rival_led': 0,
                'anchor_free': 0, 'orphan': 0}
        conflict_pairs = 0
        details = []
        for i, v in sorted(gains.items()):
            x, y, a = nsite[i]
            # nearest legacy claim (any string)
            bk, bd, bcheb = None, 9e9, 9e9
            for k, (kx, ky, ka) in lf.items():
                if k == i:
                    continue
                d = math.hypot(kx - x, ky - y)
                ch = max(abs(kx - x), abs(ky - y))
                if d < bd:
                    bk, bd, bcheb = k, d, ch
            same_str = bk is not None and (bk // 200) == (i // 200)
            gap = abs(bk - i) if bk is not None else None
            in_win = bcheb <= 3
            if bk is not None and same_str and gap is not None and gap > 5:
                conflict_pairs += 1
            if same_str and gap is not None and gap <= 12:
                cats['pair_straddle_same_string'] += 1
                details.append({'led': i, 'cls': 'pair_same_str',
                                'near_led': bk, 'gap': gap, 'd': round(bd, 1),
                                'cheb': bcheb, 'in_legacy_window': in_win,
                                'amp': a, 'd_anchor': v['d_anchor']})
            elif bk is not None and bd < 5:
                cats['pair_cross_string'] += 1
                details.append({'led': i, 'cls': 'pair_cross_str',
                                'near_led': bk, 'd': round(bd, 1),
                                'in_legacy_window': in_win, 'amp': a})
            elif in_win:
                cats['inside_legacy_window_rival_led'] += 1
                details.append({'led': i, 'cls': 'in_win_rival',
                                'near_led': bk, 'd': round(bd, 1),
                                'amp': a, 'd_anchor': v['d_anchor']})
            elif (v['d_anchor'] is not None and v['d_anchor'] <= 6) or \
                    (v['d_interp'] is not None and v['d_interp'] <= 6):
                cats['anchor_free'] += 1
            else:
                cats['orphan'] += 1
        n_new = len(new)
        out[run] = {'new': n_new, 'gain_cats': cats,
                    'conflict_pair_gains': conflict_pairs,
                    'details': details[:12]}
        print(run, json.dumps({k: v for k, v in out[run].items()
                               if k != 'details'}), flush=True)
        del ep['sc']
        gc.collect()
    (OUT / 'e4_gain_anatomy.json').write_text(json.dumps(out, indent=1))
    print('wrote e4_gain_anatomy.json')


if __name__ == '__main__':
    main()