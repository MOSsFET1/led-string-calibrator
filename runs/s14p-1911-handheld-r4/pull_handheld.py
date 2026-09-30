#!/usr/bin/env python3
"""S14P replay pull for s14p-1911 handheld run (box bench ring re-ship).
Retarget of runs/s14p-1908-handheld/pull_handheld.py: new RUN dir, cwc_frames.txt.
NOTE: a LOGA persistent arm is ALREADY ON in the box (armed 19:25 today; LOGX never
sent). We do NOT send LOGA again — go straight to BRAMP re-ship + patient read.
Breaks on the ship's [PHONE-LOG] end after frames arrived."""
import re, json, base64, io, time
from pathlib import Path
import serial
from PIL import Image

RUN = Path('/home/nellie/projects/led-display/POC LED survey/runs/s14p-1911-handheld-r4')
FP = RUN / 'cwc_frames.txt'

def log(m):
    print(f'[{time.strftime("%H:%M:%S")}] {m}', flush=True)

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
time.sleep(0.5); ser.reset_input_buffer()

# Reference recipe step restored (pull_s14p_loga.py behaviour): LOGA first.
# Rationale: the 19:25 arm was set BEFORE the ~19:4x operator power-cycle and
# is RAM state — likely wiped. LOGA on an already-armed box just re-acks.
ser.write(b'LOGA\n')
time.sleep(1.5)
ack = ser.read(ser.in_waiting or 1).decode(errors='replace').strip()
log(f'LOGA: {ack[:60]}')
ser.reset_input_buffer()

ser.write(b'BRAMP\n')

frames = fends = 0
meta = None
b64 = []
imgs = {}
t0 = None
start = time.time()
with FP.open('w') as f:
    end = time.time() + 420
    while time.time() < end:
        ln = ser.readline()
        if not ln:
            continue
        s = ln.decode(errors='replace').rstrip()
        f.write(s + '\n')
        f.flush()
        if t0 is None:
            t0 = time.time()
            log(f'first line after {time.time()-start:.1f}s: {s[:70]}')
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
            log(f'ship logend at t+{time.time()-start:.0f}s — done (fends={fends})')
            break
ser.close()
log(f'DONE headers={frames} fends={fends} decoded={len(imgs)}')
if fends != 19 or frames != 19:
    log(f'NOTE incomplete ship (fends={fends}, frames={frames})')