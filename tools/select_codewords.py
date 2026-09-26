#!/usr/bin/env python3
"""S14 codeword selection — 1600 codes from C(14,7) with d_min >= 4 + column balance.

Greedy max-min selection with regret scoring: each step picks the available
codeword with min distance >= 4 to the whole selection, scoring candidates by
resulting column imbalance (max col count, then variance, then max overlap).
Writes codewords as JSON (sorted 7-plane index lists) + selection stats.
"""
import json, sys, time
import numpy as np
from itertools import combinations
from pathlib import Path

N_NEED = 1600
DMIN = 4
PLANES = 14


def main():
    t0 = time.time()
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('codewords_7of14.json')
    pool = list(combinations(range(PLANES, ), 7)) if False else [c for c in combinations(range(PLANES), 7)]
    pool_a = np.zeros((len(pool), PLANES), dtype=np.uint8)
    for idx, cw in enumerate(pool):
        for b in cw:
            pool_a[idx, b] = 1
    sel_idx = []
    avail = np.ones(len(pool), dtype=bool)
    exhausted_at = None
    while len(sel_idx) < N_NEED:
        if not sel_idx:
            nxt = 0
        else:
            sel = pool_a[sel_idx]
            ov = pool_a @ sel.T                       # (P, s)
            dmin = (7 - ov.max(axis=1)) * 2           # min dist to selection
            ok = (dmin >= DMIN) & avail
            idxs = np.nonzero(ok)[0]
            if len(idxs) == 0:
                exhausted_at = len(sel_idx)
                break
            C = sel.sum(axis=0)
            colcount = C[None, :] + pool_a[idxs]      # (c, 14)
            maxov = ov[idxs].max(axis=1)
            score = (colcount.max(axis=1) * 1000
                     + colcount.var(axis=1) * 100
                     + maxov)
            nxt = int(idxs[np.argmin(score)])
        sel_idx.append(nxt)
        avail[nxt] = False
    sel = pool_a[sel_idx]
    cols = sel.sum(axis=0)
    selset = pool_a[sel_idx].astype(np.int16)
    ov = selset @ selset.T
    np.fill_diagonal(ov, 7)
    dmin = ((7 - ov.max(axis=1)) * 2).min()
    print(f'selected {len(sel_idx)} codewords in {time.time()-t0:.1f}s'
          + (f' (EXHAUSTED at {exhausted_at})' if exhausted_at else ''))
    print(f'per-plane ON counts: {cols.tolist()}  (ideal {len(sel_idx)/PLANES:.1f})')
    print(f'achieved d_min: {dmin}')
    codes = [sorted(pool[i]) for i in sel_idx]
    out_path.write_text(json.dumps({'n': PLANES, 'k': 7, 'dmin': int(dmin),
                                    'cols': cols.tolist(), 'codes': codes}))
    print(f'wrote {out_path}')
    return 0


if __name__ == '__main__':
    sys.exit(main())