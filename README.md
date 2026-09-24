# ESP32-C6 LED String Controller + Camera Calibrator

Camera-based calibration for an addressable LED string (WS2815 on an
ESP32-C6): the box paints patterns, a phone's browser camera watches, and the
web app builds a per-LED position map — zero install, nothing but a browser.

**Current paradigm — S13 "hole survey"** (validated: 148–149/150 LEDs located
per run in 20–45 s, dim and bright rooms, 150-px bench string):

1. All LEDs on white at `holeB` → wait for the camera AE to settle → grab the
   **master** frame.
2. Per LED (batched in K=3 windowed chains): turn that one LED **off** → grab
   the **pair** → `master − pair` isolates the LED as a *dark hole* in the
   bright field.
3. Detection = threshold → connected components → bloom merge → outside-tail
   dominance bar → per-chain window → **serpentine-continuity audit** (a hole
   farther than `holeGateK` × median pitch from its ID neighbours' midpoint is
   an impostor — e.g. a shelf reflection — rejected, marked HIDDEN,
   interpolated from its neighbours).

This replaced two dead ends, each killed by real evidence (see
`docs/`): the per-pixel ON/OFF diff *march* (identity carried by scan order —
needs 100 s of tripod stillness, dies on camera motion) and colour/hue
detection (AWB re-balance moves the field's hue-excess ±40 between grabs —
indistinguishable from a die). Luma-only hole diffs on an AE-pinned bright
field carry none of those failure modes; the next step (designed, not built)
is Twinkly-style binary coded bit-planes, which carry identity in the code
instead of in a march.

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

## Bench console (drives a scan without touching the phone)

`tools/hole_session.py` — STAT gate, single-burst `CFG=` + `SCAN`, EV-verify,
LOGP/FRAMP pulls into `runs/`. `tools/offline_hole_verify.py` re-runs
detection offline on pulled frames and cross-checks the page's verdicts.
Serial directives: `PING`, `STAT`, `SCAN`, `ABRT`, `CFG=<json>`, `EV`, `NPX=`.

## Key parameters (page CFG, serial-tunable live)

| param | default | meaning |
|---|---|---|
| `holeThr` | 60 | hole diff threshold (walk-up: plateau 45–90, cliff at 120) |
| `mergeR` | 6 | bloom-merge radius (bright-room ladder: 2 fragments, 6 optimal) |
| `holeDomK` | 8 | dominance bar: peak ≥ domK × outside-blob d10 |
| `holeWinFrac` | 0.055 | K-chain window radius floor (× frame diagonal) |
| `holeGateK` | 4 | serpentine gate in units of measured median pitch |
| `holeB` | 200 | all-on master/pair brightness |

## Known issues

- Anchor bookkeeping (fixed in S13L, bench-verified Sep 24: anchors 50/100
  found in all 3 EV-verified runs; miss list is now only the 94/95 diffuser
  pair).
- px94/95 (hidden die behind a diffuser) flaps between found/missed run-to-run;
  accepted as geometry, not detection.

## Hardware (proven on the bench, carried from the design docs)

ESP32-C6-Zero · WS2815 lane on GPIO1 (bench chips are RGB-wired, not GRB) ·
status WS2812 on GPIO8 · self-signed cert baked into the firmware (it's the
box's own hotspot identity, regenerated for every real deployment).
Runtime LED count via `NPX=` (150 default, 200 max).

## Status LED

solid red = new build flashed, phone not yet reloaded · off = survey running ·
slow green breathe = idle and up to date.