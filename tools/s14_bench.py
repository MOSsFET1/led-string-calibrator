#!/usr/bin/env python3
"""S14 burst-bench driver — frame-rate + handheld motion (Oliver, Sep 25 2026).

Drives the box over serial: BURST (page runs an all-on burst, N frames,
pacing via CFG) then BRAMP (page ships full-resolution frames + BSTATS).
No phone-side interaction needed; phone just sits on the page.

Usage:
  python3 s14_bench.py burst [--n 20 --gap 0 --hold 1000 --b 150 --dur 25]
  python3 s14_bench.py pull [run_dir]       # -> run_dir/burst_frames.txt + burst_stats.json
  python3 s14_bench.py analyse <run_dir>    # cadence + frame-to-frame motion chain

Protocol notes: BURST/BRAMP are drv directives (same path as SCAN/FRAMP);
frames arrive as FRAME {json} + FJPEG chunks + FEND, tagged 'burst:fK'.
"""
import argparse, json, sys, time, re
from pathlib import Path
import serial

PORT = '/dev/ttyACM0'
BAUD = 115200


def open_port():
    return serial.Serial(PORT, BAUD, timeout=0.2)


def drain(ser, seconds=0.5):
    end = time.time() + seconds
    out = []
    while time.time() < end:
        line = ser.readline()
        if line:
            out.append(line.decode(errors='replace').rstrip())
    return out


def send(ser, cmd, wait=0.4):
    ser.write((cmd + '\n').encode())
    return drain(ser, wait)


def do_burst(ser, n, gap, hold, b, dur, cwc=0, comp=None, cwc_test_mode=None, cwc_test_led=None):
    cfg = {'bBurstN': n, 'bBurstGap': gap, 'bBurstHold': hold, 'bBurstB': b, 'cwc': cwc}
    if comp is not None:
        cfg['bComp'] = comp   # unset = leave the page's sticky bComp alone
    if cwc_test_mode is not None:
        cfg['cwcTestMode'] = cwc_test_mode
    if cwc_test_led is not None:
        cfg['cwcTestLed'] = cwc_test_led
    for ln in send(ser, f'CFG={json.dumps(cfg)}'):
        if ln.strip():
            print(' ', ln)
    for ln in send(ser, 'BURST'):
        if ln.strip():
            print(' ', ln)
    print(f'burst: n={n} gap={gap}ms hold={hold}ms b={b}; waiting up to {dur}s ...')
    end = time.time() + dur
    lines = []
    while time.time() < end:
        lines += drain(ser, 0.5)
        joined = '\n'.join(lines)
        if ('burst stats:' in joined or 'planes + master captured' in joined
                or 'planes, residual' in joined or 'E burst' in joined):
            break
    for ln in lines:
        if ('burst' in ln or ln.startswith('E ')) and ln.strip():
            print('  ', ln)


def do_pull(ser, run_dir: Path, max_s=420):
    run_dir.mkdir(parents=True, exist_ok=True)
    # CWC bursts ship cwc:master / cwc:pNN labels; all-on bursts burst:fK.
    # Name the capture file after whichever run is in the frame store.
    fp = run_dir / 'cwc_frames.txt'
    print('BRAMP: shipping frames (a few minutes; ~16 KB per frame)...')
    # sDrv is ONE slot: a directive still latched (burst ends inside the
    # next poll) is overwritten by the next send = the pull never runs.
    # Flush it with PING (harmless when consumed) — NEVER ABRT: the page
    # sets abortFlag and benchPull breaks on it -> 0 frames shipped.
    ser.write(b'PING\n')
    time.sleep(2.0)
    ser.reset_input_buffer()
    send(ser, 'LOGP', wait=1.0)   # arm the 15 s window FIRST (S14M bug: a bare
                                  # BRAMP ships into a closed window -> 0 frames)
    time.sleep(1.5)               # LOGP must CONSUME before BRAMP lands
    ser.reset_input_buffer()
    send(ser, 'BRAMP', wait=0.5)
    end = time.time() + max_s
    frames = 0
    logends = 0
    with fp.open('w') as f:
        while time.time() < end:
            line = ser.readline()
            if not line:
                continue
            s = line.decode(errors='replace').rstrip()
            f.write(s + '\n')
            if '[PHONE] FRAME {' in s:
                frames += 1
                print(f'\r  frame {frames}', end='', flush=True)
            if '[PHONE-LOG] end' in s:
                logends += 1
                # LOGP first ships the page's LOG RING, whose own logend
                # precedes the BRAMP frame stream — breaking on the first
                # logend killed pulls at 0 frames (s14o, 28 Sep). Break on
                # a logend AFTER frames arrived, or give up at the third.
                if frames > 0 and logends >= 2:
                    break
                if logends >= 3:
                    print('  (no frames after 3 stream ends — pull aborted)')
                    break
    print()
    text = fp.read_text()
    m = re.search(r'\[PHONE\] BSTATS (\{.*\})', text)
    stats = {}
    if m:
        try:
            stats = json.loads(m.group(1))
        except Exception:
            pass
    nframes = len(re.findall(r'\[PHONE\] FRAME \{', text))
    # CWCSTATS (S14J CWC bursts): per-plane comp shifts -> cwc_stats.json
    mc = re.search(r'\[PHONE\] CWCSTATS (\{.*\})', text)
    if mc:
        try:
            cwc_stats = json.loads(mc.group(1))
            (run_dir / 'cwc_stats.json').write_text(json.dumps(cwc_stats, indent=1))
            print('cwc stats: n=%s comp=%s' % (cwc_stats.get('n'), cwc_stats.get('comp')))
        except Exception as e:
            print('cwc stats parse failed:', e)
    (run_dir / 'burst_stats.json').write_text(json.dumps(stats, indent=1))
    print(f'shipped {nframes} frames -> {fp}')
    print('stats:', stats)
    # Post-pull decode verification: the frames are useless if the analyser
    # can't see them — surface label-set completeness HERE, not at analyse.
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from offline_hole_verify import decode_run
        got = decode_run(run_dir, 'cwc')
        labels = [f['label'] for f in got if f.get('img') is not None]
        ok_imgs = len(labels)
        print(f'decode check: {ok_imgs} frames with images; labels: '
              + (', '.join(sorted(set(labels))[:24]) if labels else '(none)'))
        if nframes and ok_imgs == 0:
            print('WARNING: FRAME headers arrived but decode_run sees no images '
                  '- label/parse mismatch; check the capture file')
    except Exception as e:
        print(f'decode check skipped ({e})')
    return nframes


