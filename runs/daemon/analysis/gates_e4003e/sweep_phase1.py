#!/usr/bin/env python3
"""E4 0003E gate sweep — PHASE 1: build + baseline + parity + anchors.

Builds per-epoch score machinery with the CLI's own functions, runs the
baseline (promoted gates) with the verbatim candidate loop, parity-checks
against the real CLI subprocess on epoch r3, and stages:
  _best_<tag>.npy / _argi_<tag>.npy / _maskblur_<tag>.npy (float32/32/int32)
  _candidates.json (d>=8 lists), _union.json (id -> mean pos over epochs)
  baseline.json (per-epoch baseline final sets), phase1_result.json
  /tmp copy of synth frames.txt for the parity run (not in repo).
"""
import io
import json
import math
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import cv2
from PIL import Image, ImageFile
ImageFile.LOAD_TRUNCATED_IMAGES = True

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402

OUT = REPO / 'runs/daemon/analysis/gates_e4003e'
OUT.mkdir(parents=True, exist_ok=True)
TMP = Path('/home/nellie/.hermes/cache/scratch/gates_e4003e')
TMP.mkdir(parents=True, exist_ok=True)

EPOCHS = [('r2', 80), ('r3', 100), ('r4', 120), ('r5', 150)]
N = 600
AMP0, MAR0 = 40.0, 6.0
SUPP0 = C.SUPPRESS
PYV = '/home/nellie/.hermes/hermes-agent/venv/bin/python3'
HISTMED_CWCSTATS = {'r2': 40.5, 'r3': 50.5, 'r4': 55.5, 'r5': 54.5}


def load_run(run_dir, tag):
    frames = []
    for jpg in sorted(run_dir.glob(f'cwc_{tag}_*.jpg')):
        meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        img = Image.open(io.BytesIO(jpg.read_bytes())).convert('RGB')
        frames.append((meta['label'], img))
    return frames


