#!/usr/bin/env python3
"""E4 union(L) decode — run8..run11 (L 80/100/120/150) at promoted gates.

Verbatim machinery from tools/cwc_pos_decode.py main() (lines 319-427): the
only deltas are (a) frames load from pulled jpg+meta.json (labels in metas;
no cwc_frames.txt shipped with the cal-battery pulls), (b) results are
returned in-process instead of written into runs/ (read-only), (c) n=600,
(d) extra per-site probe (wall luma behind each claim site) for failure-axis
classification. Gates: amp 40, margin 6, mask-thr auto (S14R-0002 adaptive
rule), fullres-rad 4, SUPPRESS 7 — all code defaults.
"""
import io
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image as PILImage  # cwc_pos_decode does NOT re-export PIL
from PIL import ImageFile
ImageFile.LOAD_TRUNCATED_IMAGES = True  # safety net only; the E4 driver reads
# the WIRE-REPAIRED corpus (analysis/e4_union/repaired/<run>, capture.txt
# re-joined bytes) so every frame decodes plain — see wire_repair.py.

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'tools'))
import cwc_pos_decode as C  # noqa: E402

RUNS = REPO / 'runs/daemon/analysis/e4_union/repaired'
OUTD = REPO / 'runs/daemon/analysis/e4_union'
OUTD.mkdir(parents=True, exist_ok=True)

RUNS_L = [('run8', 'r8', 80), ('run9', 'r9', 100),
          ('run10', 'r10', 120), ('run11', 'r11', 150)]


def load_run(run_dir, tag):
    frames, skipped = [], []
    for jpg in sorted(run_dir.glob(f'cwc_{tag}_*.jpg')):
        try:
            raw = jpg.read_bytes()
            img = PILImage.open(io.BytesIO(raw)).convert('RGB')
            meta = json.loads(jpg.with_suffix('.meta.json').read_text())
        except Exception as e:  # single malformed frame: skip, don't abort
            skipped.append((jpg.name, repr(e)))
            continue
        frames.append({'meta': meta, 'label': meta['label'], 'img': img})
    return frames, skipped


def decode_one(run_dir, tag, L, n=600, amp_gate=40, margin_gate=6,
               drop_planes=()):
    frames, skipped = load_run(run_dir, tag)
    planes = {int(f['label'].split(':p')[1]): f['img']
              for f in frames if ':p' in f['label']
              and int(f['label'].split(':p')[1]) not in drop_planes}
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
    # score[i](x,y) = Σ_{p used} s[i,p]×(M − k_p·P_p)(x,y), s=+1 OFF/−1 ON.
    # Σ_p (M − k_p P_p) cancels the master's mean field exactly at n_used=24
    # with k_p=p_p/M (page's identity); for subsets add the missing planes'
    # (M − mmed) so the all-constant site still scores ~0 for every codeword.
    # Per-lamp amp = score / (#ON planes used) — for n_used=24 this is exactly
    # the CLI's best/12 (bank is exact half-duty); comparable units otherwise.
    sign = np.vstack([np.where(bits[:N, p] == 1, -1.0, 1.0)
                      for p in used_planes]).T.astype(np.float32)
    missing = [p for p in range(24) if p not in used_planes]
    # per-lamp ON count over used planes (bank is exact half-duty: ON=12 of 24;
    # for subsets it is 12 minus ON planes that were dropped)
    on_count = bits[:N, used_planes].sum(axis=1).astype(np.float32)  # (N,)
    # subset scoring, per-codeword completion: a missing plane imputed as
    # background-only contributes s_p·(M − mmed) — +1 rows for OFF lamps,
    # −1 for ON lamps. Net per codeword: (|miss| − 2·(12−on_i))·(M − mmed).
    # Implemented as a virtual plane row so argmax/amp need no special case;
    # at n_used=24 the column is exactly 0 (CLI-identical, parity-proven).
    m_on_miss = (12 - on_count).astype(np.float32)     # ON planes dropped
    corr_col = (len(missing) - 2.0 * m_on_miss).astype(np.float32)  # (N,)
    Sg = np.vstack([stacksig.astype(np.float32),
                    (mlum - mmed).astype(np.float32)[None]])
    sign_ext = np.hstack([sign, corr_col[:, None]]).astype(np.float32)
    sc = np.tensordot(sign_ext, Sg, axes=([1], [0]))   # (N, H, W)
    best = sc.max(axis=0)
    argi = sc.argmax(axis=0)
    # amp = best / ON-pl(lamp): at n_used=24, ON=12 ⇒ identical to CLI best/12.
    amp_den = np.maximum(on_count, 1.0).astype(np.float32)

    mb = cv2.GaussianBlur(mlum, (5, 5), 1.2)
    mmed_hm = C.hist_median(mlum)
    eff_thr = min(float(100), max(float(45), 1.12 * mmed_hm))  # mask-thr auto
    mask = mb >= eff_thr
    Dfull = (bits[:N][:, None, :] != bits[:N][None, :, :]).sum(-1)

    # raw score plane for per-id instrument use (kept in-memory only)
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
        amp = float(best[y, x]) / float(amp_den[i])
        row = Dfull[i]
        cand = np.where(row >= 8)[0]
        s2 = float(sc[cand, y, x].max())
        margin = (float(best[y, x]) - s2) / float(amp_den[i])
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


# Post-repair the wire bytes are complete for ALL 100 frames — no plane is
# dropped (the earlier "flare" signature was truncation corruption; the
# 24-plane full-bank path is CLI-parity-exact). Kept as a knob if a future
# corpus ships a genuinely unreadable plane.
FLARE_DROPS = {'r8': (), 'r9': (), 'r10': (), 'r11': ()}


def main():
    results = {}
    for run, tag, L in RUNS_L:
        res = decode_one(RUNS / run, tag, L, drop_planes=FLARE_DROPS[tag])
        results[tag] = res
        leds = res['leds']
        amps = [p['amp'] for p in leds]
        print(f"{res['run']} L={L}: histMed {res['histMed']:.1f} "
              f"eff_thr {res['eff_thr']:.1f} confirmed {len(leds)}/{res['N']} "
              f"amp med {sorted(amps)[len(amps)//2] if amps else 0:.1f} "
              f"min {min(amps) if amps else 0:.1f} max {max(amps) if amps else 0:.1f}")
        sys.stdout.flush()
        out = OUTD / f"{run}_ledpos.json"
        out.write_text(json.dumps(res, indent=1))
    (OUTD / 'e4_union_raw.json').write_text(json.dumps(results, indent=1))
    print('wrote per-run ledpos jsons to', OUTD)


if __name__ == '__main__':
    main()