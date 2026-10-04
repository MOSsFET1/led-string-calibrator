#!/usr/bin/env python3
"""Ground-truth check: the REAL CLI (tools/cwc_pos_decode.py) decodes each
repaired E4 run dir; per-run id sets, amps, margins must match my driver's
json EXACTLY (parity was proven on run9; this extends it to all four)."""
import base64
import json
import subprocess
from pathlib import Path

REPO = Path('/home/nellie/projects/led-display/poc_survey')
OUTD = REPO / 'runs/daemon/analysis/e4_union'
REP = OUTD / 'repaired'
raw = json.loads((OUTD / 'e4_union_raw.json').read_text())

ok_all = True
for run, tag in [('run8', 'r8'), ('run9', 'r9'),
                 ('run10', 'r10'), ('run11', 'r11')]:
    tmp = Path(f'/tmp/e4_cli_{run}')
    tmp.mkdir(exist_ok=True)
    for jpg in sorted((REP / run).glob(f'cwc_{tag}_*.jpg')):
        (tmp / jpg.name).write_bytes(jpg.read_bytes())
        m1 = REP / run / (jpg.name + '.meta.json')
        m2 = REP / run / jpg.with_suffix('.meta.json').name
        (tmp / (jpg.name + '.meta.json')).write_bytes(
            (m1 if m1.exists() else m2).read_bytes())
    txt = []
    for jpg in sorted((REP / run).glob(f'cwc_{tag}_*.jpg')):
        m1 = REP / run / (jpg.name + '.meta.json')
        m2 = REP / run / jpg.with_suffix('.meta.json').name
        meta = json.loads((m1 if m1.exists() else m2).read_text())
        b64 = base64.b64encode(jpg.read_bytes()).decode()
        txt.append('FRAME ' + json.dumps(meta))
        for i in range(0, len(b64), 76):
            txt.append('FJPEG ' + b64[i:i + 76])
        txt.append('FEND')
    (tmp / f'{tag}_frames.txt').write_text('\n'.join(txt) + '\n')
    out = subprocess.run(
        ['/home/nellie/.hermes/hermes-agent/venv/bin/python3',
         'tools/cwc_pos_decode.py', str(tmp), '--tag', tag, '--n', '600',
         '--save-json'],
        capture_output=True, text=True, cwd=str(REPO))
    cli = json.load(open(tmp / 'ledpos.json'))
    mine = raw[tag]['leds']
    a = {q['led']: (q['cx'], q['cy'], q['amp'], q['margin']) for q in cli}
    b = {q['led']: (q['cx'], q['cy'], q['amp'], q['margin']) for q in mine}
    same = set(a) & set(b)
    d_amp = max((abs(a[i][2] - b[i][2]) for i in same), default=0)
    d_mar = max((abs(a[i][3] - b[i][3]) for i in same), default=0)
    d_xy = max((abs(a[i][0] - b[i][0]) + abs(a[i][1] - b[i][1])
                for i in same), default=0)
    match = (set(a) == set(b)) and d_amp == 0 and d_mar == 0 and d_xy == 0
    ok_all &= match
    print(f"{run}: CLI {len(a)} vs driver {len(b)} | identical sets "
          f"{set(a)==set(b)} | max dAmp {d_amp} dMar {d_mar} dXY {d_xy} "
          f"{'PASS' if match else 'FAIL'}")
print('ALL-RUNS PARITY:', 'PASS' if ok_all else 'FAIL')