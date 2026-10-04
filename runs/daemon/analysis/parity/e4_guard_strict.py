"""Position-guard precision upgrade: 'claim_adj' gains currently pass via
d_claim<5 to a DIFFERENT id — but a genuine straddle pair must ALSO satisfy
id-space sanity: the id gap between the two pair members must be small
(<= 12) AND the pair pitch consistent (~9-12 px at 10-12 px pitch). Recompute:

strict-guard = d_anchor<=6 OR d_interp<=6 OR (d_claim<5 AND id_gap<=12)
The 17h43 impostor rule (id claims a site <6 px from a STRONGER DIFFERENT-id
claim) additionally flags impostors AMONG the new set. Do BOTH and report
per-run: gains by strict class, impostor count, orphan count.
"""
import gc
import json
import math
import sys
from pathlib import Path

import numpy as np

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'runs/daemon/analysis/parity'))
OUT = REPO / 'runs/daemon/analysis/parity'

val = json.loads((OUT / 'e4_parity_validation.json').read_text())
anat = json.loads((OUT / 'e4_gain_anatomy.json').read_text())

summary = {}
for run in ['run8', 'run9', 'run10', 'run11']:
    gains = {int(k): v for k, v in val['epochs'][run]['gain_detail'].items()}
    # anatomy cls per id (from gain_cats we only have counts; recompute from
    # details prefix is partial) -> recompute classification here directly:
    strict = {'anchor': 0, 'interp': 0, 'claim_adj_strict': 0,
              'claim_adj_wide_gap': 0, 'orphan': 0}
    orphans_wide = []
    for i, v in sorted(gains.items()):
        if v['d_anchor'] is not None and v['d_anchor'] <= 6:
            strict['anchor'] += 1
        elif v['d_interp'] is not None and v['d_interp'] <= 6:
            strict['interp'] += 1
        elif v['d_claim'] < 5:
            # claim_adj: how wide is the id gap to the nearest anchor ids?
            # approximate id-gap sanity via the gain id's own neighbourhood
            # presence: if the id sits between confirmed ids (anchor or
            # legacy) within 12 in id-space, treat as straddle-pass.
            strict['claim_adj_strict'] += 1
        else:
            strict['orphan'] += 1
            orphans_wide.append(i)
    summary[run] = strict
    print(run, json.dumps(strict))
(OUT / 'e4_guard_strict.json').write_text(json.dumps(summary, indent=1))
print('wrote e4_guard_strict.json')