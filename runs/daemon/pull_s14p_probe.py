#!/usr/bin/env python3
"""S14P probe pull: LOGA still armed -> BRAMP -> read; report ring labels +
all-on probe frames' luma (is the string lit?)."""
import re, json, base64, io, time
from pathlib import Path
import serial
from PIL import Image
import numpy as np

RUN = Path('/home/nellie/projects/led-display/POC LED survey/runs/s14p-tripod-led0')
FP = RUN / 'probe_frames.txt'

def log(m):
    print(f'[{time.strftime("%H:%M:%S")}] {m}', flush=True)

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
time.sleep(0.5); ser.reset_input_buffer()
ser.write(b'LOGA\n')   # re-arm (a previous LOGA survives logend; harmless)
time.sleep(1.5); ser.reset_input_buffer()
ser.write(b'LOGA\n')   # consume one cleanly... actually LOGA+LOGA: first arms,
# second toggles? No: LOGA arms ON only (LOGX releases). Two LOGAs = still ON.
time.sleep(1.5); ser.reset_input_buffer()

ser.write(b'BRAMP\n')
log('BRAMP queued (LOGA armed)')

frames = fends = 0
meta = None
b64 = []
imgs = {}
with FP.open('w') as f:
    end = time.time() + 420
    while time.time() < end:
        ln = ser.readline()
        if not ln:
            continue
        s = ln.decode(errors='replace').rstrip()
        f.write(s + '\n')
        if '[PHONE] FRAME {' in s:
            m = re.search(r'FRAME (\{.*\})', s)
            try: meta = json.loads(m.group(1))
            except Exception: meta = {'label': '?'}
            b64 = []
            frames += 1
        elif s.startswith('[PHONE] FJPEG ') and meta:
            b64.append(s.split('FJPEG ', 1)[1].strip())
        elif '[PHONE] FEND' in s:
            fends += 1
            if meta and b64:
                raw = base64.b64decode(''.join(b64))
                try:
                    imgs[meta['label']] = Image.open(io.BytesIO(raw)).convert('RGB')
                except Exception as e:
                    log(f'decode fail {meta.get("label")}: {e}')
            meta, b64 = None, []
            if fends % 10 == 0:
                log(f'fends {fends}')
        elif '[PHONE-LOG] end' in s and fends > 0:
            break
ser.close()
log(f'DONE headers={frames} fends={fends} decoded={len(imgs)}')

from collections import Counter
lab = Counter()
for f in imgs:
    m = re.match(r'^(burst|cwc):r(\d+)', f)
    if m:
        lab[f'{m.group(1)}:r{m.group(2)}'] += 1
log('ring runs: ' + ', '.join(f'{k}={v}' for k, v in sorted(lab.items())))

# the NEWEST run = probe (all-on, r3+). Luma-check its f1+ frames.
runs = sorted(lab, key=lambda k: int(k.split('r')[1]))
newest = runs[-1]
probe = sorted(k for k in imgs if k.startswith(newest + ':f') or k.startswith(newest + ' :f'))
probe = sorted(k for k in imgs if re.match(re.escape(newest) + r':f\d+$', k))
log(f'newest run {newest}: {len(probe)} probe frames')
for k in probe[:3]:
    a = np.asarray(imgs[k]).max(axis=2).astype(np.float32)
    log(f'  {k}: med={np.median(a):.0f} p99={np.percentile(a,99):.0f} px>200={(a>200).sum()}')
probe1 = [k for k in probe if k.endswith('f1')]
if probe1:
    imgs[probe1[0]].save('/tmp/s14p_probe_f1.png')
    log('probe f1 saved -> /tmp/s14p_probe_f1.png')