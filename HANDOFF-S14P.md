# Handover — S14P-1928 (02 Oct) — SUPERSEDED by the S14R era

> **RE-STAMP 03 Oct: the live handoff era is S14R.** This file is now the
> CLOSED P-line record ending at **S14P-1928**; everything "Current build"
> below describes a box already superseded by the committed, QA-PASS
> **S14R-0002** (flash pending — the box still runs S14P-1928 until the
> parent's flash). The S14R round-by-round record (0000 Golay bank switch,
> 0001 operator-UI round, 0002 brightness calibration) lives in
> S14-CWC-PLAN.md **§15**; the separate S14R handoff doc is authored by the
> main session. Former `HANDOFF-S14Q.md` retired 30 Sep night; old content
> lives in git (commit `21c5c56`) if needed. Original scope: the 01 Oct →
> 02 Oct live round sequence ending at **S14P-1928**; the 12-of-24 era was
> designated **S14R-0000** here (operator decision 02 Oct) and is now BUILT.

## Current build (HISTORICAL — superseded by S14R-0002, not yet flashed)

- Firmware on box: **S14P-1928** (flashed 02 Oct, hash-verified,
  min_spiffs, banner `=== poc_survey S14P-1928: capture-only bursts +
  manual bulk send ===`; runs on the box).
- Page BUILD string: **S14P-1928**
- `tools/cdp_1904_check.py` expects: **S14P-1928** (STAMP const)
- Build-chain: `tools/verify_embed_1928.py` verifies the embedded page
  blob is a byte-exact gzip roundtrip of `page/survey.html` and that
  the served page carries the stamp + the capture-only/bulk-send ids
  (`chkCapOnly`, `btnSendFrames`) — run it after any page edit + repack.

## S14P-1928 — capture-only bursts + manual bulk send (02 Oct)

Console-led optimisation corpus path (design S14-CWC-PLAN.md §13;
evidence reports/console-decode-20261002-run2-6.md + the sweep JSON):

- **Capture-only burst mode** (page checkbox `chkCapOnly`): the burst
  choreography is UNCHANGED — P00 1 s primer 50%-on (exactly N/2 lit,
  AE settle) → P01..P17 frame-bits planes at `cwcSettle` (floor 70) →
  fast all-on master with 70 ms flush — but everything after the
  master grab is skipped: no registration chain, no health guard, no
  in-page decode, no result view, no CWCDEC/CWCDECS/CWCSTATS ship,
  no auto-ship. Burst start logs `cwc capture:` (vs `cwc burst:`).
- **Frames accumulate in benchStore ACROSS bursts** (run-numbered
  labels `cwc:rN:*`); the Burst button re-enables promptly after each
  burst (benchUploading never rises in capture mode).
- **'Send frames (all)'** (`btnSendFrames`): manual bulk ship of the
  whole accumulated store in ONE benchPull over WS; non-destructive —
  the store KEEPS its frames after the send (re-send is a re-ship),
  cleared only at the next decode-mode burst start / reload.
- **Store wrap in capture mode drops the OLDEST frame with a loud log
  line naming the lost label** (no silent overflow).
- Rationale: the box's HTTPS kept dying (TLS-heap wedge, below);
  raw-frame console decode is the reliable path while the page stays
  a camera. This is how the 1710/2000 baseline + the 90-combo sweep
  corpus (95 frames, runs 2–6) were captured.
- **QA: PASS, exit 0, 10 checks** incl the NEW check 10 — capture-only
  ×2 bursts accumulate **38/38 frames** with ZERO decode/CWCDEC/
  CWCSTATS/auto-ship, then 'Send frames (all)' ships all 38 in one
  benchPull and re-enables the buttons; no new `E ` errors. Checks
  1–9 (stamp, CFG+burst, ship, chain, decode, canvas, test-mode,
  bit-exact multi-string, CFG-replay) all still pass.

## 02 Oct afternoon session (1927→1928 + the console round)

