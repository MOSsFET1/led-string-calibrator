# Handover — S14 continuation (30 Sep, after S14P-1906)

Get up to speed FIRST by reading, in this order:

1. `/home/nellie/projects/led-display/POC LED survey/S14-CWC-PLAN.md` —
   §4 (toggle test: GATE PASSED — the protocol is settled), §10/§10b/
   §10c (the 30 Sep rounds, the mask-gate root cause, the resolved miss
   list, the ghost anatomy), §11 (AGREED S14Q architecture —
   box-driven capture, bank-in-box, 200-byte payloads; the design for
   the NEXT build, do not implement yet), §8 (ledcloud/2 cloud format).
2. `S14-BENCH-SESSION.md` — §S14O/S14P forensics + the settled pull
   recipe + the "30 Sep" tail section (gate pass, threshold-sweep
   lessons, dim-room measurements).
3. `HANDOFF-S14P.md` (superseded in places; trust plan §4/§10/§11).

Then load the skills: `skill_view(name='led-camera-calibration')`
(hard-won lessons + build discipline; era line v1.65 = S14P-1906) and
`skill_view(name='led-string-calibrator')` (push discipline). Note the
era line was updated twice on 30 Sep by racing sessions (v1.64, then
v1.65) — read it before bumping; both ended merged and correct.

## Where things stand (verified 30 Sep, afternoon)

- Box runs **S14P-1906** (flashed + boot-verified; banner
  `poc_survey S14P-1906: masked-gate fix + dedup`). Contents by build:
  - **1904**: canvas-ownership fix (captures snapshot the PROCESSING
    canvas `procCx`, never the display canvas — the idle overlay
    redraws between grabs and 1903's test-mode path shipped overlay
    pixels instead of frames); `scanning` flag live (idle overlay
    frozen + manual paint buttons locked + status LED dark during
    bursts); the operator's BACKWARDS REGISTRATION CHAIN in-page for
    every burst (p17 registers DIRECT to master; each lower plane is
    pre-shifted by the previous TOTAL before its own integer NCC vs
    master — 128-wide decimation, sample-at convention); IN-PAGE LED
    position decode + ON-PHONE result view (static master, green =
    single-site claim, amber = multi-site codeword claim); drv? poll
    retry; non-blocking deferred STAT.
  - **1905**: default string length 200 everywhere (firmware nPx +
    page cwcN/npxin + tool --n defaults); the in-page decode ships its
    FULL sites list (CWCDEC chunks → `<run>/cwc_dec.json`; 1904
    shipped a summary only — the gap was found by reading back a run).
  - **1906**: candidate-mask threshold 200→**175**, CFG-tunable
    (`cwcMaskThr`), + STRONGEST-SITE-PER-CODEWORD dedup in cwcDecode.
