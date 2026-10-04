# Handover — S14R (LED survey R-line: 12-of-24 coded bursts)

Written 02 Oct late (S14R-0002 completion). Updated 03 Oct night after
the CAL-battery build + reflash. **04 Oct milestone update — if only
one thing is true, it's this:** box firmware is **S14R-0003c** (cal
battery, splice-fixed ship, pages from FLASH) and the **first COMPLETE
cal battery ran 10:33 04 Oct** (`done:true`, 39.4 min; 572 frames
relayed all-unique; E4 epochs r8–r11 at 25/25 on the correct rig
3×200/cwcN 600). **Corpus is fully repaired** (33 truncated jpgs
re-fixed in place from the wire, sha-manifested, EOI 528/528 — §10 +
`reports/s14r-0003c-wire-repair-inplace.md`) **and re-analyzed**
(photometry v2: NO conclusion flipped; E4 parity exact from run dirs;
§10 pointer + `reports/s14r-0003c-photometry-v2-repaired-corpus.md`).
The two known storm/wedge root causes are FIXED (benchPull splice in
0003c; daemon 0004(g) FEND-only writes, running since 17:16:08) and
remaining TLS-wedge mechanism work is instrumented (HEAPCAP, 0004 (a)
( b)). origin/main = `19174fd`. §6 holds the consolidated 0004 list.
Every S14R-era claim below is backed by a console report in
`reports/`.
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
| S14R-0002 | 02 Oct | **pre-burst brightness probe** (solid-ON `cwcSolidMs`, lamp-core P90→knee 235–250, clip ≤5%); **adaptive mask thr** `min(cwcMaskThr, max(45, 1.12·histMed))`; BSTATS telemetry (ships in CWCSTATS on CWC paths); chip dark square removed; Android evBias stays −1 (−3 retired 03 Oct before ever landing); POI tap feedback | QA PASS exit 0; verify_embed 47,408 B roundtrip |
| S14R-0003 / 0003a | 03 Oct | **CAL battery** `/cal` (autonomous tripod experiments E5 idle / E1 all-ON L-ladder 5→179 / E2 50%-duty ladder via exact coded plane / E3 settle jumps 5↔120 @250 ms / E4 real 24-plane bursts at L∈{80,100,120,150}) + `CALCFG=`/`CAL`/`CALSTATS` wire; **pages served from FLASH** const arrays (RAM-resident + lazy variants both failed — see §9); cal.html `camstat` id fix; bench_daemon serial-blip hardening | compile 1,353,122 B = 68%; ship-retry storm data still saved by disk dedup |
| S14R-0003b | 04 Oct | shipBatch 4 baked into SERVED page (mid-session CALCFG delivery proven unreliable — see §10) | superseded same day |
| S14R-0003c | 04 Oct | **benchPull splice fix — re-ship storm root cause** (store never cleared; splice shipped prefix post-logend); **FIRST COMPLETE BATTERY** `done:true` 39.4 min, 572 relays all-unique zero re-ships, E4 r8–r11 25/25 @ rig 3×200/cwcN 600 | compile 1,353,570 B = 68%; run0 428 + run8–11 25 jpgs decoded |

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
  0001's POI unverifiable). 03 Oct correction: on CWC paths this ships
  in CWCSTATS (BSTATS {} on the wire right after it is by design);
  check the CWCSTATS probe fields on every 0002 burst.
- **Exposure levers**: Android `evBias` stays **−1** (03 Oct operator
  call: the −3 intent never landed in a real burst, all 14 calibration
  corpora ran at −1, iOS ignores it anyway); burst-start re-apply hook
  kept for future use; iOS has no evBias —
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

## 6. Open work (ranked — re-ranked 04 Oct after the milestone battery)

1. **CLI suppression/ownership redesign** — page-parity: same-codeword
   dedup only; ≤5 px rivals → conflict-flagged entries, not eaten.
   Populations: 108–343 contest ids/burst (iOS corpora), ~44 contest +
   13 suppressed per android set. Biggest single-burst lever.
   E4's r8–r11 corpora (first correct-config bursts, runs 8–11) are
   fresh fuel for validating this.
