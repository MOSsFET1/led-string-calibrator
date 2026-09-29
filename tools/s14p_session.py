#!/usr/bin/env python3
"""S14P burst+pull in ONE process — the reader never leaves the port.

Tonight's lesson (29 Sep, 20:05 burst): do_burst exits right after the
completion line, the S14L auto-ship fires ~1 s later, finds no reader
(USB host NAKs), and dies mid-flight. One process holding the port
through the ship removes the failure mode entirely.
"""
import sys, time
from pathlib import Path

TOOL = Path('/home/nellie/projects/led-display/POC LED survey/tools')
sys.path.insert(0, str(TOOL))
import s14_bench  # noqa: E402

run_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('runs/s14p-session')

ser = s14_bench.open_port()
s14_bench.do_burst(
    ser,
    n=20, gap=0, hold=1000, b=150, dur=60,
    cwc=1, comp=0, cwc_test_mode=1, cwc_test_led=0,
)
# Wait out the tail (primer/handle lag): the auto-ship may already be shipping.
time.sleep(2)
s14_bench.do_pull(ser, run_dir, max_s=420)
ser.close()
print('SESSION DONE')