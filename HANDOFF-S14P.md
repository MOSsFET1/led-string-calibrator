# Handover — S14P continuation (30 Sep night, after handheld r4 + TLS wedge)

> The former `HANDOFF-S14Q.md` is retired by THIS file (renamed/rewritten
> 30 Sep night at the operator's direction). It was named for the *target*
> S14Q era (plan §11 — agreed, NOT built), which twice made fresh sessions
> believe the box had moved to S14Q and fallen back to S14P. It never
> left S14P: the letter marks the destination, not the current era.
> (The toggle-gate chain was once informally narrated 'through S14S' — a
> git-message label, never a build or file.)

Get up to speed FIRST by reading, in this order:

1. `S14-CWC-PLAN.md` — §4 (toggle test: GATE PASSED, protocol settled),
   §9/§10/§10b– §10e (the 30 Sep rounds: mask-gate root cause, resolved
   miss list, ghost anatomy, direct-registration era, the TLS wedge +
   decoded r4), §11 (AGREED S14Q architecture — box-driven capture,
   bank-in-box, 200-byte payloads; the design for the NEXT build, do
   not implement yet), §8 (ledcloud/2 cloud format).
2. `S14-BENCH-SESSION.md` — the "30 Sep" tail sections: threshold-sweep
   lessons, dim-room measurements, and "30 Sep evening" (1911 flash +
   bench, the ~19:01 unattended reboot + TLS wedge, handheld r4 decoded).
3. Older HANDOFF-S14Q.md content survives in git history (commit 21c5c56)
   if the 1906-era state is ever needed.

Then load the skills: `skill_view(name='led-camera-calibration')`
(v1.75; hard-won lessons + build discipline; references/box-tls-wedge.md
holds the TLS-wedge forensics + hardening options) and
`skill_view(name='led-string-calibrator')` (push discipline). The era
line was updated additively through v1.75 on 30 Sep — read it fresh
before bumping; concurrent sessions raced era bumps earlier today and
additive edits merged cleanly.

## Where things stand (verified 30 Sep night)

- Box runs **S14P-1911** (flashed + bench-verified; page BUILD +
  firmware PAGE_BUILD + ship CWCSTATS stamps all agree): **DIRECT
  per-plane registration** in both decoders (1910's chain accumulator
  deleted — `chain[p]` is plane p's OWN shift vs master), parabolic
  sub-peak NCC (surface store + conditioning floor cwcNccPeakMargin
  0.05), final gates amp 60 / margin **10** / mask 150.
- **Handheld r4 decoded** (runs/s14p-1911-handheld-r4/, commit 2d128f2):
  recovered from the bench ring by LOGA→BRAMP replay (19/19 frames).
  Console **191/200 @ 60/10**, 188/200 @ 60/25; the phone showed **187**
  — its localStorage kept marginGate 25 while 1911 ships 10 (sticky-CFG
  hazard). ZERO duplicate claims at both gates (669 pre-dedup sites @10,
  max 14 on one codeword; strongest-site dedup cleans it). Missing @10:
  [5,16,22,46,90,91,109,114] — 46/90/91 = operator-confirmed hidden
  trio (class I at export); the other five unknown-class pending the
  full-res re-score (r3 precedent: full-res scoring reaches exactly
  197/200, so that class is dec-NCC quantisation, NOT gate losses).
- Tripod reference stands at **197/200 zero-dup**, missing only the
  hidden trio. Handheld best = r3 194 @ dec-NCC page structure (full-res
  197); r4 191 confirms the class is repeatable handheld.
- Motion honesty: r4's median plane offset (−8.4, −1.9), net vs median
  ≈ 6.1 px — well inside the §4 budget, ~5× tighter than 1908's pan.
- **Known display bug (proven, fix spec'd, NOT built — design gate)**:
  the on-phone drift trail lies on handheld runs. 1911 feeds it
  median-referenced ABSOLUTE offsets (survey.html:814) while the
  renderer still sums pairs as a motion PATH (:398, byte-identical from
  1910): summing 18 absolute offsets is meaningless (r4's Σdx −146 →
  a ~10× overstatement of real motion). Fix = producer one-liner:
  pairs = successive differences (the 1910 expression, verbatim-correct
  for absolute offsets); renderer untouched.
- **1909 guard mis-themed for 1911 (CFG-only fix, design gate)**:
  shipped cwcGuardConf 0.90 / cwcGuardRem 5 was themed on legacy chain
  conf (~0.95 tripod) and chain remainders; direct-registration conf on
  PASSING handheld runs measures 0.74–0.89 (tripod still ~0.95), and
  conf is checked FIRST (else-if :786) — so every decent handheld round
  captions 'MOTION FLAGGED, unreliable' by construction (r3, the 18:51
  run, and — predicted from code + CWCSTATS, no caption screenshot —
  r4). Re-theme from measured conf distributions.
- **~19:01 the box rebooted unattended**; every TLS accept after it
  failed `mbedtls_ssl_setup -0x7F00` (SSL_ALLOC_FAILED — the heap cannot
  fund a second TLS session next to the RAM-malloc'd pageGz,
  poc_survey.ino L910-914). Phone couldn't reconnect (correctly greyed
  Burst + 'WS: closed'); operator power-cycled ~19:4× and reloaded the
  phone. Hardening options (mmap page from flash / decode-before-ssl /
  free-heap log on accept) AWAIT the design gate. LESSON: the LOGA arm
  is RAM state — it dies on any power-cycle.
- Commits (local, NOT pushed): `cfbe445` (S14P-1911 + bench tail),
  `2d128f2` (30 Sep evening docs + r4 decode + handoff rename). Push
  discipline: only after Oliver confirms the feature working well on
  the bench.

## Design gate — awaiting the operator's go (proposals only, nothing built)

1. Drift-line fix (survey.html:814 pairs → successive deltas; text
   :815 → 'step max / net' numbers like the legacy branch).
2. Guard re-theme (CFG-only): cwcGuardConf/cwcGuardRem re-derived from
   direct-registration distributions (tripod ~0.95 vs handheld-good
   0.74–0.89); consider a separate spread key for the absolute-offset
   vs budget check (the 1910 REVIEW bias analysis still argues a
   spread limit is real).
3. Sticky-CFG-on-flash policy: page localStorage keeps OLD gates across
   build bumps (r4's 25 vs shipped 10 decided tonight's count gap) —
   decide whether a new build's defaults should win on hello.
4. Full-res re-score FIRST (console offline on r4's six unknown
   missing), then one small page build **1912** = drift fix + whatever
   re-theme is agreed. Pre-flash QA: tools/cdp_1904_check.py (verify its
   stamp literals — grep for every 'S14P-191x' literal), then the
   standing STAT/new-stamp gate, then Oliver reloads the phone.

## Then, in order (each verified before the next)

2. Handheld §4 round ON 1912 with the re-themed guard: zero decode
   errors + drift-display telling the truth (compare on-trail net vs
   the CWCSTATS-derived net) — the §4 handheld gate attempt the plan
   wants before export.
3. ledcloud/2 export from the single-string site set (§8): class C for
   confirmed claims, I for hidden [46,90,91], X for the LED120/cw-103
   pattern-faithful reflections; show Oliver the cloud overlay for
   sign-off. Export = ONE conversion at generation (capture px → cloud
   box); consumers never rescale.
4. Only then start the §11 S14Q build — hard gates in order: C6
   async-WS spike FIRST (httpd_ws_send_frame_async on raw
   esp_https_server, unsolicited send), then the JS decoder must
   reproduce the console decoder's verdicts on the SAME captured frames
   (cwc_decode_sim gate). Structural payoff (measured, §11.3):
   0.10–0.19 s/plane vs today's 0.24–0.32 → ~2–3× less per-plane motion.

## Standing environment facts

- Serial: /dev/ttyACM0, hermes venv python for pyserial+cv2+PIL
  (`/home/nellie/.hermes/hermes-agent/venv/bin/python3`); system python3
  lacks them. ONE serial consumer at a time — ps-grep + `lsof /dev/ttyACM0`
  and kill stale pollers before opening the port; a flash RESETS the
  port (re-enumerate → re-open, then tools/boot_watch.py /
  boot_check.py). Serial probes go in small script files, never
  `python3 -c` heredocs (quoting corruption).
- The LOGA/LOGP/STAT family: LOGA arms a PERSISTENT logc window
  (RAM-resident — dies on power-cycle/reboot); LOGX releases it; a
  reader must never conclude from an empty first pull — re-arm and
  replay (the bench ring re-ships on every LOGA→BRAMP replay; ship-once
  cleared at burst start). Never conclude from an 18/19 replay — re-run
  it.
- Build cycle (every page edit, in this order, no step skipped):
  bump `BUILD` in `page/survey.html` → `firmware/tools/pack_page.py`
  (requires `PAGE_BUILD=<stamp>` in its output; verify the embedded
  blob gunzips byte-identical) → compile AND upload
  `--fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs"`
  (both, or the OTA table is clobbered) → boot_check → Oliver reloads
  the phone → STAT must show the new stamp before ANY run. Also grep
  every tool for stale build-stamp literals on each bump. `arduino-cli`
  lives at `~/.local/bin`; the pre-flash QA harness is
  `tools/cdp_1904_check.py` (restart its mock box after edits, truncate
  mock_box.log before verdicts, fresh Chromium profile per run; spawns
  its OWN mock — never hand-start one first).
- LED supply must be ON (12 V); CWC planes at 50% duty are ~0.84
  A/string @200 px — never fuse-limited (polyfuse holds 2 A).
- AE facts (measured, r6 + 30 Sep): the master at plane gain is CORRECT
  (k_p ≈ 1.0 by construction) — do NOT add a settle to brighten the
  master; dim-room regimes need NO waits and NO brightness compensation.
  The fixed 500 ms flush is the standing master-grab rule (open §11.3
  item: ~250 ms sufficiency).
- 19-frame protocol: P00 1s primer (AE settle) → P01..P17 → master
  (grabbed after the 500 ms pipeline flush). Codewords: bank-in-page
  `CWC_CODES_9OF18`; bank file `tools/codewords_9of18.json` is the
  SOURCE (byte-identical re-verified 1600/1600 on 30 Sep). Per-STRING
  code blocks are the S14Q-era plan, NOT implemented.
- In-page decode gates are CFG keys — tune over SERIAL or console
  sweeps, never by patching defaults blind: `cwcAmpGate` 60,
  `cwcMarginGate` 10 (1911), `cwcMaskThr` 150, `cwcNccPeakMargin` 0.05.
  Page CFG is STICKY across reloads via localStorage — read the page's
  cfg-application line once per reload before trusting the era defaults.
- Runs live in `runs/<dir>/`; docs updated + committed per step:
  S14-CWC-PLAN.md (protocol/architecture), S14-BENCH-SESSION.md (bench
  lessons), HANDOFF files for session breaks. Oliver reloads the phone;
  nothing else needs him except physical setup questions + the design
  gate — be autonomous otherwise, and verify every claim that matters
  from the repo/tools, not from memory.
- Multi-session hazard (bit Oliver twice): several concurrent Hermes
  TUIs work this repo — after patching any shared file RE-GREP it, and
  re-read shared docs immediately before your own edits.
- The console/phone decode disagreement classes (argmax-owner, ~1 claim)
  are a KNOWN residual; codeword-level strongest-site dedup + mask 150
  are the settled operating recipe on this single-string bench.