# ESP32-C6 LED String Controller + Camera Calibrator — S14P-1928

Camera-based calibration for an addressable LED string (WS2815 on an
ESP32-C6): the phone browser watches, the box lamps, and the web app builds
a per-LED position map — zero install, nothing but a browser. Next
line: **S14R-0000** (designated 02 Oct) moves the codeword bank to
12-of-24 (extended-Golay subcode, d_min 8) — see S14-CWC-PLAN.md §13.

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
  after each burst in DECODE mode; per-burst JPEGs land in
  `runs/daemon/runs/<label>/` (JPEGs local-only, never pushed to GitHub).
  Since **S14P-1928** the page also has a CAPTURE-ONLY burst mode
  (`chkCapOnly`): choreography unchanged, decode/ship skipped, frames
  accumulate across bursts in the page's benchStore and leave via the
  manual **'Send frames (all)'** button (a store wrap drops the OLDEST
  frame with a loud log). Directives by files dropped in
  `runs/daemon/cmds/`; `tools/s14_bench.py` for one-shot burst/pull/analyse.
  Serial directives: `PING`, `STAT`, `ABRT`, `CFG=<json>`, `BURST`, `BRAMP`,
  `LOGP`, `LOGA`, `NPX=`.
  If the box's HTTPS dies with a `mbedtls_ssl_setup -0x7F00` storm
  (TLS-heap wedge, root cause undiagnosed): RTS→EN pulse via bench serial
  reboots the box (banner replays), then restart the daemon, verify
  `=== daemon start ===` + `[LOGA] persistent arm ON`, then use cmds/.
- Docs: the page is gzip+base64-embedded in the sketch — after editing
  `page/survey.html`, run `firmware/tools/pack_page.py` to embed it.

## Frame-bits protocol + CFG strings

