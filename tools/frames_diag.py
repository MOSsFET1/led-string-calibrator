#!/usr/bin/env python3
"""Inspect a cwc_frames.txt capture: structural counts + a per-label break
diagnosis. Read-only; run under the hermes venv python.

Usage: venv python3 frames_diag.py <run_dir>/cwc_frames.txt
"""
import base64, io, json, re, sys
from pathlib import Path
from PIL import Image

fp = Path(sys.argv[1])
text = fp.read_text(errors='replace')
lines = text.splitlines()

n_frame = sum(1 for l in lines if '[PHONE] FRAME {' in l)
n_fjpeg = sum(1 for l in lines if l.startswith('[PHONE] FJPEG '))
n_fend = sum(1 for l in lines if l.strip() == '[PHONE] FEND')
n_logend = sum(1 for l in lines if '[PHONE-LOG] end' in l)
n_bstats = sum(1 for l in lines if '[PHONE] BSTATS' in l)
n_cwcstats = sum(1 for l in lines if '[PHONE] CWCSTATS' in l)
print(f'FRAME={n_frame} FJPEG={n_fjpeg} FEND={n_fend} logend={n_logend} '
      f'BSTATS={n_bstats} CWCSTATS={n_cwcstats} total_lines={len(lines)}')

# head/tail shape: where does the stream START relative to the first FRAME?
first_frame = next((i for i, l in enumerate(lines) if '[PHONE] FRAME {' in l), None)
first_fjpeg = next((i for i, l in enumerate(lines) if l.startswith('[PHONE] FJPEG ')), None)
first_fend = next((i for i, l in enumerate(lines) if l.strip() == '[PHONE] FEND'), None)
last_fend = next((len(lines)-1-i for i, l in enumerate(reversed(lines))
                  if l.strip() == '[PHONE] FEND'), None)
print(f'first FRAME at line {first_frame}, first FJPEG at {first_fjpeg}, '
      f'first FEND at {first_fend}, last FEND at {last_fend}')
if first_frame is None:
    print('NO FRAME headers at all')

# per-label FJPEG count (stream structure per frame)
cur = None
per = {}
for l in lines:
    m = re.search(r'FRAME (\{.*\})', l)
    if '[PHONE] FRAME {' in l and m:
        try:
            cur = json.loads(m.group(1)).get('label', '?')
        except Exception:
            cur = '?PARSE?'
        per.setdefault(cur, {'fj': 0, 'fend': 0, 'b64len': 0})
    elif l.startswith('[PHONE] FJPEG ') and cur:
        per[cur]['fj'] += 1
        per[cur]['b64len'] += len(l.split('FJPEG ', 1)[1].strip())
    elif l.strip() == '[PHONE] FEND' and cur:
        per[cur]['fend'] += 1
        cur = '?ORPHAN?'
print('\nper-label stream (first 26):')
for k, v in list(per.items())[:26]:
    ok = v['fend'] == 1
    print(f'  {k:>18}: FJPEG={v["fj"]:>3} b64={v["b64len"]:>7} FEND={v["fend"]} {"" if ok else "  <-- truncated/incomplete"}')

# decode every FJPEG stream and verify PIL can open it
print('\ndecode check:')
cur, b64 = None, []
decoded = 0
fails = 0
for l in lines:
    m = re.search(r'FRAME (\{.*\})', l)
    if '[PHONE] FRAME {' in l and m:
        cur = l
        b64 = []
    elif l.startswith('[PHONE] FJPEG ') and cur:
        b64.append(l.split('FJPEG ', 1)[1].strip())
    elif l.strip() == '[PHONE] FEND' and cur:
        raw = base64.b64decode(''.join(b64)) if b64 else b''
        try:
            img = Image.open(io.BytesIO(raw)); img.load()
            decoded += 1
            cur_lab = re.search(r'"label":\s*"([^"]*)"', cur)
            lab = cur_lab.group(1) if cur_lab else '?'
            if decoded <= 3 or decoded % 10 == 0:
                print(f'  ok {lab:>18} {img.size}')
        except Exception as e:
            fails += 1
            print(f'  FAIL {cur[:70]}: {e}')
        cur, b64 = None, []
print(f'decoded {decoded} ok, {fails} failed')