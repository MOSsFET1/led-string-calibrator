#!/usr/bin/env python3
"""Overnight mergeR ladder at thr=60 (operator's overnight grant, dim room).

Standing payload: thr=60, holeB=200, holeGap=100, holeBatch=3, dim off.
Sweep: mergeR 2 / 3 / 4 / 6 (6 = current standing) x 3 runs each,
EV-change verified, port auto-detect, ring pull + per-run segmentation
at the end. Judge per value: foundN (must be 149-150), fallbacks,
duration. Also captures pk distributions to watch hole-peak drift.
"""
import json, re, sys, time
from pathlib import Path
import glob
import serial as _serial

BASE = Path(__file__).resolve().parents[1]
EXPECT_BUILD = 'S13F-1900'

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
    run = BASE / 'runs' / ('merger-ladder-' + time.strftime('%Y%m%d-%H%M%S'))
    run.mkdir(parents=True, exist_ok=True)
    print('overnight mergeR ladder ->', run, flush=True)

    s = stat_alive()
    print(s, flush=True)
    if 'ALIVE' not in s:
        print('page not alive at sweep start — aborting', flush=True)
        return 1

    # standing payload with mergeR=2 first
    rd(0.3)
    ser.write(b'CFG={"holeGap":100,"holeThr":60,"holeB":200,"holeBatch":3,"holeDimNext":0,"mergeR":2}\n')
    time.sleep(2.5)
    rd(0.4)

    PLAN = []
    for mr in (2, 3, 4, 6):
        for rep in (1, 2, 3):
            PLAN.append((f'mr{mr}_r{rep}', None if rep > 1 else {'mergeR': mr}))

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
            print('PAGE DIED — recording and stopping', flush=True)
            break
        time.sleep(2)

    (run / 'summary.json').write_text(json.dumps(rows, indent=1) + '\n')
    rd(0.4)
    ser.write(b'LOGP\n')
    lines = rd(60, '[PHONE-LOG] end')
    (run / 'all_log.txt').write_text('\n'.join(lines) + '\n')
    print('\nper-run summary (page-internal, ring window — later runs overflow):', flush=True)
    starts = [k for k, l in enumerate(lines) if 'hole survey start' in l]
    for gi, k in enumerate(starts):
        nxt = starts[gi + 1] if gi + 1 < len(starts) else len(lines)
        seg = lines[k:nxt]
        start = next((x for x in seg if 'hole survey start' in x), '')
        mthr = re.search(r'thr=(\d+)', start)
        fb = []
        for x in seg:
            if 'ambiguous' in x and 'batch [' in x:
                mm = re.search(r'batch \[(\d+),', x)
                if mm:
                    fb.append(int(mm.group(1)))
        done = next((x for x in seg if 'hole survey done' in x), '')
        md = re.search(r'(\d+) found, (\d+) missed in (\d+)s misses: ?([0-9,]*)', done)
        pks = sorted(int(m.group(1)) for x in seg for m in [re.search(r'FOUND pk(\d+)', x)] if m)
        print(f"  #{gi+1} thr={mthr.group(1) if mthr else '?'} dur={md.group(3) if md else '?'}s "
              f"found={md.group(1) if md else '?'} missed=[{md.group(4).strip(',') if md else ''}] "
              f"fallbacks={len(fb)}"
              + (f" heads={sorted(set(fb))[:12]}" if fb else ""), flush=True)
        if pks:
            print(f"    pk min={pks[0]} p10={pks[len(pks)//10]} median={pks[len(pks)//2]}", flush=True)
    print('MERGER LADDER COMPLETE', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())