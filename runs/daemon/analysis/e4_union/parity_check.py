#!/usr/bin/env python3
"""Parity gate: my in-decoder 24-plane path must EXACTLY match the real CLI
(tools/cwc_pos_decode.py) on run9 — same ids, sites, amp, margin (float exact
to round-off at amp 0.1 rounding)."""
import base64
import json
import subprocess
import sys
from pathlib import Path

REPO = Path('/home/nellie/projects/led-display/poc_survey')
sys.path.insert(0, str(REPO / 'runs/daemon/analysis/e4_union'))
from e4_union_decode import decode_one, RUNS  # noqa: E402
import numpy as np  # noqa: E402

RUN, TAG = 'run9', 'r9'
# decode the REPAIRED corpus (wire-complete bytes; plain PIL decodes all 25)
SRC = REPO / 'runs/daemon/analysis/e4_union/repaired/run9'
r24 = decode_one(SRC, TAG, 100, drop_planes=())

tmp = Path('/tmp/e4_parity_run9')
tmp.mkdir(exist_ok=True)
txt = []
for jpg in sorted(SRC.glob(f'cwc_{TAG}_*.jpg')):
    meta = json.loads(jpg.with_suffix('.meta.json').read_text())
    b64 = base64.b64encode(jpg.read_bytes()).decode()
    txt.append('FRAME ' + json.dumps(meta))
    for i in range(0, len(b64), 76):
        txt.append('FJPEG ' + b64[i:i + 76])
    txt.append('FEND')
(tmp / f'{TAG}_frames.txt').write_text('\n'.join(txt) + '\n')

out = subprocess.run(
    ['/home/nellie/.hermes/hermes-agent/venv/bin/python3',
     'tools/cwc_pos_decode.py', str(tmp), '--tag', TAG, '--n', '600',
     '--save-json'],
    capture_output=True, text=True, cwd=str(REPO))
print('CLI rc', out.returncode)
print(out.stdout[-800:])
print('STDERR:', out.stderr[-800:])
cli = json.load(open(tmp / 'ledpos.json'))
mine = {q['led']: (q['cx'], q['cy'], q['amp'], q['margin'])
        for q in r24['leds']}
clis = {q['led']: (q['cx'], q['cy'], q['amp'], q['margin']) for q in cli}
print('CLI-confirmed', len(clis), 'mine', len(mine))
same = set(clis) & set(mine)
dm = max(abs(clis[i][2] - mine[i][2]) for i in same)
dmar = max(abs(clis[i][3] - mine[i][3]) for i in same)
dxy = max(abs(clis[i][0] - mine[i][0]) + abs(clis[i][1] - mine[i][1])
          for i in same)
print('parity on', len(same), 'shared: max amp diff', dm,
      'max margin diff', dmar, 'max site L1', dxy)
print('only-mine', sorted(set(mine) - set(clis))[:10],
      'only-CLI', sorted(set(clis) - set(mine))[:10])