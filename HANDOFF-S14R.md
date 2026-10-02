# Handover — S14R (LED survey R-line: 12-of-24 coded bursts)

Written 02 Oct late (session of S14R-0002 completion). **If only one
thing is true, it's this:** box firmware is being flashed to
**S14R-0002**, commit `3f22c2a` is origin/main, and every S14R-era
claim below is backed by a verified console report in `reports/`.
Author of this handover checked primary evidence (wire logs, QA logs,
git) — trust it over memory notes; when it conflicts with anything,
re-verify against the repo.

## 0. 60-second orientation

- **Repo**: `/home/nellie/projects/led-display/poc_survey` (no spaces —
  renamed from "POC LED survey" 02 Oct; all refs updated, skills too).
- **Rig**: ESP32-C6 box (`/dev/ttyACM0` @115200), AP `LED-SURVEY`,
  page https://192.168.4.1/ (TLS+wss). Phones run the page; bursts are
  painted LED-by-LED as 24 coded planes; page+box stream frames back;
  console decodes offline.
- **Physical rig truth (operator)**: strings 1, 2, 3 connected =
  600 lamps; MANY hidden or colocated; bright wall behind most
  (their glow lights it). All-on button shows the cut point.
- **Scheme**: 12-of-24 Golay-derived code, d_min 8, 1600 codewords,
  800 complement pairs, per-plane exactly N/2 lit (50% duty),
  frame-bits 246 B = 6+240 B (1920 bits). FROZEN — do not regenerate;
  `tools/cwc_bank_check.py` is the verifier of record.
- **Decode discipline**: promote gates only WITH a position guard
  (site within ~6 px of a registered anchor / ≤5 px dedup of a
  confirmed claim). Unguarded gate relaxation counted 43–59%
  phantom "recoveries" (impostor audit, round-2 report).

## 1. Build ladder (one screen)

| build | date | what | key number |
|---|---|---|---|
| S14P-1926/27/28 | 01–02 Oct | CFG replay on hello; full-rig compiled defaults (nStr 8, cwcN 1600) + promoted gates; capture-only bursts + manual bulk send | console corpus 1710/2000 @ mask100/amp40/margin6 |
| S14R-0000 | 02 Oct | 12-of-24 Golay bank migration (from 9-of-18), 24-plane bursts | d_min 4→8; QA 10/10 PASS; phone 309/600 round-1 |
| S14R-0001 | 02 Oct | burst under camera; 1–8 string row (box CFG still authoritative); survey+wake buttons removed; evBias re-apply at burst; iOS POI tap (cwcPoi) | POI pixel-inert (measured); exp='' readback gap |
| S14R-0002 | 02 Oct | **pre-burst brightness probe** (solid-ON `cwcSolidMs`, lamp-core P90→knee 235–250, clip ≤5%); **adaptive mask thr** `min(cwcMaskThr, max(45, 1.12·histMed))`; BSTATS telemetry `{bright, histMed, clipPct, probeIters}`; chip dark square removed; Android evBias −3 at burst; POI tap feedback | QA PASS exit 0; verify_embed 47,408 B roundtrip |

