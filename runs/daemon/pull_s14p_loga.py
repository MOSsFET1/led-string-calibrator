#!/usr/bin/env python3
"""S14P pull via LOGA (persistent arm — no window race at all):
LOGA -> BRAMP -> patient read of everything. Ends after the ship's logend."""
import re, json, base64, io, time
from pathlib import Path
import serial
from PIL import Image

RUN = Path('/home/nellie/projects/led-display/poc_survey/runs/images/s14p-tripod-led0')
FP = RUN / 'cwc_frames.txt'

def log(m):
    print(f'[{time.strftime("%H:%M:%S")}] {m}', flush=True)

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
time.sleep(0.5); ser.reset_input_buffer()
ser.write(b'LOGA\n')
time.sleep(1.5)
ack = ser.read(ser.in_waiting or 1).decode(errors='replace').strip()
log(f'LOGA: {ack[:60]}')
ser.reset_input_buffer()

ser.write(b'BRAMP\n')
log('BRAMP queued — patient read')

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
        elif ('[PHONE-LOG] end' in s) and fends > 0:
            log('ship logend — done')
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
    log(f'test master: med={np.median(a)} p99={np.percentile(a,99)} px>200={(a>200).sum()}')
    imgs[tmast[0]].save('/tmp/s14p_master.png')
if test:
    imgs[test[0]].save('/tmp/s14p_testp00.png')