#!/usr/bin/env python3
"""S14P full-ring pull (39 frames, 2 runs): the exact sequence that worked.
LOGP -> wait for the ring ship's own logend -> LOGP -> 0.8 s -> BRAMP ->
patient read until 2nd logend after >=39 FENDs (or 3rd logend / timeout)."""
import re, json, base64, io, time
from pathlib import Path
import serial
from PIL import Image

RUN = Path('/home/nellie/projects/led-display/poc_survey/runs/images/s14p-tripod-led0')
FP = RUN / 'cwc_frames.txt'
PORT = '/dev/ttyACM0'

ser = serial.Serial(PORT, 115200, timeout=0.5)
time.sleep(0.5); ser.reset_input_buffer()

def log(msg):
    print(f'[{time.strftime("%H:%M:%S")}] {msg}', flush=True)

# Step 1: LOGP, wait for the ring pull's own logend (sliding window closes)
ser.write(b'LOGP\n')
end = time.time() + 30
ring_lines = 0
while time.time() < end:
    ln = ser.readline()
    if ln:
        s = ln.decode(errors='replace').rstrip()
        ring_lines += 1
        if '[PHONE-LOG] end' in s:
            break
log(f'ring ship: {ring_lines} lines, window closed')

# Step 2: LOGP again, brief gap, then BRAMP (overwrites slot; BRAMP runs)
ser.write(b'LOGP\n')
time.sleep(0.8)
ser.reset_input_buffer()
ser.write(b'BRAMP\n')
log('BRAMP sent (patient read begins)')

# Step 3: patient read, writing capture + decoding frames to JPEGs live
frames = 0
fends = 0
logends = 0
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
            try:
                meta = json.loads(m.group(1))
            except Exception:
                meta = {'label': '?'}
            b64 = []
            frames += 1
        elif s.startswith('[PHONE] FJPEG ') and meta:
            b64.append(s.split('FJPEG ', 1)[1].strip())
        elif '[PHONE] FEND' in s:
            fends += 1
            if meta and b64:
                raw = base64.b64decode(''.join(b64))
                try:
                    im = Image.open(io.BytesIO(raw)).convert('RGB')
                    imgs[meta['label']] = im
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
log(f'DONE headers={frames} fends={fends} decoded={len(imgs)} -> {FP}')

# categorise
test = sorted(k for k in imgs if k.startswith('cwc') and 'master' not in k)
tmast = [k for k in imgs if k.startswith('cwc') and 'master' in k]
allon = sorted(k for k in imgs if k.startswith('burst') and ':f' in k)
amast = [k for k in imgs if k.startswith('burst') and k.endswith('master')]
log(f'test planes={len(test)} testmaster={len(tmast)} allon_f={len(allon)} allon_master={len(amast)}')
import numpy as np
if allon:
    a = np.asarray(imgs[allon[0]]).max(axis=2)
    log(f'all-on {allon[0]}: med={np.median(a)} p99={np.percentile(a,99)} px>200={(a>200).sum()}')
    imgs[allon[0]].save('/tmp/s14p_allon.png')
if test:
    imgs[test[0]].save('/tmp/s14p_testp00.png')
if tmast:
    a = np.asarray(imgs[tmast[0]]).max(axis=2)
    log(f'test master: med={np.median(a)} p99={np.percentile(a,99)} px>200={(a>200).sum()}')
if amast:
    a = np.asarray(imgs[amast[0]]).max(axis=2)
    log(f'all-on master: med={np.median(a)} p99={np.percentile(a,99)} px>200={(a>200).sum()}')