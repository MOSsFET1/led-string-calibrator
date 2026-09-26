#!/usr/bin/env python3
"""Registered delta sweep — same as delta_sweep.py, but each shifted pair is
first REGISTERED to the master (cv2 phase correlation) and rolled back before
the diff. Answers: does registration restore accuracy across the offset grid?
"""
import json, re, sys, time
from pathlib import Path
import numpy as np
import cv2

sys.path.insert(0, str(Path(__file__).resolve().parent))
from offline_hole_verify import decode_run, luma
from delta_sweep import np_roll, detect_holes_fast, TOL


def register(pair_l, master_l):
    """Return (corrected_pair, est_dx, est_dy): phase-correlate pair vs master,
    roll the pair back by the estimated shift."""
    a = np.float32(pair_l)
    b = np.sqrt(np.maximum(np.asarray(master_l, np.float32), 0.0) + 1.0)
    a = np.sqrt(np.maximum(a, 0.0) + 1.0)
    (dx, dy), conf = cv2.phaseCorrelate(b, a)
    # phaseCorrelate(a, b) returns the shift of b relative to a — here the pair
    # moved by (dx, dy) vs the master; undo it.
    rdx, rdy = int(round(-dx)), int(round(-dy))
    if rdx == 0 and rdy == 0:
        return pair_l, dx, dy, conf
    return np_roll(pair_l, rdx, rdy), dx, dy, conf


def main():
    run_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('.')
    prefix = sys.argv[2] if len(sys.argv) > 2 else 'baseline'
    max_off = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    step = int(sys.argv[4]) if len(sys.argv) > 4 else 2
    frames = decode_run(run_dir, prefix)
    pairs = []
    for f in frames:
        m = re.match(r'hole:p(\d+)', f['label'])
        if m and f['img'] is not None:
            pairs.append((int(m.group(1)), f['img']))
    stack = np.stack([luma(im).astype(np.int16) for _, im in pairs])
    master_l = stack.max(axis=0)
    base = []
    for i, img in pairs:
        acc, blobs = detect_holes_fast(master_l, luma(img))
        if acc:
            b = acc[0]
            base.append((i, b['cx'], b['cy']))
    have = {b[0] for b in base}
    pairs_s = [(i, im) for i, im in pairs if i in have]
    base_c = [(b[1], b[2]) for b in base]
    offsets = list(range(-max_off, max_off + 1, step))
    print(f'registered sweep on {len(pairs_s)} pairs, offsets [-{max_off},{max_off}] step {step}')
    rows = []
    reg_errs = []
    for dy in offsets:
        for dx in offsets:
            nc = nm = na = 0
            for idx, (i, img) in enumerate(pairs_s):
                shifted = np_roll(luma(img), dx, dy)
                corr, edx, edy, conf = register(shifted, master_l)
                # registration residual (int, px) vs the known (dx, dy)
                reg_errs.append((int(round(edx)) + dx, int(round(edy)) + dy))
                acc, blobs = detect_holes_fast(master_l, corr)
                # After registration the corrected frame aligns with the
                # master, so the hole must be detected at the BASE position
                # (NOT base + offset — that was the unregistered expectation).
                if acc:
                    exp_x, exp_y = base_c[idx]
                    best, bd = None, 1e9
                    for b in acc:
                        d = ((b['cx'] - exp_x) ** 2 + (b['cy'] - exp_y) ** 2) ** 0.5
                        if d < bd:
                            best, bd = b, d
                    if bd <= TOL:
                        nc += 1
                    else:
                        na += 1
                else:
                    nm += 1
            rows.append((dx, dy, nc, nm, na))
    # registration error summary
    ex = np.array([e[0] for e in reg_errs]); ey = np.array([e[1] for e in reg_errs])
    exact = int(((ex == 0) & (ey == 0)).sum())
    print(f'registration: exact-in-1px {exact}/{len(reg_errs)} ({100*exact/len(reg_errs):.1f}%), '
          f'median residual ({np.median(np.abs(ex)):.0f}, {np.median(np.abs(ey)):.0f}), worst ({np.abs(ex).max()}, {np.abs(ey).max()})')
    print('\naccuracy by offset magnitude (Chebyshev):')
    bymag = {}
    for dx, dy, nc, nm, na in rows:
        m = max(abs(dx), abs(dy))
        t = bymag.setdefault(m, [0, 0, 0])
        t[0] += nc; t[1] += nm; t[2] += na
    for m in sorted(bymag):
        nc, nm, na = bymag[m]
        tot = nc + nm + na
        print(f'  |off|={m:>2}: correct {nc:>5}/{tot} ({100*nc/tot:5.1f}%)  miss {nm:>5}  ambig {na:>4}')
    out = {'run': run_dir.name, 'prefix': prefix, 'max_off': max_off, 'step': step,
           'n_pairs': len(pairs_s), 'registered': True, 'rows': rows}
    op = run_dir / f'{prefix}_delta_sweep_reg.json'
    op.write_text(json.dumps(out))
    print(f'wrote {op}')
    return 0


if __name__ == '__main__':
    sys.exit(main())