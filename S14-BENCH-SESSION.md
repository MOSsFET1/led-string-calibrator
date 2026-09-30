# S14 bench session — harness + bench lessons (Sep 2026)

## S14J: validation harness (BUILT 27 Sep, reusable)

19-frame CWC-form movement test in the AGREE protocol shape (master + 18
codeword planes, no off frame). Purpose: prove comp + measurement
machinery on 50%-duty plane content before decode. All-on movement burst
still available with `--cwc 0`.

```
python3 tools/s14_bench.py burst --cwc 1 --comp 1 --b 150 --dur 40   # tripod first
python3 tools/s14_bench.py pull runs/s14j-tripod
python3 tools/cwc_analyse.py runs/s14j-tripod --save-pileup runs/s14j-tripod/pileup.png
# handheld: same, operator holds the phone
```

Page CFG keys (sticky): `cwc` (0/1), `cwcN` (LEDs mapped, 10-200; plane
payload fits the box WS limit at ≤200 px), `cwcSettle` (paint→grab ms,
clamped ≥70). Codeword bank embedded in the page. CWCSTATS ships per-
plane comp shifts in the pull; `cwc_analyse.py` reads them + the frames,
re-registers plane-vs-master console-side (ground truth), rebuilds the
pile-up, runs the S13-mirror hole detector.

Harness validation 27 Sep (mock box + headless Chromium, no phone):
- estimator round trip on live page code: identity conf 1.00, known shift
  recovered within half a grid cell, warp-back reduces error 87→30;
  **test-lesson: white-noise content is pathological for the decimated NCC**
  (sub-cell shifts decorrelate — real camera content is smooth, tripod
  measured 0.00 residual; quote quantization-aware gates, not absolute px)
- CWC burst runtime: exactly 1 all-on + 18 plane paints at cwcN=10, comp
  ON, residual max 0.00 px, CWCSTATS shipped, no runtime errors
- S14B-5 comp fix landed: NCC estimates ×W/128 (decimated grid → source
  px) before the integer pre-shift and the stored-frame warp

