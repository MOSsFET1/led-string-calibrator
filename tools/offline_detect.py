#!/usr/bin/env python3
"""Offline detector calibration on REAL pulled frames.

Replicates the page's hue-excess detection (thr=colFloor, flood fill,
dominance), then tests the LOCAL dominance hypothesis: judge each candidate
blob against the hue-excess tail in its own neighbourhood (ring around the
blob bbox) instead of the whole frame. Prints margins for whole vs local.

Usage: venv/bin/python3 offline_detect.py
"""
import re
from pathlib import Path
from collections import deque
import numpy as np
from PIL import Image

RUN = Path(__file__).resolve().parents[1] / "runs" / "pull-20260922-063524"
COLFLOOR = 40
DOMK = 8


def hue_excess(img, hue):
    a = np.asarray(img, dtype=np.int16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    if hue == 'R':
        return r - np.maximum(g, b)
    if hue == 'G':
        return g - np.maximum(r, b)
    return b - np.maximum(r, g)


def components(exc, thr):
    mask = exc >= thr
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
                vals = exc[[p[0] for p in pix], [p[1] for p in pix]]
                blobs.append({'n': len(pix), 'cy': float(np.mean(ys)), 'cx': float(np.mean(xs)),
                              'peak': float(vals.max()), 'y0': min(ys), 'y1': max(ys),
                              'x0': min(xs), 'x1': max(xs)})
    return blobs


def tail_d10(vals):
    out = np.asarray(vals)
    out = out[out > 0]
    if out.size == 0:
        return 0.0
    k = max(1, out.size // 10)
    return float(np.mean(np.sort(out)[-k:]))


def whole_d10(exc, blobs):
    mask = np.zeros(exc.shape, dtype=bool)
    for b in blobs:
        mask[b['y0']:b['y1'] + 1, b['x0']:b['x1'] + 1] = True
    return tail_d10(exc[~mask])


def local_d10(exc, b, pad=14):
    H, W = exc.shape
    y0 = max(0, b['y0'] - pad)
    y1 = min(H - 1, b['y1'] + pad)
    x0 = max(0, b['x0'] - pad)
    x1 = min(W - 1, b['x1'] + pad)
    ring = exc[y0:y1 + 1, x0:x1 + 1].copy()
    inner = np.zeros(ring.shape, dtype=bool)
    inner[(b['y0'] - y0):(b['y1'] - y0 + 1), (b['x0'] - x0):(b['x1'] - x0 + 1)] = True
    return tail_d10(ring[~inner])


def main():
    frames = sorted(RUN.glob("frame_*.jpg"))
    for fn in frames:
        m = re.search(r'survey-(\w+)', fn.name)
        hue = m.group(1)[0].upper() if m else None
        img = Image.open(fn).convert('RGB')
        W, H = img.size
        print("=== " + fn.name + " (" + str(W) + "x" + str(H) + ") ===")
        if hue == 'B' and 'black' in fn.name:
            g = np.asarray(img.convert('L'), dtype=np.float32)
            print("  luma: med=%d d90=%d pk=%d" % (np.median(g), np.percentile(g, 90), g.max()))
            continue
        if hue is None:
            continue
        exc = hue_excess(img, hue).astype(np.float32)
        blobs = components(exc, COLFLOOR)
        blobs.sort(key=lambda x: -x['peak'])
        wd = whole_d10(exc, blobs)
        print("  candidates: %d | whole-frame d10out: %.1f" % (len(blobs), wd))
        for b in blobs[:8]:
            ld = local_d10(exc, b)
            lm = b['peak'] / (DOMK * max(1.0, ld))
            wm = b['peak'] / (DOMK * max(1.0, wd))
            print("   blob n%4d pk%5.0f at (%.0f,%.0f) local_d10 %5.1f -> local_margin %6.1f | whole_margin %6.2f"
                  % (b['n'], b['peak'], b['cx'], b['cy'], ld, lm, wm))
        if blobs:
            b = blobs[0]
            ld = local_d10(exc, b)
            acc_w = b['peak'] >= DOMK * max(1.0, wd)
            acc_l = b['peak'] >= DOMK * max(1.0, ld)
            print("  VERDICT top blob: whole-frame accept=%s | local accept=%s" % (acc_w, acc_l))
        print()


if __name__ == '__main__':
    main()