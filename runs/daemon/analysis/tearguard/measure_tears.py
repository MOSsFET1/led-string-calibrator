#!/usr/bin/env python3
"""S14R-0003e tear-guard measurement: row-luma discontinuity on all 100 E4 frames.

Metric: per-frame, per-channel row-means -> 3-tap smooth [1,2,1]/4 -> adjacent abs diff ->
D = max over y and over channels (luma + chroma variants measured separately).
Corpus: runs/daemon/runs/run8..run11 (25 frames each, wire-verified repaired).
Read-only corpus: this script only reads jpgs, writes JSON to analysis/tearguard/.
"""
import json, os
import numpy as np
from PIL import Image

BASE = '/home/nellie/projects/led-display/poc_survey'
RUNS = [('run8', 'r8'), ('run9', 'r9'), ('run10', 'r10'), ('run11', 'r11')]
TORN = {('run8', 'cwc_r8_p07'), ('run10', 'cwc_r10_p05'), ('run10', 'cwc_r10_p21')}

def smooth3(v):
    # 3-tap [1,2,1]/4 boundary-safe
    out = np.empty_like(v)
    out[1:-1] = (v[:-2] + 2*v[1:-1] + v[2:]) / 4.0
    out[0] = v[0]; out[-1] = v[-1]
    return out

def maxadj(v):
    return float(np.max(np.abs(np.diff(v))))

def metrics(path):
    im = Image.open(path)  # plain PIL, no LOAD_TRUNCATED_IMAGES (corpus is wire-verified)
    a = np.asarray(im, dtype=np.float64)
    H, W = a.shape[0], a.shape[1]
    R, G, B = a[..., 0], a[..., 1], a[..., 2]
    gray = (R + G + B) / 3.0
    res = {'W': W, 'H': H}
    # luma variants
    res['D_gray3'] = maxadj(smooth3(gray.mean(axis=1)))                      # mean-of-channels
    res['D_rec709'] = maxadj(smooth3((0.2126*R + 0.7152*G + 0.0722*B).mean(axis=1)))
    # per-channel
    res['D_R'] = maxadj(smooth3(R.mean(axis=1)))
    res['D_G'] = maxadj(smooth3(G.mean(axis=1)))
    res['D_B'] = maxadj(smooth3(B.mean(axis=1)))
    res['D_rgbmax'] = max(res['D_R'], res['D_G'], res['D_B'])
    # chroma variants (red-band tear case)
    res['D_RmG'] = maxadj(smooth3((R - G).mean(axis=1)))
    res['D_RmB'] = maxadj(smooth3((R - B).mean(axis=1)))
    # unsmoothed gray for reference (how much smoothing buys)
    res['Draw_gray'] = maxadj(gray.mean(axis=1))
    # tear-row location for torn frames (argmax of smoothed luma diff)
    prof = smooth3(gray.mean(axis=1))
    res['argmax_y'] = int(np.argmax(np.abs(np.diff(prof))))
    # chroma profile argmax
    profC = smooth3((R - G).mean(axis=1))
    res['argmax_y_chroma'] = int(np.argmax(np.abs(np.diff(profC))))
    # above/below band stats around argmax (mechanism analysis)
    y = res['argmax_y']
    if 5 < y < H-5:
        top = gray[max(0, y-40):y-5].mean(); bot = gray[y+5:y+45].mean()
        topR = (R-G)[max(0, y-40):y-5].mean(); botR = (R-G)[y+5:y+45].mean()
        topB = (B-G)[max(0, y-40):y-5].mean(); botB = (B-G)[y+5:y+45].mean()
        res['band_gray_top'], res['band_gray_bot'] = float(top), float(bot)
        res['band_ratio'] = float(bot/top) if top > 1 else None
        res['band_RmG_top'], res['band_RmG_bot'] = float(topR), float(botR)
        res['band_BmG_top'], res['band_BmG_bot'] = float(topB), float(botB)
    return res

out = []
for rd, tag in RUNS:
    dp = os.path.join(BASE, 'runs/daemon/runs', rd)
    files = sorted(f for f in os.listdir(dp) if f.endswith('.jpg'))
    for f in files:
        stem = f[:-4]
        m = metrics(os.path.join(dp, f))
        m['run'] = rd; m['label'] = stem
        m['page'] = json.load(open(os.path.join(dp, stem + '.meta.json')))
        m['torn'] = (rd, stem) in TORN
        out.append(m)

os.makedirs(os.path.join(BASE, 'runs/daemon/analysis/tearguard'), exist_ok=True)
with open(os.path.join(BASE, 'runs/daemon/analysis/tearguard/measure_raw.json'), 'w') as fh:
    json.dump(out, fh, indent=1)

# ---- summary ----
clean = [m for m in out if not m['torn']]
tors = [m for m in out if m['torn']]
print(f'n={len(out)} clean={len(clean)} torn={len(tors)}')
for key in ['D_gray3', 'D_rec709', 'D_rgbmax', 'D_RmG', 'D_RmB']:
    cs = sorted(m[key] for m in clean)
    ts = sorted((m[key], m['label']) for m in tors)
    print(f'{key}: clean min={cs[0]:.2f} med={cs[len(cs)//2]:.2f} p95={cs[int(len(cs)*0.95)]:.2f} max={cs[-1]:.2f} (2nd {cs[-2]:.2f}) | torn: {ts}')
# top-10 clean frames by D_gray3 — who sits near the threshold
print('--- top 10 clean by D_gray3:')
for m in sorted(clean, key=lambda m: -m['D_gray3'])[:10]:
    print(f"   {m['label']}: D_gray3={m['D_gray3']:.2f} D_rgbmax={m['rgbmax_top'] if 'rgbmax_top' in m else m['D_rgbmax']:.2f} D_RmG={m['D_RmG']:.2f}")
print('--- top 10 clean by D_rgbmax:')
for m in sorted(clean, key=lambda m: -m['D_rgbmax'])[:10]:
    print(f"   {m['label']}: D_rgbmax={m['D_rgbmax']:.2f} (R={m['D_R']:.2f} G={m['D_G']:.2f} B={m['D_B']:.2f}) D_gray3={m['D_gray3']:.2f} D_RmG={m['D_RmG']:.2f}")
print('--- top 10 clean by D_RmG:')
for m in sorted(clean, key=lambda m: -m['D_RmG'])[:10]:
    print(f"   {m['label']}: D_RmG={m['D_RmG']:.2f} D_gray3={m['D_gray3']:.2f}")
# torn frame detail
print('--- torn detail:')
for m in tors:
    print(f"   {m['label']}: D_gray3={m['D_gray3']:.2f} argmax_y={m['argmax_y']} "
          f"D_RmG={m['D_RmG']:.2f} argmaxC={m['argmax_y_chroma']} D_rgbmax={m['D_rgbmax']:.2f}")
    for k in ['band_gray_top','band_gray_bot','band_ratio','band_RmG_top','band_RmG_bot','band_BmG_top','band_BmG_bot']:
        if k in m: print(f'      {k}={m[k]:.3f}' if isinstance(m[k], float) else f'      {k}={m[k]}')