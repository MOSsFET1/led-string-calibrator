#!/usr/bin/env python3
"""Pull the bench frame store via the SETTLED recipe (S14-BENCH-SESSION,
29 Sep): LOGA (persistent arm) -> BRAMP -> patient read. LOGA removes the
LOGP 15 s window race entirely: the arm stays open between directives and
across the whole ship.

LOGA keeps the window open after the ship's logend, so run this BEFORE
BRAMP and send LOGX when done (or leave armed for a bench daemon).

Usage: venv python3 pull_loga.py <run_dir> [--logx]
Writes <run_dir>/cwc_frames.txt + a one-line summary (frames/fends/labels).
"""
import base64, io, json, re, sys, time
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'

def main():
    run_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('runs/pull-' + time.strftime('%Y%m%d-%H%M%S'))
    do_logx = '--logx' in sys.argv
    run_dir.mkdir(parents=True, exist_ok=True)
    fp = run_dir / 'cwc_frames.txt'

    ser = serial.Serial(PORT, 115200, timeout=0.5)
    time.sleep(0.5); ser.reset_input_buffer()
    ser.write(b'LOGA\n')
    time.sleep(1.5)
    ack = ser.read(ser.in_waiting or 1).decode(errors='replace').strip()
    if 'LOGA' not in ack:
        print(f'WARN: no LOGA ack (got {ack[:80]!r}) — continuing anyway')
    ser.reset_input_buffer()

    ser.write(b'BRAMP\n')
    print(f'[{time.strftime("%H:%M:%S")}] BRAMP queued — patient read (up to 420 s)', flush=True)

    frames = fends = 0
    meta, b64, labels = None, [], []
    with fp.open('w') as f:
        end = time.time() + 420
        while time.time() < end:
            ln = ser.readline()
            if not ln:
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
                print(f'\rframe {frames}', end='', flush=True)
            elif s.startswith('[PHONE] FJPEG ') and meta:
                b64.append(s.split('FJPEG ', 1)[1].strip())
            elif '[PHONE] FEND' in s:
                fends += 1
                if meta and b64:
                    raw = base64.b64decode(''.join(b64))
                    try:
                        img = Image.open(io.BytesIO(raw)).convert('RGB')
                        img.load()
                        labels.append((meta.get('label', '?'), img.size))
                    except Exception as e:
                        print(f'\ndecode fail {meta.get("label")}: {e}', flush=True)
                meta, b64 = None, []
            elif '[PHONE] BSTATS' in s:
                print('\nBSTATS seen', flush=True)
            elif '[PHONE] CWCSTATS' in s:
                print('\nCWCSTATS seen', flush=True)
            elif '[PHONE-LOG] end' in s and fends > 0:
                print('\nship logend — done', flush=True)
                break
        else:
            print('\npull TIMED OUT at 420 s', flush=True)
    if do_logx:
        ser.write(b'LOGX\n')
        time.sleep(0.5)
    ser.close()
    labs = [l for l, _ in labels]
    uniq = sorted(set(labs))
    print(f'DONE frames={frames} fends={fends} decoded_imgs={len(labels)} '
          f'-> {fp}')
    print('labels:', ', '.join(uniq[:24]) + (' ...' if len(uniq) > 24 else ''))
    return 0

try:
    from PIL import Image
except ImportError:
    sys.exit('needs the hermes venv python (PIL)')

if __name__ == '__main__':
    sys.exit(main())