- **Sweep verdict (console, 90 combos × 5 runs, N=400)**: gates
  **mask 100 / amp 40 / margin 6 + fullres rad4** gained everywhere —
  runs 2–6 = 354/309/358/389/300 = **1710/2000 (85.5%)**; old default
  (150/60/10) = 1458; best fullres-OFF combo = 1197 (fullres rad-4 is
  worth +513 — mandatory off-tripod). **The promoted
  set IS today's optimum** — shipped as COMPILED DEFAULTS in
  S14P-1927/1928 (page CFG + firmware): `cwcMaskThr` 100,
  `cwcAmpGate` 40, `cwcMarginGate` 6, fullres refine on. suppress=1
  unchanged; suppress 2 remains harmful per the 01 Oct sweep.
- **Missing-LED repair (runs 2–6 corpus)**: the daemon writer damaged
  8 of 95 frames (7 truncated + r2 p01, which was both
  morning-blocked and PIL-broken; r2 p00 was the other
  morning-blocked label) — all **95/95 re-derived byte-exact from
  `runs/daemon/capture.txt`**; damaged originals preserved under
  `runs/daemon/runs/truncated_backup/` + `run2/morning_0843_backup/`.
  The 5-frame 12:40:26–40 r1 tail lives in `runs/daemon/runs/run1af/`
  (jpg+meta only, not decodable; morning r1 in git untouched, still
  truncated p01/p07/p13 as committed). Provenance:
  `runs/daemon/repair_report.json`.
- **Missing-LED profile (354+309+358+389+300)**: zero ids dead in all
  5 rounds; 35 ids missed ≥3 of 5; viewpoint-scattered, not
  identity-structured (worst 394–399, 246/248, 78–82) — no dead band,
  no codeword gap, and at N=400 no over-claim/phantom evidence.
- **TLS-heap wedge (twice today, root cause UNDIAGNOSED)**: esp-tls
  mbedtls `mbedtls_ssl_setup -0x7F00` (ALLOC_FAILED) storms killed the
  box's HTTPS ~08:32–08:43 (the S14P-1926 incident window) and
  12:57:15→12:58:03; the daemon died 12:58 and never recovered on its
  own (last phone capture 12:45:10). Working recipe (proven twice
  today): **RTS→EN pulse via bench serial** (flash-reset tap reboots
  the box; banner replays) → restart the daemon → verify
  `=== daemon start ===` + `[LOGA] persistent arm ON` in its output →
  then send directives via `runs/daemon/cmds/`. Heap-fragmentation vs
  leak vs socket pressure is not yet diagnosed.
- **Codeword-bank feasibility (02 Oct, computed + verified)**:
  9-of-18 @ 1600 codes CANNOT reach d≥5 (sphere bound 592; greedy
  206); **12-of-24 d_min 8 is feasible** — extended Golay [24,12,8]
  weight-12 subcode = 2576 codewords, pairwise d_min 8 verified,
  first-1600 and first-400 prefixes both d_min 8; d≥9 impossible
  (sphere bound 600 < 1600); cost +6 planes ≈ +30% burst/decode.
  **OPERATOR DECISION: the 12-of-24 era = S14R-0000** (new R-line,
  next build after S14P-1928; the P line closes here).

## S14P-1926/1927 — 02 Oct morning (incident fix + defaults flip)

- **1926** — CFG one-shot delivery gap FIXED (incident doc:
  reports/INCIDENT-S14P-1926-cfg-one-shot-gap.md): box replays a queued
  cfg on the first TWO drv? after every hello (applyCfg idempotent);
  fresh contexts always apply their own hello (connectWS._helloDone);
  burst start logs `E rig mismatch: page AxB vs box CxD` loudly; QA
  check 9 replays the actual incident shape (queue-while-up + reload).
- **1927** — compiled defaults = the FULL 8×200 rig (firmware
  nStr=8, nPerStr=200; page cwcN 1600, clamped to nStr·nPerStr at
  burst start) + the sweep-promoted gates as page defaults — the
  phone matches the console config by construction; mock defaults
  mirrored; QA PASS ×2.

## S14P-1923 — 8-string × up-to-200-LED support (02 Oct)

Design per S14-CWC-PLAN.md §6/§8/§11.4; codeword bank unchanged (1600 codes,
9-of-18, d_min 4; prefix property keeps any nStr×nPerStr ≤1600 free).

