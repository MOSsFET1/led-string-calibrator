"""Final v2 numbers: probe-metric settle, E4 ratio, idle floors, union re-derive."""
import io
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
RUN0 = REPO / 'runs/daemon/runs/run0'
D = REPO / 'runs/daemon/analysis/photometry_v2'
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402

fm = json.load(open(D / 'frame_metrics.json'))
by = {f['name']: f for f in fm}


def load(jpg):
    raw = jpg.read_bytes()
    assert raw.endswith(b'\xff\xd9')
    img = Image.open(io.BytesIO(raw))
    img.load()
    return np.asarray(img.convert('RGB'), dtype=np.uint8).max(axis=2)


# ---------- (a) probe metric (pooled core P90 - ring) per k, E1 full n ----------
print('== (a) probe metric pooled_p90 - bgRingU per k (E1, full n) ==')
PM = {}
for L in [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]:
    line = []
    for k in range(1, 19):
        r = by[f'cal_E1_L{L}_{k}']
        v = (r['pooled_p90'] - r['bgRingU']) if r['pooled_p90'] is not None else None
        line.append(v)
    PM[L] = line
    print(f'L{L:4d}: ' + ' '.join(f'{v:6.1f}' if v is not None else '   n/a'
                                  for v in line))
# deficit of k1/k2 vs settled tail median
print('k1/k2 deficit vs med(k5-18), probe metric, E1 full n:')
for L in [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]:
    tail = np.median([PM[L][k] for k in range(4, 18)])
    d1 = 100 * (PM[L][0] - tail) / tail
    d2 = 100 * (PM[L][1] - tail) / tail
    print(f'L{L:4d} k1 {d1:+5.2f}%  k2 {d2:+5.2f}%  tail {tail:6.1f}')

# k2 residual on histMed: |k2 - med(k3-10)| per rung-arm
print('== k2 residual (histMed), k2 vs med(k3-10), all 20 rung-arms ==')
mx = 0
for arm in ['E1', 'E2']:
    for L in [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]:
        k2 = by[f'cal_{arm}_L{L}_2']['histMed']
        rest = [by[f'cal_{arm}_L{L}_{k}']['histMed'] for k in range(3, 11)]
        m = float(np.median(rest))
        d = 100 * (k2 - m) / m
        mx = max(mx, abs(d))
        flag = ' <--' if abs(d) > 3 else ''
        print(f'{arm} L{L:4d} k2 {d:+5.1f}%{flag}')
print('max |k2 residual| =', round(mx, 1), '%')

# ---------- (b) E4 union-plane/master ratio (run-dir, v2 parity sites) ----------
print('== (b) E4 union-plane/master core ratio (v2, direct run dirs) ==')
RAT = {}
for run, tag, L in [('run8', 'r8', 80), ('run9', 'r9', 100),
                    ('run10', 'r10', 120), ('run11', 'r11', 150)]:
    leds = json.load(open(D / f'{run}_ledpos_v2.json'))['leds']
    mast = np.asarray(Image.open(RUN0.parent / run / f'cwc_{tag}_master.jpg')
                      .convert('RGB'), dtype=np.float32).max(axis=2)
    # per-plane union: max over planes of 3x3 core max at each site
    umax = None
    for p in range(24):
        pl = np.asarray(Image.open(RUN0.parent / run / f'cwc_{tag}_p{p:02d}.jpg')
                        .convert('RGB'), dtype=np.float32).max(axis=2)
        m2 = cv2.dilate(pl, np.ones((3, 3), np.float32))
        umax = m2 if umax is None else np.maximum(umax, m2)
    r_u, r_m, n_unc = [], [], 0
    for q in leds:
        cy, cx = q['cy'], q['cx']
        u3 = float(umax[cy - 1:cy + 2, cx - 1:cx + 2].max())
        m3 = float(mast[cy - 1:cy + 2, cx - 1:cx + 2].max())
        if m3 < 250:
            r_u.append(u3)
            r_m.append(m3)
            n_unc += 1
    r_u = np.array(r_u)
    r_m = np.array(r_m)
    ratio = float(np.median(r_u / np.maximum(r_m, 1)))
    RAT[L] = {'n_unclipped': n_unc, 'ratio_med': round(ratio, 3),
              'ratio_p10': round(float(np.percentile(r_u / np.maximum(r_m, 1), 10)), 3),
              'ratio_p90': round(float(np.percentile(r_u / np.maximum(r_m, 1), 90)), 3)}
    print(f'{tag} L={L}: n_unclipped {n_unc} ratio med {ratio:.3f} '
          f'(p10 {RAT[L]["ratio_p10"]} p90 {RAT[L]["ratio_p90"]})')

