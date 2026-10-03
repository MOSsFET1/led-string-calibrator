#!/usr/bin/env python3
"""RTS->EN pulse to hard-reset the ESP32-C6 (standing TLS-wedge recipe).
Opens ttyACM0 alone (daemon must NOT be running), pulses RTS 0.1 s, exits.
Reads whatever the boot banner prints for 6 s and echoes it for the wire check."""
import serial, time, sys

PORT = "/dev/ttyACM0"
try:
    s = serial.Serial(PORT, 115200, timeout=0.2)
except serial.SerialException as e:
    print("OPEN FAILED:", e)
    sys.exit(2)

s.reset_input_buffer()
s.rts = True
s.dtr = False
time.sleep(0.1)
s.rts = False
print("pulsed RTS->EN")

deadline = time.time() + 6
seen = []
while time.time() < deadline:
    chunk = s.read(4096)
    if chunk:
        seen.append(chunk.decode("utf-8", "replace"))
s.close()
banner = "".join(seen)
keep = [l for l in banner.splitlines() if any(k in l for k in ("rst:", "boot:", "poc_survey", "S14R", "CAL_BUILD"))]
print("banner lines:")
for l in keep[:20]:
    print(" ", l.strip()[:160])