#!/usr/bin/env python3
"""Vectorised CWC feasibility: greedy max-min with numpy bitmask ops.

Same selection rule as cwc_feasibility.py but all-pairs distances maintained
incrementally: dist[i] = 2*(k - max_overlap[i, j] for j in selected), updated
by one matmul per step (P x 1). ~0.5 s/step instead of ~2-6 s.
"""
import sys, time
import numpy as np
from itertools import combinations


def greedy(n, k, need=1600, dmin_req=4, timeout=600):
    t0 = time.time()
    pool = np.array([sum(1 << b for b in cw) for cw in combinations(range(n), k)],
                    dtype=np.uint32)
    P = len(pool)
    # popcount table for pool × pool
    bits = np.array([[(pool[i] >> b) & 1 for b in range(n)] for i in range(P)],
                    dtype=np.uint8)
    ovmax = np.zeros(P, dtype=np.int32)      # max overlap to selection
    nsel = 0
    sel_cols = np.zeros(n, dtype=np.int32)
    avail = np.ones(P, dtype=bool)
    sel_idx = []
    # incremental: ov[i] = popcount(pool[i] & pool[j]) for selected j
    ov_to_sel = np.zeros(P, dtype=np.int32)
    while len(sel_idx) < need:
        if time.time() - t0 > timeout:
            return len(sel_idx), None, 'TIMEOUT'
        if not sel_idx:
            nxt = 0
        else:
            dmin_c = (k - ov_to_sel) * 2
            ok = (dmin_c >= dmin_req) & avail
            idxs = np.nonzero(ok)[0]
            if len(idxs) == 0:
                return len(sel_idx), None, 'EXHAUSTED'
            colcount = sel_cols[None, :] + bits[idxs]
            maxov = ov_to_sel[idxs]
            score = colcount.max(axis=1) * 1000 + colcount.var(axis=1) * 100 + maxov
            nxt = int(idxs[np.argmin(score)])
        sel_idx.append(nxt)
        avail[nxt] = False
        # update overlaps to the new codeword for all remaining
        newov = bits @ bits[nxt]              # (P,) popcount of AND
        np.maximum(ov_to_sel, newov, out=ov_to_sel)
        sel_cols += bits[nxt]
    sel = bits[sel_idx].astype(np.int32)
    OV = sel @ sel.T
    np.fill_diagonal(OV, -1)
    dmin = int(((k - OV.max(axis=1)) * 2).min())
    cols = sel.sum(axis=0)
    return len(sel_idx), (dmin, int(cols.min()), int(cols.max())), 'OK'


if __name__ == '__main__':
    for (n, k) in [(14, 7), (16, 8), (17, 8), (18, 8), (18, 9), (20, 10)]:
        t0 = time.time()
        cnt, info, status = greedy(n, k, timeout=550)
        import math
        bound = math.comb(n, k - 2) // math.comb(k, k - 2)
        print(f'{k}-of-{n}: {status} count={cnt} info={info} '
              f'(packing bound {bound}, col ideal {1600*k//n}/plane) [{time.time()-t0:.0f}s]', flush=True)