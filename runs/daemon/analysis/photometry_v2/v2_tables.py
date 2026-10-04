"""V2 core analysis: all tables for the v2 report from frame_metrics.json.

Lock gates (must reproduce v1 on the v1-kept subsets before extending):
  - E1 cM-bg and pooled_p50 vs v1 (b)/(d) tables (E1 settled tail)
  - E2 cM-bg vs v1 (b) table
  - census (c) medians (idle + E1 L5/L20/40/60/80/179) — locked in census_lock
"""
import json
from pathlib import Path

import numpy as np

D = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/analysis/photometry_v2')
fm = json.load(open(D / 'frame_metrics.json'))
by = {f['name']: f for f in fm}

REPAIRED_E1 = {'L10': [4, 5, 11, 18], 'L150': [10], 'L179': [13],
               'L20': [6, 14], 'L5': [3, 10], 'L60': [14], 'L80': [10, 18]}
REPAIRED_E2 = {'L10': [8], 'L100': [8, 16], 'L120': [11], 'L179': [10, 17],
               'L20': [2], 'L40': [11], 'L5': [14], 'L60': [8], 'L80': [4]}
LS = [5, 10, 20, 40, 60, 80, 100, 120, 150, 179]
V1_CM_E1 = {'L5': 83.7, 'L10': 84.9, 'L20': 79.9, 'L40': 91.1, 'L60': 97.0,
            'L80': 99.7, 'L100': 102.2, 'L120': 104.3, 'L150': 105.6, 'L179': 105.9}
V1_CP50_E1 = {'L5': 222, 'L10': 254, 'L20': 227, 'L40': 173, 'L60': 174,
              'L80': 136, 'L100': 110, 'L120': 110, 'L150': 122, 'L179': 128}
V1_CM_E2 = {'L5': 75.8, 'L10': 82.1, 'L20': 89.5, 'L40': 81.0, 'L60': 86.6,
            'L80': 91.5, 'L100': 94.6, 'L120': 98.0, 'L150': 100.6, 'L179': 101.6}
V1_CP50_E2 = {'L5': 105, 'L10': 107, 'L20': 87, 'L40': 112, 'L60': 110,
              'L80': 104, 'L100': 95, 'L120': 94, 'L150': 80, 'L179': 82}

rows = {}
for arm in ['E1', 'E2']:
    rep = REPAIRED_E1 if arm == 'E1' else REPAIRED_E2
    for L in LS:
        for k in range(1, 19):
            rows[(arm, L, k)] = by[f'cal_{arm}_L{L}_{k}']


def cMbg(r):
    if r['pooled_mean'] is None or r['bgRingU'] is None:
        return None
    return r['pooled_mean'] - r['bgRingU']


def med(vals):
    v = [x for x in vals if x is not None]
    return float(np.median(v)) if v else None


def arm_stats(arm, L, ks, full_n):
    rep = REPAIRED_E1 if arm == 'E1' else REPAIRED_E2
    rs = []
    for k in ks:
        if not full_n and k in rep.get(f'L{L}', []):
            continue
        rs.append(rows[(arm, L, k)])
    return rs


def settled_arm(arm, L, full_n, win):
    return arm_stats(arm, L, range(*win), full_n)


out = {}

# ---- lock checks on v1-kept subsets (include k1 like v1's tail-median likely did? test) ----
print('== LOCK: E1 settled (k2-18) v1-kept: cM-bg and pooled_p50 ==')
for L in LS:
    rs = arm_stats('E1', L, range(2, 19), full_n=False)
    a = med([cMbg(r) for r in rs])
    b = med([r['pooled_p50'] for r in rs])
    print(f'L{L:4d} cMbg {a:6.1f} (v1 {V1_CM_E1[f"L{L}"]:6.1f}, d {a - V1_CM_E1[f"L{L}"]:+5.1f})'
          f'  p50 {b:6.1f} (v1 {V1_CP50_E1[f"L{L}"]:4d}, d {b - V1_CP50_E1[f"L{L}"]:+5.1f})')