2. **S14R-0004 build** — consolidated list, all wire-verified 04 Oct:
   (a) HEAPCAP beacon (1 Hz heap telemetry on LOGA: free / LFB /
   min-LFB, print on >4 KB change + boot+1 s line) — the wedge
   discriminator; (b) TLS hardening: max content buffers → 8 KB,
   max_open_sockets → 1–2 + lru_purge, static WiFi RX sizing, and a
   7F00 watchdog (3 consecutive setup-FAILs + no client >5 min →
   controlled esp_restart); (c) E4 intra-step ship trigger (today the
   whole 100-frame batch ships at experiment end — works but is the
   last big serial-blocking window); (d) ship pipeline redesign
   (chunked acks at 115200 = ~3.5 s/frame relay dominates; pipeline
   or raise baud); (e) cal.html UI: abort button always visible
   mid-battery, ws-state header tracks real socket state; (f) daemon
   refuse/warn on multi-directive cmds files (standing rule §10); (g)
   **LANDED 04 Oct 17:16:** daemon `decode_frames` writes ONLY
   FEND-terminated groups — no
   more truncated jpgs at source (the 33-file repair + photometry v2
   validated the rule end-to-end). Also fold in the probe ALGORITHM
   revision from the v2 photometry: target settled coreMean−bg
   ∈[90,105] clipCoreN=0 + 255−wall≥40 bail, skip k1 after any paint
   (k≥13 on first-paint-after-idle), presence metric (lamp-masked
   blob count ≥25, ≥3-frame median) below L20 with the LEG-2
   (painted-equilibrium) idle template, fixed L=100 (+optional 120)
   per union(L).
3. **Photometry analysis integration** — the §9 morning plan runs
   (a)–(e) on the COMPLETE 0003c battery data (run0 + run8–11);
   deliverable = probe-algorithm revision + the union(L) operating
   point. Report lands in `reports/`; fold results into the 0004
   build.
4. **POI tap real-rig validation** — 0001's tap measured pixel-inert;
   low priority until the probe revision lands.
5. **Adaptive-mask edge case** — s14r-and-r5 site 398 (hairline,
   blur 100.9, amp 44.5) documented loss under the new rule; re-check
   if any operator report says "string-2 end".
6. **Harness speed** — QA runs 4.5–8 min with the probe choreography;
   a fast-probe mock mode would help.
7. **RESOLVED 04 Oct** (was open): TLS wedge root cause — root-cause
   forensics committed (§10): LFB-freeze mechanism family, resident-
   asset hypothesis DEAD, no per-attempt leak; HEAPCAP probe is the
   remaining discriminator → folded into 0004 (2a/2b). CALCFG-vs-CFG
   displacement: moot on 0003c (battery needs no mid-session config;
   rig CFG + hello path verified good). 24-frame ship storms: root
   cause FIXED in 0003c (splice).

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
- **/cal battery**: https://192.168.4.1/cal (BUILD **`S14R-0003C-CAL`**
  since the 04 Oct reflash; 0003c ships with safe compiled defaults —
  shipBatch 4 + splice fix — so a battery needs NO config delivery:
  fresh page + START, or stage a cmds file with ONLY the `CAL`
  one-shot). `ABRT` (stop, always delivered as its OWN cmds file),
  `BRAMP` (re-ship); **cmds files carry exactly ONE directive** (§10
  standing rule); full interface spec
  `reports/s14r-0003-cal-battery-cmds.md`; constants in
  `tools/tuning_s14r0002.json` (calBattery).
- Corpora: `runs/daemon/runs/s14r-*` (each 25 jpgs + metas); era
  imagery local-only per `.gitignore` (metas/ledpos/frames packs
  tracked). **Milestone battery data (04 Oct 10:33)**:
  `runs/daemon/runs/run0/` (428 jpgs: 60 E5 idle + 368 E1/E2 ladder +
  E3) and `runs/daemon/runs/run8..run11/` (E4 CWC bursts, 25 jpgs
  each); attempt-1's night data renamed
  `runs/daemon/runs/s14r-0003-battery-attempt1-night/` (watch the
  idle-label caveat in §10). Quarantines: `s14p-drained-*`,
  `x-drain-*`, `s14r-camerablocked-*`, `run1af`, `truncated_backup/`
  — do not mix them into decode sets.
