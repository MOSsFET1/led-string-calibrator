# Handover — S14P-1919 (01 Oct)

> Former `HANDOFF-S14Q.md` retired 30 Sep night; old content lives in git
> (commit `21c5c56`) if needed. This file reflects the 30 Sep → 01 Oct
> live round sequence ending at **S14P-1919**.

## Current build

- Firmware on box: **S14P-1922** (flashed 02 Oct, upload hash-verified,
  min_spiffs, 1,322,190 B = 67%)
- Page BUILD string: **S14P-1922**
- `tools/cdp_1904_check.py` expects: **S14P-1922**
- Boot banner says S14P-1922 (banner string was stale at 1921 — fixed same day).

## S14P-1922 first handheld round (02 Oct 06:44, daemon run1)

- Phone telemetry: **confirmed 193/200, 5 spatial conflicts flagged
  (a26/b115, a28/b117, a77/b103, a78/b102, a147/b160)**, chain conf
  0.777–0.900 across all 18 registers, exp pinned 300.03, aem continuous,
  no motion flag (smooth drift dx≈6 px). vs S14P-1917 baseline phone
  190/200; console 195/200. ACCEPTED: within 2 of console parity, +3 on
  the phone baseline, conflict audit doing its job at suppress R=1.
- Captured via persistent bench daemon (runs/daemon/), LOGA armed;
  run1 frames decoded to runs/daemon/runs/run1/ (JPEGs local-only, not
  for GitHub per operator rule).

## What changed since 1919

1. **S14P-1922** — page decode now calls `nccRefine()` at score time
   (±4 px, stride 2, full-res) before bilinear sampling, matching
   `tools/cwc_pos_decode.py`; page-simulation harness verified page-vs-
   console ±1 LED per run, +0.20 LEDs ensemble mean (reports/page-
   simulation-s14p-1922.md).

## What changed since 1911

1. **S14P-1912** — direct-registration + full-resolution NCC refine in both
   page and Python (`--fullres-rad 4`, `FULLRES_STRIDE 2`); page prints
   missing-LED list; motion guard re-themed to `cwcGuardConf 0.70` /
   `cwcGuardRem 16` absolute vs master; drift line changed to successive
   differences; build-tied localStorage (new `BUILD` wipes stale saved CFG).
2. **S14P-1913** — page mask threshold default locked to `150` (was falling
   back to `175`); removed page-only ±3 px local-max filter; aligned page
   parabolic-refinement guard with Python (`peak - max(shoulder) >= margin`).
3. **S14P-1914** — bilinear resampling in `stacksig` in both page and Python;
   best-effort AE-lock-after-P00 using `exposureMode: 'manual'` (with a
   frozen `exposureCompensation` that caused a dark image on the first test).
4. **S14P-1915** — tried all-on primer as master + AE lock, then coded
   P01..P17. Phone reported MOTION FLAGGED on small real movement and only
   87 LEDs, image very dark.
