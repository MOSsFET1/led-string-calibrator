#!/usr/bin/env python3
"""S13 boot check: wait for boot, PING, then STAT. Reusable probe."""
import sys, time
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
ser = serial.Serial(PORT, 115200, timeout=0.2)

def rd(seconds):
    end = time.time() + seconds
    lines = []
    while time.time() < end:
        line = ser.readline()
        if line:
            s = line.decode('utf-8', 'replace').rstrip('\r\n')
            if any(k in s for k in ('esp-tls', 'esp_https', 'httpd_accept')):
                continue
            lines.append(s)
    return lines

print('boot banner:', flush=True)
for l in rd(4):
    if l:
        print(' ', l, flush=True)

ser.write(b'PING\n')
time.sleep(0.4)
rd(0.3)
ser.write(b'STAT\n')
out = rd(3.2)
for l in out:
    if '[STAT]' in l or '[DRV]' in l:
        print(l, flush=True)
ok = any(('S13-1900' in l or 'S14A-1900' in l) and 'ALIVE' in l for l in out)
print('BOOT CHECK:', 'PASS' if ok else 'FAIL', flush=True)
sys.exit(0 if ok else 1)