Compile FQBN (mandatory, bare FQBN overflows):
`esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
— ~1.33 MB = 67%.

## 2. Measured decode campaigns (console, n=600)

| corpus | build | bursts | per-burst | union | never | failure axis |
|---|---|---|---|---|---|---|
| s14r-0000-16h16 | R0 | 1 (iPhone) | 283 (phone 309/42c) | — | — | wall luma (exp 700) |
| s14r-0000-17h43 | R0 | 1 (iPhone, aimed) | 317 (phone 328/31c) | — | — | wall luma (AE 200) |
| s14r-and-r2..r5 | R0 | 4 (Android, exp 500) | 429/549/492/387 | **583/600** | 17 | mixed; r4 = mask-stage (dark view) |
| s14r-ios-r1..r4 | R1 | 4 (iPhone) | 287/440/487/257 | **534/600** | 66 | **interference**: 0 mask/amp ids, all rival-eats |
| (total) | | 14 verified rounds | | — | — | see two laws below |

Reports: `reports/s14r-0000-16h16-miss-classification.md` (has a
CORRECTION block — the amp25 "+97" there was refuted; true +33),
`reports/s14r-0000-17h43-aimed-round-decode-and-fixpath-verification.md`
(the impostor audit — READ FIRST for any gate work),
`reports/s14r-android-exp500-four-burst-console-decode-and-matrix.md`,
`reports/s14r-0001-ios-poi-console-decode-exposure-and-cross-round.md`.

## 3. Two laws that govern everything

1. **Wall/headroom law**: `amp ≈ k·(255 − wall)`, k per-exposure:
   0.538 @ exp 699.97, 0.87–1.03 @ 500, 0.97 @ 200 (AE).
   Saturated lamp cores (ON≈255) strangle amp; wall is mostly LED
   spill — **overdrive wastes headroom on the wall**.
   → This is why the brightness probe drives lamp cores to the
   just-below-clip knee (P90 235–250, clip ≤5%) instead of maxing.
2. **Interference/ownership law**: when photometry is fixed, misses
   become rival-codeword contests (two codes claim one lamp region;
   suppression eats the loser's evidence). iOS corpora: 100% of
   never-confirmed ids are contests. The page already demotes ≤5 px
   rivals to conflicts; **the CLI still eats them** — parity redesign
   is the top open item.

## 4. S14R-0002 brightness probe (the headline feature)

- **Flow on Burst**: `cwcAutoBright` (default on): up to 3 iterations
  of {paint solid-ON at `cwcBright`=L for `cwcSolidMs`=300 ms} →
  wait 2 s (spacing) → video snapshot → measure lamp-core P90, clip%,
  histMed → `L ← L × targetP90/measuredP90` (clamp ±1.5×; start 120,
  stepMin 24) → final level → **hold 2 s** (`waitSettleMs`; operator
  explicitly killed the earlier settle-gate design — no stability
  checks, just a fixed 2 s) → burst.
- **Metric chosen by operator**: lamp-core P90 minus local bg-ring
  median, driven to the knee. Constants in
  `tools/tuning_s14r0002.json` (source of truth; calibrated from all
  14 corpora — which were ALL overdriven at fixed bBurstB=150:
  coreP90=255 everywhere, clipPct 17–56% — the knee band is the NEW
  regime the probe aims at).
- **Telemetry**: BSTATS no longer `{}`: per probe step
  `{L, P90, clipPct, histMed}` + final `{bright, histMed, clipPct,
  probeIters}` — this was the exposure-readback fix (exp='' metas made
  0001's POI unverifiable). Check BSTATS on every 0002 burst.
- **Exposure levers**: Android `evBias=-3` applied at burst start
  (re-apply hook from 0001 makes it take effect); iOS has no evBias —
  POI tap + feedback reticle; box-side `cwcBright` overrides both
  (camera-agnostic photons lever).

## 5. Standing recipes (box + daemon)

- Flash (main session only!): stop bench_daemon first, then
  `arduino-cli upload --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" -p /dev/ttyACM0 firmware/poc_survey`
  — a first attempt can fail if the daemon holds the port. Verify by
  RTS-pulse + banner readback (`=== poc_survey S14R-0002: ... ===`).
- **Boot defaults trap**: flash boots compiled defaults (nStr=8,
  cwcN=1600) with an EMPTY CFG slot; first page hello shows nStr=8 on
  the phone. Re-apply the rig CFG
  (`CFG={"cwc":1,"cwcN":600,"nStr":3,"nPerStr":200,"cwcSuppress":1,"cwcMaskThr":100,"cwcAmpGate":40,"cwcMarginGate":6}`)
  via `runs/daemon/cmds/*.txt` — replays on the page's next polls.
  **cwcN must equal installed** (600): decode claims ≥600 under a
  smaller cwcN are cap-gap phantoms (measured to id 1570 once).
  Standing fix to verify in code: clamp decode domain to
  min(cwcN, installed).
