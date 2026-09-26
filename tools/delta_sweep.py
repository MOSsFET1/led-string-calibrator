#!/usr/bin/env python3
"""Delta sweep — how much camera movement can our diff scheme absorb? (S14 prep)

Oliver's question (24 Sep): before the S14 CWC scheme, use the EXISTING pulled
frames to test how detection handles camera movement, by artificially
introducing offsets between the all-on frame and the scan (pair) frames before
the diff — i.e. simulate mis-registration instead of causing it for real.

Method (no phone, no box needed — pure offline on pulled captures):
  1. Decode a run's <tag>_frames.txt (16 pair frames, ring keeps the last 16).
  2. Master = pixelwise max over all 16 pair frames (every LED is lit in 15 of
     16; the max is the all-on field). Sanity-check vs a real master when a
     frames file with one exists.
  3. For each pair frame, roll the PAIR by (dx, dy) [edge-clamp roll], diff
     against the un-shifted master, and run the SAME detector semantics the
     page runs: abs diff -> thr 30 -> flood fill -> bloom merge (r=2×radius)
     -> dominance bar (8× d10 outside-tail) -> area cap 1%.
  4. Per pair: base centroid = offset-0 detection (the page's own accept).
     A LED is CORRECT at (dx,dy) if the best blob's centroid is within
     TOL px of base + (dx,dy)  [TOL=6 > worst real cross-run shift 4 px];
     MISS if no accepted blob near; AMBIG if captured but lands elsewhere.
  5. Sweep a grid of offsets; report accuracy by Chebyshev magnitude + JSON.

Speed: pure-python flood + O(n²) merge is ~5 s/frame on noisy diffs (mask
fragments into ~500 blobs). cv2.connectedComponents + vectorised merge give
identical blobs in ~0.2 s — equivalence asserted vs the page detector at
offset 0 (asserted equivalence: 508 blobs -> same 6 accepts).

Usage: python3 delta_sweep.py <run_dir> <tag> [max_off=10] [step=2]
Writes <tag>_delta_sweep.json + readable table.
"""
import json, re, sys, time
from pathlib import Path
import numpy as np
import cv2

sys.path.insert(0, str(Path(__file__).resolve().parent))
from offline_hole_verify import decode_run, luma

TOL = 6.0        # centroid match tolerance (worst real cross-run shift was 4)
HOLE_THR = 30.0  # page standing
DOMK = 8.0
MERGE_R = 2.0
AREA_FRAC = 0.01


def detect_holes_fast(master, pair, thr=HOLE_THR, domk=DOMK, merge_r=MERGE_R,
                      area_frac=AREA_FRAC):
    """cv2-connectedComponents + array-based bloom merge; same semantics as
    offline_hole_verify.detect_holes (which mirrors the page). Returns
    (acc, blobs) with acc sorted by -peak."""
    diff = (np.asarray(master, np.int16) - np.asarray(pair, np.int16))
    mask = (diff >= thr).astype(np.uint8)
    H, W = mask.shape
    n, lab, stats, cents = cv2.connectedComponentsWithStats(mask, connectivity=4)
    blobs = []
    for k in range(1, n):
        x, y, w, h, area = stats[k]
        if area < 2:
            continue
        ys, xs = np.nonzero(lab == k)
        pk = float(diff[ys, xs].max())
        blobs.append({'n': int(area), 'cy': float(cents[k][1]), 'cx': float(cents[k][0]),
                      'peak': pk})
    blobs.sort(key=lambda b: -b['peak'])
    # bloom merge — same rule, array-based (identical results to the nested loop)
    merged = True
    while merged and len(blobs) > 1:
        merged = False
        n_b = len(blobs)
        cx = np.array([b['cx'] for b in blobs]); cy = np.array([b['cy'] for b in blobs])
        ra = np.array([(b['n'] / np.pi) ** 0.5 for b in blobs])
        for i in range(n_b):
            if merged:
                break
            d = np.hypot(cx - cx[i], cy - cy[i])
            for j in np.nonzero((d < merge_r * np.maximum(ra[i], ra)))[0]:
                if j <= i:
                    continue
                a, b = blobs[i], blobs[j]
                na, nb = a['n'], b['n']
                a['cx'] = (a['cx'] * na + b['cx'] * nb) / (na + nb)
                a['cy'] = (a['cy'] * na + b['cy'] * nb) / (na + nb)
                a['n'] = na + nb
                a['peak'] = max(a['peak'], b['peak'])
                blobs.pop(j)
                merged = True
                break
    d10 = _tail_d10(diff, mask)
    acc = [b for b in blobs if b['peak'] >= domk * max(1.0, d10)]
    cap = area_frac * W * H
    acc = [b for b in acc if b['n'] <= cap]
    return acc, blobs


