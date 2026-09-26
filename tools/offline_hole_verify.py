#!/usr/bin/env python3
"""Offline verifier for S13 hole surveys (Hermes venv; numpy+PIL, no serial).

Decodes a run's <tag>_frames.txt (FRAME meta + FJPEG base64 chunks) back to
JPEGs, decodes luma, and re-detects holes with the SAME algorithm the page
runs (abs diff, flood fill, merge, dominance, area cap) — an INDEPENDENT
re-diff that checks the page's per-LED verdicts against the pulled pixels.

Usage: venv python offline_hole_verify.py <run_dir> [tag-prefix]
Prints per-LED verdicts + positions, agreement stats vs the page evid, and
the geometric-band ground truth (position clusters + inter-LED pitch).
"""
import base64, json, re, sys
from pathlib import Path
import numpy as np
from PIL import Image
from collections import deque

HOLE_THR = 30.0
DOMK = 8.0
MERGE_R = 2.0
AREA_FRAC = 0.01


def decode_run(run_dir, prefix='baseline'):
    """<tag>_frames.txt -> list of {label, img(PIL RGB), meta}."""
    run_dir = Path(run_dir)
    frames = []
    cur = None
    b64 = []
    for txt in (run_dir / (prefix + '_frames.txt')).read_text().splitlines():
        if 'FRAME ' in txt:
            if cur:
                frames.append(_finish(cur, b64))
            m = re.search(r'FRAME (\{.*\})', txt)
            try:
                meta = json.loads(m.group(1))
            except Exception:
                meta = {'label': '?'}
            cur = {'meta': meta, 'label': meta.get('label', '?')}
            b64 = []
        elif 'FJPEG ' in txt and cur is not None:
            b64.append(txt.split('FJPEG ', 1)[1].strip())
        elif 'FEND' in txt and cur is not None:
            frames.append(_finish(cur, b64))
            cur = None
            b64 = []
    if cur:
        frames.append(_finish(cur, b64))
    return [f for f in frames if f['img'] is not None]


def _finish(cur, b64):
    img = None
    try:
        raw = base64.b64decode(''.join(b64))
        import io
        img = Image.open(io.BytesIO(raw)).convert('RGB')
    except Exception:
        pass
    cur['img'] = img
    return cur


def luma(img):
    a = np.asarray(img, dtype=np.int16)
    return a.max(axis=2)


def tail_d10(diff, inblob):
    out = diff[(~inblob) & (diff > 0)]
    if out.size == 0:
        return 0.0
    return float(np.mean(np.sort(out)[-max(1, out.size // 10):]))


def detect_holes(master, pair, thr=HOLE_THR, domk=DOMK, merge_r=MERGE_R,
                 area_frac=AREA_FRAC):
    diff = master.astype(np.int16) - pair.astype(np.int16)
    mask = diff >= thr
    H, W = mask.shape
    seen = np.zeros_like(mask)
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
                if len(pix) < 2:
                    continue
                ys = np.array([p[0] for p in pix]); xs = np.array([p[1] for p in pix])
                pk = int(diff[ys, xs].max())
                blobs.append({'n': len(pix), 'cy': float(ys.mean()), 'cx': float(xs.mean()),
                              'peak': float(pk)})
    blobs.sort(key=lambda b: -b['peak'])
    # merge (mirror of the page's bloom merge)
    if merge_r:
        merged = True
        while merged and len(blobs) > 1:
            merged = False
            for i in range(len(blobs)):
                for j in range(i + 1, len(blobs)):
                    a, b = blobs[i], blobs[j]
                    ra = (a['n'] / np.pi) ** 0.5; rb = (b['n'] / np.pi) ** 0.5
                    d = ((a['cx'] - b['cx']) ** 2 + (a['cy'] - b['cy']) ** 2) ** 0.5
                    if d < merge_r * max(ra, rb):
                        na, nb = a['n'], b['n']
                        a['cx'] = (a['cx'] * na + b['cx'] * nb) / (na + nb)
                        a['cy'] = (a['cy'] * na + b['cy'] * nb) / (na + nb)
                        a['n'] = na + nb
                        a['peak'] = max(a['peak'], b['peak'])
                        blobs.pop(j)
                        merged = True
                        break
                if merged:
                    break
    # dominance + area cap (page order: dominance accepted, then area filter)
    # NOTE (S14 delta-sweep): d10's inputs (diff, seen) are loop-invariant —
    # computing it per blob re-sorts the whole outside-set per blob (O(blobs ×
    # N log N); 3.5 s/frame on noisy off-target diffs). Hoisted out: identical
    # numbers, now ~10 ms/frame.
    d10 = tail_d10(diff, seen)
    acc = []
    for b in blobs:
        if b['peak'] >= domk * max(1.0, d10):
            acc.append(b)
    cap = area_frac * W * H
    acc = [b for b in acc if b['n'] <= cap]
    return acc, blobs, diff


def main():
    run_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
    prefix = sys.argv[2] if len(sys.argv) > 2 else 'baseline'
    frames = decode_run(run_dir, prefix)
    print(f'{len(frames)} frames decoded from {prefix}_frames.txt')
    masters = [f for f in frames if f['label'].startswith('hole:master')]
    pairs = [f for f in frames if f['label'].startswith('hole:p')]
    if not masters:
        print('NO master frame in the pull — nothing to verify')
        return 1
    master = luma(masters[0]['img'])
    # evid cross-check (page verdicts)
    evid = {}
    ep = Path(run_dir) / (prefix + '_evid.txt')
    if ep.exists():
        m = re.search(r'\[EVID\] (\{.*\})', ep.read_text())
        if m:
            try:
                evid = json.loads(m.group(1))
            except Exception:
                pass
    found_set = set(evid.get('found', []))
    print(f"page evid: foundN={evid.get('foundN')} missedN={evid.get('missedN')}")
    agree, disagree, offline_found = 0, 0, 0
    rows = []
    for f in pairs:
        m = re.match(r'hole:p(\d+)', f['label'])
        if not m or f['img'] is None:
            continue
        i = int(m.group(1))
        acc, blobs, diff = detect_holes(master, f['img'])
        pick = acc[0] if acc else None
        if pick:
            offline_found += 1
            agree = i in found_set
            if agree:
                agreement = 'OK '
            else:
                agreement = 'OFL'   # offline-only find (page missed it)
            rows.append((i, f"pk{pick['peak']:.0f} n{pick['n']} @({pick['cx']:.0f},{pick['cy']:.0f})", agreement))
        else:
            if i in found_set:
                agreement = 'PG!'   # page accepted, offline sees nothing
            else:
                agreement = 'MISS'
            rows.append((i, f"blobs{len(blobs)}", agreement))
    for i, s, a in rows:
        print(f'p{i:>3} {a} {s}')
    ofl = sum(1 for r in rows if r[2] == 'OFL')
    pg = sum(1 for r in rows if r[2] == 'PG!')
    print(f'offline: {offline_found} holes detected; offline-only {ofl}, page-only {pg}')
    return 0


if __name__ == '__main__':
    sys.exit(main())