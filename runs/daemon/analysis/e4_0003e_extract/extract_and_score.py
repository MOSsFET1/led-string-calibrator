#!/usr/bin/env python3
"""Extract + verify + tear-score the S14R-0003E E4 battery (epochs r5..r8).

Era gate: wire lines AFTER the last '=== daemon start ===' at 22:52 (the
0003E battery staging) AND label epochs r5-r8. NOTE: cwc:r8 collides with
the milestone battery's run8 dir (skip-if-exists will have dropped those
writes) — epochs r5-r7 land on disk via the daemon, r8 is wire-only here.
Hygiene: EOI + plain PIL + W/H vs meta. Score = exact cal.html tearScan
port (block-mean x7, luma max(r,g,b), jL/jR, skipTop 120; thresholds as
shipped: thr 26, thrRed 8.5 — BUT the page compares jR with (thrRed|0)=8,
so the shipped threshold behaved as 8; score BOTH variants).
Outputs jpgs+metas+scored.json under runs/daemon/analysis/e4_0003e_extract/.
"""
import base64, json, re
from pathlib import Path
import numpy as np
from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
CAP  = REPO / 'runs' / 'daemon' / 'capture.txt'
OUT  = REPO / 'runs' / 'daemon' / 'analysis' / 'e4_0003e_extract'
SKIP_TOP, THR_JL, THR_JR_SHIPPED, THR_JR_NOMINAL = 120, 26, 8, 8.5

def tear_score(img):
    a = np.asarray(img.convert('RGB'), dtype=np.float64)
    h, w, _ = a.shape
    cols = max(1, w // 7)
    xb = w / cols
    xs = (np.arange(cols) * xb).astype(int)
    xe = np.clip((np.arange(cols) + 1) * xb, 0, w).astype(int)
    lum = np.zeros((h, cols)); red = np.zeros((h, cols))
    for c in range(cols):
        blk = a[:, xs[c]:xe[c], :]
        lum[:, c] = blk.max(axis=2).mean(axis=1)
        red[:, c] = blk[:, :, 0].mean(axis=1)
    lumr = lum.mean(axis=1); redr = red.mean(axis=1)
    dl = np.abs(np.diff(lumr)); dg = np.abs(np.diff(redr) - np.diff(lumr))
    y0 = max(1, SKIP_TOP)
    jL = float(dl[y0 - 1:].max()); jR = float(dg[y0 - 1:].max())
    yl = int(np.argmax(dl[y0 - 1:])) + y0
    yr = int(np.argmax(dg[y0 - 1:])) + y0
    return jL, jR, yl, yr

def main():
    lns = CAP.read_text(encoding='utf-8', errors='replace').splitlines()
    era = max(i for i, ln in enumerate(lns) if '=== daemon start ===' in ln)
    groups = []; cur = None
    for ln in lns[era:]:
        if '[PHONE] FRAME ' in ln:
            try: cur = {'meta': json.loads(ln.split('[PHONE] FRAME ',1)[1]), 'buf': []}
            except Exception: cur = None
        elif '[PHONE] FJPEG ' in ln and cur is not None:
            cur['buf'].append(ln.split('[PHONE] FJPEG ',1)[1])
        elif '[PHONE] FEND' in ln and cur is not None:
            groups.append(cur); cur = None
        elif cur is not None and '[PHONE-LOG]' in ln and 'end' in ln:
            cur = None
    # longest group per label (regrabs don't re-ship; storms would repeat labels)
    sel = {}
    for g in groups:
        lbl = g['meta'].get('label','')
        if not lbl.startswith('cwc:'): continue
        size = sum(len(b) for b in g['buf'])
        if lbl not in sel or size > sum(len(b) for b in sel[lbl]['buf']):
            sel[lbl] = g
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for lbl in sorted(sel, key=lambda l: (int(re.search(r':r(\d+):', l).group(1)),
                                          1000 if ':master' in l else int(l.split(':p')[1]))):
        g = sel[lbl]; m = g['meta']
        raw = base64.b64decode(''.join(g['buf']))
        assert raw[:2] == b'\xff\xd8' and raw[-2:] == b'\xff\xd9', lbl + ' not full jpg'
        name = lbl.replace(':', '_') + '.jpg'
        (OUT / name).write_bytes(raw)
        img = Image.open(OUT / name); img.load()
        jL, jR, yl, yr = tear_score(img)
        flag_shipped = bool(m.get('torn'))
        rows.append(dict(label=lbl, t=m.get('t'), bytes=len(raw),
                         EOI=True, pil=True,
                         WH_ok=(img.size == (int(m['W']), int(m['H']))),
                         torn_shipped=flag_shipped,
                         tm_shipped=m.get('tm'),
                         jL=round(jL,1), jR=round(jR,1), yl=yl, yr=yr))
    (OUT / 'scored.json').write_text(json.dumps(rows, indent=1))
    planes = [r for r in rows if ':p' in r['label']]
    import collections
    runs = collections.defaultdict(list)
    for r in planes:
        runs[r['label'].split(':')[1]].append(r)
    print('frames:', len(rows), '(planes', len(planes), '+ masters', len(rows)-len(planes), ')')
    for rn in sorted(runs, key=lambda x: int(x[1:])):
        rs = runs[rn]
        print(f'  {rn}: {len(rs)} planes  jL max {max(r["jL"] for r in rs)}  jR max {max(r["jR"] for r in rs)}')
    # hygiene flags
    bad = [r for r in rows if not (r['EOI'] and r['pil'] and r['WH_ok'])]
    print('hygiene failures:', len(bad))
    # flag agreement: shipped torn flag vs offline score (shipped thrRed=8 via |0)
    mism = []
    for r in planes:
        torn_offline = (r['jL'] >= THR_JL) or (r['jR'] >= THR_JR_SHIPPED)
        if torn_offline != r['torn_shipped']:
            mism.append(r)
    print('flag-vs-offline disagreements:', len(mism))
    for r in mism[:10]:
        print(f"   {r['label']} shipped={r['torn_shipped']} tm={r['tm_shipped']} offline jL={r['jL']} jR={r['jR']}")
    # everything above nominal thresholds
    hot = [r for r in planes if r['jL'] >= THR_JL or r['jR'] >= THR_JR_SHIPPED]
    print('planes >= shipped thresholds (jL26 / jR8):', len(hot))
    for r in sorted(hot, key=lambda r: -max(r['jL'], r['jR'])):
        print(f"   {r['label']}  jL={r['jL']} jR={r['jR']}  y={r['yl']}/{r['yr']}  shippedFlag={r['torn_shipped']}")
    # clean distribution
    cl = [r for r in planes if r not in hot]
    print(f'clean planes: {len(cl)}  jL max {max(r["jL"] for r in cl)}  jR max {max(r["jR"] for r in cl)}')

if __name__ == '__main__':
    main()