- **New WS binary command `frame-bits`** (client→box, exactly 206 B):
  `[0]='B' [1]=1 [2]=b [3]=flags [4..5]=u16 LE epoch [6..205]=bit-plane`,
  1600 bits LSB-first, bit j = LED id j; lane=j/nPerStr, px=j%nPerStr;
  ids ≥ nStr*nPerStr ignored; off-rig lanes/tails forced black; per-lane
  write, NO mirroring. Malformed → `{"err":"frame-bits shape"}`. Ack rides
  the normal latch path with the message's u16 epoch.
- **Per-string fuse clamp** on frame-bits: maxB = max(20,
  floor(255·2/(nPerStr·0.0142))) (2 A hold, 14.2 mA/px full white —
  byte-identical to fuseClampB at nPerStr=200; at 1600 px a 50%-duty
  plane is the binding case).
- **CFG nStr (1-8) / nPerStr (1-200)**: applied box-side
  AND page-side (mid-session re-CFG ok). sCfg budget widened 128→640 B
  (real max CFG is 413 chars; verifier `tools/verify_s12_cfg.py`).
  hello reply = `{"ok":true,"id":N,"fw":"poc_survey","px":nPx,
  "nStr":nStr,"nPerStr":nPerStr}`.
- **Page**: CWC planes now ship as ONE frame-bits binary each (was JSON
  per-LED strings — a 1600-LED JSON paint would have died on the 4,096 B
  recv limit per the S12 lesson); master = all-bits-set paint; JSON `frame`
  mirrors lanes 2-8 only while nStr≤1 (single-string bench compat);
  `all`/`black`/`npx` are rig-wide (semantics are rig-level; under nStr>1
  a string-1-only black would leave 7 strings stale-lit).
- **Decode string-aware**: suppression windows and the conflict audit
  never pair ids across strings; led = global id 0..nStr*nPerStr-1;
  CWCSTATS gains nStr/nPerStr; CWCDECS gains strings/perString
  (ledcloud/2 §8 field names, ready for the export step). Sites never
  ride cwcStats (4,096 B WS cap) — they ship as chunked CWCDEC log
  lines (1,200 chars) + CWCDECS. In capture-only mode (1928) the
  decode/ship half is bypassed entirely.
- **QA (PASS, exit 0)**: existing single-string checks untouched; NEW
  burst-3 multi-string case CFG nStr=8 nPerStr=25 (200 ids across 8
  virtual strings): 18 frame-bits latches, every plane exactly N/2 lit
  rig-wide, and bit-exact per-lane proof — LED 57 (display L3, pixel 7)
  latch sequence matched its codeword 000110010111110100 across all 18
  planes.

### Burst anatomy (actual, corrects the plan's abstract)

NO all-off reference frame exists, and the primer is NOT an all-on frame:
the primer IS coded plane P00 itself, a 50%-on coded plane (exactly N/2
LEDs lit) HELD 1,000 ms for AE settle; P01..P17 follow at 100 ms settle
each (floor 70, CFG `cwcSettle`); then a fast all-ON master (JSON `all`)
is grabbed with only 70 ms of flush before AE re-meters. Decode diffs
planes vs that MASTER. 19 frames = 18 coded planes + master. AE lock
default OFF (`cwcAeLock 0`).

### Validation boundary (honest)

All multi-string page behaviour is mock-box + fake-camera QA-verified;
the REAL 8×200 rig HAS NOT BEEN SHOT yet (today's console baseline
rounds 2–6 ran CFG nStr=2 cwcN=400 box-perspective with the box ~3 m
off-axis). First real round:
CFG nStr=8 nPerStr=200 (or the installed shorter lengths), reload,
confirm header stamp + hello nStr/nPerStr in a STAT, one burst, decode,
conflict audit. The bench single string keeps behaving exactly as before
with nStr=1 (frame-bits also works there — same protocol, rig = one lane).

## S14P-1922 first handheld round (02 Oct 06:44, daemon run1)

- Phone telemetry: **confirmed 193/200, 5 spatial conflicts flagged
  (a26/b115, a28/b117, a77/b103, a78/b102, a147/b160)**, chain conf
  0.777–0.900 across all 18 registers, exp pinned 300.03, aem continuous,
  no motion flag (smooth drift dx≈6 px). vs S14P-1917 baseline phone
  190/200; console 195/200. ACCEPTED: within 2 of console parity, +3 on
  the phone baseline, conflict audit doing its job at suppress R=1.