# ---------- (c) idle floors ----------
print('== (c) idle floors ==')
hm_idle = [by[f'cal_idle_{k:02d}']['histMed'] for k in range(60)]
cens_idle = [by[f'cal_idle_{k:02d}']['nCens40'] for k in range(60)]
print('idle histMed all 60:', [round(v, 1) for v in hm_idle[::5]])
print('leg1 histMed med', float(np.median(hm_idle[:12])),
      'leg2 med', float(np.median(hm_idle[12:])),
      'leg2 tail40-59 med', float(np.median(hm_idle[40:])))
leg2_tail_frames = [f'cal_idle_{k:02d}' for k in range(40, 60)]
pm50 = [by[n]['pooled_p50'] for n in leg2_tail_frames]
pm90 = [by[n]['pooled_p90'] for n in leg2_tail_frames]
pmea = [by[n]['pooled_mean'] for n in leg2_tail_frames]
print(f'leg2 tail (k40-59, n=20) pooled core: P50 med {np.median(pm50)} '
      f'P90 med {np.median(pm90)} mean med {np.median(pmea):.1f}')
leg1_frames = [f'cal_idle_{k:02d}' for k in range(12)]
print(f'leg1 (k00-11) pooled core: P50 med {np.median([by[n]["pooled_p50"] for n in leg1_frames])} '
      f'P90 med {np.median([by[n]["pooled_p90"] for n in leg1_frames])} '
      f'mean med {np.median([by[n]["pooled_mean"] for n in leg1_frames]):.1f}')
# site-core >=100 fraction (anchor sites = thr-200 seed CCs from L179 union)
seeds = None
for k in range(1, 19):
    ml = load(RUN0 / f'cal_E1_L179_{k}.jpg')
    a = ml >= 200
    seeds = a if seeds is None else (seeds | a)
ns, lab = cv2.connectedComponents(seeds.astype(np.uint8), connectivity=8)
print('anchor seed CCs:', ns - 1)
for leg, ks in [('leg1', range(12)), ('leg2tail', range(40, 60))]:
    fr100 = []
    fr150 = []
    for k in ks:
        ml = load(RUN0 / f'cal_idle_{k:02d}.jpg')
        f = ml.astype(np.float32)
        cores = []
        for i in range(1, ns):
            m = lab == i
            cores.append(float(f[m].max()))
        cores = np.array(cores)
        fr100.append(100 * (cores >= 100).mean())
        fr150.append(100 * (cores >= 150).mean())
    print(f'{leg}: sites-core>=100 {np.median(fr100):.1f}% (range {min(fr100):.1f}-{max(fr100):.1f}), '
          f'>=150 {np.median(fr150):.1f}%')

# ---------- E4 unions re-derive from v2 ledpos ----------
print('== E4 union re-derive (v2) ==')
sets = {}
for run, tag, L in [('run8', 'r8', 80), ('run9', 'r9', 100),
                    ('run10', 'r10', 120), ('run11', 'r11', 150)]:
    sets[tag] = {q['led'] for q in json.load(open(D / f'{run}_ledpos_v2.json'))['leds']}
import itertools
for a, b in itertools.combinations(sets, 2):
    print(f'{a}∪{b}: {len(sets[a] | sets[b])}')
u4 = set.union(*sets.values())
print('union4:', len(u4), ' never-seen:', 600 - len(u4))
acc = set()
for t in ['r8', 'r9', 'r10', 'r11']:
    prev = len(acc)
    acc |= sets[t]
    print(f'+{t}: +{len(acc) - prev} -> {len(acc)}')

# E2 L20 census caveat detail + median-of-3 rule check at L5
print('== (c) paint-dependence caveat ==')
for arm in ['E1', 'E2']:
    for L in [5, 10, 20]:
        vals = [by[f'cal_{arm}_L{L}_{k}']['nCens40'] for k in range(2, 19)]
        print(f'{arm} L{L}: k2-18 min {min(vals)} med {float(np.median(vals))} '
              f'frames>=25 {sum(1 for v in vals if v >= 25)}/{len(vals)}')
# median-of-3 at E1 L5 (k>=2): triplets
v = [by[f'cal_E1_L5_{k}']['nCens40'] for k in range(2, 19)]
med3 = [sorted(v[i:i + 3])[1] for i in range(0, len(v) - 2, 3)]
print('E1 L5 k>=2 median-of-3 triplets:', med3,
      'all>=25:', all(x >= 25 for x in med3), f'({len(med3)} triplets + {len(v) % 3} leftover)')

json.dump({'probe_metric_settle': {str(k): v for k, v in PM.items()},
           'ratios': RAT}, open(D / 'v2_final_numbers.json', 'w'), indent=1)
print('wrote v2_final_numbers.json')