print('== E2 settled (k2-15) v1-kept ==')
for L in LS:
    rs = arm_stats('E2', L, range(2, 16), full_n=False)
    a = med([cMbg(r) for r in rs])
    b = med([r['pooled_p50'] for r in rs])
    print(f'L{L:4d} cMbg {a:6.1f} (v1 {V1_CM_E2[f"L{L}"]:6.1f}, d {a - V1_CM_E2[f"L{L}"]:+5.1f})'
          f'  p50 {b:6.1f} (v1 {V1_CP50_E2[f"L{L}"]:4d}, d {b - V1_CP50_E2[f"L{L}"]:+5.1f})')

out['lock'] = 'printed'

# ---- (a) per-k settle histMed, FULL n ----
print('== (a) histMed per k, FULL n (E1) ==')
tab_a = {}
for arm in ['E1', 'E2']:
    t = {}
    for L in LS:
        line = {}
        for k in range(1, 19):
            line[k] = rows[(arm, L, k)]['histMed']
        t[L] = line
    tab_a[arm] = t
    for L in LS:
        vals = [t[L][k] for k in range(1, 19)]
        print(f'{arm} L{L:4d}: ' + ' '.join(f'{v:5.1f}' for v in vals))

# k1 vs k2+ deficit quantification, per rung, full n
print('== (a) k1 transient quantification (full n) ==')
kt = {}
for arm in ['E1', 'E2']:
    for L in LS:
        k1 = rows[(arm, L, 1)]['histMed']
        rest = [rows[(arm, L, k)]['histMed'] for k in range(2, 19)]
        med_rest = float(np.median(rest))
        kt[f'{arm}_L{L}'] = {'k1': k1, 'med_k2plus': med_rest,
                             'delta_pct': round(100 * (k1 - med_rest) / med_rest, 1)}
        flag = '' if abs(k1 - med_rest) <= max(2.0, 0.03 * med_rest) else '  <-- TRANSIENT'
        print(f'{arm} L{L:4d} k1 {k1:5.1f} med(k2-18) {med_rest:5.1f} '
              f'delta {100 * (k1 - med_rest) / med_rest:+5.1f}%{flag}')
out['k1_transient'] = kt

# ---- (b)/(d) settled tables FULL n ----
print('== (b) settled FULL n ==')
res_b = {}
for arm in ['E1', 'E2']:
    win = (2, 19) if arm == 'E1' else (2, 16)
    t = []
    for L in LS:
        rs = settled_arm(arm, L, True, win)
        row = {'L': L, 'n_frames': len(rs),
               'bg': med([r['histMed'] for r in rs]),
               'cMbg': med([cMbg(r) for r in rs]),
               'p50': med([r['pooled_p50'] for r in rs]),
               'p90': med([r['pooled_p90'] for r in rs]),
               'nB40': med([r['nB40'] for r in rs]),
               'q95': med([r['q95'] for r in rs])}
        t.append(row)
        print(f"{arm} L{L:4d} n={row['n_frames']:2d} bg {row['bg']:5.1f} "
              f"cMbg {row['cMbg'] or -1:6.1f} p50 {row['p50'] or -1:6.1f} "
              f"p90 {row['p90'] or -1:6.1f} nB {row['nB40']:5.1f} q95 {row['q95']:6.1f}")
    res_b[arm] = t

# duty ratios full n
print('== (b) ratios FULL n ==')
ratios = []
for i, L in enumerate(LS):
    e1 = res_b['E1'][i]
    e2 = res_b['E2'][i]
    r_p50 = e2['p50'] / e1['p50'] if e1['p50'] else None
    r_cm = e2['cMbg'] / e1['cMbg'] if e1['cMbg'] else None
    ratios.append({'L': L, 'r_p50': r_p50, 'r_cMbg': r_cm})
    print(f"L{L:4d} p50 ratio {r_p50:5.2f}  cMbg ratio {r_cMbg if (r_cMbg:=r_cm) else 0:5.2f}")