- Captured via persistent bench daemon (runs/daemon/), LOGA armed;
  run1 frames decoded to runs/daemon/runs/run1/ (JPEGs local-only, not
  for GitHub per operator rule).

## S14P-1922 — score-time full-res NCC refine (01 Oct, wired in 1922)

Page decode now calls `nccRefine()` at score time (±4 px, stride 2,
full-res) before bilinear sampling, matching `tools/cwc_pos_decode.py`;
page-simulation harness verified page-vs-console ±1 LED per run, +0.20
LEDs ensemble mean (reports/page-simulation-s14p-1922.md).

## What changed since 1919 (01 Oct → 02 Oct)

1. **S14P-1922** — full-res NCC refine wired into page decode; first
   handheld round accepted (193/200 above).
2. **S14P-1923** — frame-bits binary command, nStr/nPerStr CFG box+page,
   sCfg 640 B, per-string polyfuse clamp, same-string suppression +
   conflict audit, ledcloud/2 fields in CWCDECS, QA multi-string
   bit-exact case.
3. **S14P-1924/1925/1926** — real-hardware CFG parse fixes (atoi offsets,
   sBitsMode latch, fuse-formula page clamp), result-chip restyle, and
   the CFG one-shot gap fix + rig-mismatch guard (incident doc above).
4. **S14P-1927** — compiled defaults = full 8×200 rig + promoted gates
   (mask 100/amp 40/margin 6).
5. **S14P-1928** — capture-only bursts + manual bulk send (top section);
   console baseline runs 2–6 (1710/2000), 90-combo sweep optimal,
   corpus repair 95/95, TLS-heap wedge recipe, feasibility computed,
   **S14R-0000 designated** for the 12-of-24 era; test-LED demo ran to
   completion (mechanical PASS; decode over-claimed on the partial rig
   — plan §14 honesty note; operator: no new one-LED-lighting UI).

## Earlier history (kept, accurate)

1. **S14P-1912** — direct-registration + full-resolution NCC refine in both
   page and Python (`--fullres-rad 4`, `FULLRES_STRIDE 2`); page prints
   missing-LED list; motion guard re-themed to `cwcGuardConf 0.70` /
   `cwcGuardRem 16` absolute vs master; drift line changed to successive
   differences; build-tied localStorage (new `BUILD` wipes stale saved CFG).
2. **S14P-1913** — page mask threshold default locked to `150` (was falling
   back to `175`); removed page-only ±3 px local-max filter; aligned page
   parabolic-refinement guard with Python (`peak - max(shoulder) >= margin`).
   (The earlier 1906-era mask-175 lesson is plan §10c history; 150 was the
   settled default until the 02 Oct sweep promoted 100 — see top section.)
3. **S14P-1914** — bilinear resampling in `stacksig` in both page and Python;
   best-effort AE-lock-after-P00 using `exposureMode: 'manual'` (with a
   frozen `exposureCompensation` that caused a dark image on the first test).
4. **S14P-1915** — tried all-on primer as master + AE lock, then coded
   P01..P17. Phone reported MOTION FLAGGED on small real movement and only
   87 LEDs, image very dark.
5. **S14P-1916** — reverted burst order: P00 1 s primer (coded plane — see
   Burst anatomy above; the "all-on primer" phrasing here was loose and is
   corrected there) → P00..P17 coded planes → fast all-on master grab before
   AE re-adjusts. AE lock reduced to only
   `applyConstraints({advanced:[{exposureMode:'manual'}]})` without touching
   `exposureCompensation`. Motion guard relaxed: `cwcGuardConf 0.65`,
   `cwcGuardRem 24`, added `cwcGuardStep 10`.
6. **S14P-1917** — **disabled AE lock by default** (`cwcAeLock: 0`). The lock
   code remains in place and can be re-enabled with `CFG={"cwcAeLock":1}`, but
   Android Chrome was honouring the manual-AE request and freezing exposure
   at the dark P00 value, collapsing phone confirms from ~190 to ~95. With
   the default off, exposure stays `aem=continuous` and detection returns to
   the pre-lock level.
