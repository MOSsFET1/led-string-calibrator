# S14R-0002 first-burst verification bundle (03 Oct 2026)

Repo: /home/nellie/projects/led-display/poc_survey @ f909908. Read-only audit;
/dev/ttyACM0 untouched; no daemon restart. capture.txt = 25,825,135 bytes,
31,057 lines (stat: mtime 2026-10-03 09:46:22.857560399 +1000).
Today's capture portion = L29986 `[09:23:13] === daemon start ===` … L31057
`[09:46:22] [PHONE-LOG] end (persistent arm stays)`. Earlier hours (06-21) in
the same file are 02 Oct era (daemon starts at L1, 1112, 2047, 3190, 4021, …).

## (1) Wire-to-disk byte check — VERIFIED

- FRAME-line census whole file by hour: 06:19, 08:21, **09:25**, 10:19, 11:19,
  12:100, 14:28, 16:25, 17:51, 18:100, 20:100, 21:75. Exactly 25 hour-09 FRAME
  lines; zero hour-09 FRAME lines before today's tail (L>=29986).
- The 25 groups: FRAME 09:44:35 (p00) → 09:46:18 (master), labels
  cwc:r1:p00..p23 + cwc:r1:master, one occurrence each in 09:44:00-09:47:00.
  Deeper same-label collisions (cwc:r1:* appears 3-9x per label file-wide from
  02 Oct and P-era) were separated purely by timestamp, as required.
- Protocol parse: FRAME json → N× `[ts] [PHONE] FJPEG <b64>` → `[ts] [PHONE] FEND`
  (chunk marker `'] [PHONE] FJPEG '`, 38-44 chunks/frame). 582 FRAME groups
  file-wide; 1 dangling group without FEND, pre-today (02 Oct 17:41 window);
  today 25/25 have FEND.
- b64 join + decode vs disk: **25/25 byte-exact** (size equal AND sha256 equal);
  all start ffd8 / end ffd9 JPEG magic. Sizes e.g. cwc_r1_p00 25,699 B,
  cwc_r1_p23 25,249 B, cwc_r1_master 29,133 B — wire == disk for all.
- Meta cross-check (25/25 pass, 0 mismatches): disk meta label/exp/t/W/H equal
  the wire FRAME json; meta wire_ts == FRAME line ts; meta fend_ts == FEND ts.
  Sample p00: exp `exp=500.05 aem=continuous ev=-1 fd=0.00`, wire_ts 09:44:35,
  fend_ts 09:44:39 on both sides; master: wire_ts 09:46:18, fend_ts 09:46:22.

## (2) BSTATS {} on the wire — verifier verdict: works as coded; docs overclaim

- Wire: L30031 CWCDECS; L30032 `[09:44:35] [PHONE] CWCSTATS ` carries the full
  telemetry: `{"build":"S14R-0002","n":24,…"nStr":3,"nPerStr":200,"bright":120,
  "histMed":47,"clipPct":0,"probeIters":1,"coreP90":237.4,
  "probeSteps":[{"L":120,"P90":237.4,"clipPct":0,"histMed":47}],
  "expAtBurst":"exp=500.05 aem=continuous ev=-1 fd=0.00","chain":[24 entries],
  "decode":{"ampGate":40,"marginGate":6,…,"confirmed":373,"conflicts":44}…}`.
  L30033 `[09:44:35] [PHONE] BSTATS {}`.
- All 18 BSTATS lines in the whole file are `{}`: L27, 1135, 1982, 3219, 4081,
  4929, 8418, 8708, 9672, 11057, 11115, 11409, 13210, 17431, 24430, 26225,
  28318, 30033. (17 of them are 02 Oct; L30033 is today.)
- Emitters — page/survey.html is the ONLY source; the .ino has no BSTATS/
  CWCSTATS emitters (poc_survey.ino L1486-1487 is comment only):
  - Ship: survey.html L1473 `await sendCmd({ cmd:'logc', d: 'BSTATS ' +
    JSON.stringify(benchStats || {}) }, …)` inside benchPull(); CWCSTATS ship
    L1470 (gated on `if (cwcStats)`).
  - CWC-burst branch fills cwcStats with ALL probe fields, L1258-1277 (probe
    fields L1266-1271, expAtBurst L1272); benchStats set to null at L1083 and
    NEVER refilled on this branch (benchStats occurrences: 835, 1083, 1395,
    1408-1409, 1431, 1473, 1537, 1542).
  - benchStats with probe fields is filled ONLY in the all-on movement-burst
    else-branch (L1395-1407; probe fields L1402-1405, expAtBurst L1406) and by
    the standalone PROBE directive (L1542-1544).