**`frame-bits`** (WS BINARY, client→box, exactly 206 B):
`[0]='B' [1]=1 [2]=b [3]=flags [4..5]=u16 LE epoch [6..205]=1600-bit plane` —
bit j (LSB-first per byte) = LED global id j. lane=j/nPerStr, px=j%nPerStr;
ids ≥ nStr×nPerStr ignored; off-rig lanes/tails forced black; per-lane
write, NO mirroring. Malformed → `{"err":"frame-bits shape"}`. Ack rides
the normal latch path with the message's u16 epoch.

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
cd "/home/nellie/projects/led-display/poc_survey"
arduino-cli compile --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" firmware/poc_survey
arduino-cli upload  --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" -p /dev/ttyACM0 firmware/poc_survey
```

**`PartitionScheme=min_spiffs` is part of the FQBN and is MANDATORY** on
every compile *and* upload: a bare `esp32:esp32:esp32c6` build compiles
101% against its 1,310,720 B default cap and fails with a bogus
"Sketch too big". Correct stamp S14P-1928 compiles at ~1.33 MB = **67%**
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
  bank, decode gates, ledcloud/2, architecture; §12/§13 is the
  implementation-status log for the frame-bits era and the 02 Oct
  console-led optimisation/feasibility record).
- **`HANDOFF-S14P.md`** — current-state handoff for build **S14P-1928**
  (build box/page/QA stamps, per-change summary, open work, repo state;
  records the S14R-0000 designation for the 12-of-24 era).
- **`reports/`** — subagent evidence reports for the current era
  (incl `console-decode-20261002-run2-6.md` + the sweep JSON — the
  1710/2000 baseline and gate-sweep evidence — and
  `INCIDENT-S14P-1926-cfg-one-shot-gap.md`).
- `archive/` — retired docs (one-line pointer at the original location or
  see `archive/README.md`).

## Current era (S14 CWC, in-page — console-led since 02 Oct)

The S13 per-LED hole survey is retired from the page (its detector +
serpentine guard still live inside the S14 decode). The burst carries
identity IN THE LIGHT PATTERN (Twinkly-style): a 50%-on coded-plane primer
(HOLD 1,000 ms for AE settle), planes P01..P17 at 100 ms settle each
(floor 70, CFG `cwcSettle`), then a fast all-ON master grab with only the
70 ms flush before AE re-meters; decode diffs each plane against that
master. Codewords 9-of-18, 1600 codes, d_min 4 (bank =
`tools/codewords_9of18.json`, prefix property); decode gates are the
**02 Oct sweep-promoted set, compiled as defaults since S14P-1927/28**:
amp ≥ 40, margin ≥ 6, threshold 100 double 3-tap box blur ×2 (console
optimum: 1710/2000 across runs 2–6 at ~3 m off-axis; old 60/10/150
defaults scored 1458), suppression radius 1 within the SAME string;
spatial conflict audit same-string; full-res ±4 px stride-2 NCC refine
is MANDATORY off-tripod (fullres-off costs ~513/2000). Sites (global
ids 0..cwcN-1) ship as CWCDEC chunks + CWCDECS summary — in
capture-only mode (S14P-1928) the decode/ship half is bypassed and
frames accumulate for the manual bulk send / console decode. Latest
handheld round (handheld-parity era): phone 193/200, 5 spatial conflicts
flagged, no motion flag; console-era parity 195/200 (string-1 bench).
The bank cannot reach d_min 5 at 1600 codes (sphere bound 592); the
12-of-24 Golay replacement (d_min 8, 1600-code prefix verified) is the
designated next era **S14R-0000** (plan §13).

## Key parameters (page CFG, serial-tunable live)

| param | default | meaning |
|---|---|---|
| `nStr` / `nPerStr` | 8 / 200 | strings / LEDs per string (frame-bits geometry; S14P-1927 defaults = full rig) |
| `allB` | 160 | All-on button brightness |
| `evBias` | -1 | exposureCompensation bias (Android only) |
| `mergeR` | 6 | blob merge (detectDiffBlobs, the CWC pile-up reader) |
| `bBurstN/Gap/Hold/B` | 20/0/1000/150 | bench burst frames, pacing, all-on hold, brightness |
| `bComp` | 0 | in-page drift compensation (seeded NCC vs burst master) |
| `cwc` | 0 | 1 = CWC-form burst (master + 18 codeword planes) |
| `cwcN` | 1600 | LEDs mapped to codewords 0..cwcN-1 (clamped to nStr×nPerStr at burst start) |
| `cwcSettle` | 100 | ms paint→grab (clamped ≥70) |
| `cwcMaskThr` | 100 | candidate-mask blur threshold (02 Oct sweep-promoted, was 150) |
| `cwcAmpGate` | 40 | decode amp gate (02 Oct sweep-promoted, was 60) |
| `cwcMarginGate` | 6 | decode d6 margin gate (02 Oct sweep-promoted, was 10) |
| `cwcSuppress` | 1 | same-string suppression radius (px) |
| `cwcTestMode` | 0 | 1 = single-LED toggle test (plan §4) |
| `cwcTestLed` | 0 | test LED index (global id; demo of 14:38 used 599 = string 3 pixel 200 — its chip was a phantom relabel, §14) |

## Known issues

- Serial directive slot is single (`sDrv`): a directive sent while another is
  pending is LOST — the bench console drains before sending (29 Sep).
- **CFG cwcN above the INSTALLED LED count = cousin-phantom over-claims**
  (the 9-of-18 d_min 4 relabel class): cwcN must equal what is physically
  wired — cwcN 1600 → 646 confirmed = 392 real + 254 relabels (S14P-1927
  morning); cwcN 600 on a one-string rig → 275 with 33 conflicts (02 Oct
  §14 demo, 'led 599' chip = phantom relabel).
- **TLS-heap wedge** (root cause UNDIAGNOSED): after long uptime the box's
  HTTPS dies with `mbedtls_ssl_setup -0x7F00` (ALLOC_FAILED) storms — twice
  on 02 Oct (~08:32–08:43, 12:57–12:58), killing the daemon's capture path.
  Recovery: RTS→EN pulse via bench serial (banner replays), restart the
  daemon, verify `=== daemon start ===` + `[LOGA] persistent arm ON`.
- Phone page must be foregrounded for the drv? poll to run (screen asleep =
  GONE on STAT); the wake lock only helps while the tab is open; a page-side
  drv?-stall self-recovery is still open (B119 class).
- Phone-vs-console parity for the 02 Oct runs 2–6 round set is UNVERIFIABLE
  (capture-only mode shipped no CWCSTATS/CWCDEC; the daemon died 12:58
  before any later telemetry) — console verdicts are the ground truth.
- JSON `frame` mirrors lanes 2-8 only while `nStr≤1`; `all`/`black`/`npx`
  apply rig-wide.

## Hardware (proven on the bench, carried from the design docs)

ESP32-C6-Zero · WS2815 lanes on the rig (bench chips are RGB-wired, not
GRB) · status WS2812 on GPIO8 · self-signed cert baked into the firmware
(it's the box's own hotspot identity, regenerated for every real
deployment). Runtime LED count via NPX= (200 max per string; firmware
nStr/nPerStr defaults are the full 8×200 rig since S14P-1927)
and CFG nStr × nPerStr (1–8 strings × up to 200 LEDs; the CWC page maps
cwcN of them to codewords).

## Status LED

solid red = new build flashed, phone not yet reloaded · dark while a paint
is recent (burst/survey activity — the LED must not photobomb the camera) ·
slow green breathe = idle and up to date.