#!/usr/bin/env python3
"""Dump the S14R-0004 validation corpus as untagged RGBA .raw frames.

Companion to page_impl_validate.js. Regimes are tagged in the filename
(<stem>@<tag>.raw) because the e4_0003d_extract dir reuses run stems.
PIL ignores the corpus JPEGs' embedded sRGB profile -> untagged pixels,
matching the proposal's canvas-readback rule.

Usage: python3 page_impl_dump_frames.py [outdir=/tmp/tearguard_corpus]
"""
import os, sys
import numpy as np
from PIL import Image

BASE = '/home/nellie/projects/led-display/poc_survey'
OUT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/tearguard_corpus'
CORPORA = [('runs/daemon/runs/run8', 'e700a'), ('runs/daemon/runs/run9', 'e700b'),
           ('runs/daemon/runs/run10', 'e700c'), ('runs/daemon/runs/run11', 'e700d'),
           ('runs/daemon/analysis/e4_0003d_extract', 'e300')]
os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    if f.endswith('.raw'):
        os.remove(os.path.join(OUT, f))
n = 0
for dp, tag in CORPORA:
    full = os.path.join(BASE, dp)
    for f in sorted(x for x in os.listdir(full) if x.endswith('.jpg')):
        stem = f[:-4]
        a = np.asarray(Image.open(os.path.join(full, f)).convert('RGB'), np.uint8)
        h, w, _ = a.shape
        rgba = np.empty((h, w, 4), np.uint8); rgba[..., :3] = a; rgba[..., 3] = 255
        with open(f'{OUT}/{stem}@{tag}.raw', 'wb') as fh:
            fh.write(w.to_bytes(4, 'little')); fh.write(h.to_bytes(4, 'little'))
            fh.write(rgba.tobytes())
        n += 1
print('dumped', n, 'frames ->', OUT)
