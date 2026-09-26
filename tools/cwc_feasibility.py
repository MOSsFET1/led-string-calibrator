#!/usr/bin/env python3
"""Feasibility test: can greedy max-min selection reach 1600 codewords for
candidate (n, k) constant-weight schemes? Reports achieved count + d_min + column spread.
"""
import sys, time
import numpy as np
from itertools import combinations


def greedy(n, k, need=1600, dmin_req=4, timeout=120):
    t0 = time.time()
    pool = list(combinations(range(n), k))
    pool_a = np.zeros((len(pool), n), dtype=np.uint8)
    for idx, cw in enumerate(pool):
        for b in cw:
            pool_a[idx, b] = 1
    sel_idx = []
    avail = np.ones(len(pool), dtype=bool)
    while len(sel_idx) < need:
        if time.time() - t0 > timeout:
            return len(sel_idx), None, 'TIMEOUT'
        if not sel_idx:
            nxt = 0
        else:
            sel = pool_a[sel_idx]
            ov = pool_a @ sel.T
            dmin_c = (k - ov.max(axis=1)) * 2
            ok = (dmin_c >= dmin_req) & avail
            idxs = np.nonzero(ok)[0]
            if len(idxs) == 0:
                return len(sel_idx), None, 'EXHAUSTED'
            C = sel.sum(axis=0)
            colcount = C[None, :] + pool_a[idxs]
            maxov = ov[idxs].max(axis=1)
            score = colcount.max(axis=1) * 1000 + colcount.var(axis=1) * 100 + maxov
            nxt = int(idxs[np.argmin(score)])
        sel_idx.append(nxt)
        avail[nxt] = False
    sel = pool_a[sel_idx].astype(np.int16)
    ov = sel @ sel.T
    np.fill_diagonal(ov, -1)
    dmin = int(((k - ov.max(axis=1)) * 2).min())
    cols = sel.sum(axis=0)
    return len(sel_idx), (dmin, int(cols.min()), int(cols.max())), 'OK'


if __name__ == '__main__':
    for (n, k) in [(14, 7), (17, 8), (18, 8), (19, 9), (20, 10), (20, 9), (16, 8)]:
        t0 = time.time()
        cnt, info, status = greedy(n, k, timeout=90)
        ideal = 1600 * k / n
        print(f'{k}-of-{n}: {status} count={cnt} info={info} '
              f'(packing bound ~C({n},{k-2})/C({k},{k-2})={int(__import__("math").comb(n, k-2)/__import__("math").comb(k, k-2))}, '
              f'col ideal {ideal:.0f}/plane) [{time.time()-t0:.0f}s]')