7. **S14P-1918** — replaced the page's double box-blur mask with a separable
   5-tap Gaussian (`sigma ≈ 1.2`) to match console `cv2.GaussianBlur((5,5),
   1.2)`. Goal: blur parity phone vs Python decoder. Baseline from the
   S14P-1917 handheld run: phone confirmed 190 LEDs, console confirmed
   **195/200**; gaps were phone-only `[16, 27, 91]` with marginal phone
   margins `(14.6, 15.6, 10.4)`, and console-only
   `[4, 22, 25, 46, 99, 100, 122, 143]`. `cwcAeLock` remains default-off.
8. **S14P-1919** — added `CFG.cwcSuppress` default `1` (3 px suppression window
   vs previous 7 px), and added a spatial conflict audit in `CWCSTATS`. All other
   gates, blur, and peak-margin settings left unchanged per the sweep
   report (reports/ensemble-blur-suppress-sweep.md). QA passes.

## Files of record

- `page/survey.html` — S14P-1928: capture-only bursts (`chkCapOnly`) +
  manual bulk send (`btnSendFrames`), accumulating benchStore with loud
  wrap-drop; frame-bits shipping (one binary per plane), nStr/nPerStr
  page-side, same-string suppression+conflict audit, compiled gate
  defaults mask 100 / amp 40 / margin 6, cwcN 1600 clamped to rig, fullres
  refine, AE lock default OFF, bilinear stacksig, build-tied
  localStorage, `cwcSuppress` default `1`.
- `firmware/poc_survey/poc_survey.ino` — S14P-1928 banner (`capture-only
  bursts + manual bulk send`) + PAGE_BUILD; WS binary `frame-bits`
  handler; CFG nStr/nPerStr (defaults 8/200 per 1927); per-string fuse
  clamp.
- `tools/cwc_pos_decode.py` — bilinear stacksig, full-res refine
  (`--fullres-rad 4` mandatory off-tripod); today's console baseline +
  sweep ran through it.
- `tools/cdp_1904_check.py` — S14P-1928 pre-flash QA (STAMP const expects
  that stamp; 10 checks incl capture-only 38/38 + replay check 9).
- `tools/verify_embed_1928.py` — build-chain verifier (page blob ==
  gzipped page byte-exact).
- `tools/bench_daemon.py` — persistent LOGA arm; per-burst JPEG ship to
  `runs/daemon/runs/<label>/`; directives via `runs/daemon/cmds/`.
- `reports/console-decode-20261002-run2-6.md` +
  `reports/console-decode-20261002-sweep-results.json` — the console
  baseline + sweep evidence (§13 of the plan mirrors it).
- `runs/daemon/repair_report.json` — corpus-repair provenance (95/95).
- `tools/codewords_9of18.json` — source codeword bank, valid for 1600 LEDs
  (the P-line bank; the S14R-0000 bank will be the Golay 12-of-24
  subcode, not built yet).
- `reports/phone-vs-console-cwc-gap.md` — phone-vs-console decoder
  differences (mask threshold, local-max filter, parabolic guard).
- `reports/page-simulation-s14p-1922.md` — page-sim ±1 LED parity result.
- Retired bench-era docs (old pull recipes etc.) live in `archive/`
  (see `archive/README.md`); §10b-era harness details remain in
  S14-CWC-PLAN.md §10b.

## Known open work (verify before claiming success)

1. **TLS-heap wedge root cause** — undiagnosed; the RTS→EN reset recipe
   is the workaround (top section). Watch for `-0x7F00` storms after
   long uptime.
2. **Phone-vs-console parity for runs 2–6** — UNVERIFIABLE: no
   CWCSTATS/CWCDEC shipped for runs 2–6 themselves (capture-only
   mode; the daemon died 12:58 before the burst ever got a decode
   ship). The only later telemetry (14:37–14:40) was the queued
   test-mode session, not runs 2–6. Console numbers are the only
   decode ground truth for the 1710/2000 corpus.
3. **Real 8×200 rig round** — the multi-string path is QA-verified on
   the mock only. First real round: CFG nStr=8 nPerStr=200 (or the
   installed shorter lengths), reload, confirm header stamp + hello
   nStr/nPerStr in a STAT, one burst (capture-only or decode mode),
   decode, conflict audit (see Validation boundary above).
