# S14 bench session — tomorrow (Oliver back home)

Goal: measure the two unknowns the S14 plan leans on — **real phone frame
cadence** (is 5 fps available?) and **handheld motion** (per-frame drift at
that cadence) — in one box-driven session. No phone interaction needed
beyond having the page open.

## Day-1 results (25 Sep, tripod, phone on S14A)

| test | cadence (phone) | shifts | exposure |
|---|---|---|---|
| burst gap=0 (n=20) | **med 153 ms = 6.54 fps** (122–271) | ≤0.02 px | pinned exp=200.02 |
| burst gap=200 (n=20) | med 437 ms = 2.29–2.56 fps (my pacing overshoots — fix in page, pace rVFC-to-rVFC) | ≤0.02 px | pinned |
| burst gap=200 + comp (S14E) | med 635 ms (1.57 fps; pulls crawl ~38 s/frame) | comp residual 0.00 px, conf 1.00 in-page; console-side cross-check 0.0 px max, conf 0.955 | pinned |

Drift compensation (Oliver's seeded scheme, all in-page): fresh all-on master
grab after hold → per-frame seeded NCC vs master (integer pre-shift by the
last composed T, remainder by NCC over ±12 px on a 128-wide decimation)
→ stored frame warped back bilinear. Tripod verdict: residual 0.0 px, done.
Bugs fixed en route (S14A→S14E): y1c ReferenceError in the warp; NCC
decimation read b with DW stride (garbage); NCC returned −motion while the
warp consumes +motion (numpy-verified strict test, content (+10,+5) → (−5,−10));
master taken from stale lumaArr (idleFrame never redraws procCx) — the
runaway 12/24/36/48 px came from this; S14E takes the master from a fresh
grabTimeout() after the hold. Pull lessons: the LOGP window arms 15 s; a
pull is ~38 s/frame at 115200 → ~8 min for 20 frames; NEVER break on
[PHONE-LOG] end (the log pull's logend precedes the frame chunks) — reader
bug that killed several pulls; tools/s14_pull.py = the proven recipe
(LOGP → 1 s → BRAMP → single patient read to FSTATS).
BSTATS/FSTATS with comp ships `shifts` (dx, dy, conf per frame).
| burst gap=200 (5 fps demand) | **med 376 ms = 2.66 fps** (329–491) | ≤0.02 px | pinned exp=200.02 |

- **5 fps: achievable but not yet held** — the phone serves 6.5–6.9 fps
  unpaced, so 5 fps is within its capability; my pacing loop (sleep to the
  gap target AFTER each grab resolves) overshot to 376 ms because the
  encoder+canvas work happens after the grab timestamp. Page-side fix:
  pace from rVFC to rVFC (capture immediately, encode outside the pacing
  window). One-line follow-up, not a design problem.
- **Tripod baseline is clean**: 0.01 px median shift, confidence ≥0.98 —
  the bench measures what it claims.
- **AE freeze confirmed at burst scale**: exposure identical across all
  frames in both bursts (~7 s and ~10 s spans).
- **Pull recipe** (learned): LOGP arms a 15 s sliding forwarding window;
  chunks arrive ~15–17 s apart (per-frame serial transfer ~15 s at 115200,
  20 frames ≈ 5 min). LOGP → 1 s → BRAMP landed 17/20 frames; BSTATS+f0–f2
  were lost to the arm-window race at pull start (stats recovered from the
  page log anyway). Keep 17/20 as the working number; a BRAMP that starts
  within the window is worth one retry at most.
- **Analysis pipeline proven on real pulls** (cadence, shifts, confidence,
  verdict lines all sane).

## Remaining: handheld round (needs Oliver holding the phone)

### Handheld-1 DONE (26 Sep, S14F, gap=0, n=20, all-on b=150)

Cadence: **median 303 ms = 3.30 fps** (290–761; one 761 ms stall), exposure
pinned all 20 frames (`exp=500.05 aem=continuous`). Per-frame |shift|
(chain vs f0, console-side cv2.phaseCorrelate, sqrt-luma + Hanning):

- **f1–f17: median 2.06 px, max ~6 px, conf 0.91–0.95 throughout** — well
  inside the 6 px residual budget. Slow sway + slight pan; path 453 px,
  net displacement (25,-12) px over ~6 s.
- **f18/f19: ±205 px at conf 0.17-0.18 — the ±205 px are ARTIFACTS, not
  motion**: frame-pull inspection shows f18 is the AE mid-transition frame
  (scene dimmed, whole image colour-shifted warm — the auto-exposure
  re-metering mid-burst, likely from the phone's own motion + scene change),
  f19 is back to normal. Correlation across an AE transition is meaningless;
  the frames themselves are intact. LESSON: an AE-transition frame must be
  DETECTED (luma histogram jump) and DROPPED, not registered.
- Verdict for the plan: at unpaced 3.3 fps handheld, per-frame drift is
  ~2 px median / ~6 px max vs the 5-6 px read budget — the single-burst
  handheld scheme is VIABLE at 3.3 fps. The 5 fps pacing question is moot
  for the budget (3.3 fps already meets it); more fps would shrink it
  further but is not required.

Same three commands while holding the phone as in a real survey (breathe,
sway, slight pan — not clamped). Compare per-frame |shift| against the
tripod 0.01 px reference; green light for the chained-registration plan is
median well under 6 px at the burst cadence.

## Before starting

1. ~~Flash the S14A build~~ **DONE 25 Sep** — flashed + verified:
   boot OK on serial (`page build ''` = no client yet, expected), embedded
   page extracted from the .ino and confirmed S14A with all bench code
   (gzip blob, 72,241 B), serial directives CFG=/BURST/BRAMP queue OK
   (directive slot left clean via PING), analyse pipeline smoke-tested
   end-to-end on a synthetic handheld burst (`runs/s14-synth-smoke`:
   recovers true per-frame shifts, 5 fps cadence read, verdicts sane).
2. Boot check on the day: `python3 tools/s13_boot_check.py` → expect `page build 'S14A-1900'`
3. Phone: reload `https://192.168.4.1/` (accept cert warning). Status LED
   must go GREEN (page↔build match). Serial capture starts BEFORE phone reload.

## Tripod baseline (calibrates the bench itself)

```
python3 tools/s14_bench.py burst --n 20 --gap 0 --hold 1000 --b 150 --dur 25
python3 tools/s14_bench.py pull runs/s14-tripod
python3 tools/s14_bench.py analyse runs/s14-tripod
```
Expect: cadence = whatever the phone does unpaced; shifts ≈ 0 px. This is
the reference — handheld numbers only mean something against it.

## 5 fps demand test

```
python3 tools/s14_bench.py burst --n 20 --gap 200 --b 150 --dur 25
python3 tools/s14_bench.py pull runs/s14-gap200
python3 tools/s14_bench.py analyse runs/s14-gap200
```
Read: if median interval lands ≈200 ms, the phone can serve 5 fps (the
burst demands it via gap pacing). If it stays >300 ms, 5 fps is dead and
2-3 fps is the handheld design point.

## Handheld motion (the real test)

Pick up the phone. Hold it as you would during a real survey — NOT clamped
still; breathe, sway slightly, maybe a slow pan. Run:

```
python3 tools/s14_bench.py burst --n 20 --gap 200 --b 150 --dur 25
python3 tools/s14_bench.py pull runs/s14-handheld
python3 tools/s14_bench.py analyse runs/s14-handheld
```

The analyse step prints per-frame (dx, dy) + phase-correlation confidence,
flags JUMPs > 8 px, and prints path length + net displacement. What we're
looking for:
- per-frame |shift| median — this is what chained registration must absorb
  (S13-era tolerance was 6 px; handheld per-frame drift should be well under
  at 5 fps);
- confidence dips — where phase correlation gets shaky (motion + JPEG noise);
- total path length vs net displacement — sway (reversible) vs walk-away.

Optional: repeat handheld at gap 0 (max rate) to see if faster frames shrink
per-frame motion as predicted.

## What "good" looks like

- Tripod: 0 px shifts, cadence stable.
- 5 fps demand: median ≤ 220 ms.
- Handheld: per-frame |shift| well under 6 px at the chosen cadence,
  confidence high throughout — that's the green light for the single-burst
  handheld plan (chained registration, §6 of S14-CWC-PLAN.md).

## Notes

- The burst paints ALL ON at b=150 for its whole duration (bench only —
  the real CWC burst alternates planes at the same 50% duty).
- Frames store at FULL resolution (406x720) in a dedicated bench ring
  (48 max), separate from the survey ring.
- BRAMP ships a `BSTATS` line first (n, median/min/max ms, fps as measured
  phone-side) — `burst_stats.json` in the run dir. Page-log lines in the
  same pull corroborate.
- If a pull stalls: ABRT then BRAMP again (ring survives ~48 frames).