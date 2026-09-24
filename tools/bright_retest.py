#!/usr/bin/env python3
"""Bright-room retest on S13E (system python3).

Lighting regime changed (operator turned the lights on). Protocol:
b200 x2 -> b255 x2, all with holeDimNext=0 (standing), EV-change verified,
frames pulled per phase for offline impostor analysis. Judge: foundN,
fallback heads (NEW heads vs the dark-room deterministic set = the
impostor signature), per-run blob counts, d10 margins, timing.
"""
import json, re, sys, time
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
BASE = Path(__file__).resolve().parents[1]
EXPECT_BUILD = 'S13E-1900'

import serial as _serial

def find_port():
    """Auto-detect the survey box: try every ACM/USB port, STAT each, keep
    the one that answers ALIVE with the expected build (the port index
    changes on USB re-enumeration — ttyACM0 became ttyACM1 mid-session)."""
    import glob
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
    print('no serial port answers STAT with', EXPECT_BUILD, flush=True)
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

def frame_pull(tag, run):
    rd(0.4)
    ser.write(b'FRAMP\n')
    fr = rd(200, '[PHONE-LOG] end')
    (run / (tag + '_frames.txt')).write_text('\n'.join(fr) + '\n')
    return sum(1 for l in fr if 'FRAME {' in l)

def main():
    run = BASE / 'runs' / ('hole-bright-' + time.strftime('%Y%m%d-%H%M%S'))
    run.mkdir(parents=True, exist_ok=True)
    print('bright-room retest ->', run, flush=True)

    s = stat_alive()
    print(s, flush=True)
    if 'ALIVE' not in s or EXPECT_BUILD not in s:
        print('page not alive on ' + EXPECT_BUILD + ' — reload first', flush=True)
        return 1

    # standing payload, dim explicitly OFF (the A/B left it 0, this is idempotent)
    rd(0.3)
    ser.write(b'CFG={"holeGap":100,"mergeR":6,"holeThr":15,"holeB":200,"holeBatch":2,"holeDimNext":0}\n')
    time.sleep(2.5)
    rd(0.4)

    PLAN = [
        ('b200_r1', None),
        ('b200_r2', None),
        ('b255_r1', {'holeB': 255}),
        ('b255_r2', None),
    ]
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
        if ok:
            nf = frame_pull(tag, run)
            print('  frames:', nf, flush=True)
        st = stat_alive()
        if 'ALIVE' not in st:
            print('PAGE DIED — stopping', flush=True)
            break
        time.sleep(2)

    # restore standing brightness
    rd(0.3)
    ser.write(b'CFG={"holeB":200}\n')
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
    print('BRIGHT-ROOM RETEST COMPLETE', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())