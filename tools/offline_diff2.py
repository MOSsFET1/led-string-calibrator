#!/usr/bin/env python3
"""The decisive offline experiment: accent frame MINUS all-on frame from the
SAME survey (2-4 s apart, tripod, AE pinned). Static scene cancels; only the
accent die should survive. Tests both hue-excess diff and raw-channel diff.

Usage: venv/bin/python3 offline_diff2.py
"""
import numpy as np
from PIL import Image
from pathlib import Path
from collections import deque

RUN = Path(__file__).resolve().parents[1] / "runs" / "pull-20260922-064337"
DOMK = 8


def load(name):
    return np.asarray(Image.open(RUN / name).convert('RGB'), dtype=np.int16)


def hue_excess(a, hue):
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    if hue == 'R':
        return r - np.maximum(g, b)
    if hue == 'G':
        return g - np.maximum(r, b)
    return b - np.maximum(r, g)


def tail_d10(vals):
    out = np.asarray(vals)
    out = out[out > 0]
    if out.size == 0:
        return 0.0
    return float(np.mean(np.sort(out)[-max(1, out.size // 10):]))


def components(vals, thr):
    mask = vals >= thr
    H, W = mask.shape
    seen = np.zeros(mask.shape, dtype=bool)
    blobs = []
    for sy in range(H):
        for sx in range(W):
            if mask[sy, sx] and not seen[sy, sx]:
                q = deque([(sy, sx)])
                seen[sy, sx] = True
                pix = []
                while q:
                    y, x = q.popleft()
                    pix.append((y, x))
                    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < H and 0 <= xx < W and mask[yy, xx] and not seen[yy, xx]:
                            seen[yy, xx] = True
                            q.append((yy, xx))
                ys = [p[0] for p in pix]
                xs = [p[1] for p in pix]
                vv = vals[[p[0] for p in pix], [p[1] for p in pix]]
                blobs.append({'n': len(pix), 'cy': float(np.mean(ys)), 'cx': float(np.mean(xs)),
                              'peak': float(vv.max()),
                              'y0': min(ys), 'y1': max(ys), 'x0': min(xs), 'x1': max(xs)})
    return blobs


def report(diff, thr, tag):
    blobs = components(diff.astype(np.float32), thr)
    blobs.sort(key=lambda x: -x['peak'])
    if not blobs:
        print("  [%s thr%d] nothing" % (tag, thr))
        return
    mask = np.zeros(diff.shape, dtype=bool)
    for b in blobs:
        mask[b['y0']:b['y1'] + 1, b['x0']:b['x1'] + 1] = True
    d10 = tail_d10(diff[~mask])
    top = blobs[0]
    margin = top['peak'] / (DOMK * max(1.0, d10))
    print("  [%s thr%d] cand %d d10out %.1f | top n%d pk%.0f at (%.0f,%.0f) %dx%d margin %.1f"
          % (tag, thr, len(blobs), d10, top['n'], top['peak'], top['cx'], top['cy'],
             top['x1'] - top['x0'] + 1, top['y1'] - top['y0'] + 1, margin))
    Image.fromarray(np.clip(diff, 0, 255).astype(np.uint8)).save(
        RUN / ("diff_" + tag + "_thr" + str(thr) + ".png"))


def main():
    pairs = [
        ('R', 'frame_372.2_survey-R.jpg', 'frame_370.6_survey-all.jpg'),
        ('G', 'frame_373.6_survey-G.jpg', 'frame_370.6_survey-all.jpg'),
        ('B', 'frame_375.1_survey-B.jpg', 'frame_370.6_survey-all.jpg'),
    ]
    for hue, acc_fn, all_fn in pairs:
        acc = load(acc_fn)
        al = load(all_fn)
        print("=== " + hue + " accent (" + acc_fn + ") vs all-on ===")
        # raw channel diff
        ch = {'R': 0, 'G': 1, 'B': 2}[hue]
        raw_diff = acc[..., ch] - al[..., ch]
        print(" raw ch%d diff: med=%d pk=%d" % (ch, np.median(raw_diff), raw_diff.max()))
        for thr in (40, 60, 80):
            report(raw_diff, thr, hue + "raw")
        # hue-excess diff
        ex_diff = hue_excess(acc, hue) - hue_excess(al, hue)
        print(" hue-excess diff: med=%d pk=%d" % (np.median(ex_diff), ex_diff.max()))
        for thr in (40, 60, 80):
            report(ex_diff, thr, hue + "exc")


if __name__ == '__main__':
    main()