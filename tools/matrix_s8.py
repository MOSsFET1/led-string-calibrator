#!/usr/bin/env python3
"""S8 brightness matrix (day experiment, operator away).

Sweeps accent brightness (accB) at fixed field (allB=160), then field
brightness (allB) at accB=200. Each combo: CFG -> SCAN -> EV pull -> FRAMP
frame pull. Results in runs/matrix-<ts>/ (evid + frames per combo).

Usage: python3 matrix_s8.py
"""
import json, re, time
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
BASE = Path(__file__).resolve().parents[1]
ser = serial.Serial(PORT, 115200, timeout=0.2)

def rd(seconds, endmark=None, keep_filter=True):
    end = time.time() + seconds
    lines = []
    while time.time() < end:
        line = ser.readline()
        if line:
            s = line.decode('utf-8', 'replace').rstrip('\r\n')
            if keep_filter and any(k in s for k in ('esp-tls', 'esp_https', 'httpd_accept')):
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
    for l in rd(3.2, keep_filter=True):
        if '[STAT]' in l:
            return l
    return '[STAT] (none)'

def main():
    stamp = time.strftime('%Y%m%d-%H%M%S')
    run = BASE / 'runs' / f'matrix-{stamp}'
    run.mkdir(parents=True, exist_ok=True)
    print('matrix run in', run, flush=True)

    s = stat()
    print(s, flush=True)
    if 'ALIVE' not in s or 'S8' not in s:
        print('PAGE NOT ALIVE ON S8 — aborting matrix (no CFG applied)', flush=True)
        (run / 'ABORTED.txt').write_text(s + '\n')
        return 1

    combos = [
        {'accB': 80}, {'accB': 200}, {'allB': 80}, {'allB': 255},
    ]
    results = []
    for i, cfg in enumerate(combos):
        # trigger with EV-based verification: each completed survey OVERWRITES
        # the box's single evid string, so evid-before != evid-after proves the
        # survey ran to completion. Wait up to 150 s (runs can stall to ~120 s).
        def evid_now():
            directive('EV')
            ev = rd(2.0)
            return next((l for l in ev if '[EVID]' in l), '')
        ev_before = evid_now()
        accepted = False
        fr_now = []
        for attempt in (1, 2, 3, 4):
            # ONE serial burst: loop() buffers both lines before the next
            # drv? poll can serve — CFG + directive always travel together
            # (the 0.2 s gap let a poll consume the CFG alone: ~1/3 of runs)
            ser.write(('CFG=' + json.dumps(cfg) + '\nSCAN\n').encode())
            # poll EV until it changes (survey done -> evid ship) or 150 s
            ok = False
            for _ in range(30):
                time.sleep(5)
                e = evid_now()
                if e and e != ev_before and '(none yet)' not in e:
                    ok = True
                    break
            if ok:
                accepted = True
                evid = e
                print('combo', cfg, 'attempt', attempt, 'RAN (evid changed)', flush=True)
                break
            print('combo', cfg, 'attempt', attempt, 'NO evid change — retriggering', flush=True)
        if not accepted:
            print('combo', cfg, 'FAILED after 4 attempts', flush=True)
            results.append({'cfg': cfg, 'evid': 'FAILED: survey never completed'})
            continue
        # frames: pull AFTER completion (no race with an in-flight survey)
        directive('FRAMP', wait=0.5)
        fr_now = rd(150, '[PHONE-LOG] end')
        nf = sum(1 for l in fr_now if 'FRAME' in l and 'FJPEG' not in l)
        print('  frames:', nf, flush=True)
        # reuse the verification pull's frames (they ARE this combo's data)
        (run / f'combo{i}_{list(cfg.keys())[0]}{list(cfg.values())[0]}.txt').write_text(
            evid + '\n\n' + '\n'.join(fr_now) + '\n')
        results.append({'cfg': cfg, 'evid': evid})
        # liveness check between combos
        s = stat()
        if 'ALIVE' not in s:
            print('PAGE DIED at combo', cfg, '— stopping matrix', s, flush=True)
            break
        time.sleep(3)

    (run / 'summary.json').write_text(json.dumps(results, indent=1) + '\n')
    print('MATRIX COMPLETE', len(results), 'combos', flush=True)
    return 0

if __name__ == '__main__':
    import sys
    sys.exit(main())