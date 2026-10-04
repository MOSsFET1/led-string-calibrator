#!/usr/bin/env python3
"""Robust tear-detector benchmark: self-normalizing vs absolute jump metrics.

Corpus: 197 frames
  - run8..run11 (exp 699.97, 3 torn: cwc_r8_p07 y=191, cwc_r10_p05 y=383,
    cwc_r10_p21 y=575)
  - e4_0003d_extract (exp 300.03, 1 torn: cwc_r12_p15 y=272)
Per frame: row means (gray=(R+G+B)/3, rg=R-G, bg=B-G) -> 3-tap [1,2,1]/4 smooth ->
adjacent jump series j[i] (page-exact 4-tap formula) for i >= skipTop=120.

Per arm candidates:
  A  Dmax            absolute max jump                     (current 0003e metric)
  B  rMAD            Dmax / (1.4826*MAD(all jumps)+floor)  global self-normalized
  C  rP              Dmax / (p995(jumps)+floor)            percentile reference
  D  rLOC            Dmax / (localMAD(y)+floor)            local +-45-row scale,
                                                            window excludes +-3 rows
  E  SLVL            |level shift at argmax| / (frame level MAD+floor), 20-60 rows
                                                            each side of the jump
Floors: gray 1.0 (8-bit LSB), chroma 1.0. Read-only corpus.
"""
import json, os
import numpy as np
from PIL import Image

BASE = '/home/nellie/projects/led-display/poc_survey'
SKIP_TOP = 120
WIN = 45          # local-scale halfwindow (rows)
GUARD = 3         # rows excluded around argmax in local scale
LVL_IN, LVL_OUT = 20, 60   # level-shift band rows from tear row
FLOOR = 1.0

CORPORA = [
    ('runs/daemon/runs/run8',  'exp700'),
    ('runs/daemon/runs/run9',  'exp700'),
    ('runs/daemon/runs/run10', 'exp700'),
    ('runs/daemon/runs/run11', 'exp700'),
    ('runs/daemon/analysis/e4_0003d_extract',              'exp300'),
]
TORN = {('run8', 'cwc_r8_p07'), ('run10', 'cwc_r10_p05'),
        ('run10', 'cwc_r10_p21'), ('e4_0003d_extract', 'cwc_r12_p15')}

def smooth3(v):
    out = np.empty_like(v)
    out[1:-1] = (v[:-2] + 2*v[1:-1] + v[2:]) / 4.0
    out[0] = v[0]; out[-1] = v[-1]
    return out

def jump_series(v):
    """page-exact 0003e arm(): d_i = 0.25*|(v[i-1]+2v[i]+v[i+1])-(v[i]+2v[i+1]+v[i+2])|
    over i in [1, h-3] — exactly one 3-tap smoothing, implicit in the expression.
    Returns |diff(smooth3(v))| restricted to the page's scan range (indices 1..h-3)."""
    return np.abs(np.diff(smooth3(v)))[1:len(v)-2]

def mad(x):
    x = np.asarray(x, float)
    if x.size == 0: return 0.0
    return float(np.median(np.abs(x - np.median(x)))) * 1.4826

def arms(img):
    a = np.asarray(img.convert('RGB'), dtype=np.float64)
    R, G, B = a[..., 0], a[..., 1], a[..., 2]
    return {'gray': (R + G + B) / 3.0, 'rg': R - G, 'bg': B - G}

def metrics(path, runtag, stem):
    im = Image.open(path); im.load()
    res = {}
    for arm, prof in arms(im).items():
        v = prof.mean(axis=1)                    # raw row means (single smooth inside jump_series)
        j = jump_series(v)
        lo = max(0, SKIP_TOP - 1)                # index into j: row >= SKIP_TOP
        jj = j[lo:]
        if jj.size < 20:
            for m in ('Dmax','rMAD','rP','rLOC','SLVL'): res[f'{arm}_{m}'] = 0.0
            continue
        dmax = float(jj.max())
        iy = int(np.argmax(jj)) + lo             # index i in j coords; rows i..i+3
        yc = iy + 1                              # candidate row center
        gMAD = mad(jj)
        p995 = float(np.percentile(jj, 99.5))
        a, b = max(0, iy - WIN), min(len(j), iy + WIN + 1)
        locJ = np.concatenate([j[a:max(0, iy - GUARD + 1)], j[iy + GUARD + 1:b]]) \
               if (iy - GUARD + 1 > a and iy + GUARD + 1 <= len(j)) else \
               np.array([gMAD / 1.4826])
        lMAD = mad(locJ)
        t0, t1 = max(0, yc - LVL_OUT), max(0, yc - LVL_IN)
        b0, b1 = min(len(v), yc + LVL_IN), min(len(v), yc + LVL_OUT)
        if t1 > t0 and b1 > b0:
            shift = float(np.median(v[b0:b1]) - np.median(v[t0:t1]))
        else:
            shift = 0.0
        vmad = mad(np.abs(np.diff(v[SKIP_TOP:])))
        res[f'{arm}_Dmax'] = dmax
        res[f'{arm}_rMAD'] = dmax / (gMAD + FLOOR)
        res[f'{arm}_rP']   = dmax / (p995 + FLOOR)
        res[f'{arm}_rLOC'] = dmax / (lMAD + FLOOR)
        res[f'{arm}_SLVL'] = abs(shift) / (vmad + FLOOR)
        res[f'{arm}_y'] = yc          # arm argmax row (for cross-arm coherence stats)
        if arm == 'gray':
            res['y'] = yc
    res['torn'] = (runtag, stem) in TORN
    res['label'] = stem
    res['regime'] = CORPORA[[c[0] for c in CORPORA].index(None)] if False else None
    return res