- Serial rescue: `tools/rts_pulse_reset.py` (new 04 Oct) — the
  §5 RTS→EN recipe as a script; prints the boot banner; run only with
  the daemon stopped.
- Git: main == origin/main (= `bfe0c79`); PAT at `~/LED_PAT.txt`;
  commit discipline: image-grep the staged set
  (`git diff --cached --name-only | grep -icE '\.(jpg|png)$'` = 0)
  before every commit.

## 8. Operator style notes (matter for the next agent)

- Honest tested-vs-sim accounting wins; over-claims get corrected with
  ground truth (operator carries the physical truth of the rig).
- No images to GitHub (standing hard rule; grep before commit).
- Single-burst is the product target; multi-viewpoint is offline
  context only, not a dependency.
- Subagents for heavy reads/compute; main session = decisions + serial
  + git + verified results only. Verify every important claim with a
  tool before asserting it.

## 9. 03 Oct night log (S14R-0003 → reflash → battery) — verified incidents

**Timeline (all verified on the wire or from code, line refs at time):**
day session: evBias −3 decision (stays −1, docs/json updated) + first
0002 burst reports committed/pushed; night: S14R-0003 battery build →
flash 21:0x → **connection-refused wedge** (mbedtls −0x7F00
SSL_SETUP_FAILED at 21:21:12 with ZERO wss clients — cal asset's
~26 KB resident from boot starved TLS session setup) → lazy-decode
reflash → `/cal` serves its own 500 "page not loaded" (decodeB64
couldn't malloc 25.8 KB contiguous mid-session; wire showed no
"asset decoded" at request time) → **S14R-0003a flash-resident pages**
(decodeB64/decodePage/malloc buffers DELETED; handlers serve const
PROGMEM arrays; RAM globals 62,500 B) → `/cal` loads but camera stuck
"Cam: starting" + no wake lock → root cause ONE dom-bug: code wrote
`$('camst')` but element id is `camstat` — null-deref killed
startCamera BEFORE getUserMedia AND before boot line 952's
requestWake(); fixed (4 refs), repacked, verified, reflashed →
battery armed and phone started it 22:19-ish (26+ E5 idle frames,
room at exp=799.98 tonight) → operator: "stuck on E5, ship 24 then
25/26/27/28/29 frames with minutes between" → wire forensics: SAME
base64 payloads re-shipped repeatedly = **ship-retry storm**: every
logc chunk awaits an ack (tries 2 × timeout 2000), box relays at
115200 ≈ 3.5 s/frame so the 24-frame batch (~90 s serial time) blows
the window, chunks retry, ship abandons WITHOUT clearing the store,
next trigger re-ships all → growing counts, ~minutes/attempt; the
daemon's disk dedup saved the data anyway (28 jpgs in run0/) →
battery ABRT'd cleanly 22:29:44 (`done:false, mins:10.9`) →
restarted 22:30:18 with shipBatch 4, E5 skipped (idle data already on
wire).

**Standing traps learned tonight (all new):**
1. Ship-chunk acks vs baud: keep **shipBatch ≤ 4** on any burst-size
   ship; ~32 logc chunks/frame at 3.5 s/frame relay dominates.
2. Manual serial banner checks **reboot the C6** (`rst:0x15
   USB_UART_HPSYS` on control-line transitions) — check banners via
   the daemon wire, never open the port while the daemon holds it
   (kill daemon first, port frees, and then prefer the wire).
3. Never serve big assets from RAM/heap on a TLS box; flash const
   arrays are the pattern (packers page_assets.py / verify_flash_pages.py).
4. Daemon cmds auto-execute at cold start even with no page connected
   (2-credit slot replay design) — staging order matters, not timing.
5. capture.txt timestamps have no date; era-split by content
   (uptime `t`, build stamps, line numbers), not by label alone.

**Postscript (23:0x, after the handover stamp):** battery attempt 2 did
not survive — the page's websocket dropped 22:34:13 (phone screen/AE
asleep; old-config instance) and no page has returned; the box still
holds attempt-2 config (shipBatch 4, E5-skip) with 2 credits, so the
battery self-starts whenever the phone wakes. Attempt-1's E5 idle trace
is banked: 29 labels (27 clean, 2 quarantined `.trunc` — in-store
capture truncation, deterministic across 9–12 re-ship occurrences; new
bug species), wire-repaired byte-exact, PROVENANCE.json in-run,
committed 06cdc6e/7f19e83. Overnight watcher armed on `done:true`
(corrected full-growth scan — the stall false-positive was a 3 KB tail
drowning in a 100 KB FJPEG dump). Banked-data analysis dispatched.

