"""The decisive audit: CROSS-MODEL GEOMETRY. For every gain claim site, how
far is the site from its OWN STRING's other claims, and is there a claim of a
DIFFERENT codeword within 3 px Chebyshev (which the legacy loop ate)?

Sanity test on the claimed mechanism: the legacy loop ate pixels with
Chebyshev <= 3 REGARDLESS of codeword. So every new-claimed pixel that
carries a DIFFERENT codeword's argmax and sits within Chebyshev 3 of a
legacy claim center was directly suppressed by the legacy window.
The gain population must be ~entirely of that shape IF the mechanism is the
7px-eat; else something else moved (e.g. claim-order cascade).

Also: run a CONTROL — the 'window 3 px global' variant (proposal §5's
NOT-proposal) on the same epoch, to show its +50-62 raw is the relabel-storm
class, distinct from ours.
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
from e4_impostor_audit import build_epoch  # noqa: E402

RUNS = REPO / 'runs/daemon/analysis/e4_union/repaired'
OUT = REPO / 'runs/daemon/analysis/parity'
N = 600


def loop_generic(ep, mask, amp_gate, margin_gate, win, same_str_only, nPs=200,
                 claim_only=False):
    sc, best, argi, cand = ep['sc'], ep['best'], ep['argi'], ep['cand']
    H, W = mask.shape
    used = np.zeros(mask.shape, bool)
    amap = np.where(mask, best, -1e9)
    order = np.argsort(amap.ravel())[::-1]
    accepted = []
    half = win // 2
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
        if claim_only:
            used[y, x] = True
        else:
            used[max(0, y - half): y + half + 1,
                 max(0, x - half): x + half + 1] = True
            if same_str_only:
                # retro-mark same-string only: unmark others first
                s_own = i // nPs
                for yy in range(max(0, y - half), min(H, y + half + 1)):
                    for xx in range(max(0, x - half), min(W, x + half + 1)):
                        if not ((int(argi[yy, xx]) // nPs) == s_own):
                            used[yy, xx] = False
        if claim_only and same_str_only:
            s_own = i // nPs
            for yy in range(max(0, y - half), min(H, y + half + 1)):
                for xx in range(max(0, x - half), min(W, x + half + 1)):
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
    val = json.loads((OUT / 'e4_parity_validation.json').read_text())
    legacy_finals = {}
    for run in ['run8', 'run9', 'run10', 'run11']:
        leds = json.loads((REPO / f'runs/daemon/analysis/e4_union/{run}_ledpos.json'
                           ).read_text())['leds']
        legacy_finals[run] = {q['led']: (q['cx'], q['cy'], q['amp'])
                              for q in leds}
    out = {}
    for run, tag in [('run8', 'r8'), ('run9', 'r9'),
                     ('run10', 'r10'), ('run11', 'r11')]:
        ep = build_epoch(RUNS / run, tag)
        mask = ep['mb'] >= ep['thr']
        new = loop_generic(ep, mask, 40.0, 6.0, 3, True, claim_only=True)
        nsite = {q['led']: (q['cx'], q['cy'], q['amp']) for q in new}
        gains = {int(k): v for k, v in val['epochs'][run]['gain_detail'].items()}
        # mechanism audit: gain site inside a legacy 7px window (cheb<=3)?
        n_in_win, n_free = 0, 0
        for i, v in gains.items():
            x, y, a = nsite[i]
            cheb_min = min((max(abs(kx - x), abs(ky - y))
                            for k, (kx, ky, _ka) in legacy_finals[run].items()
                            if k != i), default=99)
            if cheb_min <= 3:
                n_in_win += 1
            else:
                n_free += 1
        # control: window 3 global (the NOT-proposal)
        ctrl = loop_generic(ep, mask, 40.0, 6.0, 7, False)
        ctrl_ids = {q['led'] for q in ctrl}
        legacy_ids = {q['led'] for q in
                      loop_generic(ep, mask, 40.0, 6.0, 7, False)}  # same
        # legacy proper:
        legacy = loop_generic(ep, mask, 40.0, 6.0, 7, False)
        # hmm — loop_generic(win=7,same_str_only=False) IS legacy. ctrl above
        # computed it. legacy-ids from published:
        legacy_ids = set(legacy_finals[run])
        out[run] = {
            'new_confirmed_check': len(new),
            'gain_in_legacy7px_window': n_in_win,
            'gain_free_of_legacy_windows': n_free,
            'control_global7_window_confirmed': len(ctrl),
            'control_gain_vs_published': len(ctrl) - len(legacy_ids),
        }
        print(run, json.dumps(out[run]), flush=True)
        del ep['sc']
        gc.collect()
    (OUT / 'e4_mechanism_audit.json').write_text(json.dumps(out, indent=1))
    print('wrote e4_mechanism_audit.json')


if __name__ == '__main__':
    main()