- Design evidence: survey.html L1313-1315 comment — "original all-on movement
  burst (unchanged; CWCSTATS added so the pull ships stats even when BSTATS
  loses the arm race)". Plan §15.6 (S14-CWC-PLAN.md L958-962): "BSTATS/CWCSTATS
  carry the probe + exposure telemetry"; L957: probeOnly probe ships as
  CWCSTATS/BSTATS. README.md L137-138: "every burst's BSTATS now carries
  {bright, histMed, clipPct, probeIters} + expAtBurst".
- Verdict: not a lost-data bug — the probe/telemetry contract is met on the
  wire via CWCSTATS (a deliberate, commented carrier). But `benchStats` stays
  null through the CWC branch (this era's primary burst path), so the README/§15.6
  claim that BSTATS itself carries the fields is unfulfilled on CWC bursts;
  JSON.stringify(null||{}) = {} at L1473. Structural, every session, by code.
  Candidate fix: refill benchStats in the CWC branch (~6 lines) or stop shipping
  bare {}.

## (3) evBias -3 — plumbing verified; -3 NOT active in this burst

- Burst-start hook: survey.html L1122 `applyEvBiasAtBurst();` inside benchRun,
  context comment L1118-1121 "S14R-0002 exposure: iOS runs POI-only (NO evBias
  constraint, ever); Android re-applies the CFG evBias AT BURST START (dir D:
  -3 stops at burst start)".
- Function: survey.html L453-462 — iOS skip `_evLastApplied='ios-none'` (L457);
  Android L458-461 forwards CFG.evBias to applyEvBias(); dedupe guard L459
  `if (v === _evLastApplied) return;`. applyEvBias (L432-441) clamps to camera
  range L436 `Math.max(caps.exposureCompensation.min || -3, Math.min(CFG.evBias,
  caps.exposureCompensation.max || 3))` then applies exposureCompensation.