out['duty'] = {'E1': res_b['E1'], 'E2': res_b['E2'], 'ratios': ratios}

# L>=40 cMbg ratio subset (v1: 0.89-0.97)
sub = [r for r in ratios if r['L'] >= 40]
out['duty_ratio_L40plus'] = {'r_cMbg': [round(r['r_cMbg'], 3) for r in sub],
                             'r_p50': [round(r['r_p50'], 3) for r in sub]}

# ---- (c) census FULL n: idle n=60 + L5..179 full, thr40 census + L5-vs-idle separability
print('== (c) census full ==')
cen = {}
cens = {f['name']: f['nCens40'] for f in fm}
idle_all = [cens[f'cal_idle_{k:02d}'] for k in range(60)]
idle_00_11 = [cens[f'cal_idle_{k:02d}'] for k in range(12)]
idle_12_59 = [cens[f'cal_idle_{k:02d}'] for k in range(12, 60)]
print(f'idle n=60: med {np.median(idle_all)} range {min(idle_all)}-{max(idle_all)} '
      f'p95 {np.percentile(idle_all, 95)}; idle00-11 med {np.median(idle_00_11)} '
      f'({min(idle_00_11)}-{max(idle_00_11)}); idle12-59 med {np.median(idle_12_59)} '
      f'({min(idle_12_59)}-{max(idle_12_59)}')
cen['idle_all'] = idle_all
# does any idle frame reach 25? (threshold re-validation)
fail_ids = [f'cal_idle_{k:02d}' for k in range(60) if cens[f'cal_idle_{k:02d}'] >= 25]
print('idle frames with census >= 25:', fail_ids if fail_ids else 'NONE (max '
      f'{max(idle_all)})')
cen['idle_fail_ids'] = fail_ids
for arm in ['E1', 'E2']:
    for L in LS:
        vals = [rows[(arm, L, k)]['nCens40'] for k in range(1, 19)]
        print(f'{arm} L{L:4d}: med {np.median(vals):5.1f} min {min(vals):3d} '
              f'max {max(vals):3d} vals {vals if L in (5,) else ""}')
        cen[f'{arm}_L{L}'] = vals
out['census'] = cen

# L5 vs idle separability at full n (threshold 25), with k>=2 rule
l5v = cen['E1_L5']
lt2 = [v for k_i, v in zip(range(1, 19), l5v) if k_i >= 2]
print(f'E1 L5 full: frames>=25 {sum(1 for v in l5v if v >= 25)}/18; '
      f'k>=2 only: {sum(1 for v in lt2 if v >= 25)}/17')
out['census']['E1_L5_sep'] = {'all18': sum(1 for v in l5v if v >= 25),
                              'k2plus': sum(1 for v in lt2 if v >= 25)}

# also E2 L5 (recovered frame was k14)
l5e2 = cen['E2_L5']
print(f'E2 L5 full: med {np.median(l5e2)}, frames>=25 {sum(1 for v in l5e2 if v >= 25)}/18')

# ---- (d) transfer FULL n: E1 cMbg monotone range + band [90,105] crossing + wall
print('== (d) band check FULL n ==')
e1_cm = {r['L']: r['cMbg'] for r in res_b['E1']}
e2_cm = {r['L']: r['cMbg'] for r in res_b['E2']}
# wall per settled frame = ring? v1's wall-pixel median >215 -> use q95? The
# 255-wall >= 40 bail means wall median <= 215. Measure wall as the pooled
# NON-blob, non-ring background: whole-frame q95 is the spill-wall read.
# v1 didn't print per-frame wall on run0; it derived the bail from E4. Keep
# E4-side bail (wall_at_site from the decode summary) + run0 ring side.
out['band_E1_cm'] = e1_cm
out['band_E2_cm'] = e2_cm

json.dump(out, open(D / 'v2_tables.json', 'w'), indent=1, default=str)
print('wrote v2_tables.json')