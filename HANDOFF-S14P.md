# Handoff: S14P Single-LED Toggle Test (29 Sep)

## Current State

**Build**: S14P-1901 (page + firmware, flashed + boot-verified 29 Sep)
- `page/survey.html`: BUILD = S14P-1901 · `firmware/poc_survey/poc_survey.ino`: PAGE_BUILD = S14P-1901
- S14P-1900→1901 fix: the test branch no longer calls benchPull itself —
  the shared S14L auto-ship ships the store EXACTLY once (the 1900 test
  branch double-shipped 19+19 frames; found by the mock+CDP harness,
  which now PASSES: CWCSTATS=1 FRAME=19 FEND=19 master=1 planes=18).
- Box on `/dev/ttyACM0` alive (PING/STAT verified), but **phone page
  GONE** (STAT: `page build ''` — screen asleep/tab closed). Reload
  `https://192.168.4.1/` on the phone before anything (accept cert, allow
  camera, status LED GREEN).

## What the 29 Sep forensics found (page benchPull EXONERATED)

The page's test mode + ship path is correct (code-read + harness): 19
frames are pushed to `benchStore` with run-tagged labels (`cwc:rN:p00` /
`cwc:rN:master`), and the shared S14L end-of-burst auto-ship ships the
store exactly once (in S14P-1900 the test branch ALSO called benchPull
at its end — a 19+19 double-ship, removed in 1901). The 28 Sep
'empty pull' failures were in the CONSOLE + SERIAL layer:

1. **28 Sep s14p pull was serial-dead** — 0-byte `cwc_frames.txt`, no
   `[DRV]` acks at all. `/dev/ttyACM0` re-enumerated at 23:50 (box
   re-enumeration mid-session). Nothing was running on the port on 29
   Sep (no bench daemon) — the console port just died.
2. **Reader bug (the s14o 0-frame analysis)**: `do_pull` broke on the
   FIRST `[PHONE-LOG] end`, but the LOGP ring pull completes with its
   OWN logend BEFORE the BRAMP frame stream starts — the reader died
   before any frame arrived, keeping only the LOG ring (the capture's
   fingerprint: 19 frames' FJPEG in the log ECHO, no `FRAME {`
   headers). FIXED: break on a logend AFTER frames arrived.
3. **Directive-slot race**: LOGP + BRAMP back-to-back overwrite the
   firmware's single `sDrv` slot — BURST still latched when BRAMP
   lands = pull lost. FIXED in `s14_bench.py`: PING-flush + 2 s wait,
   1.5 s between LOGP and BRAMP. NEVER ABRT for this (page abortFlag
   → benchPull breaks → 0 frames).
3b. **Label mismatch (FIXED, analysis layer)**: the page ships
   run-tagged labels (`cwc:rN:pNN`) since S14L; `cwc_analyse` now
   accepts both shapes.
4. **cwc_analyse --test-led is now the REAL bit read** (was a stub):
   pile-up hole location, per-plane master×gain reads, conf-gated
   registration (low-conf planes read raw), top-9 normalisation, ON/OFF
   gate at 0.5. Synthetic-validated: clean = 18/18 PASS; one corrupted
   bit = FAIL flagging exactly that plane.

## CLI to run (once the phone is back on the page)

```bash
cd "/home/nellie/projects/led-display/POC LED survey"
# 0. gate: phone page loaded + build matched (status LED green)
/home/nellie/.hermes/hermes-agent/venv/bin/python3 - <<'EOF'   # STAT gate
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.2)
s.write(b'STAT\n'); time.sleep(2.5)
print(s.read(s.in_waiting or 1).decode(errors='replace'))
EOF
# 1. burst (test mode)
/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/s14_bench.py burst --cwc 1 --comp 0 --b 150 --dur 45 --cwc-test-mode 1 --cwc-test-led 0
# 2. pull — arm LOGP FIRST, then BRAMP (never inside another directive's window)
/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/s14_bench.py pull runs/s14p-tripod-led0
# 3. analysis (tripod gate)
/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/cwc_analyse.py runs/s14p-tripod-led0 --test-led 0
```

## Success Criteria (Tripod Gate)

- TEST MODE VERDICT: PASS — 18/18 bits correct
- Registration residual < 1 px on all planes
- Bimodal ON/OFF ratio > 1.5x at the test LED

## Notes

- Serial reads need the hermes venv python for pyserial/cv2
  (`/home/nellie/.hermes/hermes-agent/venv/bin/python3`).
- LED supply must be ON (user forgot once already).
- SSL cert warnings on phone are expected (self-signed CN=led-survey).
- Tripod first, then handheld — same protocol, residual budget ~5 px.
- Docs: plan §4 = protocol/analysis spec; bench session §S14O/S14P =
  today's forensics + pull recipe.
