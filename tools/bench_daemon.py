#!/usr/bin/env python3
"""S14L bench daemon — persistent-serial capture for the auto-uploading page.

The page (S14L+) auto-ships every burst ~1 s after it ends (Burst button or
the BURST directive). The box only prints the page's logc chunks inside an
armed window; LOGA (new firmware) arms it PERSISTENTLY, so this daemon holds
the serial port, captures everything, decodes frames to JPEGs, and executes
trigger commands dropped in cmds/ (one command per file line: CFG={...} /
BURST / PING / STAT / ...). No operator interaction; the phone just sits on
the page (portrait, screen on, wake lock held).

Usage:
  python3 tools/bench_daemon.py                 # capture everything forever
  python3 tools/bench_daemon.py --decode-only   # decode a finished capture

Outputs (runs/daemon/):
  capture.txt      every serial line, host-timestamped, append-only
  runs/<label>/    per-burst decoded JPEGs + meta.json
  cmds/            drop a text file here with one directive per line
"""
import argparse, base64, io, json, re, sys, time
from pathlib import Path

import serial

BASE = Path(__file__).resolve().parent
ROOT = BASE.parent
RUNS = ROOT / 'runs' / 'daemon'
CAPTURE = RUNS / 'capture.txt'
CMDDIR = RUNS / 'cmds'
PORT = '/dev/ttyACM0'
BAUD = 115200


def host_ts() -> str:
    return time.strftime('%H:%M:%S')


def write_cap(line: str, f):
    f.write(f'[{host_ts()}] {line}\n')
    f.flush()


def open_serial(log):
    """Open the bench port, retrying ~1 min while a USB re-enum settles."""
    last = None
    for attempt in range(30):
        try:
            s = serial.Serial(PORT, BAUD, timeout=0.5)
            time.sleep(0.5); s.reset_input_buffer()
            return s
        except (serial.SerialException, OSError) as e:
            last = e
            log(f'!! serial open retry {attempt+1}/30: {e}')
            time.sleep(2.0)
    print(f'FATAL: {PORT} never opened: {last}', flush=True)
    sys.exit(1)


def arm_persistent(ser, log):
    """Send LOGA (persistent auto-ship arm) + capture the 1 s ack window."""
    ser.write(b'LOGA\n')
    t_end = time.time() + 1.0
    while time.time() < t_end:
        ln = ser.readline()
        if ln:
            log(ln.decode(errors='replace').rstrip())
    ser.reset_input_buffer()


