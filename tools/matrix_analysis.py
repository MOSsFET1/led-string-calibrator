#!/usr/bin/env python3
"""Analyse the S8 brightness matrix: for every combo, decode its frames,
run the temporal-diff detector (accent vs all-on of the same combo), and
print per-combo die metrics (peak, size, margin) + cross-combo comparison.

Usage: venv/bin/python3 matrix_analysis.py <matrix-run-dir>
"""
import base64
import json
import re
import sys
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image

DOMK = 8
DIFFTHR = 40


def decode_frames(txt):
    frames = []
    cur_meta, cur_jpg = None, []
    for line in txt.split('\n'):
        if 'FRAME {' in line:
            m = re.search(r'FRAME (\{.*\})', line)
            cur_meta = json.loads(m.group(1)) if m else {}
            cur_jpg = []
        elif 'FJPEG ' in line:
            cur_jpg.append(line.split('FJPEG ', 1)[1])
        elif 'FEND' in line and cur_meta is not None:
            jpg = re.sub(r'\s+', '', ''.join(cur_jpg))
            try:
                data = base64.b64decode(jpg + '==')
                import io
                img = Image.open(io.BytesIO(data)).convert('RGB')
                frames.append((cur_meta, np.asarray(img, dtype=np.int16)))
            except Exception:
                pass
            cur_meta = None
    return frames


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
                              'peak': float(vv.max())})
    return blobs


def diff_detect(acc, al, hue, thr=40, domk=DOMK):
    d = hue_excess(acc, hue).astype(np.int16) - hue_excess(al, hue).astype(np.int16)
    blobs = components(d.astype(np.float32), thr)
    blobs.sort(key=lambda x: -x['peak'])
    if not blobs:
        return None
    mask = np.zeros(d.shape, dtype=bool)
    return d, blobs


def top_margin(d, blob, thr=40, domk=DOMK):
    mask = np.zeros(d.shape, dtype=bool)
    y0 = int(blob['cy']) - 8
    y1 = int(blob['cy']) + 8
    x0 = int(blob['cx']) - 8
    x1 = int(blob['cx']) + 8
    mask[max(0, y0):y1, max(0, x0):x1] = True
    d10 = tail_d10(d[~mask])
    return blob['peak'] / (domk * max(1.0, d10)), d10


def main():
    run = Path(sys.argv[1])
    summary = json.loads((run / 'summary.json').read_text())
    print(f"{'combo':<16} {'hue':>3} {'found':>5} {'pk':>5} {'n':>5} {'margin':>7} {'d10out':>7}")
    for item in summary:
        cfg = item['cfg']
        key = list(cfg.keys())[0]
        val = list(cfg.values())[0]
        combo_files = sorted(run.glob(f'combo*{key}{val}.txt'))
        if not combo_files:
            continue
        frames = decode_frames(combo_files[0].read_text())
        labels = {}
        for meta, arr in frames:
            lab = meta.get('label', '')
            if 'all' in lab:
                labels['all'] = (meta, arr)
                continue
            m = re.search(r'survey:(\w)', lab)
            if m:
                hue = m.group(1)
                # keep the LAST accent frame per hue (most settled)
                labels[hue] = (meta, arr)
        al = labels.get('all', (None, None))[1]
        if al is None:
            print(f"{str(cfg):<16} no all-on frame — skipped")
            continue
        for hue in 'RGB':
            if hue not in labels:
                continue
            acc = labels[hue][1]
            d = (hue_excess(acc, hue).astype(np.int16)
                 - hue_excess(al, hue).astype(np.int16))
            blobs = components(d.astype(np.float32), DIFFTHR)
            blobs.sort(key=lambda x: -x['peak'])
            if not blobs:
                print(f"{key + '=' + str(val):<16} {hue:>3} NO")
                continue
            b = blobs[0]
            mask = np.zeros(d.shape, dtype=bool)
            mask[int(b['cy']) - 8:int(b['cy']) + 8, int(b['cx']) - 8:int(b['cx']) + 8] = True
            d10 = tail_d10(d[~mask])
            margin = b['peak'] / (DOMK * max(1.0, d10))
            print(f"{key + '=' + str(val):<16} {hue:>3} {'Y':>5} {b['peak']:5.0f} {b['n']:5d} {margin:7.2f} {d10:7.1f}")


if __name__ == '__main__':
    main()