4. **S14R-0000 (12-of-24 era)** — designated, NOT started
   *(SUPERSEDED 02/03 Oct: BUILT — S14R-0000 bank swapped + verified,
   0001 operator-UI + exposure round, 0002 brightness calibration + QA
   PASS; full record S14-CWC-PLAN.md §15; real-rig 0002 round pending
   the flash)*: Golay subcode bank (2576 codewords, d_min 8) + what a
   24-plane burst costs in choreography/AE.
5. **Phone-side 1600-id display practicalities** — the result view boxes
   200 ids fine; how a 1600-id map should render/select on the phone is
   an open UI question.
6. **Test-LED demo: RAN AND COMPLETED — mechanical PASS, honest
   verdict** — CFG `{"cwc":1,"cwcN":600,"nStr":3,"nPerStr":200,
   "cwcSuppress":1,"cwcMaskThr":100,"cwcAmpGate":40,"cwcMarginGate":6,
   "cwcTestMode":1,"cwcTestLed":599}` delivered (id 599 = string 3
   pixel 200); completed round 14:38:18→14:39:15 under S14P-1928
   (CWCSTATS: n 18 planes, testBits [3,4,6,8,11,12,13,15,17], chain
 conf 0.817–0.902, confirmed 275, conflicts 33). CORRECTED 02 Oct eve
 from operator ground truth: strings 1-3 WERE connected (600 lamps
 installed; many hidden/colocated). The CFG rig keys (nStr=3/nPerStr=200)
 applied, but the decode bank ran the FULL 1600 — wire claims extend to
 id 1570, so every claim with id >=600 is a d=4 cousin-phantom relabel
 in the unpainted id space. The ~sub-600 confirms (incl. the 'led 599'
 chip: x267,y422 amp 153 margin 94.5) are substantially real and
 PLAUSIBLY GENUINE — with 3x200 installed, ~275 real detections from
 one viewpoint is consistent with many hidden/colocated lamps per the
 operator. Standing rule reaffirmed: decode domain must clamp to
 min(cwcN, installed) — a >cap relabel is contamination, not detection.
 Also note: the page showed nStr=8 after the S14R flash because boot
 restores compiled defaults with an empty CFG slot; the queued rig
 CFG re-applies on the page's next drv? polls (15:44 re-queue done).
 Verdict: the test-mode path works end-to-end
   mechanically; interpreting it requires installed-count == cwcN.
   **OPERATOR DECISION (verbatim): no new one-LED-lighting UI
   feature — the existing all-on button already shows where to cut a
   string.** (Full record: S14-CWC-PLAN.md §14.)
7. **ledcloud/2 §8 export tool** — CWCDECS now carries strings/perString
   (the §8 field names); the export converter itself is NOT built yet.
   Console verdicts stay authoritative for cloud export.
8. **Page-side drv?-stall self-recovery** — still open (B119 class,
   S14P-1925 note).

## How to continue this session *(SUPERSEDED 03 Oct — the active build is
S14R-0002, QA-PASS, NOT YET FLASHED; the flash + first real 0002 round are
the next actions, and continue-instructions now come from the S14R handoff
doc / plan §15. Kept unedited as the P-line-era recipe.)*

1. Confirm page shows `S14P-1928` and WS open. If the box's HTTPS is
   dead (`mbedtls_ssl_setup -0x7F00` storm in the daemon log), apply
   the RTS→EN reset recipe (top section) BEFORE anything else: pulse
   reset via bench serial → banner replays → restart the daemon →
   verify `=== daemon start ===` + `[LOGA] persistent arm ON` → then
   use `runs/daemon/cmds/`.
2. Real-rig round per open item 3 (CFG nStr/nPerStr → BURST → capture-
   only accumulate → 'Send frames (all)' → console decode), captured
   through the bench daemon. The old LOGP→BRAMP per-pull recipe is
   retired (archive/S14-BENCH-SESSION.md) — `tools/bench_daemon.py`
   is the primary path; in capture-only mode the page does NOT
   auto-ship, the operator presses 'Send frames (all)'.
3. Decode the shipped frames console-side
   (`tools/cwc_pos_decode.py`, fullres rad 4, mask 100 / amp 40 /
   margin 6); compare against the 1710/2000 runs-2–6 baseline.
