#!/usr/bin/env python3
"""Overnight detector-tuning sweep for the LED survey POC.

Assumes: box on USB serial (/dev/ttyACM0), phone page OPEN on the box (S6+),
string visible to the camera. Waits (up to 8 h) for the page to come alive,
then sweeps CFG detection params (domKHue x colFloor), running 2 surveys per
combo, pulling the evidence summary after each. Finishes with LOGP + FRAMP
pulls (decision records + frame images). Everything lands in
runs/overnight-<ts>/ for morning analysis.

Gentle by design: LEDs run only during surveys (~10 s each).
Usage: venv/bin/python3 overnight_sweep.py [hours]   (default 8)
"""
import json, sys, time
from datetime import datetime
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
BASE = Path(__file__).resolve().parents[1]
MAX_HOURS = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0
ser = serial.Serial(PORT, 115200, timeout=0.2)

def rd(seconds, keep=()):
    end = time.time() + seconds
    out = []
    while time.time() < end:
        line = ser.readline()
        if line:
            s = line.decode('utf-8', 'replace').rstrip('\r\n')
            if any(k in s for k in ('esp-tls', 'esp_https', 'httpd_accept')):
                continue
            out.append(s)
            if keep and any(k in s for k in keep):
                break
    return out

def directive(cmd):
    ser.write((cmd + '\n').encode())
    time.sleep(0.1)

def stat():
    directive('STAT')
    lines = rd(3.2, keep=('[STAT]',))
    for l in lines:
        if '[STAT]' in l:
            return l
    return '[STAT] (no response)'

def main():
    stamp = time.strftime('%Y%m%d-%H%M%S')
    run = BASE / 'runs' / f'overnight-{stamp}'
    run.mkdir(parents=True, exist_ok=True)
    print(f'overnight sweep, results in {run}', flush=True)
    print(stat(), flush=True)

    # wait for the page (up to MAX_HOURS)
    deadline = time.time() + MAX_HOURS * 3600
    alive = False
    while time.time() < deadline:
        s = stat()
        print(time.strftime('%H:%M:%S'), s, flush=True)
        if 'ALIVE' in s:
            if "page build 'S6" in s:
                alive = True
                break
            print('page alive but WRONG BUILD — waiting for reload with S6', flush=True)
        time.sleep(300)
    if not alive:
        print('NEVER SAW S6 PAGE — no sweep run', flush=True)
        (run / 'ABORTED-no-page.txt').write_text('page never came alive with S6\n')
        return 1

    combos = []
    for dkh in (4, 8, 16):
        for cf in (30, 60):
            combos.append({'domKHue': dkh, 'colFloor': cf})
    results = []
    for ci, cfg in enumerate(combos):
        for rep in (1, 2):
            directive('CFG=' + json.dumps(cfg))
            time.sleep(0.5)
            rd(0.5)
            directive('SCAN')
            # survey ~10 s + margin; wait for the evid ship (page logs evid via WS,
            # box only stores; we just wait a fixed window)
            time.sleep(30)
            directive('EV')
            ev = rd(2.0, keep=('[EVID]',))
            fname = run / f'cfg{ci}_dkh{cfg["domKHue"]}_cf{cfg["colFloor"]}_r{rep}.txt'
            fname.write_text('\n'.join(ev) + '\n')
            one = next((l for l in ev if '[EVID]' in l), '')
            print(cfg, 'r' + str(rep), (one[7:170] if len(one) > 7 else 'no evid'), flush=True)
            results.append({'cfg': cfg, 'rep': rep, 'evid': [l for l in ev if '[EVID]' in l]})
            # if the page died mid-sweep, stop the sweep (but still do pulls)
            directive('STAT')
            s = rd(3.2, keep=('[STAT]',))
            st = next((l for l in s if '[STAT]' in l), '')
            if 'GONE' in st:
                print('page went away mid-sweep at', cfg, flush=True)
                break
        else:
            continue
        break

    (run / 'summary.json').write_text(json.dumps(results, indent=1) + '\n')

    # final pulls: decision records + frames
    directive('LOGP')
    logcap = rd(20, keep=('[PHONE-LOG] end',))
    (run / 'logpull.txt').write_text('\n'.join(logcap) + '\n')
    print('log pull:', sum(1 for l in logcap if '[PHONE]' in l), 'lines', flush=True)
    directive('FRAMP')
    framecap = rd(60, keep=('[PHONE-LOG] end',))
    (run / 'frames.txt').write_text('\n'.join(framecap) + '\n')
    print('frame pull:', sum(1 for l in framecap if 'FJPEG' in l), 'chunks', flush=True)
    print('SWEEP COMPLETE', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())