- CFG chain: pollDrvOnce L1500 `if (r.cfg) applyCfg(r.cfg);` ("cfg BEFORE
  directives: burst reads CFG synchronously"); applyCfg def L210. Firmware
  side: poc_survey.ino L1513-1533 (CFG= queue, sCfgEsc into sCfg, sCfgReplay
  re-arm), reply embedding L1277-1314.
- No `cwcEvBias` symbol anywhere; page default CFG.evBias is **-1** (L89).
  The literal -3 exists only as: comments (L449, L1119), the clamp bound (L436),
  and tools/tuning_s14r0002.json `exposure.androidEvBiasBurstStart: -3`.
- Wire evidence: today's capture portion (L29986-31057) contains ZERO [CMD]/[CFG]
  directive lines — nothing delivered evBias=-3 this session. And the on-wire
  exposure readback is `ev=-1` on every frame: expLine() (L499-507) reports
  getSettings().exposureCompensation, so the APPLIED bias was -1, not -3 —
  consistent with default -1 + no CFG delivery. exp=500.05 aem=continuous is
  identical on all 25 frames = the same continuous-AE regime as the 02 Oct
  android corpora. Readback meaning: ev=-1 is the track's applied exposure
  compensation; had -3 applied, it should read -3.
- Verdict: code plumbing VERIFIED (file:lines above); delivery of -3 NOT proven
  and readback CONTRADICTS it — flag to main session (CFG evBias=-3 must be
  queued for a real -3 burst).

## (4) Daemon status — DOWN (verified)

- `pgrep -af 'bench_daemon\.py'` matched only the probing shell itself; earlier
  `ps aux | grep -iE 'bench_daemon|poc_survey'` → PS_NONE rc=1; full ps shows
  no bench_daemon process (only hermes tooling).
- No *.log under runs/daemon/; capture.txt is the only log. It ends L31057
  `[09:46:22] [PHONE-LOG] end (persistent arm stays)` — a clean reader-session
  close, no exit/crash/traceback lines; file mtime 09:46:22 today.
- In-window box health (today): TLS wedge signatures pre-burst —
  `-0x7780` handshake failures 09:43:09-09:43:13 (L29988-29999) then `-0x7F00`
  setup/`session creation failed` 09:43:13-09:43:16 (L30000-30013) — then quiet
  until the burst shipped 09:44:33+. Daemon start today: L29986 `[09:23:13]`.
- Verdict: bench_daemon DOWN as of check time (09:55-ish); last file activity
  09:46:22 with a clean close marker; no crash signature visible in its log.

## (5) gitignore guard — VERIFIED

- `git check-ignore -v runs/daemon/runs/s14r2-and-0944/cwc_r1_p00.jpg` →
  `.gitignore:20:runs/daemon/runs/*/*.jpg` (rc 0) — images excluded.
- `git check-ignore -v …/cwc_r1_p00.jpg.meta.json` → rc 1, NOT ignored — metas
  trackable.
- `git ls-files -- runs/daemon/runs/s14r2-and-0944/` → 0 files; git status shows
  `?? runs/daemon/runs/s14r2-and-0944/` — nothing staged yet, whole dir untracked.

## (6) Docs spot-check — DISCREPANT (2 flags)

- §15 exists: S14-CWC-PLAN.md L795 `## 15. The S14R era as-built (02 Oct,
  S14R-0000 → S14R-0002)`. Subsections (grep ^###): **15.1** S14R-0000 — 12-of-24
  Golay bank; **15.2** First real 12-of-24 rounds + round-1 miss investigation;
  **15.3** Android exp500 corpora (r2-r5); **15.4** S14R-0001 — operator UI +
  (iOS) exposure round; **15.5** CLI identity lever (queued); **15.6** S14R-0002
  — pre-burst brightness calibration + adaptive thresholds; **15.7** Where the
  S14R era stands / what is next. **SEVEN subsections, not six as briefed.**
- Tuning pointer: L936 `tuning evidence \`tools/tuning_s14r0002.json\`` (also
  README L135). File stat: 2717 bytes ✓.
- Constants cross-check (json ↔ plan §15.6 ↔ page code):
  - target P90 band 235-250: json coreP90Min/Max = 235/250 = page bProbeMin/Max
    (L169-170) = plan L941 ✓
  - clip ≤5%: json clipMaxPct 5.0 = bProbeClipHi 5.0 (L171) ✓
  - start L 120: json startL = bProbeStart 120 (L168) ✓
  - stepMin 24: json "stepMin": 24 = bProbeStep 24 (L171) — page comment "legacy
    step (the active adjustment is multiplicative)" ✓
  - clamp 1.5x/step: json adjust text; code L1040 `Math.max(0.667, Math.min(1.5,
    tgt/P90))` ✓
  - settle 2000 ms: json waitSettleMs = bProbeDelayMs/waitSettleMs 2000 (L174-176)
    ✓ (fixed hold, no settle gate — plan L944-945 ✓)
  - **MISMATCH (a): "solid 300 ms"** — no solid-paint duration constant exists in
    tuning_s14r0002.json or plan §15.6. The only source is HANDOFF-S14R.md L79
    `{paint solid-ON at cwcBright=L for cwcSolidMs=300 ms}` — `cwcSolidMs` exists
    in ZERO code files (repo grep). Actual page behaviour: probeMeasure (L963-966)
    paints solid all-ON then `sleep(Math.max(500, CFG.bProbeDelayMs|0||2000))` —
    ≥500 ms, nominally 2000 ms. The 300 ms figure is not implemented and not
    specified in the plan.
  - **MISMATCH (b): adaptive-mask clamp bounds** — brief says "clamped 30..100";
    json/plan/code all encode thr = min(cwcMaskThr, max(45, 1.12×histMed)):
    json maskK 1.12 + maskFloor 45; page L1803-1806; CLI cwc_pos_decode.py
    L296-301/382-390. Effective clamp is **45..100** (maskThr=100 upper). "30"
    appears only in the 3b22b35 commit MESSAGE ("clamp(maskA+maskB*histMed,30,100)")
    — superseded wording, no 30 in json/plan/code.

## Provenance

- Parser: hermes kernel, stdlib only (json/base64/hashlib/re); capture.txt read
  once into memory (31,057 lines); no writes outside scratch; /dev/ttyACM0 never
  opened; daemon never restarted.
- All 25 sha256 wire==disk; details in byte_check JSON of the parent's reply.