def analyse_dir(run_dir: Path):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from offline_hole_verify import decode_run, luma
    import numpy as np
    import cv2

    frames = decode_run(run_dir, 'burst')
    print(f'{len(frames)} frames decoded')
    if len(frames) < 3:
        print('not enough frames')
        return 1
    ts = [float(f['meta']['t']) for f in frames]
    iv = [b - a for a, b in zip(ts, ts[1:])]
    iv_sorted = sorted(iv)
    med = iv_sorted[len(iv_sorted) // 2]
    print(f'cadence: median {med*1000:.0f} ms ({1/med:.2f} fps), '
          f'min {min(iv)*1000:.0f} ms, max {max(iv)*1000:.0f} ms')
    exps = {f['meta'].get('exp', '?') for f in frames}
    print('exposure lines:', list(exps) if len(exps) < 4 else f'{len(exps)} distinct')
    lums = [luma(f['img']).astype(np.float32) for f in frames]
    win = cv2.createHanningWindow((lums[0].shape[1], lums[0].shape[0]), cv2.CV_32F)
    shifts = [(0.0, 0.0)]
    confs = [1.0]
    prev = np.sqrt(np.maximum(lums[0], 0) + 1.0)
    for k in range(1, len(lums)):
        cur = np.sqrt(np.maximum(lums[k], 0) + 1.0)
        (dx, dy), conf = cv2.phaseCorrelate(prev, cur, win)
        shifts.append((float(dx), float(dy)))
        confs.append(float(conf))
        prev = cur
    print('\nframe-to-frame shifts (px) + confidence:')
    for k, ((dx, dy), cf) in enumerate(zip(shifts, confs)):
        mag = (dx * dx + dy * dy) ** 0.5
        flag = '  <-- JUMP' if mag > 8 else ''
        print(f'  f{k:>2}: ({dx:+6.2f},{dy:+6.2f}) conf {cf:.3f}{flag}')
    path = sum((s[0] * s[0] + s[1] * s[1]) ** 0.5 for s in shifts)
    net = (sum(s[0] for s in shifts), sum(s[1] for s in shifts))
    print(f'\npath length: {path:.1f} px, net displacement: ({net[0]:.1f},{net[1]:.1f}) px')
    mags = [(s[0] * s[0] + s[1] * s[1]) ** 0.5 for s in shifts[1:]]
    if mags:
        print(f'per-frame |shift|: median {sorted(mags)[len(mags)//2]:.2f} px, max {max(mags):.2f} px')
    print('\nVERDICT HELPS:')
    print(f'  - phone cadence: {1/med:.1f} fps median -> 5 fps {"FEASIBLE" if 1/med >= 4.5 else "NOT proven"}')
    print(f'  - handheld: per-frame drift budget at 5 fps = {med*1000:.0f} ms; '
          f'median measured {sorted(mags)[len(mags)//2]:.2f} px' if mags else '')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['burst', 'pull', 'analyse'])
    ap.add_argument('run_dir', nargs='?')
    ap.add_argument('--n', type=int, default=20)
    ap.add_argument('--gap', type=int, default=0)
    ap.add_argument('--hold', type=int, default=1000)
    ap.add_argument('--b', type=int, default=150)
    ap.add_argument('--cwc', type=int, default=1, help='1 = CWC-form burst (19 frames)')
    ap.add_argument('--comp', type=int, default=None, help='force bComp 0/1 (default: sticky)')
    ap.add_argument('--dur', type=int, default=25)
    ap.add_argument('--cwc-test-mode', type=int, default=None, help='test mode: 0=normal, 1=single-LED')
    ap.add_argument('--cwc-test-led', type=int, default=None, help='test LED index')
    args = ap.parse_args()
    if args.cmd == 'burst':
        ser = open_port()
        do_burst(ser, args.n, args.gap, args.hold, args.b, args.dur, cwc=args.cwc, comp=args.comp, cwc_test_mode=args.cwc_test_mode, cwc_test_led=args.cwc_test_led)
        return 0
    if args.cmd == 'pull':
        ser = open_port()
        ts = time.strftime('%Y%m%d-%H%M%S')
        run = Path(args.run_dir) if args.run_dir else Path(f'runs/s14bench-{ts}')
        do_pull(ser, run)
        return 0
    if not args.run_dir:
        print('analyse needs a run dir')
        return 1
    return analyse_dir(Path(args.run_dir))


if __name__ == '__main__':
    sys.exit(main())