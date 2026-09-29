#!/usr/bin/env python3
"""S14P re-pull, ONE unbroken foreground call: PING-flush -> LOGP -> 1.5 s ->
BRAMP -> single patient read. Ends on (frames arrived AND 2nd logend) or 3rd
logend or 600 s. Writes runs/s14p-tripod-led0/cwc_frames.txt."""
import time
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
BAUD = 115200
run_dir = Path('/home/nellie/projects/led-display/POC LED survey/runs/s14p-tripod-led0')
run_dir.mkdir(parents=True, exist_ok=True)
fp = run_dir / 'cwc_frames.txt'

ser = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.3); ser.reset_input_buffer()
ser.write(b'PING\n'); time.sleep(2.0)     # flush any latched directive
ser.reset_input_buffer()
ser.write(b'LOGP\n'); time.sleep(1.5)     # arm + let it consume
ser.reset_input_buffer()
ser.write(b'BRAMP\n')
time.sleep(0.5)

end = time.time() + 600
frames = 0
logends = 0
with fp.open('w') as f:
    while time.time() < end:
        line = ser.readline()
        if not line:
            continue
        s = line.decode(errors='replace').rstrip()
        f.write(s + '\n')
        if '[PHONE] FRAME {' in s:
            frames += 1
            print(f'frame {frames} {time.strftime("%H:%M:%S")}', flush=True)
        if '[PHONE-LOG] end' in s:
            logends += 1
            if frames > 0 and logends >= 2:
                print('end-of-stream after frames', flush=True)
                break
            if logends >= 3:
                print('giving up (3 stream ends, no frames)', flush=True)
                break
ser.close()
print(f'TOTAL {frames} frames -> {fp}')