#!/usr/bin/env python3
"""Temporal-difference detection experiment on real pulled frames.

Hypothesis: hue-excess(accent frame) - hue-excess(background frame) cancels
static scene clutter; the accent die is the only positive residual.

Usage: venv/bin/python3 offline_diff.py
"""
import numpy as np
from PIL import Image
from pathlib import Path
from collections import deque

RUN = Path(__file__).resolve().parents[1] / "runs" / "pull-20260922-063524"


def hue_excess(img, hue):
    a = np.asarray(img, dtype=np.int16)
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


def report(diff, thr):
    blobs = components(diff.astype(np.float32), thr)
    blobs.sort(key=lambda x: -x['peak'])
    if not blobs:
        print("  thr=%d: nothing" % thr)
        return
    mask = np.zeros(diff.shape, dtype=bool)
    for b in blobs:
        mask[b['y0']:b['y1'] + 1, b['x0']:b['x1'] + 1] = True
    d10 = tail_d10(diff[~mask])
    print("  thr=%d: %d candidates | d10out %.1f" % (thr, len(blobs), d10))
    for b in blobs[:5]:
        margin = b['peak'] / (8.0 * max(1.0, d10))
        print("    blob n%4d pk%5.0f at (%.0f,%.0f) size %dx%d margin %.1f"
              % (b['n'], b['peak'], b['cx'], b['cy'], b['x1'] - b['x0'] + 1, b['y1'] - b['y0'] + 1, margin))


def main():
    acc = Image.open(RUN / "frame_27.0_survey-B.jpg").convert('RGB')
    bg = Image.open(RUN / "frame_119.6_survey-black.jpg").convert('RGB')
    print("frames: accent %s background %s" % (str(acc.size), str(bg.size)))
    print("NOTE: 92 s apart, different paints, handheld -> HARSH diff test")
    print("(real survey diffs accent vs all-on ~2 s apart on a tripod)")
    ea = hue_excess(acc, 'B').astype(np.int16)
    eb = hue_excess(bg, 'B').astype(np.int16)
    diff = ea - eb
    print("diff: med=%d d90=%d pk=%d" % (np.median(diff), np.percentile(diff, 90), diff.max()))
    for thr in (30, 40, 60, 80):
        report_blobs = report_blobs if False else None
        report(diff, thr)
    d = np.clip(diff, 0, 255).astype(np.uint8)
    Image.fromarray(d).save(RUN / "diff_visual.png")
    print("saved: " + str(RUN / "diff_visual.png"))


def report(diff, thr):
    blobs = components(diff.astype(np.float32), thr)
    blobs.sort(key=lambda x: -x['peak'])
    if not blobs:
        print("  thr=%d: nothing" % thr)
        return
    mask = np.zeros(diff.shape, dtype=bool)
    for b in blobs:
        mask[b['y0']:b['y1'] + 1, b['x0']:b['x1'] + 1] = True
    d10 = tail_d10(diff[~mask])
    print("  thr=%d: %d candidates | d10out %.1f" % (thr, len(blobs), d10))
    for b in blobs[:5]:
        margin = b['peak'] / (8.0 * max(1.0, d10))
        print("    blob n%4d pk%5.0f at (%.0f,%.0f) size %dx%d margin %.1f"
              % (b['n'], b['peak'], b['cx'], b['cy'], b['x1'] - b['x0'] + 1, b['y1'] - b['y0'] + 1, margin))


if __name__ == '__main__':
    main()