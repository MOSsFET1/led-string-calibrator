#!/usr/bin/env python3
"""S14R-0003f: pairwise (comparison-based) tear detection analysis.
Read-only over run dirs. Outputs pairwise_measurements.json.
Statistics per consecutive-frame pair (run order: p00..p23):
  - per-row mean-abs-diff profile on smoothed row means, per row value gray=(R+G+B)/3,
    computed from 3x3 box-smoothed? NO: match tear guard convention, 3-tap [1,2,1]/4
    smoothing ALONG ROWS is not defined for a pair -- a pair diff profile is
    |rowmean_N - rowmean_N+1| per row, then the tear-guard 3-tap smooth ACROSS y.
  - arm diff: same on R-G and B-G row values.
  - band localization: argmax row, band = +/-3 rows around it, top/bottom band means,
    gain ratio, chroma shift across band.
  - normalized: arm max / median of profile (self-normalizing dimensionless).
  - global shift: diff of full-frame mean gray, and max row-mean diff.
"""
import json, os, glob
import numpy as np

ROOT = "/home/nellie/projects/led-display/poc_survey/runs/daemon"
OUT = "/home/nellie/projects/led-display/poc_survey/runs/daemon/analysis/tearguard"

def load_rgba(path):
    import cv2
    im = cv2.imread(path, cv2.IMREAD_UNCHANGED)  # BGR or BGRA
    if im is None:
        raise RuntimeError("decode fail " + path)
    if im.ndim == 2:
        im = np.stack([im]*3, axis=-1)
    if im.shape[2] == 4:
        b, g, r, a = im[...,0].astype(np.float64), im[...,1].astype(np.float64), im[...,2].astype(np.float64), im[...,3].astype(np.float64)
    else:
        b, g, r = im[...,0].astype(np.float64), im[...,1].astype(np.float64), im[...,2].astype(np.float64)
        a = np.full(im.shape[:2], 255.0)
    return r, g, b, a

def rowvals(r, g, b):
    gray = (r + g + b) / 3.0
    return gray.mean(axis=1), (r - g).mean(axis=1), (b - g).mean(axis=1)

def smooth3(v):
    k = np.array([1.0, 2.0, 1.0]) / 4.0
    return np.convolve(v, k, mode="same")

def tear_arm(profile):
    """max adjacent 3-tap diff, tear-guard formula"""
    s = smooth3(profile)
    d = np.abs(s[1:-2] + s[2:-1]*2 + s[3:] - (s[:-3] + s[1:-2]*2 + s[2:-1]))
    # note: matches 0.25*|(...)-( ... )| with the 1/4 folded in? guard formula:
    # 0.25*abs((v[i-1]+2v[i]+v[i+1]) - (v[i]+2v[i+1]+v[i+2]))
    if len(d):
        return 0.25 * np.max(d), None
    return 0.0, None

def pair_stats(pa, pb):
    ra, ga, ba, _ = pa
    rb, gb, bb, _ = pb
    prof = {}
    for name, va, vb in (("gray", rowvals(ra,ga,ba)[0], rowvals(rb,gb,bb)[0]),
                         ("rg", rowvals(ra,ga,ba)[1], rowvals(rb,gb,bb)[1]),
                         ("bg", rowvals(ra,ga,ba)[2], rowvals(rb,gb,bb)[2])):
        d = np.abs(va - vb)
        s = smooth3(d)
        adj = np.abs(s[:-1] - s[1:])
        y = int(np.argmax(adj))
        val = float(adj[y])
        med = float(np.median(adj))
        prof[name] = dict(argmax_y=y, max_diff=val, median_diff=med,
                          norm=(val/med if med > 1e-9 else None),
                          band_mean_above=float(d[max(0,y-3):y+4].mean()))
    # global AEm shift
    gshift = abs(float(rowvals(ra,ga,ba)[0].mean()) - float(rowvals(rb,gb,bb)[0].mean()))
    # band gain/colour across tear argmax of gray arm
    yg = prof["gray"]["argmax_y"]
    if 5 < yg < len(prof) - 5:
        top = slice(max(0, yg-3), yg)
        bot = slice(yg+1, min(len(prof), yg+4))
        for arm, (ta, tb) in (("gray", (rowvals(ra,ga,ba)[0], rowvals(rb,gb,bb)[0])),
                              ("rg", (rowvals(ra,ga,ba)[1], rowvals(rb,gb,bb)[1])),
                              ("bg", (rowvals(ra,ga,ba)[2], rowvals(rb,gb,bb)[2]))):
            prof["band_" + arm] = dict(top_a=float(ta[top].mean()), bot_a=float(ta[bot].mean()),
                                       top_b=float(tb[top].mean()), bot_b=float(tb[bot].mean()))
    prof["global_gray_shift"] = gshift
    return prof

def load_run(run):
    d = os.path.join(ROOT, "runs", run)
    frames = []
    for p in range(24):
        f = os.path.join(d, f"cwc_{run.replace('run','r')}_p{p:02d}.jpg")
        if not os.path.exists(f):
            print("MISSING", f); return None
        frames.append((f"cwc:{run.replace('run','r')}:p{p:02d}", load_rgba(f)))
    return frames

def main():
    import cv2
    tears = {("run8","p07"), ("run10","p05"), ("run10","p21"), ("run12","p15")}
    results = []
    for run in ["run8","run9","run10","run11","run12"]:
        fr = load_run(run)
        if fr is None: continue
        for i in range(len(fr)-1):
            la, fa = fr[i]; lb, fb = fr[i+1]
            st = pair_stats(fa, fb)
            ra, pa = la.split(":")[1:3]   # 'r8', 'p00'
            rb, pb_ = lb.split(":")[1:3]
            istear_pair = any((run_key, pl) in tears for run_key, pl in
                              ((("run"+ra[1:]), pa), (("run"+rb[1:]), pb_)))
            st.update(run=run, a=la, b=lb, involves_tear=istear_pair)
            results.append(st)
    with open(os.path.join(OUT, "pairwise_measurements.json"), "w") as f:
        json.dump(results, f, indent=1)
    # summary
    clean = [r for r in results if not r["involves_tear"]]
    tear  = [r for r in results if r["involves_tear"]]
    for nm in ("gray","rg","bg"):
        c = sorted(r[nm]["max_diff"] for r in clean)
        t = sorted(r[nm]["max_diff"] for r in tear) or [float('nan')]
        print(f"arm {nm}: clean n={len(c)} p50={c[len(c)//2]:.2f} p95={c[int(len(c)*0.95)]:.2f} max={c[-1]:.2f}")
        print(f"        tear-pairs n={len([x for x in t if x==x])} vals={[round(x,1) for x in t if x==x]}")
    cn = sorted(r["gray"]["norm"] for r in clean)
    tn = sorted(r["gray"]["norm"] for r in tear)
    print(f"gray norm (max/median): clean p50={cn[len(cn)//2]:.2f} max={cn[-1]:.2f} | tear min={tn[0]:.2f}")
    print(f"global_gray_shift: clean max={max(r['global_gray_shift'] for r in clean):.2f} tear max={max(r['global_gray_shift'] for r in tear):.2f}")

if __name__ == "__main__":
    main()