def _tail_d10(diff, outside_mask):
    out = diff[(~outside_mask.astype(bool)) & (diff > 0)]
    if out.size == 0:
        return 0.0
    return float(np.mean(np.sort(out)[-max(1, out.size // 10):]))


def np_roll(a, dx, dy):
    """Roll with edge clamp (scene doesn't wrap; clamped border = out of view).
    Reads ONLY from `a`; writes to a fresh array (no dy/dx branch may read
    from `out` — an earlier version did, producing garbage at dy==0/dx==0)."""
    out = np.empty_like(a)
    if dy > 0:
        out[:dy, :] = a[0, :]
        out[dy:, :] = a[:-dy, :]
    elif dy < 0:
        out[dy:, :] = a[-1, :]
        out[:dy, :] = a[-dy:, :]
    else:
        out[:, :] = a
    if dx > 0:
        src = out.copy()          # horizontal shift must read the post-dy image
        out[:, :dx] = src[:, :1]
        out[:, dx:] = src[:, :-dx]
    elif dx < 0:
        src = out.copy()
        out[:, dx:] = src[:, -1:]
        out[:, :dx] = src[:, -dx:]
    return out


def main():
    run_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('.')
    prefix = sys.argv[2] if len(sys.argv) > 2 else 'baseline'
    max_off = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    step = int(sys.argv[4]) if len(sys.argv) > 4 else 2
    frames = decode_run(run_dir, prefix)
    print(f'{len(frames)} frames decoded from {run_dir.name}/{prefix}_frames.txt')
    pairs = []
    for f in frames:
        m = re.match(r'hole:p(\d+)', f['label'])
        if m and f['img'] is not None:
            pairs.append((int(m.group(1)), f['img']))
    if not pairs:
        print('no hole:p* frames found')
        return 1
    stack = np.stack([luma(im).astype(np.int16) for _, im in pairs])
    master_l = stack.max(axis=0)
    print(f'master = pixelwise max over {len(pairs)} pair frames')
    # base centroids at offset 0
    base = []
    for i, img in pairs:
        acc, blobs = detect_holes_fast(master_l, luma(img))
        if acc:
            b = acc[0]
            base.append((i, b['cx'], b['cy']))
    print(f'{len(base)}/{len(pairs)} pair frames have a base (offset-0) detection')
    have = {b[0] for b in base}
    pairs_s = [(i, im) for i, im in pairs if i in have]
    base_c = [(b[1], b[2]) for b in base]
    offsets = list(range(-max_off, max_off + 1, step))
    total = len(offsets) ** 2 * len(pairs_s)
    print(f'sweeping dx,dy in [{-max_off},{max_off}] step {step} on {len(pairs_s)} pairs ({total} detects) ...')
    rows = []
    t0 = time.time()
    for dy in offsets:
        for dx in offsets:
            nc = nm = na = 0
            shift_errs = []
            for idx, (i, img) in enumerate(pairs_s):
                pair_l = np_roll(luma(img), dx, dy)
                acc, blobs = detect_holes_fast(master_l, pair_l)
                if acc:
                    exp_x = base_c[idx][0] + dx
                    exp_y = base_c[idx][1] + dy
                    best, bd = None, 1e9
                    for b in acc:
                        d = ((b['cx'] - exp_x) ** 2 + (b['cy'] - exp_y) ** 2) ** 0.5
                        if d < bd:
                            best, bd = b, d
                    if bd <= TOL:
                        nc += 1
                        shift_errs.append((best['cx'] - exp_x, best['cy'] - exp_y))
                    else:
                        na += 1
                else:
                    nm += 1
            if shift_errs:
                exs = [s[0] for s in shift_errs]
                eys = [s[1] for s in shift_errs]
                rows.append((dx, dy, nc, nm, na,
                             float(np.median(exs)), float(np.median(eys))))
            else:
                rows.append((dx, dy, nc, nm, na, 0.0, 0.0))
    dt = time.time() - t0
    print(f'sweep done in {dt:.0f}s ({dt/max(1,total)*1000:.0f} ms/detect)')
    out = {'run': run_dir.name, 'prefix': prefix, 'max_off': max_off, 'step': step,
           'n_pairs': len(pairs_s), 'tol': TOL, 'rows': rows}
    op = run_dir / f'{prefix}_delta_sweep.json'
    op.write_text(json.dumps(out))
    print(f'wrote {op}')
    print('\naccuracy by offset magnitude (Chebyshev |dx|,|dy|):')
    bymag = {}
    for dx, dy, nc, nm, na, *_ in rows:
        m = max(abs(dx), abs(dy))
        t = bymag.setdefault(m, [0, 0, 0])
        t[0] += nc; t[1] += nm; t[2] += na
    for m in sorted(bymag):
        nc, nm, na = bymag[m]
        tot = nc + nm + na
        print(f'  |off|={m:>2}: correct {nc:>5}/{tot} ({100*nc/tot:5.1f}%)  miss {nm:>5}  ambig {na:>4}')
    # cliff: largest magnitude with >=95% correct
    cliff = 0
    for m in sorted(bymag):
        nc, nm, na = bymag[m]
        if nc / (nc + nm + na) >= 0.95:
            cliff = m
    print(f'\n95% cliff: |off| <= {cliff} px')
    return 0


if __name__ == '__main__':
    sys.exit(main())