**Morning plan (battery ends ~22:56):** extract runs dirs → parallel
subagent analysis: (a) AE settle curves per L ± 2 s-hypothesis test,
(b) all-ON vs 50%-duty same-L brightness, (c) raw-core blob metric
feasibility at L=5+, (d) instrument-vs-raw transfer curve (band
re-map), (e) E4 union(L) at {80,100,120,150} → objective function for
the probe-revision build. Deliverable: probe-algorithm
recommendation + proposed S14R-0004 changes. Then fix (7)'s CFG
displacement in the same build.

## 10. 04 Oct log (S14R-0003b/c → milestone battery → repair → v2) — verified

**Milestone:** first COMPLETE cal battery — `done:true` 39.4 min at
10:33:16, build 0003C-CAL, order E5→E1→E2→E3→E4, 572 FRAME relays
09:53–10:33 ALL unique (zero re-ships — the benchPull splice fix held
under the 100-frame E4 mega-ship), E4 epochs r8–r11 25/25 each at
rig 3×200/cwcN 600 (queued rig CFG drained by the fresh page's hello
post-reflash — the good path). E4's whole 100-frame batch ships at
experiment end (CWC path lacks the intra-step ship trigger, item 2c)
— on the ack-2-try design this relayed intact in ~6 min.

**Corpus repair (operator-directed, standing gate):** 33 truncated
jpgs found across run0+run8–11 (13 E1 + 11 E2 + idle_49 newly-caught
+ 8 E4) — ALL repaired IN PLACE from capture.txt by prefix-provenance
byte-join; 528/528 EOI + plain-PIL + meta-equality green; 315,929 B
restored; 0 unresolvable; manifest
`runs/daemon/analysis/run_repair/manifest.json`; report
`reports/s14r-0003c-wire-repair-inplace.md`. Root cause: the daemon's
30 s drain tick decoded IN-FLIGHT frame groups and the skip-if-exists
dedup (≥8 KB) locked the truncation in. **Fixed at source 17:16**
(0004(g): decode only FEND-terminated groups; drop partials — they
complete on the wire before the next tick). Idle two-legs label
collision resolved by BYTE EQUALITY (never by label): k00–11 leg-1
(warm-state), k12–59 leg-2 (settling to painted equilibrium);
`idle_provenance.json` in run_repair/. **IMAGE-HYGIENE GATE standing
(skill v2.03): EOI + wire-sha-verify EVERY jpg before ANY analysis;
repair-then-analyze, never analyze-then-caveat.**

**Photometry v2 on the repaired corpus** (reports/
`s14r-0003c-photometry-v2-repaired-corpus.md`): NO conclusion
flipped. (a) AE lands by k2 at every L, no tau — skip k1 after any
paint (≥3% histMed shift on 12/20 rung-arms), k≥13 on the
first-paint-after-idle rung (it dips to k10 then rises to k13);
(b) duty photometric ratio ~1.05 (x1.70 RETIRED — it was
k1-unsettled-vs-settled), holds stronger at full n; (c) presence
census 17/17 at k≥2 (v1's two "transient failures" were the two
truncated frames — attribution corrected), idle floor is TWO-STATE
(leg-2 painted equilibrium P50 105.5 is the restart-state template),
≥3-frame median mandatory at L≤20; (d) band [90,105] + 255−wall≥40
re-derives identically at full n. E4 parity EXACT from run dirs
alone: 412/454/390/411, unions 458/456/443/475/462/456, 4-burst 481
— v1 wire-derived numbers confirmed.