def build_epoch(tag):
    frames = load_run(REPO / 'runs/daemon/runs' / f's14r0003e-{tag}', tag)
    planes = {int(lab.split(':p')[1]): img for lab, img in frames if ':p' in lab}
    masts = [img for lab, img in frames if 'master' in lab]
    assert masts and len(planes) == 24, (tag, len(planes), len(masts))
    mlum = np.asarray(masts[0], dtype=np.float32).max(axis=2).astype(np.float32)
    H, W = mlum.shape
    planeL = {}
    for p, img in planes.items():
        planeL[p] = np.asarray(img, dtype=np.float32).max(axis=2).astype(np.float32)
    tot, surf, info = C.register_direct(mlum, planeL, C.CWC_NCC_PEAK_MARGIN,
                                        verbose=False)
    for p in sorted(planeL):
        sdx = C.round_half_up(tot[p][0])
        sdy = C.round_half_up(tot[p][1])
        rdx, rdy, _n = C.ncc_refine(mlum, planeL[p], sdx, sdy, rad=C.FULLRES_RAD)
        tot[p] = (float(rdx) + (tot[p][0] - sdx),
                  float(rdy) + (tot[p][1] - sdy), tot[p][2])
    stacksig = np.empty((24, H, W), np.float32)
    mmed = C.hist_median(mlum)
    for j, p in enumerate(sorted(planeL)):
        tdx = float(tot[p][0])
        tdy = float(tot[p][1])
        mx = np.arange(W, dtype=np.float32) + np.float32(tdx)
        my = np.arange(H, dtype=np.float32) + np.float32(tdy)
        mx, my = np.meshgrid(mx, my)
        sh = cv2.remap(planeL[p], mx, my, cv2.INTER_LINEAR,
                       borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        stacksig[j] = mlum - (float(C.hist_median(planeL[p])) / mmed) * sh
    with open(C.BASE / 'codewords_12of24.json') as fh:
        codes = json.load(fh)
    codes = codes['codes'] if isinstance(codes, dict) else codes
    bits = np.zeros((N, 24), dtype=np.int16)
    for i in range(N):
        for p in codes[i]:
            bits[i][p] = 1
    sign = (1 - 2 * bits[:N]).astype(np.float32)
    sc = np.tensordot(sign, stacksig, axes=([1], [0]))
    best = sc.max(axis=0)
    argi = sc.argmax(axis=0).astype(np.int32)
    mb = cv2.GaussianBlur(mlum, (5, 5), 1.2)
    Dw = (bits[:N][:, None, :] != bits[:N][None, :, :]).sum(-1)
    cand = [np.where(Dw[i] >= 8)[0].astype(np.int32) for i in range(N)]
    return dict(tag=tag, best=best, argi=argi, sc=sc, mb=mb, mmed=float(mmed),
                cand=cand, H=H, W=W, planes=len(planes), mlum=mlum)


def gate_loop(best, argi, sc, cand, mask, amp_gate, margin_gate, supp_win):
    """VERBATIM cwc_pos_decode.main candidate-selection block."""
    used = np.zeros(mask.shape, bool)
    amap = np.where(mask, best, -1e9)
    order = np.argsort(amap.ravel())[::-1]
    accepted = []
    for j in order:
        y, x = divmod(int(j), mask.shape[1])
        if not mask[y, x] or used[y, x]:
            continue
        i = int(argi[y, x])
        amp = float(best[y, x]) / 12.0
        if amp < amp_gate:
            continue
        s2 = float(sc[cand[i], y, x].max())
        margin = (float(best[y, x]) - s2) / 12.0
        if margin < margin_gate:
            continue
        used[max(0, y - supp_win // 2): y + supp_win // 2 + 1,
             max(0, x - supp_win // 2): x + supp_win // 2 + 1] = True
        accepted.append({'led': i, 'cx': x, 'cy': y,
                         'amp': round(amp, 1), 'margin': round(margin, 1)})
    byled = {}
    for q in accepted:
        cur = byled.get(q['led'])
        if cur is None or q['amp'] > cur['amp']:
            byled[q['led']] = q
    return sorted(byled.values(), key=lambda q: q['led'])


def synth_frames_txt(tag):
    """<tag>_frames.txt for the parity CLI run (jpgs copied to tmp dir)."""
    src = REPO / 'runs/daemon/runs' / f's14r0003e-{tag}'
    dst = TMP / f'e4003e_{tag}'
    dst.mkdir(exist_ok=True)
    lines = []
    for jpg in sorted(src.glob(f'cwc_{tag}_*.jpg')):
        meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        b = jpg.read_bytes()
        import base64
        b64 = base64.b64encode(b).decode()
        wrapped = '\n'.join(b64[i:i + 76] for i in range(0, len(b64), 76))
        lines.append('FRAME ' + json.dumps(meta, separators=(',', ':')))
        lines.append('FJPEG ' + wrapped)
        lines.append('FEND')
        (dst / jpg.name).write_bytes(b)
    (dst / f'{tag}_frames.txt').write_text('\n'.join(lines) + '\n')
    return dst


def main():
    t0 = time.time()
    out = {}
    union_pos = {}
    for tag, L in EPOCHS:
        ep = build_epoch(tag)
        thr_auto = min(100.0, max(45.0, 1.12 * ep['mmed']))
        mask = ep['mb'] >= thr_auto
        final = gate_loop(ep['best'], ep['argi'], ep['sc'], ep['cand'],
                          mask, AMP0, MAR0, SUPP0)
        np.save(OUT / f'_best_{tag}.npy', ep['best'])
        np.save(OUT / f'_argi_{tag}.npy', ep['argi'])
        np.save(OUT / f'_maskblur_{tag}.npy', ep['mb'])
        (OUT / f'_cand_{tag}.json').write_text(
            json.dumps([c.tolist() for c in ep['cand']],
                       separators=(',', ':')))
        amps = sorted(q['amp'] for q in final)
        out[tag] = {
            'L': L, 'histMed_cli': ep['mmed'],
            'histMed_cwcstats': HISTMED_CWCSTATS[tag],
            'thr_auto': thr_auto, 'mask_px': int(mask.sum()),
            'confirmed': len(final),
            'amp_med': amps[len(amps) // 2] if amps else None,
            'amp_min': amps[0] if amps else None,
            'max_id': max(q['led'] for q in final) if final else None,
            'final': final,
        }
        for q in final:
            union_pos.setdefault(q['led'], []).append((q['cx'], q['cy']))
        print(f'[{tag}] L={L}: CONF {len(final)}/600 thr {thr_auto:.1f} '
              f'histMed {ep["mmed"]} amp_med {out[tag]["amp_med"]} '
              f'({time.time()-t0:.0f}s)', flush=True)
        del ep['sc']
        import gc
        gc.collect()

    # parity: epoch r3 through the REAL CLI subprocess
    dst = synth_frames_txt('r3')
    r = subprocess.run(
        [PYV, str(REPO / 'tools/cwc_pos_decode.py'), str(dst), '--tag', 'r3',
         '--n', '600', '--amp-gate', '40', '--margin-gate', '6',
         '--mask-thr', '100', '--mask-adaptive', '1', '--mask-k', '1.12',
         '--mask-floor', '45', '--fullres-rad', '4', '--save-json'],
        capture_output=True, text=True, timeout=570)
    cli_ok = r.returncode == 0 if hasattr(r, 'returncode') else False
    rcode = getattr(r, 'returncode', None)
    parity = {'rc': rcode, 'stdout_tail': r.stdout[-400:] if hasattr(r, 'stdout') else ''}
    try:
        cli_json = json.loads((dst / 'ledpos.json').read_text())
        mine = out['r3']['final']
        amine = {q['led']: q for q in mine}
        diffs = []
        for q in cli_json:
            m = amine.get(q['led'])
            if m is None:
                diffs.append((q['led'], 'missing'))
            elif (m['cx'], m['cy']) != (q['cx'], q['cy']) or \
                    abs(m['amp'] - q['amp']) > 1e-6 or \
                    abs(m['margin'] - q['margin']) > 1e-6:
                diffs.append((q['led'], (m['cx'], m['cy'], m['amp'], m['margin']),
                              (q['cx'], q['cy'], q['amp'], q['margin'])))
        extra = [k for k in amine if k not in {q['led'] for q in cli_json}]
        parity.update(n_cli=len(cli_json), n_mine=len(mine), diffs=diffs[:20],
                      extra=extra[:20],
                      pass_=not diffs and not extra and
                      len(cli_json) == len(mine))
    except Exception as e:
        parity['error'] = repr(e)
        parity['pass_'] = False
    print('PARITY:', json.dumps({k: v for k, v in parity.items() if k != '_'}),
          flush=True)

    # union anchors: mean position per id across epochs (present in >=1)
    union_anchor = {int(k): [round(float(np.mean([p[0] for p in v])), 1),
                             round(float(np.mean([p[1] for p in v])), 1)]
                    for k, v in union_pos.items()}
    (OUT / 'baseline.json').write_text(json.dumps(out, indent=1))
    (OUT / '_union_anchor.json').write_text(
        json.dumps(union_anchor, separators=(',', ':')))
    (OUT / 'phase1_result.json').write_text(json.dumps(
        {'epochs': {k: {kk: vv for kk, vv in v.items() if kk != 'final'}
                    for k, v in out.items()},
         'parity': parity, 'runtime_s': round(time.time() - t0, 1)}, indent=1))
    print('phase1 complete', flush=True)


if __name__ == '__main__':
    main()