#!/usr/bin/env python3
"""Extract + verify + tear-score tonight's S14R-0003D E4 frames from the wire.

Era gate: capture.txt line >= 67086 (the 20:33:25 daemon start) AND exp 300.03
(unique to tonight — all prior corpora ran 200/500/700/800-class exposures).
Group rule: [PHONE] FRAME json -> [PHONE] FJPEG b64 lines -> [PHONE] FEND;
only FEND-terminated groups (0004(g) discipline). Duplicate labels: keep the
longest group (ship-storm-era rule). Hygiene: EOI + plain PIL decode + W/H vs
meta. Tear score: exact port of cal.html tearScan() (block-mean x7 over W,
rows full H, luma = max(r,g,b), jL = max |dl|, jR = max |d(red)-d(luma)|,
scan from y >= skipTop=120, thresholds jL 18 / jR 6).
Read-only outside runs/daemon/analysis/e4_0003d_extract/.
"""
import base64, json, re, sys
from pathlib import Path
import numpy as np
from PIL import Image

REPO   = Path('/home/nellie/projects/led-display/poc_survey')
CAP    = REPO / 'runs' / 'daemon' / 'capture.txt'
OUT    = REPO / 'runs' / 'daemon' / 'analysis' / 'e4_0003d_extract'
SKIP_TOP, THR_JL, THR_JR = 120, 18, 6

def split(buf):  # decode wire group -> jpg bytes
    raw = base64.b64decode(''.join(buf), validate=False)
    assert raw[:2] == b'\xff\xd8' and raw[-2:] == b'\xff\xd9', 'not a full jpg'
    return raw

def tear_score(img: Image.Image):
    a = np.asarray(img.convert('RGB'), dtype=np.float64)  # H,W,3
    h, w, _ = a.shape
    cols = max(1, w // 7)
    xb = w / cols                      # block width (drawImage area-average equiv)
    xs = (np.arange(cols) * xb).astype(int)          # block starts
    xe = np.clip((np.arange(cols) + 1) * xb, 0, w).astype(int)
    lum = np.zeros((h, cols)); red = np.zeros((h, cols))
    for c in range(cols):
        blk = a[:, xs[c]:xe[c], :]
        lum[:, c] = blk.max(axis=2).mean(axis=1)     # max(r,g,b) per px, then mean
        red[:, c] = blk[:, :, 0].mean(axis=1)
    lumr = lum.mean(axis=1); redr = red.mean(axis=1) # same 0-255 scale as page
    dl = np.abs(np.diff(lumr)); dg = np.abs(np.diff(redr) - np.diff(lumr))
    y0 = max(1, SKIP_TOP)
    jL = dl[y0 - 1:].max() if len(dl) else 0.0
    jR = dg[y0 - 1:].max() if len(dg) else 0.0
    yl = int(np.argmax(dl[y0 - 1:])) + y0
    yr = int(np.argmax(dg[y0 - 1:])) + y0
    return jL, jR, yl, yr

def main():
    lines = CAP.read_text(encoding='utf-8', errors='replace').splitlines()
    groups = []   # (label, meta, b64[], fendlineno)
    i = ERA_FROM = None
    # era start: first '=== daemon start ===' at/after line 67080 (0-idx 67079)
    for idx, ln in enumerate(lines):
        if idx >= 67079 and '=== daemon start ===' in ln:
            ERA_FROM = idx; break
    assert ERA_FROM is not None, 'era start not found'
    i = ERA_FROM; cur = None
    while i < len(lines):
        ln = lines[i]
        if '[PHONE] FRAME ' in ln:
            js = ln.split('[PHONE] FRAME ', 1)[1]
            try: meta = json.loads(js)
            except Exception: meta = None
            cur = {'meta': meta, 'buf': [], 'start': i}
        elif '[PHONE] FJPEG ' in ln and cur is not None:
            cur['buf'].append(ln.split('[PHONE] FJPEG ', 1)[1])
        elif '[PHONE] FEND' in ln and cur is not None:
            groups.append(cur); cur = None
        elif cur is not None and ('[PHONE-LOG]' in ln and 'end' in ln):
            cur = None      # torn-off group: drop
        i += 1
    sel = {}
    sel_sizes = {}
    for g in groups:
        m = g['meta']
        if not m: continue
        lbl = m.get('label', '')
        if not lbl.startswith('cwc:'): continue
        if '300.03' not in str(m.get('exp', '')): continue
        size = sum(len(x) for x in g['buf'])
        if lbl not in sel or size > sum(len(x) for x in sel[lbl]['buf']) + 0:
            if lbl not in sel or size > sel_sizes[lbl]:
                sel[lbl] = g; sel_sizes[lbl] = size
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for lbl in sorted(sel):
        g = sel[lbl]; m = g['meta']
        raw = split(g['buf'])
        run = REPO / 'runs'; dst = OUT / lbl.replace(':', '_').replace('cwc_', '') if False else None
        name = lbl.replace(':', '_') + '.jpg'
        (OUT / name).write_bytes(raw)
        img = Image.open(OUT / name); img.load()
        w, h = img.size
        wh_ok = (w == int(m['W']) and h == int(m['H']))
        jL, jR, yl, yr = tear_score(img)
        rows.append(dict(label=lbl, exp=m.get('exp', ''), t=m.get('t'),
                         bytes=len(raw), EOI=raw[-2:] == b'\xff\xd9',
                         pil=True, WH_ok=wh_ok, jL=round(jL, 1), jR=round(jR, 1),
                         yl=yl, yr=yr,
                         torn=bool(jL >= THR_JL or jR >= THR_JR)))
    (OUT / 'scored.json').write_text(json.dumps(rows, indent=1))
    n_plane = sum(1 for r in rows if ':p' in r['label'])
    n_mast = sum(1 for r in rows if ':master' in r['label'])
    print(f'era from line {ERA_FROM+1}; groups cwc-300.03: {len(rows)} '
          f'({n_plane} planes, {n_mast} masters)')
    planes = [r for r in rows if ':p' in r['label']]
    import collections
    runs = collections.defaultdict(list)
    for r in planes:
        runs[r['label'].split(':p')[0].split(':')[1]].append(r)
    for rn in sorted(runs):
        rs = runs[rn]
        mx = max(rs, key=lambda r: max(r['jL'], 10 * r['jR']))
        print(f'run r{rn}: {len(rs)} planes; jL max {max(r["jL"] for r in rs):.1f} '
              f'jR max {max(r["jR"] for r in rs):.1f}; '
              f'worst={mx["label"]} jL={mx["jL"]} jR={mx["jR"]} yl={mx["yl"]}')
    bad = [r for r in rows if not (r['EOI'] and r['pil'] and r['WH_ok'])]
    print('hygiene failures:', len(bad))
    torn_rows = [r for r in planes if r['torn']]
    print(f'planes scoring >= threshold: {len(torn_rows)}')
    for r in sorted(torn_rows, key=lambda r: -max(r['jL'], r['jR']))[:12]:
        print(f'  {r["label"]}  jL={r["jL"]:>5} jR={r["jR"]:>4}  argRow y={r["yl"]}/{r["yr"]}')
    ys = [r['yl'] for r in planes if r['jL'] >= 0.8 * THR_JL]
    print('jL>=14.4 argrows:', sorted(collections.Counter(ys).items(), key=lambda x: -x[1])[:10])

if __name__ == '__main__':
    main()