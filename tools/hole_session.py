#!/usr/bin/env python3
"""S13 hole-survey console session (system python3, /dev/ttyACM0).

Usage:
  python3 hole_session.py baseline   # E0 only: standing page defaults (b=200)
  python3 hole_session.py matrix     # brightness x2, gap sweep, masterEvery

Discipline (bench-proven): single-burst CFG+SCAN writes; every combo verified
by an evid CHANGE before its data is pulled; waits measured in minutes (surveys
stall 25s->2min on tired sessions); frame store persists across runs so a
failed trigger's pull is the PREVIOUS run's data — never skip verification.
"""
import json, re, sys, time
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
BASE = Path(__file__).resolve().parents[1]
EXPECT_BUILD = 'S13D-1900'
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

def trigger_and_verify(cfg, wait_s=300):
    """Single-burst CFG+SCAN; verify by evid change; poll up to wait_s."""
    before = evid_now()
    payload = ('CFG=' + json.dumps(cfg) + '\nSCAN\n').encode() if cfg else b'SCAN\n'
    ser.write(payload)
    time.sleep(0.3)
    rd(0.3)
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
    lp = rd(40, '[PHONE-LOG] end')
    (run / (tag + '_log.txt')).write_text('\n'.join(lp) + '\n')
    print(tag, 'log lines', sum(1 for l in lp if '[PHONE]' in l), flush=True)

def summarize_evid(e):
    """Compact one-line summary of a hole evid line for the console."""
    m = re.search(r'\[EVID\] (\{.*\})', e)
    if not m:
        return e[:160]
    try:
        o = json.loads(m.group(1))
        return (f"b={o.get('holeB', o.get('allB'))} found={o.get('foundN')} "
                f"missed={o.get('missedN')} thr={o.get('thr')} domK={o.get('domK')}")
    except Exception:
        return e[:160]

def run_combo(run, tag, cfg, wait_s=300, pull=False):
    e = trigger_and_verify(cfg, wait_s=wait_s)
    if not e:
        print(tag, 'TRIGGER FAILED (no evid change) — see page log before retrigger', flush=True)
        (run / (tag + '_evid.txt')).write_text('FAILED')
        return None
    (run / (tag + '_evid.txt')).write_text(e)
    print(tag, 'OK:', summarize_evid(e), flush=True)
    if pull:
        frame_pull(tag, run)
    return e

def main():
    stage = sys.argv[1] if len(sys.argv) > 1 else 'baseline'
    stamp = time.strftime('%Y%m%d-%H%M%S')
    run = BASE / 'runs' / f'hole-{stage}-{stamp}'
    run.mkdir(parents=True, exist_ok=True)
    print('stage', stage, '->', run, flush=True)

    s = stat()
    print(s, flush=True)
    if 'ALIVE' not in s or EXPECT_BUILD not in s:
        print(f'PAGE NOT ALIVE/{EXPECT_BUILD} — reload the phone page first', flush=True)
        (run / 'ABORTED.txt').write_text(s + '\n')
        return 1

    if stage == 'baseline':
        e = run_combo(run, 'baseline', None)
        if e:
            log_pull('baseline', run)
            frame_pull('baseline', run)
    elif stage == 'matrix':
        # brightness sweep, 2 runs per value (repeats catch flakes)
        for b in (120, 160, 255):
            for rep in (1, 2):
                tag = f'b{b}_r{rep}'
                if not run_combo(run, tag, {'holeB': b}, pull=True):
                    return 1
                st = stat()
                if 'ALIVE' not in st:
                    print('PAGE DIED — stopping', st, flush=True)
                    return 1
                time.sleep(2)
        # gap sweep at standing brightness
        for gap in (150, 600):
            for rep in (1, 2):
                tag = f'gap{gap}_r{rep}'
                if not run_combo(run, tag, {'holeGap': gap}, pull=True):
                    return 1
                time.sleep(2)
        # masterEvery: 0 = the operator's pure original scheme, 25 = sparser
        for me in (0, 25):
            for rep in (1, 2):
                tag = f'me{me}_r{rep}'
                if not run_combo(run, tag, {'holeMasterEvery': me}, pull=True):
                    return 1
                time.sleep(2)
        # restore standing defaults (CFG persists on the page until reload)
        directive('CFG=' + json.dumps({'holeB': 200, 'holeGap': 300,
                                       'holeMasterEvery': 10}), wait=0.4)
        rd(0.4)
        print('standing hole params restored (b=200 gap=300 me=10)', flush=True)
    else:
        print('unknown stage', stage)
        return 1
    print('STAGE COMPLETE', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())