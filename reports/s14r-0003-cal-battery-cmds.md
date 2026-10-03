# S14R-0003 CAL battery — how to drive it (operator + agent reference)

**Page URL (phone):** `https://192.168.4.1/cal` (same AP `LED-SURVEY` /
`survey2026`, same self-signed cert as the survey page; banner reads
`CAL MODE`, build stamp `S14R-0003-CAL`).

The survey page (`https://192.168.4.1/`, build `S14R-0002`) is untouched;
both pages share the box's one `/ws` WebSocket. The box serves both routes.

## Cmds files (drop in `runs/daemon/cmds/`, one directive per line; the
bench daemon sends each + ~1.5 s ack window + deletes the file; the serial
directive slot is single, so keep one directive per line and let the daemon
pace them)

`cal_start.txt` (battery start; the CFG line first so rig shape + battery
config arrive before CAL):

```
LOGA
CFG={"cwc":1,"cwcN":600,"nStr":3,"nPerStr":200,"cwcSuppress":1,"cwcMaskThr":100,"cwcAmpGate":40,"cwcMarginGate":6}
CALCFG={"order":["E5","E1","E2","E3","E4"],"e1Ladder":[5,10,20,40,60,80,100,120,150,179],"e2Ladder":[5,10,20,40,60,80,100,120,150,179],"e4Ladder":[80,100,120,150],"snapCount":18,"snapGapMs":500,"solidMs":300,"dwellMs":2000,"stepGapMs":5000,"e3Jump":[5,120,5],"e3TraceMs":12000,"e3GapMs":250,"idleCount":60,"idleGapMs":1000,"e4GapMs":10000,"shipBatch":24,"blobSegThr":40,"blobMinPx":4,"dutyBank":0,"e4DutyBank":-1}
CAL
```

`cal_abort.txt`:

```
ABRT
```

`cal_pull.txt` (re-ship whatever the page still holds):

```
BRAMP
```

Only `CALCFG=` / `CAL` are new: `CAL` rides the one-shot directive slot like
BURST/PROBE; `CALCFG=<json>` rides the same 2-credit replay slot as `CFG=`
(the cal page applies numbers to its CFG and everything else to its CAL
constants; unknown keys on the survey page are ignored, so a CALCFG replay
is harmless there). Full ladder override = send the arrays you want; omit a
key to keep the compiled default from `tools/tuning_s14r0002.json`
(`calBattery` section).

## Wire behaviour (all unchanged formats)

- Frames: `FRAME {t,label,W,H,exp}` → `FJPEG <b64>` chunks → `FEND`, exactly
  the survey page's ship, decoded by the bench daemon as before.
- Labels: `cal:e1:L<L>:k`, `cal:e2:L<L>:k`, `cal:E3:<from>to<to>:tNNN`,
  `cal:idle:NN`; E4 uses the UNTOUCHED survey space `cwc:rN:p00..p23` +
  `cwc:rN:master` (one battery epoch N persists across page reloads via
  localStorage) so `tools/cwc_pos_decode.py` runs on those runs as-is.
- Telemetry: `CALSTATS {...}` per step (histMed page-parity + raw-core P90 +
  blob count, kind step) and per battery (kind battery, done/mins);
  `CWCSTATS {mode:"calE4", L, ...}` per E4 burst; `[ts] [PHONE]` in
  capture.txt as always, so offline analysis reads one file.

## Battery shape (defaults; ~26 min on the 600-lamp rig)

E5 idle 60×1 s → E1 all-ON ladder (10 L × [hold 300 ms + 18 snapshots @
500 ms + 5 s dwell]) → E2 same ladder with the exact coded 50%-duty plane →
E3 5→120 / 120→5 jumps, 12 s fine trace @ 250 ms → E4 duty dwell 2 s + ONE
real 24-plane CWC burst at L∈{80,100,120,150}, 10 s gaps.