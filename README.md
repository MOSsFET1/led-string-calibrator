# ESP32-C6 LED String Controller + Camera Calibrator

Camera-based calibration for an addressable LED string (WS2815 on an
ESP32-C6): the box paints patterns, a phone's browser camera watches, and the
web app builds a per-LED position map — zero install, nothing but a browser.

**Predecessor — S13 "hole survey"** (validated 148–149/150 LEDs per run in
20–45 s, dim and bright rooms; retired from the page in S14K — the S14
decode still reuses its detector + serpentine guard):

1. All LEDs on white → AE settle → **master** frame.
2. Per LED (K=3 windowed chains): LED **off** → **pair** →
   `master − pair` isolates it as a *dark hole*.
3. Detection = threshold → components → bloom merge → dominance bar →
   per-chain window → **serpentine-continuity audit** (an impostor — e.g.
   a shelf reflection — is rejected, marked HIDDEN, interpolated).

This replaced two dead ends, each killed by real evidence (see
`docs/`): the per-pixel ON/OFF diff *march* (identity carried by scan order —
needs 100 s of tripod stillness, dies on camera motion) and colour/hue
detection (AWB re-balance moves the field's hue-excess ±40 between grabs —
indistinguishable from a die). Luma-only hole diffs on an AE-pinned bright
field carry none of those failure modes.

**Current era — S14 CWC (built, handheld-validated)**: the hole survey is
retired from the page (S14K removed the survey line); the burst now carries
identity IN THE LIGHT PATTERN (Twinkly-style). One handheld burst = master +
a 50%-duty primer plane (S14N, settles the phone AE once) + 18 codeword
bit-planes (9-of-18, 1600 codes, d_min 4); each LED's identity arrives as a
9-bit code read against the registered master × per-plane gain. First
handheld round (r6, 27 Sep): 13 cores decode strictly from deliberate-
movement frames; per-bit error ~17% — bulk decode is the next step
(see `S14-CWC-PLAN.md` §3/§9; point cloud = `ledcloud/2`, §8).
**In progress (S14O/S14P-1901)**: single-LED toggle test (plan §4) — validate
the bit-read path end-to-end on one LED (zero-error tripod gate) before bulk
decode; page ship path harness-PASSED (mock box + headless Chromium),
console analysis built + synthetic-validated. The 28 Sep 'frames don't
ship' failure was console-side (pull reader + directive-slot race), not
the page — see `S14-BENCH-SESSION.md` §S14O/S14P.

## Layout

```
firmware/poc_survey/   ESP32-C6 Arduino sketch (arduino-cli; FQBN below)
page/survey.html       the single-file web app (served BY the box)
firmware/tools/        pack_page.py (embed page into the sketch), embed_cert.py
tools/                 bench console + offline verification scripts
runs/                  console session logs + pulled camera frames
docs/                  the research/test-plan trail (F1 plan, pivot, S13 logs)
```

The page is gzip+base64-embedded in the sketch — **the box serves
everything**: self-signed HTTPS + WebSocket on :443 from its own hotspot
(`LED-SURVEY` / `survey2026`). The phone joins the hotspot, opens
`https://192.168.4.1/`, accepts the self-signed cert warning, allows the
camera, and runs calibration + review entirely in the browser.

## Build & flash

```
cd firmware/poc_survey
arduino-cli compile --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" .
arduino-cli upload  --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" -p /dev/ttyACM0 .
```

Partition scheme `min_spiffs` is part of the FQBN — repeat it on every compile
*and* upload, or a default-scheme build clobbers the OTA table. After changing
`page/survey.html`: bump `BUILD` in the page, run
`firmware/tools/pack_page.py`, bump `PAGE_BUILD` in the sketch to the same
stamp, recompile, reflash, then reload the page on the phone. The onboard
status LED shows solid red until the phone loads the matching build.

## Bench console (drives a burst without touching the phone)

`tools/s14_bench.py` — `burst` (CFG + BURST), `pull` (LOGP→BRAMP frames
into `runs/<dir>/cwc_frames.txt`), `analyse` (cadence + motion).
`tools/bench_daemon.py` — persistent-serial capture with LOGA (arm that
survives between pulls) + cmds/ trigger files. `tools/cwc_analyse.py` —
the 19-frame CWC analysis (registration, pile-up, single-LED bit read).
`tools/offline_hole_verify.py` — frame decode + the S13-mirror detector.
Serial directives: `PING`, `STAT`, `ABRT`, `CFG=<json>`, `BURST`,
`BRAMP`, `LOGP`, `LOGA`, `NPX=`.

## Key parameters (page CFG, serial-tunable live)

| param | default | meaning |
|---|---|---|
| `allB` | 160 | All-on button brightness |
| `evBias` | -1 | exposureCompensation bias (Android only) |
| `mergeR` | 6 | blob merge (detectDiffBlobs, the CWC pile-up reader) |
| `bBurstN/Gap/Hold/B` | 20/0/1000/150 | bench burst frames, pacing, all-on hold, brightness |
| `bComp` | 0 | in-page drift compensation (seeded NCC vs burst master) |
| `cwc` | 0 | 1 = CWC-form burst (master + 18 codeword planes) |
| `cwcN` | 150 | LEDs mapped to codewords 0..cwcN-1 |
| `cwcSettle` | 100 | ms paint→grab (clamped ≥70) |
| `cwcTestMode` | 0 | 1 = single-LED toggle test (plan §4) |
| `cwcTestLed` | 0 | test LED index |

## Known issues

- S13 leftovers are history (px94/95 diffuser = geometry, anchor bookkeeping
  fixed Sep 24) — carried as accepted limits into the S14 decode gates.
- Serial directive slot is single (`sDrv`): a directive sent while another is
  pending is LOST — the bench console drains before sending (29 Sep).
- Phone page must be foregrounded for the drv? poll to run (screen asleep =
  GONE on STAT); the wake lock only helps while the tab is open.

## Hardware (proven on the bench, carried from the design docs)

ESP32-C6-Zero · WS2815 lane on GPIO1 (bench chips are RGB-wired, not GRB) ·
status WS2812 on GPIO8 · self-signed cert baked into the firmware (it's the
box's own hotspot identity, regenerated for every real deployment).
Runtime LED count via `NPX=` (150 default, 200 max — the CWC page maps
cwcN of them to codewords).

## Status LED

solid red = new build flashed, phone not yet reloaded · dark while a paint
is recent (burst/survey activity — the LED must not photobomb the camera) ·
slow green breathe = idle and up to date.