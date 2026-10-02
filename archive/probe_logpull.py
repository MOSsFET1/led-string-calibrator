#!/usr/bin/env python3
"""Pull the page's log ring via LOGP (no BRAMP). Evidence for the 13:0x-13:5x era."""
import sys, time, serial, pathlib

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
out = pathlib.Path(__file__).parent / 'page_log_pull.txt'
budget = 40

ser = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.3); ser.reset_input_buffer()
ser.write(b'PING\n')          # clear drv slot
time.sleep(1.0); ser.reset_input_buffer()
ser.write(b'LOGP\n')          # arm window + page ships its log ring
end = time.time() + budget
lines = 0
with out.open('w') as f:
    while time.time() < end:
        ln = ser.readline()
        if not ln:
            continue
        s = ln.decode(errors='replace').rstrip()
        f.write(s + '\n'); lines += 1
        if '[PHONE-LOG] end' in s:
            break
ser.close()
print(f'captured {lines} lines -> {out}')