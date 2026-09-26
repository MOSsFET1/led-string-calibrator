#!/usr/bin/env python3
"""S14 bench pull — the proven recipe as one patient read.

LOGP -> 1 s -> BRAMP -> read until FSTATS captured and >= N frames, or
budget end. No input resets, no mid-pull re-arming, no break on logend
(the log pull's logend arrives BEFORE the frame chunks; breaking on it
kills the pull — that was the reader bug on 25 Sep).

Usage: python3 tools/s14_pull.py <out_file> [budget_s]
"""
import sys, time, serial, pathlib

out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path('runs/pull.txt')
budget = int(sys.argv[2]) if len(sys.argv) > 2 else 540
want = int(sys.argv[3]) if len(sys.argv) > 3 else 20

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1.0)
time.sleep(0.5); ser.reset_input_buffer()
ser.write(b'PING\n')                       # clear drv slot
time.sleep(0.5); ser.reset_input_buffer()
ser.write(b'LOGP\n')                       # arm the 15 s forward window
time.sleep(1.0); ser.reset_input_buffer()
ser.write(b'BRAMP\n')                      # page starts framePull

end = time.time() + budget
n_frame = 0
t0 = time.time()
stats_line = None
with out.open('w') as f:
    while time.time() < end:
        ln = ser.readline()
        if not ln:
            continue
        s = ln.decode(errors='replace').rstrip()
        f.write(s + '\n')
        if 'FRAME {' in s:
            n_frame += 1
            if n_frame % 4 == 0:
                print(f'frame {n_frame} t+{time.time()-t0:.0f}s', flush=True)
        if 'FSTATS' in s and stats_line is None:
            stats_line = s
            print(f'FSTATS at t+{time.time()-t0:.0f}s', flush=True)
        if n_frame >= want and stats_line:
            break
ser.close()

import re
text = out.read_text()
labels = re.findall(r'"label":"([^"]*)"', text)
uniq = sorted(set(labels), key=lambda l: int(l.split('f')[1]) if 'f' in l else 0)
m = re.search(r'FSTATS (\{.*\})', text)
print(f'frames: {len(uniq)} unique / {len(labels)} lines')
print('FSTATS:', (m.group(1)[:400] if m else 'missing'))