4. Everything through S14P-1928 is committed and PUSHED (origin/main =
   6b554b2). Push the next good state with the PAT at `~/LED_PAT.txt`;
   no new images to GitHub (ALL bench imagery is gitignored — operator
   rule; metas/ledpos.json/direct_shifts.json/cwc_frames.txt stay
   tracked).
5. If the round is bad: dark image → check `cwcAeLock` was not enabled
   (`AE locked to manual` in the log); motion flagged → note which guard
   tripped (conf/rem/step) and adjust `cwcGuardConf`, `cwcGuardRem`,
   `cwcGuardStep` over serial; decode short → check per-string conflict
   flags and the suppress radius (`cwcSuppress`).

## Repo state

*(RE-STAMP 03 Oct — superseded: origin/main has ADVANCED through the
S14R era to **3f22c2a**; the P-line statement below is the state as of
the 1928 close.)*

- Branch `main` AT origin/main = **6b554b2** — S14P-1928 committed and
  PUSHED. (Older notes saying b461654 / "local commits unpushed at
  00c929c" are stale.)
- Commit 6b554b2 message carries the session summary: console baseline
  1710/2000, sweep promoted, corpus repair 95/95, TLS-wedge recipe.
- `.gitignore` (since 1928): ALL bench imagery is local-only —
  `runs/daemon/runs/*/*.jpg` + `truncated_backup/` +
  `morning_0843_backup/` + `runs/qa-1904/*.png` + `led_overlay.png`;
  **metas, ledpos.json, direct_shifts.json and cwc_frames.txt packs
  stay tracked**.
- No new images pushed to GitHub (operator instruction).

## Build cycle (every page/firmware edit)

1. Bump `BUILD` in `page/survey.html` and `PAGE_BUILD`/banner in
   `firmware/poc_survey/poc_survey.ino`.
2. Update `tools/cdp_1904_check.py` expected stamp (STAMP const), and
   the `verify_embed_1928.py` stamp if it drifts from the build.
3. `python3 firmware/tools/pack_page.py`
4. `arduino-cli compile --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" firmware/poc_survey`
   `PartitionScheme=min_spiffs is MANDATORY` — never compile bare:
   `esp32:esp32:esp32c6` selects the default partition (app cap 1,310,720 B)
   and this firmware is ~1.33 MB → 101% "Sketch too big". That is a command
   artifact, not code growth (see "Flash-size fact" below).
5. `arduino-cli upload --fqbn ... -p /dev/ttyACM0 firmware/poc_survey`
6. Verify boot banner; restart serial capture reader (flash resets port).
   QA first (and any time you need the harness):
   `/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/cdp_1904_check.py`
   — the PROJECT VENV IS GONE (ambient python3 has no websockets; use the
   Hermes-agent venv, websockets 15.0.1 + websocket-client 1.9.2).
   `mock_box.py` spawns its own box; never hand-start one before QA.
7. Oliver reloads phone; confirm page header + WS open.

## Flash-size fact (01 Oct, verified — resolves the "101%" scare)

- Standing scheme `min_spiffs`: app cap = 0x1E0000 = 1,966,080 B (core
  3.3.11 `tools/partitions/min_spiffs.csv`). Verified compile of the
  S14P-1922 tree on this scheme: `Sketch uses 1322190 bytes (67%)`, exit 0.
- A bare `esp32:esp32:esp32c6` compile (default partition, app cap
  0x140000 = 1,310,720 B) gives `Sketch uses 1331528 bytes (101%)` —
  reproduced byte-exact. The scratch-era HANDOFF-S14P-1922-SIZE ran the
  bare command, hence its "flash-blocked" premise and its huge_app
  "workaround"; huge_app (app 0x300000 = 3,145,728 B, no OTA, no SPIFFS)
  is NOT needed — min_spiffs holds the sketch at 67% with ~644 KB headroom.
- Real sketch growth since the 66% era is modest: S13L (Sep 24)
  1,297,088 B → now 1,322,190 B = +25,102 B of CWC feature work (page
  blob +11.4 KB gz ≈ +14 KB flash; C++ +9.7 KB → ~1.5 KB compiled; rest
  ELF/packing). A bare-default compile squeaked by at 99% back then and
  fails only because of that +25 KB — on the worked axis (min_spiffs)
  nothing ever left the 65–67% band.