Bench-driving the harness: `python3 tools/mock_box.py` (serves the REAL
page on :8443 with the box's cert + the exact WS protocol), then
`/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/cdp_cwc_test.py`
(headless Chromium fake camera; CFG + BURST via mock_directives.txt).

## Pull recipe + serial lessons (HARD-WON, keep)

- The box prints the page's logc chunks ONLY inside an armed window:
  LOGP = 15 s sliding window; **LOGA = persistent arm** (bench daemon).
  A pull is `LOGP → 1 s → BRAMP → single patient read` — and NEVER break
  on the FIRST `[PHONE-LOG] end` (the LOGP ring pull's own logend
  precedes the frame stream — reader bug that killed several pulls;
  `tools/s14_bench.py do_pull` now encodes the full recipe).
- The auto-ship (S14L) fires ~1 s after a burst ends, so a 15 s LOGP
  window is usually still open; **LOGA removes the race entirely**.
- `sDrv` in the firmware is ONE slot: BURST still latched while a pull
  runs = the pull is LOST (directive overwrite). Never queue BURST/
  BRAMP/LOGP while another directive is pending.
- If a pull stalls: ABRT then BRAMP again (ring survives ~48 frames;
  bench ring 100).
- ~16 KB per frame at 115200 ≈ 1.5 s/frame serial floor (S14L: 20-frame
  ship in ~10–30 s); 12.7 MB `runs/daemon/capture.txt` grew from the
  daemon echo, not the box.
- Frames store at FULL resolution (406x720-class) in a dedicated bench
  ring (`benchStore`, 100 max), labels run-tagged: `cwc:rN:pNN` /
  `cwc:rN:master` / `burst:rN:fK` — one pull recovers every run.
- Day-1/2 cadence reference (25–26 Sep): tripod unpaced 6.5 fps /
  gap-200 2.3–2.7 fps; handheld per-frame drift 1–2 px median at 3.3
  fps, max 2.2 px, net whole-burst drift 7–14 px — numbers carried into
  S14-CWC-PLAN §3; single-burst handheld viable.
- AE-transition frames must be DETECTED (luma histogram jump) and
  DROPPED, not registered (26 Sep: ±205 px 'artifacts' at conf 0.17).

## S14O/S14P: single-LED toggle test (28–29 Sep)

**Purpose**: validate the bit-read path end-to-end on one known LED
(zero errors on tripod) before scaling to the 150-LED decode. Protocol
+ analysis: see S14-CWC-PLAN §4. Interpreters: serial + analysis tools
run under `/home/nellie/.hermes/hermes-agent/venv/bin/python3` (pyserial
+ cv2; system python3 has neither).

**28 Sep status**: S14P-1900 flashed; test mode captured 18 planes +
master (phone log 'cwc test: 18 planes + master captured'), but both
pulls shipped nothing.

**29 Sep forensics (serial side — the page's benchPull was NOT the
blocker)**:

1. **s14p pull was serial-dead**: `runs/s14p-tripod-led0/cwc_frames.txt`
   0 bytes — no [DRV] acks at all. `/dev/ttyACM0` re-enumerated at
   23:50 (file date) = the box re-enumerated mid-session; the console's
   open port died. Re-plug/verify STAT before the next pull.
2. **Reader bug (the s14o 0-frame analysis — fingerprint in the
   capture)**: `do_pull` broke on the FIRST `[PHONE-LOG] end`, but the
   LOGP ring pull completes with its OWN logend BEFORE the BRAMP frame
   stream starts — the reader died before any frame arrived and kept
   only the LOG ring (19 frames' FJPEG lines in the log ECHO, no
   `FRAME {` headers — which `decode_run` then skipped). FIXED: break
   on a logend AFTER frames arrived (2nd), or the 3rd logend as
   give-up; the frame counter now greps the real `[PHONE] FRAME {`
   shape, and a post-pull `decode_run` completeness check runs at
   pull time.
3. **Directive-slot race (the likely 28 Sep s14p killer)**: LOGP +
   BRAMP back-to-back overwrite the firmware's single `sDrv` slot —
   BURST still latched when BRAMP lands = the pull never runs (26 Sep
   lost 17/20 frames to this shape). FIXED in `s14_bench.py do_pull`:
   PING-flush the slot + 2 s wait before LOGP, 1.5 s between LOGP and
   BRAMP. NEVER use ABRT for this — the page sets abortFlag and
   benchPull breaks on it → 0 frames even when the pull runs.
3b. **Label mismatch (FIXED, analysis layer)**: benchCapture pushes
   run-tagged labels (`cwc:rN:p00`) since S14L;
   `cwc_analyse.load_frames` fullmatched bare `cwc:pNN`/`cwc:master` →
   master 0 / planes 0 → INCOMPLETE even when every frame was in the
   capture. Analyser now accepts both shapes.
4. **cwc_analyse --test-led is now REAL** (was a stub): locates the
   LED's hole in the pile-up, reads per-plane luma at the LED against
   master×gain (r6 recipe), top-9 normalisation, ON/OFF gate at 0.5,
   per-plane conf-gated registration (low-conf OFF planes read raw —
   their hole is the only structure, phase corr has nothing to lock).
   Validated on synthetic runs: clean burst = 18/18 PASS; one corrupted
   bit = FAIL with exactly that plane flagged. Also fixed: the codeword
   bank loader now unwraps the dict format (`bank['codes']`).

**Build note**: the double-ship found by the harness (test branch's own
`benchPull` + the S14L shared auto-ship) is FIXED in S14P-1901 — the
test branch no longer calls benchPull; the shared end-of-burst
auto-ship ships the store exactly once (harness: CWCSTATS=1 FRAME=19
FEND=19 master=1 planes=18, labels run-tagged). Flash S14P-1901
(compiled + uploaded 29 Sep), reload the phone, then:

```
cd "/home/nellie/projects/led-display/POC LED survey"
/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/s14_bench.py burst --cwc 1 --comp 0 --b 150 --dur 45 --cwc-test-mode 1 --cwc-test-led 0
/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/s14_bench.py pull runs/s14p-tripod-led0
/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/cwc_analyse.py runs/s14p-tripod-led0 --test-led 0
```

target: TEST MODE VERDICT PASS — 18/18 + residual < 1 px + ratio > 1.5x.

## 30 Sep — the toggle gate PASSES, then the 200-LED rounds (S14P-1904/1905)

- **Builds 1904/1905** (details in S14-CWC-PLAN §4/§10b): canvas-ownership
  fix (benchCapture snapshots procCx; the 1903 test-mode path shipped
  IDLE-OVERLAY pixels — the display canvas gets redrawn between grabs),
  `scanning` flag actually live now (idle overlay frozen + manual paint
  buttons locked mid-burst), backwards registration chain IN-PAGE per the
  operator spec (P17→master direct; each lower plane pre-shifted by the
  previous TOTAL), in-page position decode + ON-PHONE result view
  (static master, green/amber site boxes), drv? retry, non-blocking STAT,
  default string length 200.
- **Toggle gate (blocked since 28 Sep) PASSED**: LED 0 18/18 at THREE
  sites (twin-site note prints — mirrored-string structure is the
  expected shape), LED 1 18/18. LED 25 honest-FAIL 16/18 at its true
  pixel, failing EXACTLY the 3 planes where LED 24 (site 3 px away) is
  ON and LED 25 OFF → bloom crosstalk from the lit neighbour; d_min 6
  kept it an honest reject (no false claim).
- **Analyser lessons now encoded**: (1) candidate sites = threshold
  LADDER on the blurred master (254/250 any-area + 224≤60 + 200≤40 px
  comps); a single 200 threshold merges SKIRTS into giant blobs and the
  'best site' lands mid-skirt at master luma 21; a flat 254 misses cores
  in near-saturated zones (18/18 sites with blurred luma only 231/236).
  (2) The pile-up normalisation must CLIP, not shift by min(): one
  negative outlier lifted a whole synthetic background over threshold.
  (3) run_round.py must WRITE cwc_stats.json + reassemble CWCDEC chunks
  (the gate analyser keys testMode off that file; only s14_bench used to
  write it). (4) Mock-box parity: LOGA semantics + the 1905 sites-list
  ship shape are in cdp_1904_check.py's hard checks.
- **200-LED rounds** (1905-pos1 etc., nPx/cwcN 200): phone decode 209
  sites / 170 LEDs (amp 62–226 med 133); console cross-check same-id
  205/209; console-only 91 (skirt/ball = string 2's jumble; string 2
  unplugged per operator for focus). Misdetected group = leds ~20–24:
  physically at the multimeter/PSU occlusion + near-saturation zone of
  the desk loop (master crop shows the strand clearly but its plane
  pixels read coin-flips → the segment is OCCLUDED/defocused, not
  unlit). Handling per §5: honest reject → class I interpolate from
  serpentine neighbours.
- **[SUPERSEDED occlusion call]** — see plan §10c: the real root cause
  of the 1905 misses was the CANDIDATE MASK, not occlusion
  (operator-corrected); the 1906 recipe recovers 20–25 + 150–156.
- **1906 PHONE ROUND 14:58 (first phone-side 1906 validation,
  runs/s14p-1906-phone-pos1)**: boot_check ALIVE 'S14P-1906'; run_round
  19/19 frames, labels unique; page decode 194 LEDs → cwc_dec.json;
  console **197/200** missing [46,90,91] (margin-0-everywhere), ZERO
  dup claims both sides; LED 16 recovered amp 137.1 margin 77.2; amp
  max 250.8, no 255 clip; 20–25 + 150–156 all present amp 117–207;
  LED 25 VERIFIED (167,424) amp 136.9 margin 83.9 (toggle 16/18 =
  bloom crosstalk, now confirmed). Gate PASSED. One page-vs-console
  divergence: codeword 120's page box 67 px right on a specular
  REFLECTION (253,301 vs console 186,303 on-pitch); console
  authoritative, page mask divergence → cwc_decode_sim gate list;
  other 193 shared IDs median 0.00 px. NEXT: handheld toggle repeat
  (§4), then ledcloud/2 export (§8).
- **OPERATOR CONFIRMED (30 Sep pm): 46/90/91 HIDDEN** — physically not
  visible; the margin-0 trio was never a decoder defect and the honest
  unclaim was CORRECT. The §10c 1:1-assignment queue is DROPPED.
  Data agreement (the probe DID complete its work before the 429 credit
  wall; runs/s14p-1906-phone-pos1/assign_probe.json +
  tools/cwc_assign_probe.py kept): 1:1 assignment is INFEASIBLE for
  46/90/91 (0 feasible sites each under the standing gates; total 197 —
  identical vs baseline); relaxed-gates census: 46's only candidate is a
  DARK pixel (blur 84.6, amp 82.1 at 7.6 px off-mid — a ghost shape),
  90/91 relax to NEGATIVE margins (−122.3, −142.5 — owners dominate).
  Calibration lesson: a neighbour-midpoint guard at 1.5× pitch would
  FALSE-REJECT 4 real claims (29 32 px, 30 70.5 px, 31 35.5 px,
  181 40.5 px off-midpoint) — the string curves; pitch guards must be
  section-aware, never flat off-midpoint. Trio ships class I in the §8
  export. Crop evidence: master around 90's expected site shows the
  strand passing BEHIND a glass bottle (LEDs 88/89 lit on the flanks,
  no core at 90) — runs/s14p-1906-phone-pos1/crops/.
- **1908 (page-only)**: result-view label chips −30% (font 14→10,
  chip 15→11 h) + site-box stroke alpha 0.55 (see-through); labels
  validated by cdp_1904_check on 1907. Roadmap: §4 handheld → §8.
- **HANDHELD 1908 FORENSICS (30 Sep, console-side)** — operator's first
  §4 handheld attempt failed ('lots of missing LEDs'); late LOGA replay
  recovered 19/19 frames → runs/s14p-1908-handheld/ (+FORENSICS.md,
  led_overlay.png, page_decode_sim.json, review brief). Verdict:
  **MOTION-DEGRADED burst, not a recipe/protocol/firmware defect.**
  Burst mechanics healthy (exposure pinned exp=200.02 every frame, k
  1.02–1.06, master clean post-500 ms flush). The phone panned ~30 px
  across the 18 planes (dx 28.5 / dy 25.4; page chain + phaseCorrelate
  agree within 1–3 px): per-plane remainders 3–9.5 px (med 4.5) = 3–5×
  the 1–2 px/frame plan, at/over the ~5 px §4 budget. Registration
  tracked coarsely (chain conf 0.57–0.81 vs 0.948–0.955 tripod) but
  ±3–8 px slips at ~20 px pitch mix LED cores with neighbour blooms →
  stacksig profiles lose bimodality (15/27 clean; some ON/OFF same
  sign) → score field fills with common-mode energy, every codeword
  finds amp 140–230 somewhere (argmax blur-luma can be 43!), gates
  pass ghosts, and the count runs at NOISE-FLOOR sensitivity:
  identical-recipe replicas give 22/100/142 LEDs on ±1–2 px rounding
  alone. Console honest floor 39/200 (zero dups, pitch med 167.7 px =
  NOT serpentine); phone claimed 118 — BOTH unreliable. Lessons:
  (1) no gate/mask change can fix a motion-exceeded budget (same
  shape as the mask-sweep law); (2) a flat neighbour-midpoint guard
  false-rejects curved sections (29/30/31/181 up to 70.5 px off-mid);
  (3) HEALTH GUARD is the missing visible signal.
- **1909**: in-page handheld motion guard (page-only edit + banner):
  CFG `cwcGuardConf` 0.90 / `cwcGuardRem` 5 (themed on tripod 0.95±0.00
  vs degraded 0.57–0.81, and the 5 px §4 budget); any plane outside →
  `E MOTION WARNING` log line (plane list) + result-view caption
  '— MOTION FLAGGED, unreliable'. Mock-box QA PASS with NO false trip
  on the static scene. Technique for the next handheld attempt: SLOW
  small pans with brief pauses, keep total drift <~15 px and
  per-plane remainder ≤5 px — cadence 0.24–0.32 s/plane ⇒ ~5 s of
  painting. If disciplined handheld still fails the 197/200 + zero-dup
  gate: escalate to plan §5 mid-burst re-anchor / §9 blob-landmark
  lever; the §11 ack-driven capture remains the structural fix.
- **1911 (direct per-plane registration, both decoders)**: page
  cwcChain → `pD.map(pd => ncc(mD, pd))` (accumulator + shiftAt
  deleted); guard rem = per-plane own-shift magnitude; drift trail vs
  median plane shift; CFG cwcMaskThr 150 / cwcMarginGate 10 / amp 60 /
  cwcNccPeakMargin 0.05. Console mirrored (register_direct) +
  revalidated offline (runs/s14p-1910-direct-parity.json): tripod
  197/200 zero-dup [46,90,91] LED16/25 exact-preserved (pos deltas
  med 0.0, max 10.2 = LED144-class sub-px argmax flips + LED120 flip
  to the pitch-geometric site); r3 at final settings **194/200 zero
  dups pitch 19.2** missing [43,46,68,69,90,91] — 3 short of parity
  target, honest residual of the 128-wide dec-NCC K=5.625 quantisation
  (full-res-raw-luma scoring of the SAME frames reaches exactly 197,
  missing [46,90,91]); direct-vs-GT per-plane error r3 med 0.69/max
  2.19 (was chained med 1.25/max 2.28 — bias collapsed as predicted);
  degraded 1908 → 168 ≥ 122 ✓ (interpretability still poor on that
  burst — over budget, as before). **PHANTOM VERDICT REVERSED**: the
  old "(89,631) ghost" (codeword-103 pattern, 280 px from site) reads
  REAL at mask 150 — raw per-plane samples 238–255 in exactly 103's ON
  planes, 18–71 in OFF = a genuine pattern read, the LED-120 anatomy
  (specular/secondary reflection faithfully reproducing the pattern),
  NOT a decoder artifact; mask ladder 60/10: 150/155 admit it, 160+ do
  not, all hold 197 — recommended default REMAINS mask 150 (r3 max),
  the bottom-fold second claim joins the §8 fold-zone/label-authority
  exclusion class. NOTE: the pre-1910 mask-175 lore ("never ≤160,
  phantom survives even after dedup") is a description of PATTERN-
  FAITHFUL reflections, not single-pixel glints — dedup cannot remove
  a different SITE reading the same codeword; keep the claim, treat
  as class-X collocated-or-reflection in the export. Phone-vs-console
  count gap (177 vs 168 on r3 at 60/25) superseded by final-settings
  agreement (194/194 same-id, r3).
- **1910 (parabolic sub-peak, both decoders)**: page cwcChain ncc()
  stores its 25×25 NCC surface and fits the 3-point parabola per axis
  (Δ=(y₋−y₊)/2(y₊+y₋−2y₀), clamp ±0.5, interior + conditioning guard
  CFG `cwcNccPeakMargin` 0.05; boundary/flat → integer pick);
  console cwc_pos_decode.py brought to TRUE page parity — the old
  console had a double-warp (+shift on a SAMPLE-AT convention moved
  content twice; residuals +6.3/−12.7/−25.4 px vs page-style 0.00) and
  inverted gain k (med(M)/med(P) vs page med(P)/med(M)) — both fixed.
  Offline validation (runs/s14p-1910-parity-check.json + -brief.md):
  tripod 197/200, zero dups, missing [46,90,91], LED16 (135,571)
  amp 130.5 marg 75.2, LED25 (166,424), position deltas vs old console
  med 0.0 / max 69 px (=LED120 only, where parity now agrees with the
  page's own shipped verdict — see below); synthetic sub-px case
  2.30→0.27 px warp error; refined shift moves ≤0.67 px from integer
  chain per plane. HANDHELD 1908 re-decode with parity+refinement:
  39 → 122 LEDs, ZERO dups, pitch med 167.7 → 21.4 px, serpentine
  runs (longest 10) — the console no longer amplifies the motion
  degradation (comparators 100/142 were NOT apples-to-apples: the sim
  had a same-window NCC bug; on identical footing integer chain 136,
  phaseCorrelate tots 180). Threshold reconciliation: 0.05 (tripod
  margins med 0.176, fit active 36/36; degraded handheld med 0.045 →
  refused 24/36 flat peaks). LED120 OPEN ITEM: post-parity,
  console+page AGREE on the reflection site (both wrong vs pitch) —
  console authority for export now rests on the PITCH convention, not
  decoder identity; exclusion list for §8 export: [46,90,91 hidden,
  120 pitch-resolved].
- **OPERATOR PIPELINE FACT (30 Sep)**: after each burst the phone
  AUTO-SHIPS the 19 JPEGs to the box (~4 KB WS chunks; `benchUploading`
  holds the Burst button greyed a few minutes — that is the upload).
  Nothing is stranded on the phone; console LOGA/BRAMP replays the box
  ring. Ship-once clear at each burst start = only the LAST run stays
  in the ring (runs 1–2 overwritten by r3 — fine, r3 studied).
- **1910 HANDHELD-3 FORENSICS (17:41–18:05, runs/s14p-1910-handheld-3/,
  REVIEW.md/review.json/gt_shifts.json + lever probes)**: operator r3,
  phone said 177 + MOTION FLAGGED; console parity decode (60/25) gave
  168/200, zero dups, amp med 134.7, pitch med 21.1 — 29 true misses
  beyond the hidden trio (phone 177 vs console 168 discrepancy noted,
  unexplained). MOTION WAS NOT THE BINDER this run: net drift ~5.7 px,
  per-plane GT steps med 1.22/max 2.19 px SMOOTH (no jerk — operator
  discipline worked, ~5× tighter than 1908's 30 px pan); the ~5 px
  budget was NEVER breached; the flag is over-conservative (shipped
  thresholds flagged ZERO planes; tightened rem>3/conf<0.75 would flag
  only p09/p14). The binder: chained totals carried a 1.25 med / 2.28
  max px BIAS vs GT which corrupts stacked bit profiles at 20 px pitch
  (systemic, not plane-visual — retakes would NOT have saved planes).
  Miss census: LED27 genuine fold-crowding (d6 pair with 116, 6 px
  apart, amp 99.2 vs 87.4); 50/155/179/47 mask-kills (edge-on tiny
  cores blur 143–172 under 175); 46/90/91 hidden in this pose too;
  the rest = chain-bias stack corruption + steal-suppress (magnets
  82/197/164/145 with 48/17/16/13 pre-dedup sites at 40/10).
  LEVERS RANKED (validated on this run's frames): (1) DIRECT per-plane
  registration vs master (no chain accumulation) +26 LEDs, zero ghost
  cost — GT tots 194 → mask 150 → 195 → margin 10 → 197 missing exactly
  [46,90,91], ZERO dups, amp med 160.4/marg med 89.5 = the tripod gate
  reproduced on the flagged run; plan §3 already proved plane-vs-master
  never degrades with raw shift. (2) mask 150 +1 (LED50; codeword-103
  phantom measured amp ~40 here — not a threat at these margins).
  (3) margin 25→10 +1 (LED27 at 11.8). (4) mask 140 +1 (LED47) only if
  the ≤160 phantom floor is re-tested. (5) gate sweeps on the shipped
  chain: 153–177 across the whole 45–90×10–40 grid — noise-floor
  insensitive, never the fix. (6) retake-guard: saves nothing (loss
  systemic). (7) 2-segment mid-burst re-anchor REJECTED on data (only
  55/170 cross-segment agreement; per-segment stacks halve codeword
  dimension). VERDICT: §4 handheld gate CAN pass at this motion level;
  expected robustness ~6–10 px net drift at 2× budget headroom.

## 29 Sep session (continued): WS-heap + the pull recipe + POWER

- **WS won't open (S14P-1901 loaded)**: the box's TLS accepts failed with
  `mbedtls_ssl_setup -0x7F00` (ALLOC_FAILED) on every connection — no heap
  left for a second TLS session after long uptime. A DTR reboot cleared it;
  page stamped hello within seconds. If this recurs across reboots it's a
  leak to chase; across uptime it's a known C6 heap squeeze with
  max_open_sockets=4 + the embedded page + WS buffers. Reboot = workaround.
  `-0x7780` handshake errors before cert-accept are the browser's TLS alert
  until the self-signed cert is accepted — expected noise, not a fault.
- **Pull recipe SETTLED**: LOGP's 15 s sliding window only gates serial
  PRINTS; the ship runs regardless. LOGP+BRAMP in one poll cycle = BRAMP
  overwrites the LOGP slot (single sDrv slot) → nothing armed → 100% drop.
  Two LOGPs 0.8 s apart = same race. The recipe that works EVERY time:
  **LOGA (persistent arm) → BRAMP → patient read**. The 39-frame full-ring
  pull (19 test + 20 all-on, 2.4 MB) completed in ~4 min with logend at the
  end. `pull_s14p_loga.py` is the reference implementation.

## 29 Sep late — ship-once fix + one-connection round + first position decode

- **Ship-once page (S14P-1902/1903, operator request)**: the bench store
  NO LONGER ACCUMULATES runs — every burst start clears the ring
  ('bench store cleared: K frames dropped'; LOGA keeps the window open so
  a late reader still works). Pile-up root cause closed: the old
  100-frame ring shipped every run since the page loaded, so re-pulls
  re-shipped old frames. Harness-verified: tools/cdp_once_ship.py
  (burst A 19 frames -> burst B clears A -> ships 19, not 38).
- **Build history tonight**: 1902 (store clear, Nellie session) ->
  1903 (master-grab latency fix, OTHER session, same repo) — the box now
  runs 1903 which CONTAINS both changes. Serial-port re-enumeration after
  a flash killed a port opened earlier (read the STAT cascade as 'port
  was reset', re-open + re-gate, kill stale pollers first).
- **One-connection round** `tools/run_round.py <run> [--cwc-test-mode 0
  --cwc-n 150 --b 150 --comp 0]`: LOGA + CFG/BURST + the page's auto-ship
  IS the pull (no BRAMP), one serial session, 19/19 frames every time
  (s14p-1903-pos1: capture complete, decoded 19, master + p00..p17).
- **First full position round** (tripod, b150, comp OFF):
  `cwc_analyse` — reg residual 0.03 px median, master dip CLEAN (ratio
  0.96, NO plane-content contamination — the 1903 flush fixed it).
  Position decode `tools/cwc_pos_decode.py` (per-codeword score map,
  per-LED peak site, d6 margin): 154 sites / 150 codes with multi-site
  structure => union of sites over all 150 codewords = 177 distinct LED
  sites on the string routes (runs/s14p-1903-pos1/sites_union.png).
  Mirrored multi-string structure CONFIRMED (80 leds have exactly 2
  sites; firmware mirrors lane1 to all 8 lanes; 2 strings in frame).
- **The other session** (pts/2) built `tools/s14_detect.py` (positions
  from pseudo-master bright blobs + blink classification — its output
  predates the clean-master fix; its r4/r5 lessons land in the skill +
  this doc), patched 1903, and was interrupted mid-skill-update.

## 30 Sep evening: 1911 flash + bench, out-of-hours box reboot + TLS wedge

- **S14P-1911 flashed and bench-verified** (HEAD cfbe445, unpushed).
  Pre-flash QA harness PASS at 18:33 on the mock (that session's
  uncommitted artifacts: runs/qa-1904/result.png +
  tools/mock_directives.txt + tools/mock_log_pull.txt). The evening
  handheld burst then ran and AUTO-SHIPPED 19 frames by 18:51 with
  CWCSTATS conf 0.74–0.87 → the page's in-page MOTION FLAGGED caption
  fired; the ship completed cleanly.
- **~19:01 the box rebooted UNATTENDED** and every TLS accept after it
  failed `mbedtls_ssl_setup returned -0x7F00`
  (MBEDTLS_ERR_SSL_ALLOC_FAILED): post-reboot the heap cannot fund a
  second TLS session alongside the page (pageGz is malloc'd in RAM per
  poc_survey.ino L910-914, plus the second TLS session reserve). The
  phone could not reconnect — zero phone traffic for ~150 s, Burst
  greyed with 'WS: closed'. **Correct phone-side read: the greyed
  button = wsOpen false (no connection), NOT an upload in progress.**
- **CORRECTION (the 30 Sep OPERATOR PIPELINE FACT above)**: 'greyed a
  few minutes — that is the upload' is WRONG. The auto-ship takes
  ~40–60 s and cannot hang; a multi-minute grey is the WS being down
  (this incident's exact shape). The auto-ship itself was fine tonight.
- **Recovered ~19:4×**: operator power-cycled the box, reloaded the
  phone page — WS healthy again.
- **Handheld r4 DECODED (20:14–20:17, console cross-check)**: recovered
  from the bench ring by a LOGA→BRAMP replay into
  runs/s14p-1911-handheld-r4/ (19/19 frames, labels cwc:r1:p00..p17 +
  master). LESSON: the LOGA persistent arm does NOT survive a box
  power-cycle (RAM state) — the first two pulls read 0 bytes /
  empty-ring until a fresh LOGA acked. Phone claimed 187 @ gates 60/**25**
  — the phone's localStorage CFG kept marginGate 25; build 1911 ships 10.
  Console: **191/200 @ 60/10**, 188/200 @ 60/25, zero duplicate claims
  after strongest-site dedup (669 pre-dedup sites @10, max 14 for one
  codeword). Missing @10: [5,16,22,46,67,90,91,109,114] — 46/90/91 = the
  standing hidden trio; 5/16/22/67/109/114 unknown-class pending the
  full-res-NCC re-score (r3 precedent: that class is dec-NCC
  quantisation, not gate losses). Motion honest: median plane offset
  (−8.4,−1.9), net vs median ≈ 6.1 px — inside budget. The 1909 guard
  DID flag this run (every plane's direct-conf 0.767–0.885 < 0.90);
  guard re-theme pending (CFG-only, skill v1.73/v1.75).
