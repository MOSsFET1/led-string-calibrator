# ESP32-C6 LED String Controller + Camera Calibrator — S14P-1923

Camera-based calibration for an addressable LED string (WS2815 on an
ESP32-C6): the phone browser watches, the box lamps, and the web app builds
a per-LED position map — zero install, nothing but a browser.

**Actors**

- **Phone page directs the show** — single-file `page/survey.html`, served
  BY the box (self-signed HTTPS + WebSocket on :443 from its own hotspot
  `LED-SURVEY` / `survey2026`). It sequences the burst, paints the rig,
  captures frames, decodes LED identity/positions IN-PAGE.
- **Box lamps + acknowledges on latch** — ESP32-C6 drives WS2815 lanes and
  acks every paint with a JSON latch-ack (echoing the paint's epoch).
  Status WS2812 on GPIO8 (see Status LED below).
- **Bench console/daemon on serial `/dev/ttyACM0`** — `tools/bench_daemon.py`
  (persistent LOGA arm) auto-ships every burst's frame store over WS ~1 s
  after each burst; per-burst JPEGs land in `runs/daemon/runs/<label>/`
  (JPEGs local-only, never pushed to GitHub). Directives by files dropped in
  `runs/daemon/cmds/`; `tools/s14_bench.py` for one-shot burst/pull/analyse.
  Serial directives: `PING`, `STAT`, `ABRT`, `CFG=<json>`, `BURST`, `BRAMP`,
  `LOGP`, `LOGA`, `NPX=`.
- Docs: the page is gzip+base64-embedded in the sketch — after editing
  `page/survey.html`, run `firmware/tools/pack_page.py` to embed it.

## Frame-bits protocol + CFG strings

**`frame-bits`** (WS BINARY, client→box, exactly 206 B):
`[0]='B' [1]=1 [2]=b [3]=flags [4..5]=u16 LE epoch [6..205]=1600-bit plane` —
bit j (LSB-first per byte) = LED global id j. lane=j/nPerStr, px=j%nPerStr;
ids ≥ nStr×nPerStr ignored; off-rig lanes/tails forced black; per-lane
write, NO mirroring. Malformed → `{"err":"frame-bits shape"}`. Ack rides
the normal latch path with the message's u16 epoch.

**CFG keys `nStr` (1–8) / `nPerStr` (1–200)** (defaults 1/200): applied
box-side AND page-side; hello reply is
`{"ok":true,"id":N,"fw":"poc_survey","px":nPx,"nStr":nStr,"nPerStr":nPerStr}`.
Per-string polyfuse clamp on frame-bits brightness:
`maxB = max(20, floor(255·2/(nPerStr·0.0142)))` (2 A hold, 14.2 mA/px).
JSON `frame` writes lane 1 and mirrors lanes 2-8 ONLY while `nStr≤1`;
`all`/`black`/`npx` are rig-wide.

## Build & flash (compile BEFORE flashing, every page or firmware change)

```
cd "/home/nellie/projects/led-display/POC LED survey"
arduino-cli compile --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" firmware/poc_survey
arduino-cli upload  --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" -p /dev/ttyACM0 firmware/poc_survey
```

**`PartitionScheme=min_spiffs` is part of the FQBN and is MANDATORY** on
every compile *and* upload: a bare `esp32:esp32:esp32c6` build compiles
101% against its 1,310,720 B default cap and fails with a bogus
"Sketch too big". Correct stamp S14P-1923 compiles at 1,325,940 B = **67%**
(app cap 1,966,080 B under min_spiffs) / RAM 18%.

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
real page pre-flash; `mock_box.py` spawns its own box. Bench console needs
the venv too (pyserial lives there).

## Documentation pointers

- **`S14-CWC-PLAN.md`** — design authority (capture protocol, codeword
  bank, decode gates, ledcloud/2, architecture; §11.4/§12 is the
  implementation-status log for the frame-bits era).
- **`HANDOFF-S14P.md`** — current-state handoff for build **S14P-1923**
  (build box/page/QA stamps, per-change summary, open work, repo state).
- **`reports/`** — subagent evidence reports for the current era.
- `archive/` — retired docs (one-line pointer at the original location or
  see `archive/README.md`).

## Current era (S14 CWC, in-page)

The S13 per-LED hole survey is retired from the page (its detector +
serpentine guard still live inside the S14 decode). The burst carries
identity IN THE LIGHT PATTERN (Twinkly-style): a 50%-on coded-plane primer
(HOLD 1,000 ms for AE settle), planes P01..P17 at 100 ms settle each
(floor 70, CFG `cwcSettle`), then a fast all-ON master grab with only the
70 ms flush before AE re-meters; decode diffs each plane against that
master. Codewords 9-of-18, 1600 codes, d_min 4 (bank =
`tools/codewords_9of18.json`, prefix property); decode gates: amp ≥ 60,
margin ≥ 10, threshold 150 double 3-tap box blur ×2, suppression radius 1
within the SAME string; spatial conflict audit same-string. Sites (global
ids 0..1599) ship as CWCDEC chunks + CWCDECS summary. Registration is
direct per-plane NCC vs master with parabolic sub-peak + full-res ±4 px
stride-2 refine. Latest handheld round: phone 193/200, 5 spatial conflicts
flagged, no motion flag; console-era parity 195/200.

## Key parameters (page CFG, serial-tunable live)

| param | default | meaning |
|---|---|---|
| `nStr` / `nPerStr` | 1 / 200 | strings / LEDs per string (frame-bits geometry) |
| `allB` | 160 | All-on button brightness |
| `evBias` | -1 | exposureCompensation bias (Android only) |
| `mergeR` | 6 | blob merge (detectDiffBlobs, the CWC pile-up reader) |
| `bBurstN/Gap/Hold/B` | 20/0/1000/150 | bench burst frames, pacing, all-on hold, brightness |
| `bComp` | 0 | in-page drift compensation (seeded NCC vs burst master) |
| `cwc` | 0 | 1 = CWC-form burst (master + 18 codeword planes) |
| `cwcN` | 150 | LEDs mapped to codewords 0..cwcN-1 |
| `cwcSettle` | 100 | ms paint→grab (clamped ≥70) |
| `cwcMaskThr` | 150 | candidate-mask blur threshold |
| `cwcAmpGate` | 60 | decode amp gate |
| `cwcMarginGate` | 10 | decode d6 margin gate |
| `cwcSuppress` | 1 | same-string suppression radius (px) |
| `cwcTestMode` | 0 | 1 = single-LED toggle test (plan §4) |
| `cwcTestLed` | 0 | test LED index |

## Known issues

- Serial directive slot is single (`sDrv`): a directive sent while another is
  pending is LOST — the bench console drains before sending (29 Sep).
- Phone page must be foregrounded for the drv? poll to run (screen asleep =
  GONE on STAT); the wake lock only helps while the tab is open.
- JSON `frame` mirrors lanes 2-8 only while `nStr≤1`; `all`/`black`/`npx`
  apply rig-wide.

## Hardware (proven on the bench, carried from the design docs)

ESP32-C6-Zero · WS2815 lanes on the rig (bench chips are RGB-wired, not
GRB) · status WS2812 on GPIO8 · self-signed cert baked into the firmware
(it's the box's own hotspot identity, regenerated for every real
deployment). Runtime LED count via NPX= (150 default, 200 max per string)
and CFG nStr × nPerStr (1–8 strings × up to 200 LEDs; the CWC page maps
cwcN of them to codewords).

## Status LED

solid red = new build flashed, phone not yet reloaded · dark while a paint
is recent (burst/survey activity — the LED must not photobomb the camera) ·
slow green breathe = idle and up to date.