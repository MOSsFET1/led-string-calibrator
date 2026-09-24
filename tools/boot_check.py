#!/usr/bin/env python3
"""Boot-verify poc_survey over USB serial: read boot banner, PING, STAT.
Usage: python3 boot_check.py [port]   (default /dev/ttyACM0)"""
import sys, time
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
ser = serial.Serial(PORT, 115200, timeout=0.2)
time.sleep(0.5)
ser.reset_input_buffer()

def read_for(seconds, label):
    end = time.time() + seconds
    got = []
    while time.time() < end:
        line = ser.readline()
        if line:
            s = line.decode('utf-8', 'replace').rstrip('\r\n')
            got.append(s)
            print(s)
    if not got:
        print(f'[{label}] (nothing)')
    return got

time.sleep(3)            # boot
read_for(4, 'banner')
ser.write(b'PING\n')
read_for(1.0, 'ping')
ser.write(b'STAT\n')
read_for(3.5, 'stat')
ser.write(b'EV\n')
read_for(1.0, 'ev')
ser.close()