5. **S14P-1916** — reverted burst order to original regime: P00 1 s primer →
   P00..P17 coded planes → fast all-on master grab before AE re-adjusts.
   AE lock reduced to only `applyConstraints({advanced:[{exposureMode:'manual'}]})
   without touching `exposureCompensation`. Motion guard relaxed:
   `cwcGuardConf 0.65`, `cwcGuardRem 24`, added `cwcGuardStep 10`.
6. **S14P-1917** — **disabled AE lock by default** (`cwcAeLock: 0`). The lock
   code remains in place and can be re-enabled with `CFG={"cwcAeLock":1}`, but
   Android Chrome was honouring the manual-AE request and freezing exposure
   at the dark P00 value, collapsing phone confirms from ~190 to ~95. With
   the default off, exposure stays `aem=continuous` and detection returns to
   the pre-lock level.
7. **S14P-1918** — replaced the page's double box-blur mask with a separable
   5-tap Gaussian (`sigma ≈ 1.2`) to match console `cv2.GaussianBlur((5,5), 1.2)`.
   Goal is blur parity between phone and Python decoder. Baseline from the
   S14P-1917 handheld run: phone confirmed 190 LEDs, console confirmed
   **195/200**; gaps were phone-only `[16, 27, 91]` with marginal phone margins
   `(14.6, 15.6, 10.4)`, and console-only `[4, 22, 25, 46, 99, 100, 122, 143]`.
   `cwcAeLock` remains default-off (`0`). QA passes for 1918 with venv Python.
8. **S14P-1919** — added `CFG.cwcSuppress` default `1` (3 px suppression window
   vs previous 7 px), and added a spatial conflict audit in `CWCSTATS`. All other
   gates, blur, and peak-margin settings were left unchanged per the sweep
   report. Expected outcome: ~+4 visible LEDs mean vs the S14P-1917 baseline,
   targeting console parity or within 1–2 LEDs. QA passes for 1919 with venv
   Python.

## Files of record

- `page/survey.html` — S14P-1919, separable 5-tap Gaussian mask, AE lock default
  OFF, bilinear stacksig, build-tied localStorage, `cwcSuppress` default `1`,
  spatial conflict audit in `CWCSTATS`.
- `firmware/poc_survey/poc_survey.ino` — S14P-1919 banner + PAGE_BUILD.
- `tools/cwc_pos_decode.py` — bilinear stacksig, full-res refine.
- `tools/cdp_1904_check.py` — S14P-1919 pre-flash QA.
- `tools/codewords_9of18.json` — source codeword bank, valid for 1600 LEDs.
- `reports/phone-vs-console-cwc-gap.md` — subagent report on phone-vs-console
  differences (mask-threshold bug, local-max filter, parabolic guard).
- `runs/s14p-1919-handheld/cwc_frames.txt` — next capture target for the
  current run.

## Known open work (verify before claiming success)

1. **Handheld S14P-1919** needs a real run to `runs/s14p-1919-handheld/cwc_frames.txt`
   to validate the improvement over the S14P-1917 baseline (phone 190/200, console
   195/200). The 1919 changes (`cwcSuppress` 1, spatial conflict audit) are in
   place; verify whether visible LEDs move toward console parity or within 1–2
   LEDs. 1917 baselines: phone-only `[16, 27, 91]` with margins `(14.6, 15.6,
   10.4)`; console-only `[4, 22, 25, 46, 99, 100, 122, 143]`.
2. **Motion robustness target**: the 1916 guard (24 px absolute, 10 px step)
   accepted the 1917 handheld run without a motion flag; it appears good.
3. **AE lock is now default-OFF**. If you ever want to re-test it, send
   `CFG={"cwcAeLock":1}` over serial, reload the page, and run. The evidence
   shows Android Chrome honours the lock and darkens the image.
4. **1600-LED scaling** is a design discussion, not built: 8 strings × 200 LEDs
   via ESP32-C6 PARLIO 8 lanes is feasible; codeword bank already supports
   1600; current WS recv limit is 4096 bytes, so a 1600-LED JSON frame
   (~8 KB) needs a compact binary frame format.

## How to continue this session

1. Confirm page shows `S14P-1919` and WS open.
2. Capture reader should already be armed for
   `runs/s14p-1919-handheld/cwc_frames.txt`. If not, restart it with `LOGA`.
3. After Oliver runs handheld, retrieve the phone log and decode both page
   output and console frames. Compare missing lists against the 1917 baselines
   (phone 190/200, console 195/200).
4. If 1919 handheld is good, push to GitHub: the last push was `00c929c`;
   there are now local commits that need committing/merging first. Use the
   PAT at `~/LED_PAT.txt`.
5. If 1919 is bad, inspect the log: dark image → check whether AE lock was
   accidentally enabled (`AE locked to manual`); motion flagged → note which
   guard tripped (conf/rem/step) and adjust `cwcGuardConf`, `cwcGuardRem`,
   `cwcGuardStep` over serial.

## Repo state

- Branch `main` at GitHub `00c929c`; local working tree has unpushed changes.
- Backup branch `backup-before-image-strip` at `3286a94`.
- `git stash` still holds dirty files from the image-strip push.
- No new images should be pushed to GitHub (operator instruction).

## Build cycle (every page/firmware edit)

1. Bump `BUILD` in `page/survey.html` and `PAGE_BUILD`/banner in
   `firmware/poc_survey/poc_survey.ino`.
2. Update `tools/cdp_1904_check.py` expected stamp.
3. `python3 firmware/tools/pack_page.py`
4. `arduino-cli compile --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs" firmware/poc_survey`
   `PartitionScheme=min_spiffs is MANDATORY` — never compile bare:
   `esp32:esp32:esp32c6` selects the default partition (app cap 1,310,720 B)
   and this firmware is ~1.33 MB → 101% "Sketch too big". That is a command
   artifact, not code growth (see "Flash-size fact" below).
5. `arduino-cli upload --fqbn ... -p /dev/ttyACM0 firmware/poc_survey`
6. Verify boot banner; restart serial capture reader (flash resets port).
7. Oliver reloads phone; confirm page header + WS open.

## Flash-size fact (01 Oct, verified — resolves the "101%" scare)

- Standing scheme `min_spiffs`: app cap = 0x1E0000 = 1,966,080 B (core
  3.3.11 `tools/partitions/min_spiffs.csv`). Verified compile of the
  S14P-1922 tree on this scheme: `Sketch uses 1322190 bytes (67%)`, exit 0.
- A bare `esp32:esp32:esp32c6` compile (default partition, app cap
  0x140000 = 1,310,720 B) gives `Sketch uses 1331528 bytes (101%)` —
  reproduced byte-exact. The scratch-era HANDOFF-S14P-1922-SIZE ran the
  bare command, hence its "flash-blocked" premise and its huge_app
  "workaround"; huge_app (app 0x300000 = 3,145,728 B, no OTA, no SPIFFS)
  is NOT needed — min_spiffs holds the sketch at 67% with ~644 KB headroom.
- Real sketch growth since the 66% era is modest: S13L (Sep 24)
  1,297,088 B → now 1,322,190 B = +25,102 B of CWC feature work (page
  blob +11.4 KB gz ≈ +14 KB flash; C++ +9.7 KB → ~1.5 KB compiled; rest
  ELF/packing). A bare-default compile squeaked by at 99% back then and
  fails only because of that +25 KB — on the worked axis (min_spiffs)
  nothing ever left the 65–67% band.