- **The mask-gate sweep (the session's key finding)**: the 20–25 +
  150–156 misses failed the CANDIDATE MASK (blur≥200; their blur-luma
  is 184–191 at the shallow-angle frame edge), NOT the amp/margin
  gates — at their true sites amp 90–124 / margin 56–78 already exceed
  60/25. No gate pair recovers a pixel the mask never offers (measured
  across 7 sweep points down to amp 25/margin 10). MASK_THR 175 =
  mid-gap (weakest needed luma 180 / strongest phantom 167). NEVER
  ≤160: a codeword-103 phantom at (89,631), 280 px from its true site,
  survives there even after strongest-site dedup.
- **Validated counts** (console `cwc_pos_decode.py --amp-gate 60
  --margin-gate 25 --save-json`): pos1 196/200, ZERO dups, missing
  [16,46,90,91]; pos3 (dim room, 14:29) 197/200, ZERO dups, missing
  [46,90,91]. 16/46/90/91 are margin-0.0-everywhere cases (their argmax
  pixels are owned by other codewords' cores) — the fix is a
  per-codebook nearest-site 1:1 assignment: an ALGORITHM change,
  deliberately queued (not gate tuning).
- **Rig state (operator-confirmed)**: ONLY STRING 1 ACTIVE in the
  pos1/pos2/pos3 rounds. Every duplicate site per codeword is an
  erroneous ghost: settled with pixels — the persistent "far twins"
  sit INSIDE the translucent drawer organiser (clipped specular glints,
  raw 255 / blurred 203–221, no discrete LED core), strengthened at
  lights-off; object reflections, NOT a second lit string (a
  run-runner subagent misread this as "string 2 still connected" —
  corrected in §10b). The handoff-era 'never force 1:1' rule applies
  ONLY to real multi-string installs; on this bench strongest-site-
  per-codeword IS the operating rule (now in both decoders).
- **Dim room measured**: exposure pinned the whole burst (readback
  byte-stable, AE never re-metered), background luma 101→85, LED p99
  246→252 (contrast UP), per-plane gains 0.98–1.00, decode amp/margin
  medians IMPROVED (133→148 / 85→95). Room-dim regimes need NO waits
  and NO brightness compensation — the gain-normalised read handles
  them; do not re-litigate this.
- Commits (local, NOT pushed): `5300c11` (1906 + sweep recipe +
  ghost anatomy + QA harness PASS), `eb18721` (1904/1905 + gate pass +
  docs). Push discipline: only after Oliver confirms the feature
  working well on the bench.

## Your first task: re-shoot the 200-LED round on the PHONE (1906)

The console recipe is validated; the flashed page needs its own shot
(the on-phone boxed view is Oliver's deliverable):

1. Gate: phone page loaded on `https://192.168.4.1/` (cert accept,
   camera allow) — verify with `tools/boot_check.py` (expect
   `page build 'S14P-1906' -> ALIVE`); Oliver reloads the phone if it
   shows GONE. LED supply must be ON (ask Oliver — he forgot once).
   String default is NOW 200.
2. `/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/run_round.py runs/s14p-1906-pos1`
3. `/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/cwc_pos_decode.py runs/s14p-1906-pos1 --amp-gate 60 --margin-gate 25 --save-json`
4. GATE: console ≥195/200 LEDs, zero duplicate codeword claims after
   the strongest-site pass, the raw-site overshoot in the seen skirt
   range, and none of 20–25/150–156 lost. Then show Oliver the ON-PHONE
   boxed overlay: expect ~197 green boxes, no ghosts.

## Then, in order (each step verified before the next)

2. Handheld repeat of the toggle test (§4 protocol; residual budget
   ~5 px, zero decode errors the target). Last tripod-era step before
   cloud export — it exercises the chained registration under real
   motion, which tripod rounds cannot.
3. ledcloud/2 export from the single-string site set (§8): class C for
   confirmed claims, I for the [16,46,90,91] orphan class (per-LED
   REASON in the burst log), show Oliver the cloud overlay for sign-off.
   Export = ONE conversion at generation (capture px → cloud box);
   consumers never rescale.
4. Per-codebook nearest-site 1:1 assignment (queued) if Oliver wants
   [16,46,90,91] recovered rather than interpolated. Sketch: score every
   codeword at every union site; Hungarian against the codebook; accept
   per the standing gates. NOT started.
5. Only then start the §11 S14Q build — hard gates in order: C6
   async-WS spike FIRST (httpd_ws_send_frame_async on raw
   esp_https_server, unsolicited send), then the JS decoder must
   reproduce the console decoder's verdicts on the SAME captured frames
   (cwc_decode_sim gate). 30 Sep risk update: the in-page recipe now
   MATCHES the console recipe line-for-line (205/209 same-id + 0.0 px
   median position agreement on shared IDs measured on pos3; the 4
   remaining disagreements were the mask, now fixed) — the residual
   schedule risk is the WS transport spike, not the decode math.

## Standing environment facts

- Serial: /dev/ttyACM0, hermes venv python for pyserial+cv2+PIL
  (`/home/nellie/.hermes/hermes-agent/venv/bin/python3`); system
  python3 lacks them. ONE serial consumer at a time — run
  `ps auxww | grep -E "python3.*(serial|bench|pull|run_round)"` and
  kill stale pollers before opening the port; a flash RESETS the port
  (re-enumerate → re-open, then tools/boot_watch.py / boot_check.py).
- Build cycle (every page edit, in this order, no step skipped):
  bump `BUILD` in `page/survey.html` → `firmware/tools/pack_page.py`
  (requires `PAGE_BUILD=<stamp>` in its output) → compile AND upload
  with `--fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs"`
  (both, or the OTA table is clobbered) → boot_check → Oliver reloads
  the phone → STAT must show the new stamp before ANY run. `arduino-cli`
  lives at `~/.local/bin`. Page changes need the JS scope audit script
  if helpers moved between scopes. The pre-flash QA harness is
  `tools/cdp_1904_check.py` (mock box + headless Chromium + fake
  camera): restart the mock box after any page edit (it serves the
  page bytes captured at startup), truncate mock_box.log before any
  verdict, fresh headless-Chrome profile dir per run.
- LED supply must be ON (12 V); polyfuse per string holds 2 A
  (fuseClampB caps b≈179 @200 px; CWC planes at 50% duty are ~0.84
  A/string @200 px — never fuse-limited).
- AE facts (measured, r6 + 30 Sep): the master at plane gain is CORRECT
  (k_p ≈ 1.0 by construction) — do NOT add a settle to brighten the
  master. The stable-pair + 2×-count flush rule is designed but NOT yet
  bench-proven (open item, plan §11.3); the fixed 500 ms flush is the
  standing fallback and is what these rounds used.
- 19-frame protocol: P00 1s primer (AE settle) → P01..P17 → master
  (grabbed after the 500 ms pipeline flush). Codewords: bank-in-page
  `CWC_CODES_9OF18`; bank file `tools/codewords_9of18.json` is the
  SOURCE and the page's embedded copy matches (re-verified byte-identical
  1600/1600 on 30 Sep). Per-LED state ships as JSON today; per-STRING
  code blocks (string s = codes 200s+i) are the S14Q-era plan, NOT yet
  implemented — do not assume them in a decode.
- The in-page decode gates are CFG keys — tune over SERIAL or console
  sweeps, never by patching defaults blind: `cwcAmpGate` 60,
  `cwcMarginGate` 25, `cwcMaskThr` 175.
- Runs live in `runs/<dir>/`; docs updated + committed per step:
  S14-CWC-PLAN.md (protocol/architecture), S14-BENCH-SESSION.md (bench
  lessons), HANDOFF files for session breaks. Oliver reloads the phone;
  nothing else needs him except physical setup questions — be
  autonomous otherwise, and verify every claim that matters (spec,
  pinout, arithmetic) from the repo/tools, not from memory.
- Multi-session hazard (bit Oliver twice): several concurrent Hermes
  TUIs work this repo — after patching any shared file RE-GREP it (a
  parallel session may have edited it meanwhile); two skill era-line
  updates raced here on 30 Sep and merged cleanly.