def decode_frames(capture_path: Path, out_root: Path, min_len=8000):
    """Decode [PHONE] FRAME/FJPEG/FEND groups -> per-label JPEG files."""
    frames = []
    cur = None
    if not capture_path.exists():
        return frames
    for line in capture_path.read_text(errors='replace').splitlines():
        body = re.sub(r'^\[\d\d:\d\d:\d\d\] ', '', line)
        if body.startswith('[PHONE] FRAME {'):
            if cur:
                frames.append(cur)
            m = re.search(r'FRAME (\{.*\})', body)
            try:
                meta = json.loads(m.group(1))
            except Exception:
                meta = {'label': f'?{len(frames)}'}
            cur = {'meta': meta, 'label': meta.get('label', '?'), 'b64': []}
        elif body.startswith('[PHONE] FJPEG ') and cur is not None:
            cur['b64'].append(body.split('FJPEG ', 1)[1].strip())
        elif body.startswith('[PHONE] FEND') and cur is not None:
            frames.append(cur)
            cur = None
        elif body.startswith('[PHONE-LOG] end') and cur is not None:
            cur = None   # 0004(g): stream cut mid-frame — NEVER decode a partial
                         # group; it completes on the wire before the next 30 s
                         # tick and the next pass decodes it whole (the old
                         # 'keep what arrived' wrote truncated jpgs and the
                         # skip-if-exists dedup locked the damage in: 33 files,
                         # repaired 04 Oct from the wire)
    # NO trailing-cur flush: an unterminated group is INCOMPLETE by
    # definition — decode only FEND-terminated images.

    written = []
    for fr in frames:
        label = fr['label']
        # runs tagged: cwc:rN:master / cwc:rN:pNN / burst:rN:fK
        m = re.search(r':r(\d+)', label)
        run = m.group(1) if m else '0'
        outdir = out_root / f'run{run}'
        outdir.mkdir(parents=True, exist_ok=True)
        jpg = outdir / (re.sub(r'[:*?"<>|]', '_', label) + '.jpg')
        if jpg.exists() and jpg.stat().st_size >= min_len:
            continue
        try:
            raw = base64.b64decode(''.join(fr['b64']))
            img_ok = len(raw) > min_len and raw[:2] == b'\xff\xd8'
            if img_ok:
                jpg.write_bytes(raw)
                (outdir / (jpg.stem + '.meta.json')).write_text(
                    json.dumps(fr['meta'], indent=1))
                written.append(jpg.name)
            else:
                print(f'  skip {label}: decode {len(raw)} B (incomplete chunk group?)')
        except Exception as e:
            print(f'  skip {label}: {e}')
    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--decode-only', action='store_true')
    args = ap.parse_args()

    if args.decode_only:
        got = decode_frames(CAPTURE, RUNS / 'runs')
        print(f'decoded {len(got)} new frames from {CAPTURE}')
        return 0

    RUNS.mkdir(parents=True, exist_ok=True)
    CMDDIR.mkdir(exist_ok=True)

    print(f'daemon on {PORT}, capture -> {CAPTURE}')
    with CAPTURE.open('a') as f:
        ser = open_serial(lambda s: write_cap(s, f))
        write_cap(f'=== daemon start ===', f)
        try:
            arm_persistent(ser, lambda s: write_cap(s, f))
        except (serial.SerialException, OSError) as e:
            write_cap(f'!! arm failed at startup (port churn?): {e}', f)
            time.sleep(2.0)
            ser = open_serial(lambda s: write_cap(s, f))
            arm_persistent(ser, lambda s: write_cap(s, f))
        while True:
          try:
            line = ser.readline()
            if line:
                s = line.decode(errors='replace').rstrip()
                if s:
                    write_cap(s, f)
                    if 'FRAME {' in s:
                        m = re.search(r'"label":"([^"]*)"', s)
                        print(f'  frame {m.group(1) if m else "?"}', flush=True)
                    elif 'bench pull done' in s or 'bench pull:' in s:
                        write_cap('', f)  # (kept blank for readability)
                        print('  ' + s.split('] ')[-1], flush=True)
          except (serial.SerialException, OSError) as e:
            # USB re-enum / port blip: drop the handle, reconnect, re-arm LOGA.
            # The box keeps running autonomously (arm is box-side persistent, the
            # battery never needs the daemon) — only OUR wire recording pauses.
            write_cap(f'!! serial loop error: {e} — reconnecting', f)
            try: ser.close()
            except Exception: pass
            time.sleep(2.0)
            ser = open_serial(lambda s: write_cap(s, f))
            arm_persistent(ser, lambda s: write_cap(s, f))
            write_cap('!! reconnected + LOGA re-armed', f)
          # execute trigger commands from cmds/
          for cf in sorted(CMDDIR.glob('*.txt')):
            cmds = [c.strip() for c in cf.read_text().splitlines() if c.strip()]
            print(f'executing {cf.name}: {cmds}', flush=True)
            for c in cmds:
                write_cap(f'>> CMD {c}', f)
                ser.write((c + '\n').encode())
                time.sleep(0.8)
                # capture the immediate ack ([DRV] x queued / [CFG] / [STAT])
                t_end = time.time() + (3.5 if c.startswith('STAT') else 1.5)
                while time.time() < t_end:
                    ln = ser.readline()
                    if ln:
                        write_cap(ln.decode(errors='replace').rstrip(), f)
                time.sleep(0.4); ser.reset_input_buffer()
            cf.unlink()   # one-shot
            if any(c.strip() == 'BURST' for c in cmds):
                print('  BURST sent (auto-ship follows ~1 s after the burst)', flush=True)
          # periodic decode of any completed frame groups
          if int(time.time()) % 30 == 0:
            try:
                decode_frames(CAPTURE, RUNS / 'runs')
            except Exception:
                pass
            time.sleep(1.0)


if __name__ == '__main__':
    sys.exit(main())