def main():
    rows = []
    for dp, regime in CORPORA:
        full = os.path.join(BASE, dp)
        runtag = os.path.basename(dp).replace('s14r0003d-', '')
        for f in sorted(x for x in os.listdir(full) if x.endswith('.jpg')):
            stem = f[:-4]
            m = metrics(os.path.join(full, f), runtag, stem)
            m['run'] = runtag; m['regime'] = regime
            rows.append(m)
    outp = os.path.join(BASE, 'runs/daemon/analysis/tearguard/robust_scores.json')
    with open(outp, 'w') as fh:
        json.dump(rows, fh, indent=1)
    print(f'n={len(rows)} torn={sum(r["torn"] for r in rows)} -> {outp}')

    ARMS = ['gray', 'rg', 'bg']
    STAT = ['Dmax', 'rMAD', 'rP', 'rLOC', 'SLVL']
    def sel(reg=None, torn=None):
        return [r for r in rows
                if (reg is None or r['regime'] == reg)
                and (torn is None or r['torn'] == torn)]
    print('\n=== per-arm per-regime: max clean | (clean p95) vs min tear / argmax y ===')
    for arm in ARMS:
        for stat in STAT:
            k = f'{arm}_{stat}'
            line = f'{k:<12}'
            for reg in ('exp700', 'exp300'):
                rc = [r[k] for r in sel(reg, False)]
                rt = [r[k] for r in sel(reg, True)]
                if rt:
                    line += f' | Cmax {max(rc):8.3f} Tv  Tmn {min(rt):9.3f}'
                elif rc:
                    line += f' | Cmax {max(rc):8.3f} Tv      -'
                else:
                    line += ' |            -'
            # combined margin
            allc = [r[k] for r in sel(None, False)]
            allt = [r[k] for r in sel(None, True)]
            cm, tm = max(allc), min(allt)
            line += f' || ALL Cmax {cm:8.3f} Tmn {tm:9.3f} margin {tm/cm:6.2f}'
            print(line)
    print('\n=== combined rule score = max over arms (per stat) ===')
    for stat in STAT:
        for reg in ('exp700', 'exp300', None):
            rc = [max(r[f'{a}_{stat}'] for a in ARMS) for r in sel(reg, False)]
            rt = [max(r[f'{a}_{stat}'] for a in ARMS) for r in sel(reg, True)]
            tag = reg or 'ALL'
            print(f'combined {stat:<5} [{tag:<6}] clean max {max(rc):8.3f}  '
                  f'tear min {min(rt):9.3f}  margin {min(rt)/max(rc):6.2f}'
                  + (f'  clean p99 {sorted(rc)[int(len(rc)*0.99)]:8.3f}' if len(rc) > 50 else ''))
    print('\ntorn frame detail (rule-relevant stats, all regimes):')
    for r in rows:
        if r['torn']:
            print(f'  {r["label"]:<16} y={r["y"]:<4} '
                  + ' '.join(f'{a}:{r[a+"_Dmax"]:.1f}/{r[a+"_rMAD"]:.1f}'
                             for a in ARMS)
                  + f'  SLVL g/rg/bg {r["gray_SLVL"]:.1f}/{r["rg_SLVL"]:.1f}/{r["bg_SLVL"]:.1f}'
                  + f'  rLOC g/rg/bg {r["gray_rLOC"]:.1f}/{r["rg_rLOC"]:.1f}/{r["bg_rLOC"]:.1f}')

if __name__ == '__main__':
    main()