#!/usr/bin/env python3
"""V2 E4 decode DIRECTLY from run dirs (no wire pass) + parity vs v1.

The v2 corpus is wire-repaired IN PLACE and manifest-verified
(run_repair/manifest.json, 528/528 EOI+PIL+sha green), so the driver runs
with ImageFile.LOAD_TRUNCATED_IMAGES *removed* (the repaired run dirs are
plain-decodable; truncated-image fallback is neither needed nor allowed).
Everything else is verbatim from analysis/e4_union/e4_union_decode.py (the
v1 driver whose outputs are CLI-parity-exact): same register_direct /
ncc_refine / gates (amp 40, margin 6, mask-thr auto, SUPPRESS 7, rad 4,
n=600), same scoring including the subset-completion column.

Output: ledpos jsons per run (written with a _v2 suffix) + a per-id diff
against the v1 ledpos jsons (which were decoded from the wire-repaired
copies) — counts, amp, margin, site; verdict string in the summary.
"""
import io
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image as PILImage
# NOTE: deliberately NOT setting ImageFile.LOAD_TRUNCATED_IMAGES — the
# repaired corpus must decode plainly; a truncated file would raise here.

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402
from offline_hole_verify import plane_index  # noqa: E402

RUNS = REPO / 'runs/daemon/runs'          # v2: direct from run dirs
OUTD = REPO / 'runs/daemon/analysis/photometry_v2'
OUTD.mkdir(parents=True, exist_ok=True)
V1 = REPO / 'runs/daemon/analysis/e4_union'

RUNS_L = [('run8', 'r8', 80), ('run9', 'r9', 100),
          ('run10', 'r10', 120), ('run11', 'r11', 150)]


def load_run(run_dir, tag):
    frames, skipped = [], []
    for jpg in sorted(run_dir.glob(f'cwc_{tag}_*.jpg')):
        raw = jpg.read_bytes()
        assert raw.endswith(b'\xff\xd9'), f'no EOI: {jpg.name}'
        try:
            img = PILImage.open(io.BytesIO(raw)).convert('RGB')
            img.load()
            meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        except Exception as e:
            skipped.append((jpg.name, repr(e)))
            raise
        frames.append({'meta': meta, 'label': meta['label'], 'img': img})
    return frames, skipped


