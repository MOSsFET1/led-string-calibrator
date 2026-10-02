#!/usr/bin/env python3
"""Decode the r6 handheld CWC read: per-core normalise, 2-means binarise, match."""
import json, collections
import numpy as np

codes = json.load(open('tools/codewords_9of18.json'))['codes']
mat = np.zeros((len(codes), 18), dtype=np.float32)
for i, c in enumerate(codes):
    for p in c:
        mat[i, p] = 1


def decode(R, tag):
    top9 = np.sort(R, axis=1)[:, -9:].mean(axis=1, keepdims=True)
    Rn = R / np.maximum(top9, 0.1)
    bits = np.zeros_like(Rn)
    for p in range(18):
        col = Rn[:, p]
        mid = (np.percentile(col, 25) + np.percentile(col, 75)) / 2
        bits[:, p] = (col > mid).astype(np.float32)
    bits = bits.astype(np.uint8)
    w = bits.sum(axis=1)
    d = (bits[:, None, :] != mat[None, :, :]).sum(axis=2)
    best = d.argmin(axis=1)
    bestd = d.min(axis=1)
    d2 = d.copy()
    d2[np.arange(len(bits)), best] = 999
    second = d2.min(axis=1)
    good = (bestd <= 2) & (w == 9) & ((second - bestd) >= 2)
    print(f'{tag}: weight hist {np.bincount(w, minlength=19)[6:13]}, '
          f'strict decodes {int(good.sum())}, distinct {len(set(best[good].tolist()))}')
    return bits


decode(np.load('runs/s14n-cwc-handheld-0927/ratios_masterref.npy'), '5x5')
R1 = np.load('runs/s14n-cwc-handheld-0927/ratios_1px.npy')
decode(R1, '1px')
decode(0.5 * (np.load('runs/s14n-cwc-handheld-0927/ratios_masterref.npy') + R1), 'avg')