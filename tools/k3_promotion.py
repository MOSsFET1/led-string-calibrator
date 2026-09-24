#!/usr/bin/env python3
"""K=3 promotion burst for S13C: full standing payload, up to 3 runs.
Promotion rule: the LAST TWO batched runs both 150/150 with <10 fallbacks.
Run 1 after a reload is necessarily singles (map build) and doesn't count.
"""
import json, re, sys, time
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
BASE = Path(__file__).resolve().parents[1]
ser = serial.Serial(PORT, 115200, timeout=0.2)

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

def evid_now():
    ser.write(b'EV\n')
    return next((l for l in rd(2.0) if '[EVID]' in l), '')

def log_pull():
    rd(0.4)
    ser.write(b'LOGP\n')
    return rd(40, '[PHONE-LOG] end')

run = BASE / 'runs' / ('hole-k3promo-' + time.strftime('%Y%m%d-%H%M%S'))
run.mkdir(parents=True, exist_ok=True)
print('promo burst ->', run, flush=True)

ser.write(b'STAT\n')
st = next((l for l in rd(3.2) if '[STAT]' in l), '')
print(st, flush=True)
if 'S13D-1900' not in st or 'ALIVE' not in st:
    print('NOT GATED — page must be alive on S13D', flush=True)
    sys.exit(1)

# full standing payload (idempotent whether or not the page was reloaded)
rd(0.3)
ser.write(b'CFG={"holeGap":100,"mergeR":6,"holeThr":15,"holeBatch":3}\n')
time.sleep(2.5)
rd(0.4)
cfgline = next((l for l in log_pull() if 'cfg hole' in l.lower()), '(no cfg line)')
print('cfg applied:', cfgline.strip()[:120], flush=True)
if 'holeBatch=3' not in cfgline:
    print('CFG DID NOT TAKE — aborting', flush=True)
    sys.exit(1)

results = []
for i in (1, 2, 3):
    rd(0.3)
    before = evid_now()
    t0 = time.time()
    ser.write(b'SCAN\n')
    got = None
    while time.time() - t0 < 240:
        time.sleep(5)
        e = evid_now()
        if e and e != before and '(none yet)' not in e:
            got = e
            break
    dt = time.time() - t0
    foundN, missed = None, None
    if got:
        o = json.loads(re.search(r'\[EVID\] (\{.*\})', got).group(1))
        foundN, missed = o['foundN'], o['missed']
    print(f'RUN {i}: wall {dt:.0f}s foundN={foundN} missed={missed}', flush=True)
    lines = log_pull()
    (run / f'run{i}_log.txt').write_text('\n'.join(lines) + '\n')
    gates = [k for k, l in enumerate(lines) if re.search(r'hole (singles|batched) run', l)]
    fb = 0
    if gates:
        start = gates[-1]
        nxt = gates[-1]
        # segment = from the LAST gate in this pull to its end (this pull ends with run i)
        fb = sum(1 for l in lines[start:] if 'ambiguous' in l)
        gated = next((l for l in lines[start:] if 'hole batched run' in l), '')
        print(f'   gate: {gated.split("]")[0][-60:]}', flush=True)
    print(f'   fallbacks in segment: {fb}', flush=True)
    results.append((dt, foundN, missed, fb, gated))
    time.sleep(2)

# promotion: last two runs must be BATCHED (gate says batched), 150/150, <10 fallbacks
tail = results[-2:]
ok = all((f == 150 and not m and fbk < 10 and 'batched' in g) for _, f, m, fbk, g in tail)
print('PROMOTION VERDICT:', 'PROMOTE K=3' if ok else 'NOT PROMOTED', flush=True)
for i, (dt, f, m, fbk, g) in enumerate(results, 1):
    print(f'  run{i}: {dt:.0f}s found={f} missed={m} fallbacks={fbk} batched={"batched" in g}', flush=True)
sys.exit(0 if ok else 2)