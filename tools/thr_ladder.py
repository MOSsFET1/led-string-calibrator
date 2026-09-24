#!/usr/bin/env python3
"""Threshold ladder on the live S13E page (CFG-only, no rebuild).

Axis: holeThr 22 and 30 at mergeR=6 (thr=15 already measured in the bright
room: 7-8 fallbacks, heads 37-42+56, 149/150). 2 runs per value. Judge:
foundN, fallback count/heads, timing. The winner becomes the compiled
default in S13F (with the luma-weighted centroid).
"""
import json, re, sys, time
from pathlib import Path
import serial

BASE = Path(__file__).resolve().parents[1]
EXPECT_BUILD = 'S13E-1900'

import serial as _serial
import glob

def find_port():
    for cand in sorted(glob.glob('/dev/ttyACM*') + glob.glob('/dev/ttyUSB*')):
        try:
            s = _serial.Serial(cand, 115200, timeout=0.2)
        except Exception:
            continue
        s.write(b'STAT\n')
        end = time.time() + 3.5
        while time.time() < end:
            line = s.readline()
            if line:
                t = line.decode('utf-8', 'replace')
                if '[STAT]' in t:
                    if 'ALIVE' in t and EXPECT_BUILD in t:
                        s.close()
                        return cand
                    break
        s.close()
    return None

PORT = find_port()
if not PORT:
    print('no port answers STAT with', EXPECT_BUILD, flush=True)
    sys.exit(1)
print('serial port:', PORT, flush=True)
ser = _serial.Serial(PORT, 115200, timeout=0.2)

def rd(seconds, endmark=None):
    end = time.time() + seconds
    out = []
    while time.time() < end:
        line = ser.readline()
        if line:
            s = line.decode('utf-8', 'replace').rstrip('\r\n')
            if any(k in s for k in ('esp-tls', 'esp_https', 'httpd_accept')):
                continue
            out.append(s)
            if endmark and endmark in s:
                break
    return out

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

def trigger_scan(cfg=None, wait_s=240):
    rd(0.3)
    before = evid_now()
    payload = b'SCAN\n' if not cfg else (('CFG=' + json.dumps(cfg) + '\n').encode() + b'SCAN\n')
    ser.write(payload)
    time.sleep(0.3)
    rd(0.3)
    deadline = time.time() + wait_s
    while time.time() < deadline:
        time.sleep(5)
        e = evid_now()
        if e and e != before and '(none yet)' not in e:
            return True, e
    return False, None

def main():
    run = BASE / 'runs' / ('hole-thrladder-' + time.strftime('%Y%m%d-%H%M%S'))
    run.mkdir(parents=True, exist_ok=True)
    print('threshold ladder ->', run, flush=True)

    s = stat_alive()
    print(s, flush=True)
    if 'ALIVE' not in s:
        print('page not alive — aborting', flush=True)
        return 1

    PLAN = [('thr22_r1', {'holeThr': 22}), ('thr22_r2', None),
            ('thr30_r1', {'holeThr': 30}), ('thr30_r2', None)]
    rows = []
    for tag, cfg in PLAN:
        t0 = time.time()
        ok, e = trigger_scan(cfg)
        wall = time.time() - t0
        found = missed = None
        if e:
            m = re.search(r'\[EVID\] (\{.*\})', e)
            if m:
                try:
                    o = json.loads(m.group(1))
                    found, missed = o.get('foundN'), o.get('missed')
                except Exception:
                    pass
        print(f'{tag}: {"OK" if ok else "FAILED"} wall={wall:.0f}s foundN={found} missed={missed}', flush=True)
        rows.append({'tag': tag, 'ok': ok, 'wall': round(wall), 'foundN': found, 'missed': missed})
        st = stat_alive()
        if 'ALIVE' not in st:
            print('PAGE DIED — stopping', flush=True)
            break
        time.sleep(2)

    # restore the pre-ladder standing thr (the ladder's last value persists)
    rd(0.3)
    ser.write(b'CFG={"holeThr":15}\n')
    time.sleep(2.0)
    rd(0.4)

    rd(0.4)
    ser.write(b'LOGP\n')
    lines = rd(60, '[PHONE-LOG] end')
    (run / 'all_log.txt').write_text('\n'.join(lines) + '\n')
    print('\nper-run summary (page-internal):', flush=True)
    gates = [(k, l) for k, l in enumerate(lines) if re.search(r'hole (singles|batched) run', l)]
    for gi, (k, gline) in enumerate(gates):
        mode = 'batched' if 'batched' in gline else 'singles'
        nxt = gates[gi + 1][0] if gi + 1 < len(gates) else len(lines)
        seg = lines[k:nxt]
        fb = []
        for x in seg:
            if 'ambiguous' in x and 'batch [' in x:
                mm = re.search(r'batch \[(\d+),', x)
                if mm:
                    fb.append(int(mm.group(1)))
        done = next((x for x in seg if 'hole survey done' in x), '')
        m = re.search(r'(\d+) found, (\d+) missed in (\d+)s', done)
        f, miss, dur = (m.group(1), m.group(2), m.group(3)) if m else ('?', '?', '?')
        print(f'  #{gi+1} {mode:>7} dur={dur}s found={f} missed={miss} fallbacks={len(fb)}'
              + (f' heads={sorted(set(fb))[:14]}' if fb else ''), flush=True)
    (run / 'summary.json').write_text(json.dumps(rows, indent=1) + '\n')
    print('THRESHOLD LADDER COMPLETE', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())