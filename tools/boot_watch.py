#!/usr/bin/env python3
"""One-shot serial gate: wait for the box's boot banner (post-flash or
post-crash), print the first clean lines, and exit. Run under the venv."""
import time
import serial

PORT = '/dev/ttyACM0'
ser = serial.Serial(PORT, 115200, timeout=0.2)
end = time.time() + 30
saw_banner = False
while time.time() < end:
    ln = ser.readline()
    if not ln:
        continue
    s = ln.decode('utf-8', 'replace').rstrip()
    print(s)
    if 'waiting for download' in s:
        saw_banner = True
        # the ESP32 Roman-boot banner follows within ~0.1-1 s of reset
        end = time.time() + 6
ser.close()
print('---', 'saw boot' if saw_banner else 'no boot banner seen')