- **Label-collision trap**: page reload resets the phone run counter
  → `cwc:rN:*` labels can collide with old run dirs → skip-if-exists
  silently drops new frames. Rule: keep the label space clean (move
  old runN dirs aside before a new phone connects) and extract from
  the wire (`capture.txt` FRAME→FJPEG→FEND groups, base64) — proven
  byte-exact 10/10 dirs; frame writes can be truncated by the 30 s
  flush tick, and the wire re-derivation repairs them (95/95 done).
- **TLS-heap wedge** (undiagnosed root cause): mbedtls −0x7F00/−0x7780
  connection-failure storms kill HTTPS+wss; ack retries/drv?-fails on
  the phone + stale capture = the signature. Fix: RTS→EN pulse via
  bench serial (rts=True; dtr=False; 0.1 s; rts=False), banner replay
  = box healthy; restart daemon after.
- Daemon: `tools/bench_daemon.py` (venv python), logs
  `runs/daemon/capture.txt`, executes `cmds/*.txt` (CFG/BURST one-shot),
  persists frames to `runs/daemon/runs/runN/`. ONLY the main session
  touches `/dev/ttyACM0`; subagents never open it.

## 6. Open work (ranked)

1. **CLI suppression/ownership redesign** — page-parity: same-codeword
   dedup only; ≤5 px rivals → conflict-flagged entries, not eaten.
   Populations: 108–343 contest ids/burst (iOS corpora), ~44 contest +
   13 suppressed per android set. Biggest single-burst lever.
2. **First S14R-0002 real-rig burst** — validates probe + BSTATS +
   adaptive mask (expect: knee-band cores, non-empty BSTATS, dark-view
   rescue ~+22 at no cost). Compare against s14r-and-r5 (387) and
   ios-r4-class views.
3. **POI tap real-rig validation** — 0001's tap measured pixel-inert;
   0002 adds feedback + BSTATS so either behaviour is observable.
4. **TLS wedge root cause** — 3 storms today; RTS recipe works but is
   a band-aid. Worth a dedicated probe session once decode work calms.
5. **Adaptive-mask edge case** — s14r-and-r5 site 398 (hairline,
   blur 100.9, amp 44.5) documented loss under the new rule; re-check
   if any operator report says "string-2 end".
6. **Harness speed** — QA runs 4.5–8 min with the probe choreography
   (mock box paints in real time); a fast-probe mock mode would help.

## 7. Environment pointers

- Venv (numpy/cv2/PIL/pyserial): `/home/nellie/.hermes/hermes-agent/venv/bin/python3`
- Console decoder: `tools/cwc_pos_decode.py` — `--bank
  tools/codewords_12of24.json` default; 9-of-18 legacy via --bank;
  `--mask-thr auto` = adaptive rule; numeric = legacy override.
- Bank verifier: `tools/cwc_bank_check.py` (rc 0 = good).
- Embed verifier: `tools/verify_embed_s14r.py` (stamp S14R-0002).
- QA: `tools/cdp_1904_check.py` — ~4.5–8 min; run DETACHED
  (`setsid nohup ... > /tmp/qa.log 2>&1 &`), poll the LOG, never wait
  in a capped call; mock box is self-spawned; the REAL port forbidden.
- Corpora: `runs/daemon/runs/s14r-*` (each 25 jpgs + metas); era
  imagery local-only per `.gitignore` (metas/ledpos/frames packs
  tracked). Quarantines: `s14p-drained-*`, `x-drain-*`,
  `s14r-camerablocked-*`, `run1af`, `truncated_backup/` — do not mix
  them into decode sets.
- Git: main == origin/main; PAT at `~/LED_PAT.txt`; commit discipline:
  image-grep the staged set (`git diff --cached --name-only |
  grep -icE '\.(jpg|png)$'` = 0) before every commit.

## 8. Operator style notes (matter for the next agent)

- Honest tested-vs-sim accounting wins; over-claims get corrected with
  ground truth (operator carries the physical truth of the rig).
- No images to GitHub (standing hard rule; grep before commit).
- Single-burst is the product target; multi-viewpoint is offline
  context only, not a dependency.
- Subagents for heavy reads/compute; main session = decisions + serial
  + git + verified results only. Verify every important claim with a
  tool before asserting it.