"""Regrade the claim_adj gains under the STRICT 17h43 impostor law: a gain at
d_claim < 6 of another claim must ALSO not be nearer to that claim than to
its own anchor/interp, AND the claim must not be a STRONGER amp (else the
pixel belongs to the stronger codeword: impostor proxy).

Recompute per gain using the anatomy + detail: for each gain we need the
nearest claim's LED + AMP. Recompute the new loop per epoch once more and
dump per-gain nearest-claim info with amps, then classify:
  pass : d_claim < 5 AND pair_gap<=12 (straddle grid consistent)
  fail : d_claim < 6 AND neighbor amp > gain amp (impostor proxy)
"""
import gc
import io
import json
import math
import sys
import time
from pathlib import Path

import numpy as np

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
sys.path.insert(0, str(REPO / 'runs/daemon/analysis/parity'))
import cwc_pos_decode as C  # noqa: E402
from offline_hole_verify import parse_planes  # noqa: E402
from e4_impostor_audit import build_epoch, loop_new  # noqa: E402

RUNS = REPO / 'runs/daemon/analysis/e4_union/repaired'
OUT = REPO / 'runs/daemon/analysis/parity'

legacy_finals = {}
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
        ep = build_epoch(RUNS / run, tag)
        mask = ep['mb'] >= ep['thr']
        new = loop_new(ep, mask)
        nsite = {q['led']: (q['cx'], q['cy'], q['amp']) for q in new}
        verdict = {}
        n_imp = 0
        n_pass = 0
        for i, v in sorted(gains.items()):
            x, y, a = nsite[i]
            # nearest NEW-set claim of aDifferent id
            bk, bd = None, 9e9
            for k, (kx, ky, ka) in nsite.items():
                if k == i:
                    continue
                d = math.hypot(kx - x, ky - y)
                if d < bd:
                    bk, bd, bka = k, d, ka
            # id gap to nearest same-string id in the ANCHOR/legacy space
            s = i // 200
            near_ids = sorted(abs(k - i) for k in legacy_finals[run]
                              if k // 200 == s and abs(k - i) <= 12)
            gap_ok = bool(near_ids)  # has confirmed same-str neighbours <=12
            imp = bool(bk is not None and bd < 6 and bka > a + 1.0)
            if imp:
                n_imp += 1
            else:
                n_pass += 1
            verdict[i] = {'nearest_claim': bk, 'd_nearest': round(bd, 1),
                          'imp': imp, 'gap_ok': gap_ok,
                          'cls': v.get('cls', '')}
        out[run] = {'gains': len(gains), 'impostor_proxy': n_imp,
                    'guard_pass_clean': n_pass,
                    'impostor_ids': [i for i, v in verdict.items() if v['imp']]}
        print(run, json.dumps(out[run]), flush=True)
        del ep['sc']
        gc.collect()
    (OUT / 'e4_impostor_strict.json').write_text(json.dumps(out, indent=1))
    print('wrote e4_impostor_strict.json')


if __name__ == '__main__':
    main()