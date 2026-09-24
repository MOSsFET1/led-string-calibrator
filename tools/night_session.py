#!/usr/bin/env python3
"""Tonight's experiment session (S9, fresh page session).

Sequence: STAT gate -> baseline survey -> darkPair AE-race survey ->
field2 2-colour survey -> accB/allB matrix (single-burst triggers, EV
verification, 200 s windows). All results under runs/night-<ts>/.
Usage: python3 night_session.py
"""
import json
import time
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

def stat():
    directive('STAT')
    for l in rd(3.2):
        if '[STAT]' in l:
            return l
    return '[STAT] (none)'

def evid_now():
    directive('EV')
    return next((l for l in rd(2.0) if '[EVID]' in l), '')

def trigger_and_verify(cfg, wait_s=200):
    """Single-burst CFG+SCAN; verify by evid change; poll up to wait_s."""
    before = evid_now()
    directive('CFG=' + json.dumps(cfg), wait=0.1)
    directive('SCAN', wait=0.1)
    deadline = time.time() + wait_s
    while time.time() < deadline:
        time.sleep(5)
        e = evid_now()
        if e and e != before and '(none yet)' not in e:
            return e
    return None

def frame_pull(tag, run):
    directive('FRAMP', wait=0.5)
    fr = rd(150, '[PHONE-LOG] end')
    nf = sum(1 for l in fr if 'FRAME {' in l)
    nj = sum(1 for l in fr if 'FJPEG ' in l)
    (run / (tag + '_frames.txt')).write_text('\n'.join(fr) + '\n')
    print(tag, 'frames', nf, 'chunks', nj, flush=True)

def log_pull(tag, run):
    directive('LOGP', wait=0.5)
    lp = rd(25, '[PHONE-LOG] end')
    (run / (tag + '_log.txt')).write_text('\n'.join(lp) + '\n')
    print(tag, 'log lines', sum(1 for l in lp if '[PHONE]' in l), flush=True)

def main():
    stamp = time.strftime('%Y%m%d-%H%M%S')
    run = BASE / 'runs' / f'night-{stamp}'
    run.mkdir(parents=True, exist_ok=True)
    print('night session in', run, flush=True)
    s = stat()
    print(s, flush=True)
    import re as _re
    if 'ALIVE' not in s or not _re.search(r"page build 'S\d+-1900'", s):
        print('PAGE NOT ALIVE/STAMPED — aborting', flush=True)
        (run / 'ABORTED.txt').write_text(s + '\n')
        return 1

    # E0: fresh-session baseline at defaults
    directive('CFG={"darkPair":0,"field2":0,"accB":200,"allB":160}', wait=0.4)
    rd(0.5)
    directive('SCAN')
    e = None
    for _ in range(40):
        time.sleep(5)
        e = evid_now()
        if e and '(none yet)' not in e:
            break
    print('baseline evid:', e[7:150] if e else 'NONE', flush=True)
    (run / 'baseline_evid.txt').write_text((e or 'NONE') + '\n')
    frame_pull('baseline', run)

    # E1: dark-pair AE race
    directive('CFG={"darkPair":1}', wait=0.4)
    rd(0.3)
    before = evid_now()
    directive('SCAN')
    ok = False
    for _ in range(48):
        time.sleep(5)
        e = evid_now()
        if e and e != before and '(none yet)' not in e:
            ok = True
            break
    print('darkPair evid:', (e[7:180] if e else 'NONE'), 'ok', ok, flush=True)
    (run / 'darkpair_evid.txt').write_text((e if ok else 'FAILED') + '\n')
    log_pull('darkpair', run)          # grabRaw lines carry exposure per grab
    frame_pull('darkpair', run)

    # E2: 2-colour field (R+G field, detect B on the third channel)
    directive('CFG={"darkPair":0,"field2":1}', wait=0.4)
    rd(0.3)
    before = evid_now()
    directive('SCAN')
    ok = False
    for _ in range(40):
        time.sleep(5)
        e = evid_now()
        if e and e != before and '(none yet)' not in e:
            ok = True
            break
    print('field2 evid:', (e[7:180] if e else 'NONE'), 'ok', ok, flush=True)
    (run / 'field2_evid.txt').write_text((e if ok else 'FAILED') + '\n')
    frame_pull('field2', run)
    directive('CFG={"field2":0}', wait=0.3)   # restore default field

    # E3: brightness matrix on the FRESH session (single-burst, EV-verified)
    for cfg in ({'accB': 80}, {'accB': 255}, {'allB': 80}, {'allB': 255}):
        before = evid_now()
        ser.write(('CFG=' + json.dumps(cfg) + '\nSCAN\n').encode())
        got = None
        for _ in range(40):
            time.sleep(5)
            e = evid_now()
            if e and e != before and '(none yet)' not in e:
                got = e
                break
        print('matrix', cfg, (got[7:170] if got else 'FAILED'), flush=True)
        (run / ('mx_' + ''.join(k + str(v) for k, v in cfg.items()) + '_evid.txt')).write_text(got or 'FAILED')
        if got:
            frame_pull('mx_' + list(cfg.keys())[0] + str(list(cfg.values())[0]), run)
        st = stat()
        if 'ALIVE' not in st:
            print('PAGE DIED — stopping matrix', st, flush=True)
            break
        time.sleep(2)

    # restore defaults
    directive('CFG={"accB":200,"allB":160,"darkPair":0,"field2":0}', wait=0.3)
    print('NIGHT SESSION COMPLETE', flush=True)
    return 0

if __name__ == '__main__':
    import sys
    sys.exit(main())