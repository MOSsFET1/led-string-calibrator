#!/usr/bin/env python3
"""Dump 8 frames (4 clean + 4 torn) as .raw RGBA (w,h header + bytes) + run node parity."""
import json, subprocess, os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = '/tmp/tearguard_parity'
os.makedirs(OUT, exist_ok=True)
SEL = [('runs/daemon/runs/run8', 'cwc_r8_p07'),
       ('runs/daemon/runs/run10', 'cwc_r10_p05'),
       ('runs/daemon/runs/run10', 'cwc_r10_p21'),
       ('runs/daemon/analysis/e4_0003d_extract', 'cwc_r12_p15'),
       ('runs/daemon/runs/run11', 'cwc_r11_p23'),   # clean, worst gray
       ('runs/daemon/analysis/e4_0003d_extract', 'cwc_r10_p11'),  # clean, worst chroma-rMAD (exp300)
       ('runs/daemon/runs/run8', 'cwc_r8_master'),  # clean, high SLVL at exp700
       ('runs/daemon/runs/run9', 'cwc_r9_p12')]     # clean mid
BASE='/home/nellie/projects/led-display/poc_survey'
paths=[]
for dp, stem in SEL:
    p=os.path.join(BASE,dp,stem+'.jpg')
    a=np.asarray(Image.open(p).convert('RGB'), np.uint8)  # H,W,3
    h,w,_=a.shape
    rgba=np.empty((h,w,4),np.uint8); rgba[...,:3]=a; rgba[...,3]=255
    with open(f'{OUT}/{stem}.raw','wb') as fh:
        fh.write(w.to_bytes(4,'little')); fh.write(h.to_bytes(4,'little'))
        fh.write(rgba.tobytes())
    paths.append(f'{OUT}/{stem}.raw')
r=subprocess.run(['node', os.path.join(HERE,'robust_algo_parity.js'), *paths],
                 capture_output=True, text=True)
print(r.stdout); print(r.stderr)