#!/usr/bin/env python3
"""S14P re-pull with the SETTLED recipe (slot-race aware):
  PING flush -> LOGP#1 (consumed: ring ships, window refreshes, logend closes,
  then 15 s idle-close) -> wait for quiet -> LOGP#2, wait 2.5 s (> poll period,
  guaranteed consumed; its ring-ship re-freshens the window) -> BRAMP (consumed
  at the next poll; benchPull's own chunk cadence then holds the window open
  for the whole ship) -> patient read.
Writes runs/s14p-tripod-led0/cwc_frames.txt + decodes JPEGs live to /tmp."""
import re, json, base64, io, time
from pathlib import Path
import serial
from PIL import Image

RUN = Path('/home/nellie/projects/led-display/POC LED survey/runs/s14p-tripod-led0')
FP = RUN / 'cwc_frames.txt'

def log(m):
    print(f'[{time.strftime("%H:%M:%S")}] {m}', flush=True)

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
time.sleep(0.5); ser.reset_input_buffer()

def drain_until(pred, cap):
    """Read lines (discarding) until pred(line) or cap seconds."""
    end = time.time() + cap
    seen = 0
    while time.time() < end:
        ln = ser.readline()
        if ln:
            s = ln.decode(errors='replace').rstrip()
            seen += 1
            if pred(s):
                return s, seen
    return None, seen

# flush any latched directive
ser.write(b'PING\n'); time.sleep(2.0); ser.reset_input_buffer()

# LOGP#1: ring ship + its logend
ser.write(b'LOGP\n')
s, n1 = drain_until(lambda l: '[PHONE-LOG] end' in l, 40)
log(f'LOGP#1: ring ship {n1} lines, logend={bool(s)}')

# wait for FULL quiet (the 15 s idle window-close is irrelevant once we re-arm,
# but ensure no ship is in flight so BRAMP doesn't interleave)
time.sleep(4)
ser.reset_input_buffer()

# LOGP#2: consumed at the next poll (2.5 s > poll period), re-opens the window
# and triggers another ring ship (chunks keep refreshing it)
ser.write(b'LOGP\n')
time.sleep(2.5)
ser.reset_input_buffer()

# BRAMP: consumed at the next poll; benchPull's CWCSTATS lands inside the
# open window; the ship's own ~20-100 ms chunk cadence holds it open.
ser.write(b'BRAMP\n')
log('BRAMP queued — patient read')

frames = fends = logends = 0
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
        elif '[PHONE-LOG] end' in s:
            logends += 1
            log(f'stream end #{logends} (fends={fends})')
            if logends >= 2 and fends >= 39:
                break
            if logends >= 3:
                break
ser.close()
log(f'DONE headers={frames} fends={fends} decoded={len(imgs)}')

import numpy as np
test = sorted(k for k in imgs if k.startswith('cwc') and 'master' not in k)
tmast = [k for k in imgs if k.startswith('cwc') and 'master' in k]
allon = sorted(k for k in imgs if k.startswith('burst') and ':f' in k)
amast = [k for k in imgs if k.startswith('burst') and k.endswith('master')]
log(f'test planes={len(test)} testmaster={len(tmast)} allon={len(allon)} allon_master={len(amast)}')
if allon:
    a = np.asarray(imgs[allon[0]]).max(axis=2)
    log(f'all-on {allon[0]}: med={np.median(a)} p99={np.percentile(a,99)} px>200={(a>200).sum()}')
    imgs[allon[0]].save('/tmp/s14p_allon.png')
if tmast:
    a = np.asarray(imgs[tmast[0]]).max(axis=2)
    log(f'test master {tmast[0]}: med={np.median(a)} p99={np.percentile(a,99)} px>200={(a>200).sum()}')
    imgs[tmast[0]].save('/tmp/s14p_master.png')
if test:
    imgs[test[0]].save('/tmp/s14p_testp00.png')