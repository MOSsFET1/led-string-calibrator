# Handoff prompt — S14 continuation (30 Sep)

Copy everything below the line into a fresh session with the same agent.

---

Get up to speed FIRST by reading, in this order:

1. `/home/nellie/projects/led-display/POC LED survey/S14-CWC-PLAN.md` —
   §4 (single-LED toggle test, the protocol we're about to gate),
   §10 (first full position round, 29 Sep evening),
   §11 (AGREED S14Q architecture — box-driven capture, bank-in-box,
   200-byte payloads; the design for the NEXT build, do not implement
   yet), §8 (ledcloud/2 point cloud format).
2. `S14-BENCH-SESSION.md` — §S14O/S14P forensics + the settled pull
   recipe + the "29 Sep late" section at the tail (tonight's ship-once
   fix, one-connection round, first position decode).
3. `HANDOFF-S14P.md` (superseded in places by the two above — trust
   plan §10/§11 where they conflict).

Then load the skill (it holds the hard-won lessons and the build
discipline): `skill_view(name='led-camera-calibration')` and
`skill_view(name='led-string-calibrator')` for push discipline. Note
both skills mention S14P-1901 as "current" — the box actually runs
**S14P-1903** (see below); trust the repo docs, and offer to refresh
the skill's era line once the next gate passes.

## Where things stand (verified last night, Tue 29 Sep)

- Box runs **S14P-1903**: contains BOTH the ship-once store fix
  (bench ring cleared at each burst START, in `page/survey.html`) and
  the master-grab flush fix (all-on master grabbed after a 500 ms
  pipeline flush, comment marked "S14P-1902 fix" in the test-mode
  branch). Verified over serial: `page build 'S14P-1903' -> ALIVE`.
- Tonight's clean full round lives in
  `runs/s14p-1903-pos1/` (19/19 frames, master dip-test CLEAN ratio
  0.96, registration residual 0.03 px median, conf 0.83–0.89,
  177 LED sites detected). Overlays: `sites_union.png`, `led_1to1.png`.
- KEY STRUCTURAL FACT: the firmware MIRRORS lane1's paint to all 8
  lanes and 2 physical 200-LED strings sit in the camera view — every
  codeword lights its LED in BOTH strings, so every codeword has 2+
  sites in frame (measured: 80 leds with exactly 2 sites). This is
  correct data, NOT an anomaly. Never force 1:1 site-per-codeword
  (plateaued at 94–111/150 with misassignments); strings get separated
  by serpentine path tracing LATER.
- Tools built last night (all committed, HEAD e3b984d):
  `tools/run_round.py <run_dir> [--cwc-test-mode 0 --cwc-n 150 --b 150
  --comp 0]` — the one-connection round: LOGA arm + CFG/BURST + the
  page's own auto-ship IS the pull, single serial session, 19/19 every
  time. Use this INSTEAD of s14_bench burst+pull for full rounds.
  `tools/cwc_pos_decode.py` (per-codeword score-map position decode,
  d6 identity margin), `tools/cwc_bank_check.py` (bank block facts),
  `tools/cdp_once_ship.py` (ship-once regression harness — PASSES),
  `tools/pull_loga.py`, `tools/frames_diag.py`, `tools/boot_watch.py`.
- A SECOND Hermes session (pts/2) was working this repo concurrently
  last night and produced S14P-1903's master-latency patch +
  tools/s14_detect.py; it was interrupted mid-skill-update at 20:57 and
  its last skill_manage failed. If Oliver still has extra sessions open,
  check `ps auxww | grep tui_gateway` and kill stale serial pollers
  before touching /dev/ttyACM0. After patching any shared file,
  re-grep it — a parallel session may have edited it meanwhile.
- Commits last night: e3b984d (plan §11 S14Q architecture),
  a3b467d (first position round), 3c1e924 (tools). NOT pushed to GitHub
  (push discipline: only after Oliver confirms a feature working well).

## Your first task: the single-LED toggle test tripod gate (plan §4)

The master is now clean (dip ratio 0.96), so the gate that was blocked
since 28 Sep should finally pass. Sequence:

1. Gate: phone page loaded on
   `https://192.168.4.1/` (cert accept, camera allow) — verify with
   `tools/boot_check.py` (expect `page build 'S14P-1903' -> ALIVE`);
   Oliver reloads the phone if it shows GONE. LED supply must be ON
   (ask Oliver — he forgot once). String length is 150 (box default).
2. `` `/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/run_round.py
   runs/s14p-1903-test-lead0 --cwc-test-mode 1 --cwc-test-led 0```
3. `` `/home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/cwc_analyse.py
   runs/s14p-1903-test-lead0 --test-led 0``
4. GATE: `TEST MODE VERDICT: PASS` — 18/18 bits, residual < 1 px on all
   planes, ON/OFF ratio > 1.5×. If any bit fails, diagnose from the
   per-plane table before touching code (the analyser names the plane).

## Then, in order (each step verified before the next)

2. LED 1 and LED 25 repeat of the toggle test (different codewords) —
   cheap confidence that the read isn't tuned to LED 0's pattern.
3. Handheld repeat of step 1 (same protocol; residual budget ~5 px,
   zero-error target stands). Oliver holds the phone.
4. Positions-only point set from `runs/s14p-1903-pos1/` site union
   (keep the amp/margin gates from `cwc_pos_decode.py`): emit
   ledcloud/2 (§8) with class C for strong single sites, then show
   Oliver the cloud overlay for sign-off.
5. Serpentine string tracing over the site union (positions only) —
   separates string 1 from string 2. This is the step that needs
   Oliver's eyes on the overlay, since string topology is physical.
6. Only then start the S14Q build per plan §11 — gates: C6 async-WS
   spike FIRST (httpd_ws_send_frame_async on raw esp_https_server,
   unsolicited send), then the JS decoder must reproduce the console
   decoder's verdicts on the SAME captured frames before any
   box-driven trust. The JS decoder is the schedule risk; the plumbing
   is not.

## Standing environment facts

- Serial: /dev/ttyACM0, hermes venv python for pyserial+cv2+PIL
  (`/home/nellie/.hermes/hermes-agent/venv/bin/python3`); system
  python3 lacks them. ONE serial consumer at a time — check
  `ps auxww | grep -E "python3.*(serial|bench|pull)"` and kill stale
  pollers before opening the port; a flash RESETS the port (re-enumerate
  → re-open, then tools/boot_watch.py / boot_check.py).
- Build cycle (every page edit, in this order, no step skipped):
  bump `BUILD` in `page/survey.html` → `firmware/tools/pack_page.py`
  (requires `PAGE_BUILD=<stamp>` in its output) → compile AND upload
  with `--fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs"`
  (both, or the OTA table is clobbered) → boot_check → Oliver reloads
  the phone → STAT must show the new stamp before ANY run. page changes
  also need the JS scope audit script if helpers moved between scopes.
  `arduino-cli` lives at `~/.local/bin`.
- LED supply must be ON (12 V); polyfuse per string holds 2 A
  (fuseClampB caps b≈239 @150 px; CWC planes at 50% duty are ~0.63 A).
- AE facts (measured, r6 + tonight): the master at plane gain is CORRECT
  (k_p ≈ 1.0 by construction) — do NOT add a settle to make the master
  brighter; the stable-pair + 2×-count flush rule is designed but NOT
  yet bench-proven (open item, plan §11.3). Bench it cheaply first time
  you run a burst: it is a phone-side rule to prototype in the page's
  test branch, not a firmware change.
- 19-frame protocol: P00 1s primer (AE settle) → P01..P17 → master.
  Codewords: bank-in-page `CWC_CODES_9OF18` (page builds paints), 
  bank file `tools/codewords_9of18.json` is the SOURCE and the page's
  embedded copy matches (both are S14P-1903). Per-LED state ships as
  JSON today; per-STRING code blocks (string s = codes 200s+i) are the
  S14Q-era plan, NOT yet implemented — do not assume them in a decode.
- Runs live in `runs/<dir>/`; docs updated + committed per step:
  S14-CWC-PLAN.md (protocol/architecture), S14-BENCH-SESSION.md (bench
  lessons), HANDOFF files for session breaks. Oliver reloads the phone;
  nothing else needs him except physical setup questions — be
  autonomous otherwise, and verify every claim that matters (spec,
  pinout, arithmetic) from the repo/tools, not from memory.