**NEW bug species (04 Oct evening, operator-spotted in the gallery):
TORN CAPTURES — capture-time, NOT recoverable.** Three E4 frames
(`cwc_r8_p07`, `cwc_r10_p05` — tear at y≈191/y≈383 — and
`cwc_r10_p21`, red-channel band from y≈590) are single frames where
the PHONE CAMERA delivered a torn readout: top of frame is a normal
exposure, below a sharp horizontal line the content is a different
(red-shifted, mis-gained) state. These bytes are FAITHFULLY
transmitted and stored (disk == wire bytes, EOI clean, PIL clean) —
nothing to repair FROM; the corruption happened in the sensor/AE
readout during the rapid 24-plane E4 dwell. Corroborated by the
decode side: their per-plane kbg values are the run's outliers (r8
p07 rank 24/24, r10 p05 23/24, p21 24/24). Impact contained: the
union-across-planes decoder absorbed them (412/454/390/411 confirmed
WITH these frames present); only their below-tear evidence is noise.
0004 candidate: in-page tear detector (row-luma discontinuity metric)
→ auto re-grab that plane before continuing the burst.

**Morning sequence (aborted attempts + fixes, all wire-verified):**
§9-style timeline: 06:44 first post-overnight TLS failures (handshake
stage), 08:09–08:11 setup-stage -0x7F00 storm 24/24 = the wedge (RTS
cleared 08:19; second pulse 08:32 with page closed = clean), 09:00
multi-directive cmds file tore CALCFG (battery ran compiled defaults
E5+ship24 → storm → daemon died 08:51 silently — restart+ABRT), second
daemon death 09:07–09:12 (same storm species), 0003b flashed 09:31
(shipBatch 4 in served page), splice fix + 0003c flashed 09:39,
battery 09:52→10:33 COMPLETE. Standing rules hardened (§5/§7/skill):
cmds ONE directive per file; never RTS on handshake-stage labels;
post-reset handshake noise (7780/0050) = healthy-box retry bursts.

**Failure-mode deep-dives (the two root causes behind the morning):**

1. **TLS wedge recurred heap-free (resident-asset hypothesis DEAD).**
   08:09:27–08:11:06: 24/24 connection attempts failed at
   `mbedtls_ssl_setup -0x7F00` — synchronous malloc failure (<12 ms,
   never reached the network). Cleared by RTS→EN only. Mechanism (best
   fit): largest-free-block freeze — each TLS attempt transiently
   allocates ~33–37 KB (2×16 KB content bufs + cert/key/drbg), phone
   fires 2–4 parallel sockets, WiFi softAP RX high-water (~51 KB)
   reshapes the heap; between retry bursts NOTHING large allocates, so
   the LFB never re-coalesces. App code clean (no Strings/NVS/leaks;
   failure path frees everything — checked). Post-reset handshake
   errors (−0x7780/−0x0050) are client retry noise on a healthy box —
   NEVER reset on that label alone.

2. **24-frame ship storm wedges the chain (page AND daemon).**
   Sequence 09:00–09:31: multi-directive cmds file raced the page's
   drv? poll on the single cfg slot → torn JSON ("E cfgparse" on the
   page) → battery started on COMPILED defaults (E5 + shipBatch 24) →
   24-frame batch ship-storm → wire froze 09:07:41 (daemon wedged,
   died silently) → fresh page re-shipped the whole bench store after
   WS reconnect (second storm input). Two daemon deaths today; each
   needed kill + restart + (box reset).

**Standing rule upgraded: cmds files carry ONE directive per file.**
Multi-directive files are a torn-payload footgun; pacing with wire
verification between sends is the only safe pattern. (Last night's
single-file success was luck of polling phase.)

**Build S14R-0003b/0003c record:** 0003b (shipBatch 4 baked into the
served page) was superseded within hours by **0003c** when the
re-ship storm's ROOT CAUSE surfaced: cal.html's benchPull — the
"survey benchPull copy" — was missing the store clear. Survey never
needed one inside the pull (benchRun clears leftovers at the next
burst start, survey :1093); the battery loop has no such boundary, so
every shipIfDue re-shipped the whole store (4→5→6… today; 24→25→26…
03 Oct). 0003c: benchPull counts fully-relayed frames and
`benchStore.splice(0, shipped)` after logend (abort/exception keeps
the rest — conservative) — live-verified by the milestone battery
above. (The older postscript details, including the original
skip-if-exists idle-collision note, were overtaken by the corpus
REPAIR section: the idle labels were byte-resolved from the wire, not
dropped.)