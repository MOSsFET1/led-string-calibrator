#!/usr/bin/env python3
"""One-shot CWC position round (S14P-1903): LOGA -> CFG+BURST -> the page's
own auto-ship IS the pull (no BRAMP), all in ONE serial session.

Why this shape (S14-BENCH-SESSION lessons, 29 Sep):
- LOGA = persistent arm: no 15 s LOGP window race, works across the whole
  capture + ship; LOGX only when the whole round is over.
- The S14L auto-ship fires ~1 s after the burst ends. With LOGA armed the
  ship reaches serial on its own -> no BRAMP at all, so the single
  directive slot (sDrv) is used BY THE BURST and nothing else.
- The port is opened once and closed once: no re-enumeration race.
- Ship-once (S14P-1902/1903 page) means exactly ONE run's frames come back.

Usage: venv python3 run_round.py <run_dir> [--cwc-test-mode 0|1] [--cwc-test-led N]
       [--cwc-n 150] [--b 150] [--comp 0|1] [--max-capture-s 30]
Writes <run_dir>/cwc_frames.txt (+ prints a completeness report).
"""
import argparse, base64, io, json, re, sys, time
from pathlib import Path
import serial
from PIL import Image

PORT = '/dev/ttyACM0'
WANT_FENDS = 25   # master + 24 planes (S14R-0000)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run_dir')
    ap.add_argument('--cwc-test-mode', type=int, default=0)
    ap.add_argument('--cwc-test-led', type=int, default=0)
    ap.add_argument('--cwc-n', type=int, default=200)
    ap.add_argument('--b', type=int, default=150)
    ap.add_argument('--comp', type=int, default=0)
    ap.add_argument('--max-capture-s', type=int, default=30)
    args = ap.parse_args()
    run_dir = Path(args.run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)
    fp = run_dir / 'cwc_frames.txt'

    ser = serial.Serial(PORT, 115200, timeout=0.5)
    time.sleep(0.5); ser.reset_input_buffer()

    # 1. persistent arm FIRST (instant, firmware-side)
    ser.write(b'LOGA\n'); time.sleep(1.0)
    ack = ser.read(ser.in_waiting or 1).decode(errors='replace').strip()
    if 'LOGA' not in ack:
        print(f'WARN: no LOGA ack (got {ack[:80]!r})'); return 1
    print('LOGA armed', flush=True)
    ser.reset_input_buffer()

    # 2. CFG (sCfg slot) + BURST (sDrv slot) in one write; page applies cfg
    #    before the directive on its next poll (<=1.5 s)
    cfg = {'cwc': 1, 'cwcN': args.cwc_n, 'bBurstB': args.b,
           'bComp': args.comp, 'cwcSettle': 100,
           'cwcTestMode': args.cwc_test_mode, 'cwcTestLed': args.cwc_test_led}
    ser.write(('CFG=' + json.dumps(cfg) + '\nBURST\n').encode())
    time.sleep(0.5)
    ack2 = ser.read(ser.in_waiting or 1).decode(errors='replace').strip()
    got = [l for l in ack2.splitlines() if l.startswith('[CFG]') or l.startswith('[DRV]')]
    print('; '.join(got) or f'WARN: no CFG/BURST ack ({ack2[:80]!r})', flush=True)

    # 3. patient read: burst runs ~12 s (capture), auto-ship ~20 s (~1.4 MB)
    frames = fends = 0
    meta, b64, labels = None, [], []
    cwcdec_parts = []      # CWCDEC chunks (in-page decode SITES, S14P-1905)
    cwcdec_summary = None  # CWCDECS summary (gates + counts + k gains)
    cwcstats_ln = None     # CWCSTATS line (chain + decode summary)
    t_start = time.time()
    t_arm_for = None   # extend the deadline when a new FRAME arrives
    with fp.open('w') as f:
        while True:
            ln = ser.readline()
            if not ln:
                # idle tick: give the capture/ship time but never hang forever
                if time.time() - t_start > 100 or (
                        t_arm_for and time.time() > t_arm_for):
                    print('\nTIMEOUT waiting for stream', flush=True)
                    break
                continue
            s = ln.decode(errors='replace').rstrip()
            f.write(s + '\n')
            if '[PHONE] FRAME {' in s:
                m = re.search(r'FRAME (\{.*\})', s)
                try:
                    meta = json.loads(m.group(1)) if m else {'label': '?'}
                except Exception:
                    meta = {'label': '?'}
                b64 = []
                frames += 1
                print(f'\rframe {frames} (fends {fends})', end='', flush=True)
                t_arm_for = time.time() + args.max_capture_s   # inter-frame gap cap
            elif s.startswith('[PHONE] FJPEG ') and meta:
                b64.append(s.split('FJPEG ', 1)[1].strip())
            elif '[PHONE] FEND' in s:
                fends += 1
                if meta and b64:
                    try:
                        raw = base64.b64decode(''.join(b64))
                        img = Image.open(io.BytesIO(raw)); img.load()
                        labels.append((meta.get('label', '?'), img.size))
                    except Exception as e:
                        print(f'\ndecode fail {meta.get("label")}: {e}', flush=True)
                meta, b64 = None, []
            elif '[PHONE] CWCSTATS ' in s:
                cwcstats_ln = s.split('CWCSTATS ', 1)[1].strip()
                print('\nCWCSTATS seen', flush=True)
            elif '[PHONE] CWCDEC ' in s:
                # chunked in-page decode SITES list: reassemble in order
                cwcdec_parts.append(s.split('CWCDEC ', 1)[1].strip())
            elif '[PHONE] CWCDECS ' in s:
                try:
                    cwcdec_summary = json.loads(s.split('CWCDECS ', 1)[1].strip())
                except Exception as e:
                    print(f'WARN: CWCDECS parse failed: {e}')
            elif '[PHONE] BSTATS' in s:
                print('\nBSTATS seen', flush=True)
            elif '[PHONE-LOG] end' in s and fends >= 5:
                print('\nship logend — stream done', flush=True)
                break
    ser.close()
    # the gate analyser keys its TEST-MODE branch off cwc_stats.json
    # (testMode/testBits); run_round is the pull for the gate sequence, so
    # it owns writing this file (S14P-1904 fix: it was printed + discarded).
    if cwcstats_ln:
        try:
            (run_dir / 'cwc_stats.json').write_text(
                json.dumps(json.loads(cwcstats_ln), indent=1))
            print('cwc_stats.json written')
        except Exception as e:
            print(f'WARN: cwc_stats parse failed: {e}')
    else:
        print('WARN: no CWCSTATS in stream (page < 1904?)')
    if cwcdec_parts or cwcdec_summary:
        # S14P-1905 shape: CWCDEC chunks = the SITES list; CWCDECS = summary.
        dec_out = {}
        if cwcdec_parts:
            try:
                dec_out['sites'] = json.loads(''.join(cwcdec_parts))
            except Exception as e:
                print(f'WARN: CWCDEC reassembly failed: {e}')
        if cwcdec_summary:
            dec_out.update(cwcdec_summary)
        if dec_out:
            (run_dir / 'cwc_dec.json').write_text(json.dumps(dec_out, indent=1))
            sites = dec_out.get('sites', [])
            print(f'in-page decode: {len(sites)} sites '
                  f'(amp>={dec_out.get("ampGate")}, margin>={dec_out.get("marginGate")}) '
                  f'-> cwc_dec.json')
    ok = [l for l, _ in labels]
    print(f'DONE frames={frames} fends={fends} decoded={len(ok)} -> {fp}')
    have = sorted(int(l.split(':p')[1]) for l in ok if ':p' in l)
    missing = [p for p in range(24) if p not in have]
    nmaster = len([l for l in ok if l.endswith('master')])
    print(f'planes {len([p for p in have if 0 <= p < 24])}/24, '
          f'master {nmaster}/1'
          + (f'  MISSING: {missing}' if missing or nmaster == 0 else ''))
    if len(ok) < WANT_FENDS:
        print('INCOMPLETE — see the counts above')
        return 1
    # completeness: every frame must have exactly 1 FEND (no truncation)
    print('labels unique:', len(set(ok)) == len(ok), f'({len(ok)} decoded)')
    return 0


if __name__ == '__main__':
    sys.exit(main())