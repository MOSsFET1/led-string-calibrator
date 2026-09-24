#!/usr/bin/env python3
"""S13F validation (thr=30 + luma-weighted centroid, fresh reload).

Sequence: STAT gate -> run 1 (map build, singles expected) -> 3 batched
runs. Judge: foundN, fallback counts/heads vs the S13E baselines
(thr15 bright room: 7-8 fallbacks, impostor heads 37-42+56; thr30 ladder:
4 fallbacks, heads 20/38/39/44), page-internal durations.
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
    print('no port answers STAT with', EXPECT_BUILD, '— reload the page first', flush=True)
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
    run = BASE / 'runs' / ('s13f-val-' + time.strftime('%Y%m%d-%H%M%S'))
    run.mkdir(parents=True, exist_ok=True)
    print('S13F validation ->', run, flush=True)

    # compiled defaults are standing on a fresh reload; verify via the start line
    rd(0.3)
    before = evid_now()
    t0 = time.time()
    ok, e = trigger_scan(None)          # RUN 1: map build (singles expected)
    print(f'R1: {"OK" if ok else "FAILED"} wall={time.time()-t0:.0f}s', flush=True)
    for i in (2, 3, 4):
        t0 = time.time()
        ok, e = trigger_scan(None)
        found = missed = None
        if e:
            m = re.search(r'\[EVID\] (\{.*\})', e)
            if m:
                try:
                    o = json.loads(m.group(1))
                    found, missed = o.get('foundN'), o.get('missed')
                except Exception:
                    pass
        print(f'R{i}: {"OK" if ok else "FAILED"} wall={time.time()-t0:.0f}s foundN={found} missed={missed}', flush=True)
        time.sleep(2)

    rd(0.4)
    ser.write(b'LOGP\n')
    lines = rd(60, '[PHONE-LOG] end')
    (run / 'all_log.txt').write_text('\n'.join(lines) + '\n')
    print('\nper-run summary (page-internal):', flush=True)
    starts = [k for k, l in enumerate(lines) if 'hole survey start' in l]
    for gi, k in enumerate(starts):
        nxt = starts[gi + 1] if gi + 1 < len(starts) else len(lines)
        seg = lines[k:nxt]
        start = next((x for x in seg if 'hole survey start' in x), '')
        mthr = re.search(r'thr=(\d+)', start)
        mode = next((x for x in seg if re.search(r'hole (singles|batched) run', x)), '')
        fb = []
        for x in seg:
            if 'ambiguous' in x and 'batch [' in x:
                mm = re.search(r'batch \[(\d+),', x)
                if mm:
                    fb.append(int(mm.group(1)))
        done = next((x for x in seg if 'hole survey done' in x), '')
        md = re.search(r'(\d+) found, (\d+) missed in (\d+)s misses: ?([0-9,]*)', done)
        print(f"  #{gi+1} thr={mthr.group(1) if mthr else '?'} "
              f"{('batched' if 'batched' in mode else 'singles') if mode else '?':>7} "
              f"dur={md.group(3) if md else '?'}s found={md.group(1) if md else '?'} "
              f"missed={md.group(2) if md else '?'} fallbacks={len(fb)}"
              + (f' heads={sorted(set(fb))[:14]}' if fb else ''), flush=True)
    print('S13F VALIDATION COMPLETE', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())