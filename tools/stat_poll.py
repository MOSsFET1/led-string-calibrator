import sys, time, serial

port = '/dev/ttyACM0'
s = serial.Serial(port, 115200, timeout=0.2)
try:
    for i in range(int(sys.argv[1]) if len(sys.argv) > 1 else 4):
        s.write(b'STAT\n')
        time.sleep(1.2)
        t = s.read(s.in_waiting or 1).decode(errors='replace').strip()
        print(t, flush=True)
        time.sleep(int(sys.argv[2]) if len(sys.argv) > 2 else 8)
finally:
    s.close()