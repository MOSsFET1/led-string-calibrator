#!/usr/bin/env python3
"""S14P pull via LOGA (persistent arm — no window race at all):
LOGA -> BRAMP -> patient read of everything. Ends after the ship's logend.
Adapted from runs/daemon/pull_s14p_loga.py for the operator's handheld 1908
burst (late read — the burst ended minutes before this ran)."""
import re, json, base64, io, time
from pathlib import Path
import serial
from PIL import Image

RUN = Path('/home/nellie/projects/led-display/POC LED survey/runs/s14p-1908-handheld')
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