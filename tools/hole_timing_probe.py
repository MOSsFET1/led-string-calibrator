#!/usr/bin/env python3
"""S13 timing-floor probe: gap 100 x2, 80, 60 at standing b=200, me=10.

Answers the operator's speed question empirically: where does the pair
timing stop being sufficient (first miss / evid anomaly)? Single-burst
triggers, EV-change verification, log + frame pulls per combo.
"""
import json, time
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
BASE = Path(__file__).resolve().parents[1]
ser = serial.Serial(PORT, 115200, timeout=0.2)

def rd(seconds, endmark=None):
    end = time.time() + seconds
    lines = []
    while time.time() < end:
        line = ser.readline()
        if line:
            s = line.decode('utf-8', 'replace').rstrip('\r\n')
            if any(k in s for k in ('esp-tls', 'esp_https', 'httpd_accept')):
                continue
            lines.append(s)
            if endmark and endmark in s:
                break
    return lines

def directive(cmd, wait=0.3):
    ser.write((cmd + '\n').encode())
    time.sleep(wait)

def evid_now():
    directive('EV')
    return next((l for l in rd(2.0) if '[EVID]' in l), '')

def stat_alive():
    directive('STAT')
    out = rd(3.2)
    return next((l for l in out if '[STAT]' in l), '[STAT] (none)')

stamp = time.strftime('%Y%m%d-%H%M%S')
run = BASE / 'runs' / f'hole-timing-{stamp}'
run.mkdir(parents=True, exist_ok=True)
print('timing probe ->', run, flush=True)

s = stat_alive()
print(s, flush=True)
if 'ALIVE' not in s or 'S13A-1900' not in s:
    print('page not alive on S13A — aborting', flush=True)
    raise SystemExit(1)

COMBOS = [('gap100_r1', 100), ('gap100_r2', 100), ('gap80_r1', 80), ('gap60_r1', 60)]
for tag, gap in COMBOS:
    before = evid_now()
    payload = ('CFG={"holeGap":' + str(gap) + '}\nSCAN\n').encode()
    ser.write(payload)
    time.sleep(0.3)
    rd(0.3)
    t0 = time.time()
    got = None
    # allow up to 240 s; a healthy gap-100 run should take ~55-65 s
    while time.time() - t0 < 240:
        time.sleep(5)
        e = evid_now()
        if e and e != before and '(none yet)' not in e:
            got = e
            break
    dt = time.time() - t0
    if not got:
        print(tag, 'TRIGGER FAILED after', int(dt), 's', flush=True)
        (run / (tag + '_evid.txt')).write_text('FAILED')
        st = stat_alive()
        print(st, flush=True)
        if 'ALIVE' not in st:
            print('page dead — stopping', flush=True)
            break
        continue
    (run / (tag + '_evid.txt')).write_text(got)
    print(tag, 'gap', gap, 'wall', int(dt), 's evid:', got[:120], flush=True)
    # log pull gives the per-LED FOUND/miss lines for this run
    directive('LOGP', wait=0.5)
    lp = rd(40, '[PHONE-LOG] end')
    (run / (tag + '_log.txt')).write_text('\n'.join(lp) + '\n')
    # frame pull for offline verification
    directive('FRAMP', wait=0.5)
    fr = rd(150, '[PHONE-LOG] end')
    (run / (tag + '_frames.txt')).write_text('\n'.join(fr) + '\n')
    time.sleep(2)

# restore standing gap
directive('CFG={"holeGap":300}', wait=0.4)
rd(0.4)
print('standing gap restored (300)', flush=True)
print('TIMING PROBE COMPLETE', flush=True)