# ESP32-C6 LED String Controller + Camera Calibrator — S14R-0002

Camera-based calibration for an addressable LED string (WS2815 on an
ESP32-C6): the phone browser watches, the box lamps, and the web app builds
a per-LED position map — zero install, nothing but a browser. Current
build **S14R-0002** (02 Oct): the R-line runs the 12-of-24 Golay codeword
bank with a pre-burst brightness calibration and adaptive decode thresholds.
Committed + QA-PASS, **NOT YET FLASHED** (the box still runs S14P-1928 —
flash 0002 before the next burst; see HANDOFF-S14P.md re-stamp note and
S14-CWC-PLAN.md §15).

**Actors**

- **Phone page directs the show** — single-file `page/survey.html`, served
  BY the box (self-signed HTTPS + WebSocket on :443 from its own hotspot
  `LED-SURVEY` / `survey2026`). It sequences the burst, paints the rig,
  captures frames, decodes LED identity/positions IN-PAGE. Since S14R-0002
  every CWC burst is preceded by the brightness probe (below).
- **Box lamps + acknowledges on latch** — ESP32-C6 drives WS2815 lanes and
  acks every paint with a JSON latch-ack (echoing the paint's epoch).
  Status WS2812 on GPIO8 (see Status LED below).
- **Bench console/daemon on serial `/dev/ttyACM0`** — `tools/bench_daemon.py`
  (persistent LOGA arm) auto-ships every burst's frame store over WS ~1 s
  after each burst in DECODE mode; per-burst JPEGs land in
  `runs/daemon/runs/<label>/` (image corpora for the S14R era live under
  `runs/daemon/runs/s14r-*`, local-only, never pushed to GitHub). Since
  **S14P-1928** the page also has a CAPTURE-ONLY burst mode
  (`chkCapOnly`): choreography unchanged, decode/ship skipped, frames
  accumulate across bursts in the page's benchStore and leave via the
  manual **'Send frames (all)'** button (a store wrap drops the OLDEST
  frame with a loud log). Directives by files dropped in
  `runs/daemon/cmds/`; `tools/s14_bench.py` for one-shot burst/pull/analyse.
  Serial directives: `PING`, `STAT`, `ABRT`, `CFG=<json>`, `BURST`, `BRAMP`,
  `PROBE`, `LOGP`, `LOGA`, `NPX=` (PROBE = standalone brightness-calibration
  run, new in S14R-0002).
  If the box's HTTPS dies with a `mbedtls_ssl_setup -0x7F00` storm
  (TLS-heap wedge, root cause undiagnosed, recurring): RTS→EN pulse via
  bench serial reboots the box (banner replays), then restart the daemon,
  verify `=== daemon start ===` + `[LOGA] persistent arm ON`, then use cmds/.
- Docs: the page is gzip+base64-embedded in the sketch — after editing
  `page/survey.html`, run `firmware/tools/pack_page.py` to embed it.

## Frame-bits protocol + CFG strings

**`frame-bits`** (WS BINARY, client→box, exactly 246 B since S14R-0000):
`[0]='B' [1]=1 [2]=b [3]=flags [4..5]=u16 LE epoch [6..245]=1920-bit plane`
(240 B = 24 Golay planes' payload) — bit j (LSB-first per byte) = LED
global id j. lane=j/nPerStr, px=j%nPerStr; ids ≥ nStr×nPerStr ignored;
off-rig lanes/tails forced black; per-lane write, NO mirroring. Malformed →
`{"err":"frame-bits shape"}`. Ack rides the normal latch path with the
message's u16 epoch.

**CFG keys `nStr` (1–8) / `nPerStr` (1–200)** (defaults 8/200 = the full
rig since S14P-1927; 1/200 keeps a bench single string): applied
box-side AND page-side; hello reply is
`{"ok":true,"id":N,"fw":"poc_survey","px":nPx,"nStr":nStr,"nPerStr":nPerStr}`.
Per-string polyfuse clamp on frame-bits brightness:
`maxB = max(20, floor(255·2/(nPerStr·0.0142)))` (2 A hold, 14.2 mA/px).
JSON `frame` writes lane 1 and mirrors lanes 2-8 ONLY while `nStr≤1`;
`all`/`black`/`npx` are rig-wide.

## Build & flash (compile BEFORE flashing, every page or firmware change)

```
cd /home/nellie/projects/led-display/poc_survey
arduino-cli compile --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" firmware/poc_survey
arduino-cli upload  --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" -p /dev/ttyACM0 firmware/poc_survey
```

**`PartitionScheme=min_spiffs` is part of the FQBN and is MANDATORY** on
every compile *and* upload: a bare `esp32:esp32:esp32c6` build compiles
101% against its 1,310,720 B default cap and fails with a bogus
"Sketch too big". Correct stamp S14R-0002 compiles at 1,342,722 B =
**68%** (app cap 1,966,080 B under min_spiffs) / RAM 18% (verified 03 Oct
before the flash).

Page edits: bump `BUILD` in `page/survey.html`, run
`firmware/tools/pack_page.py`, bump `PAGE_BUILD` in the sketch to the same
stamp, recompile, reflash, then reload the page on the phone.

## Tooling interpreter

The project venv is **GONE**. For QA runs use:

```
/home/nellie/.hermes/hermes-agent/venv/bin/python3
```

(ambient `python3` has no websockets). QA: this interpreter +
`tools/cdp_1904_check.py` — mock box + headless Chromium validation of the
real page pre-flash (STAMP const now expects S14R-0002; 12 checks); the
build-chain verifier is `tools/verify_embed_s14r.py`. `mock_box.py`
spawns its own box. Bench console needs the venv too (pyserial lives
there).

## Documentation pointers

- **`S14-CWC-PLAN.md`** — design authority (capture protocol, codeword
  bank, decode gates, ledcloud/2, architecture; §13 = 02 Oct console-led
  optimisation, §15 = the S14R-0000→0002 implementation log).
- **`HANDOFF-S14P.md`** — P-line handoff (S14P-1928, re-stamped;
  the S14R era it records lives in plan §15 — the S14R handoff doc is
  authored separately by the operator's main session).
- **`reports/`** — subagent evidence reports (S14R era:
  `s14r-0000-16h16-miss-classification.md`,
  `s14r-0000-17h43-aimed-round-decode-and-fixpath-verification.md`,
  `s14r-android-exp500-four-burst-console-decode-and-matrix.md`,
  `s14r-0001-ios-poi-console-decode-exposure-and-cross-round.md`; P-era
  console/sweep evidence and `INCIDENT-S14P-1926-cfg-one-shot-gap.md`).
- `archive/` — retired docs (one-line pointer at the original location or
  see `archive/README.md`).

## Current era (S14R CWC, in-page — 12-of-24 Golay + brightness probe)

The burst carries identity IN THE LIGHT PATTERN (Twinkly-style): a 50%-on
coded-plane primer (HOLD 1,000 ms for AE settle), planes P00..P23 at 100 ms
settle each (floor 70, CFG `cwcSettle`), then a fast all-ON master grab
with only the 70 ms flush before AE re-meters; decode diffs each plane
against that master. **25 frames/burst** (24 Golay planes + master). The
bank is the **12-of-24 extended-Golay subcode** (`tools/codewords_12of24.json`,
`page/codewords.js`): 1600 codes, weight 12 of 24, d_min 8, emitted as 800
complementary pairs so every plane paints EXACTLY N/2 lamps at any even
prefix (verified: per-plane ON = 800/800 at N=1600, bank d_min 8); the
9-of-18 P-line bank is retired with S14P-1928.

**S14R-0002 pre-burst brightness calibration** (`bProbe 1` + a PROBE
directive): before every burst the page paints SOLID all-ON frames at
increasing levels (start L 120, ~2 s apart so the AE re-converges, 2–3
multiplicative iterations L×target/measured clamped ±1.5×), measures the
lamp-core P90 (5×5 core max) + clip fraction per step, and drives the
cores to the JUST-BELOW-CLIP knee — band coreP90 235–250 with clip ≤5% —
then HOLDs the chosen level a fixed `waitSettleMs` 2 s and fires the burst
at it (bBurstB is overridden, telemetry fallback). All 14 S14R corpora
bursts overdrove (lamp-core clip 17.1–56.0% at fixed bBurstB 150; tuning
evidence `tools/tuning_s14r0002.json`), so the knee band is new ground.
The console can run the calibration standalone (`PROBE` directive →
`"probeOnly":true` CWCSTATS + BSTATS) and every burst's **BSTATS now
carries {bright, histMed, clipPct, probeIters}** + `expAtBurst` — NOTE (03 Oct, verified on first 0002 burst): on CWC paths
the probe telemetry ships in **CWCSTATS** (bright/histMed/clipPct/probeIters/
probeSteps); BSTATS stays `{}` there by design and fills only on
probe-only/all-on paths
(`exposure readback fix`). Operator-simplified constants: `stepDelayMs`
2000 spacing + a FIXED `waitSettleMs` 2000 hold, NO settle gate.

**S14R-0002 adaptive mask threshold** (`cwcMaskAdaptive 1`):
`thr = min(cwcMaskThr, max(cwcMaskFloor, cwcMaskK × histMed(master)))`
(page + `tools/cwc_pos_decode.py` `--mask-thr auto` parity) — dark-view
mask-class losses (77 ids at and-r4) get a frame-relative floor while
bright views saturate at 100: +22 ids, −0 regression on the A/B corpus
(one documented cost: s14r-and-r5 hairline site 398).

**Identity lever still open** (top of the queue, all corpora): CLI
suppression/ownership redesign — same-codeword dedup + ≤5 px rival-site
demote-to-conflict (page-parity); the iPhone 0001 corpus misses were
100% rival-eats (contest+suppressed 108–343 contest-class ids/burst,
Guard-pass on relaxed gates only 6/472 — never relax amp 40).

Era decodes so far (console, gates amp 40 / margin 6, cwcN honored):
S14R-0000 16:16 283/600 + 17:43 317/600 (multi-phone parity: shared-L1
median 0.0 px); Android exp500 r2-r5 429/549/492/387 union 583/600 (97.2%,
17 never-seen anatomised); iPhone 0001 r1-r4 287/440/487/257 union 534/600
(89%, 66 never = 100% rival-eats). First real 0002 burst (validating the
brightness probe + BSTATS telemetry) is the next rig action after the
flash.

## Key parameters (page CFG, serial-tunable live)

| param | default | meaning |
|---|---|---|
| `nStr` / `nPerStr` | 8 / 200 | strings / LEDs per string (frame-bits geometry; full-rig defaults) |
| `allB` | 160 | All-on button brightness |
| `evBias` | -1 | exposureCompensation bias (Android only; re-applied AT BURST START since 0002; iOS runs POI-only, no evBias constraint at all). 03 Oct operator call: STAYS at -1 — the -3 value never landed in a real burst (all 25 first-0002 readbacks ev=-1, zero CFG lines on the wire), every calibration corpus ran at -1, and iOS ignores it anyway so platform parity wins |
| `cwcPoi` (+cwcPoiX/cwcPoiY) | 1 | camera-view tap sets AE/AFF POI (iOS lever); re-applied each burst; 4 s crosshair feedback (0002) |
| `mergeR` | 6 | blob merge (detectDiffBlobs, the CWC pile-up reader) |
| `bBurstN/Gap/Hold/B` | 20/0/1000/150 | bench burst frames, pacing, all-on hold, brightness (bBurstB overridden by a run probe's chosen level) |
| `bComp` | 0 | in-page drift compensation (seeded NCC vs burst master) |
| `cwc` | 1 | 1 = CWC-form burst (master + **24** Golay planes) |
| `cwcN` | 1600 | LEDs mapped to codewords 0..cwcN-1 (clamped to nStr×nPerStr at burst start) |
| `cwcSettle` | 100 | ms paint→grab (clamped ≥70) |
| `bProbe` | 1 | 1 = pre-burst brightness probe (S14R-0002 headline; PROBE directive runs it standalone) |
| `bProbeStart/Step/Iters` | 120/24/3 | first level tried / legacy step / max multiplicative iterations (2–3) |
| `bProbeMin/Max/ClipHi/Pct` | 235/250/5.0/90 | lamp-core P90 knee band + clip% ceiling + the P metric |
| `bProbeDelayMs` / `waitSettleMs` | 2000/2000 | probe-step spacing (AE re-converges) / fixed post-probe hold before the burst |
| `bProbeCoreMinLuma` | 170 | blurred luma a pixel must clear to count as a lamp core |
| `cwcMaskThr` | 100 | candidate-mask blur threshold (02 Oct sweep-promoted) |
| `cwcMaskAdaptive` | 1 | S14R-0002: 1 = thr = min(cwcMaskThr, max(cwcMaskFloor, cwcMaskK·histMed)) — dark-view rescue, bright views saturate; 0 = fixed (S14P-1927 behaviour) |
| `cwcMaskK` / `cwcMaskFloor` | 1.12 / 45 | adaptive multiplier on master histMed / never-mask-below floor |
| `cwcAmpGate` | 40 | decode amp gate (02 Oct sweep-promoted; hold at 40 — relaxed-gate gains measured 43–100% impostor in the S14R audits) |
| `cwcMarginGate` | 6 | decode d8 margin gate (Golay bank judges at d_min 8) |
| `cwcSuppress` | 1 | same-string suppression radius (px) |
| `cwcTestMode` / `cwcTestLed` | 0/0 | single-LED toggle test (plan §4) / test LED index |

## Known issues

- **S14R-0002 is NOT flashed yet** (QA-PASS + committed; parent flashes
  next). The box still runs S14P-1928 — the first real 0002 burst
  (validating the probe + BSTATS) is pending the flash.
- Serial directive slot is single (`sDrv`): a directive sent while another is
  pending is LOST — the bench console drains before sending (29 Sep).
- **CLI identity ownership** (top lever): `tools/cwc_pos_decode.py`
  argmax-per-site + ±3 px suppression still eats contested codewords —
  the page's conflict lists are healthier; same-codeword dedup + ≤5 px
  rival demote-to-conflict (page-parity) is designed, not yet built.
- **TLS-heap wedge** (root cause UNDIAGNOSED, recurring): esp-tls-mbedtls
  `mbedtls_ssl_setup -0x7F00` / handshake `-0x7780` storms after long
  uptime kill the box's HTTPS (twice on 02 Oct + the 06:44 wedge in
  today's capture.txt). Recovery: RTS→EN pulse via bench serial (banner
  replays), restart the daemon, verify `=== daemon start ===` + `[LOGA]
  persistent arm ON`.
- Phone page must be foregrounded for the drv? poll to run (screen asleep =
  GONE on STAT); the wake lock is now requested automatically at boot and
  on visibilitychange (S14R-0001 — the separate wake button is GONE); a
  page-side drv?-stall self-recovery is still open (B119 class).
- JSON `frame` mirrors lanes 2-8 only while `nStr≤1`; `all`/`black`/`npx`
  apply rig-wide.

## Hardware (proven on the bench, carried from the design docs)

ESP32-C6-Zero · WS2815 lanes on the rig (bench chips are RGB-wired, not
GRB) · status WS2812 on GPIO8 · self-signed cert baked into the firmware
(it's the box's own hotspot identity, regenerated for every real
deployment). Runtime LED count via NPX= (200 max per string; firmware
nStr/nPerStr defaults are the full 8×200 rig) and CFG nStr × nPerStr
(1–8 strings × up to 200 LEDs; the CWC page maps cwcN of them to
codewords).

## Status LED

solid red = new build flashed, phone not yet reloaded · dark while a paint
is recent (burst/survey activity — the LED must not photobomb the camera) ·
slow green breathe = idle and up to date.