def decode_one(run_dir, tag, L, n=600, amp_gate=40, margin_gate=6,
               drop_planes=()):
    frames, skipped = load_run(run_dir, tag)
    planes = {plane_index(f['label']): f['img']
              for f in frames if ':p' in f['label']
              and plane_index(f['label']) not in drop_planes}
    masts = [f for f in frames if 'master' in f['label']]
    assert masts and len(planes) >= 20, (tag, len(planes), len(masts),
                                         'skipped', skipped)
    master = masts[0]['img']
    mlum = np.asarray(master, dtype=np.float32).max(axis=2).astype(np.float32)
    H, W = mlum.shape

    planeL = {p: np.asarray(planes[p], dtype=np.float32).max(axis=2)
              .astype(np.float32) for p in planes}
    tot, surf, info = C.register_direct(mlum, planeL,
                                        C.CWC_NCC_PEAK_MARGIN, verbose=False)
    for p in sorted(planeL):
        sdx = C.round_half_up(tot[p][0])
        sdy = C.round_half_up(tot[p][1])
        rdx, rdy, _n = C.ncc_refine(mlum, planeL[p], sdx, sdy,
                                    rad=C.FULLRES_RAD)
        tot[p] = (float(rdx) + (tot[p][0] - sdx),
                  float(rdy) + (tot[p][1] - sdy), tot[p][2])
    shifts = [tot[p] for p in sorted(tot)]

    used_planes = sorted(planeL)
    stacksig = np.empty((len(used_planes), H, W), np.float32)
    kbgs = []
    mmed = C.hist_median(mlum)
    for j, p in enumerate(used_planes):   # j indexes ROWS = used_planes[j]
        tdx, tdy = float(tot[p][0]), float(tot[p][1])
        mx, my = np.meshgrid(
            np.arange(W, dtype=np.float32) + np.float32(tdx),
            np.arange(H, dtype=np.float32) + np.float32(tdy))
        sh = cv2.remap(planeL[p], mx, my, cv2.INTER_LINEAR,
                       borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        kp = float(C.hist_median(planeL[p])) / mmed
        kbgs.append((p, round(kp, 3)))
        stacksig[j] = mlum - kp * sh

    codes = json.load(open(C.BASE / 'codewords_12of24.json'))
    codes = codes['codes'] if isinstance(codes, dict) else codes
    N = min(n, len(codes))
    n_used = len(used_planes)
    bits = np.zeros((N, 24), dtype=np.int16)
    for i in range(N):
        for p in codes[i]:
            bits[i][p] = 1
    sign = np.vstack([np.where(bits[:N, p] == 1, -1.0, 1.0)
                      for p in used_planes]).T.astype(np.float32)
    missing = [p for p in range(24) if p not in used_planes]
    on_count = bits[:N, used_planes].sum(axis=1).astype(np.float32)
    m_on_miss = (12 - on_count).astype(np.float32)
    corr_col = (len(missing) - 2.0 * m_on_miss).astype(np.float32)
    Sg = np.vstack([stacksig.astype(np.float32),
                    (mlum - mmed).astype(np.float32)[None]])
    sign_ext = np.hstack([sign, corr_col[:, None]]).astype(np.float32)
    sc = np.tensordot(sign_ext, Sg, axes=([1], [0]))
    best = sc.max(axis=0)
    argi = sc.argmax(axis=0)

    mb = cv2.GaussianBlur(mlum, (5, 5), 1.2)
    mmed_hm = C.hist_median(mlum)
    eff_thr = min(float(100), max(float(45), 1.12 * mmed_hm))
    mask = mb >= eff_thr
    Dfull = (bits[:N][:, None, :] != bits[:N][None, :, :]).sum(-1)

    ledpos = []
    used = np.zeros(mask.shape, bool)
    amap = np.where(mask, best, -1e9)
    order = np.argsort(amap.ravel())[::-1]
    contested = 0
    for j in order:
        y, x = divmod(int(j), mask.shape[1])
        if not mask[y, x] or used[y, x]:
            continue
        i = int(argi[y, x])
        amp = float(best[y, x]) / float(amp_den_of(bits, used_planes, i, N))
        row = Dfull[i]
        cand = np.where(row >= 8)[0]
        s2 = float(sc[cand, y, x].max())
        margin = (float(best[y, x]) - s2) / float(amp_den_of(bits, used_planes, i, N))
        if amp < amp_gate or margin < margin_gate:
            contested += 1
            continue
        used[max(0, y - C.SUPPRESS // 2): y + C.SUPPRESS // 2 + 1,
             max(0, x - C.SUPPRESS // 2): x + C.SUPPRESS // 2 + 1] = True
        ledpos.append({'led': i, 'cx': x, 'cy': y,
                       'amp': round(amp, 1), 'margin': round(margin, 1)})
    byled = {}
    for q in ledpos:
        cur = byled.get(q['led'])
        if cur is None or q['amp'] > cur['amp']:
            byled[q['led']] = q
    ledpos = list(byled.values())
    leds = sorted(ledpos, key=lambda q: q['led'])

    probe = {}
    for q in leds:
        probe[q['led']] = float(mb[q['cy'], q['cx']])
    res = {'tag': tag, 'run': run_dir.name, 'L': L, 'N': N,
           'confirmed': len(leds), 'leds': leds,
           'frames_loaded': len(frames),
           'skipped_frames': skipped,
           'histMed': float(mmed_hm), 'eff_thr': eff_thr,
           'exp': masts[0]['meta'].get('exp', ''),
           'wall_at_site': probe, 'conf_pre_gate': contested,
           'kbgs': kbgs,
           'shifts': {str(p): [round(float(tot[p][0]), 2),
                               round(float(tot[p][1]), 2),
                               round(float(tot[p][2]), 3)]
                      for p in sorted(tot)}}
    return res


def amp_den_of(bits, used_planes, i, N=600):
    on_count = int(bits[i][[p for p in used_planes]].sum())
    return max(on_count, 1)


def main():
    summary = {}
    for run, tag, L in RUNS_L:
        res = decode_one(RUNS / run, tag, L)
        summary[tag] = {k: v for k, v in res.items() if k != 'leds'}
        amps = [p['amp'] for p in res['leds']]
        print(f"{res['run']} L={L}: confirmed {len(res['leds'])}/{res['N']} "
              f"amp med {sorted(amps)[len(amps)//2] if amps else 0:.1f} "
              f"min {min(amps) if amps else 0:.1f} histMed {res['histMed']:.1f} "
              f"eff_thr {res['eff_thr']:.1f}")
        sys.stdout.flush()
        (OUTD / f'{run}_ledpos_v2.json').write_text(json.dumps(res, indent=1))

        # parity vs v1
        v1 = json.loads((V1 / f'{run}_ledpos.json').read_text())
        v1m = {q['led']: q for q in v1['leds']}
        v2m = {q['led']: q for q in res['leds']}
        only1 = sorted(set(v1m) - set(v2m))
        only2 = sorted(set(v2m) - set(v1m))
        common = sorted(set(v1m) & set(v2m))
        dAmp = [abs(v1m[i]['amp'] - v2m[i]['amp']) for i in common]
        dMar = [abs(v1m[i]['margin'] - v2m[i]['margin']) for i in common]
        dSite = max([abs(v1m[i]['cx'] - v2m[i]['cx']) +
                     abs(v1m[i]['cy'] - v2m[i]['cy']) for i in common], default=0)
        verdict = ('EXACT' if (not only1 and not only2 and max(dAmp, default=0) == 0
                               and max(dMar, default=0) == 0 and dSite == 0)
                   else 'DIFF')
        summary[tag]['parity'] = {'verdict': verdict,
                                  'n_v1': len(v1m), 'n_v2': len(v2m),
                                  'only_v1': len(only1), 'only_v2': len(only2),
                                  'max_dAmp': max(dAmp, default=0),
                                  'max_dMarg': max(dMar, default=0),
                                  'max_dSite': dSite}
        print(f"  parity vs v1(wire-derived): {verdict} "
              f"v1={len(v1m)} v2={len(v2m)} only_v1={len(only1)} "
              f"only_v2={len(only2)} maxdAmp={max(dAmp, default=0)} "
              f"maxdMarg={max(dMar, default=0)} maxdSite={dSite}")

    (OUTD / 'e4_v2_summary.json').write_text(json.dumps(summary, indent=1))
    print('wrote e4_v2_summary.json')


if __name__ == '__main__':
    main()