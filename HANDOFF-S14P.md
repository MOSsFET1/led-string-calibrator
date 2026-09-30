# Handover — S14P-1916 (01 Oct)

> Former `HANDOFF-S14Q.md` retired 30 Sep night; old content lives in git
> (commit `21c5c56`) if needed. This file reflects the 30 Sep → 01 Oct
> live round sequence ending at **S14P-1916**.

## Current build

- Firmware on box: **S14P-1916**
- Page BUILD string: **S14P-1916**
- `tools/cdp_1904_check.py` expects: **S14P-1916**

Always confirm the page header and a `STAT` log line match before running.

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
5. **S14P-1916** — **reverted burst order to original regime**: P00 1 s
   primer → P00..P17 coded planes → fast all-on master grab before AE
   re-adjusts. AE lock now only calls `applyConstraints({ advanced:
   [{exposureMode:'manual'}] })` and does NOT touch `exposureCompensation`.
   Motion guard relaxed: `cwcGuardConf 0.65`, `cwcGuardRem 24`, added
   `cwcGuardStep 10` to catch sudden jerks without flagging slow handheld
   drift.

## Files of record

- `page/survey.html` — S14P-1916, bilinear stacksig, fixed AE lock,
  re-themed guard, build-tied localStorage.
- `firmware/poc_survey/poc_survey.ino` — S14P-1916 banner + PAGE_BUILD.
- `tools/cwc_pos_decode.py` — bilinear stacksig, full-res refine.
- `tools/cdp_1904_check.py` — S14P-1916 pre-flash QA.
- `tools/codewords_9of18.json` — source codeword bank, valid for 1600 LEDs.
- `reports/phone-vs-console-cwc-gap.md` — subagent report on phone-vs-console
  differences (mask-threshold bug, local-max filter, parabolic guard).
- `runs/s14p-1916-handheld/serial.txt` — capture reader target for next run.

## Known open work (verify before claiming success)

1. **Handheld S14P-1916** has NOT been run yet. Next step: Oliver reloads the
   page, confirms `S14P-1916` header + WS open, runs handheld, then downloads
   the phone log. Read it from the box (or have Oliver transfer it) and run
   both page parse and `tools/cwc_pos_decode.py` on the CWC block.
2. **Motion robustness target**: the 1915 round flagged motion on ~6-7 px
   movement. The 1916 guard (24 px absolute, 10 px step) should accept that,
   but it is untested.
3. **AE lock is best-effort only**: iOS Safari ignores it; Android Chrome may
   honour it. If the next run is still dark, disable AE lock with
   `CFG={"cwcAeLock":0}` over serial and re-test, OR consider whether the
   phone meter re-acts during P01..P17 despite the lock.
4. **1600-LED scaling** is a design discussion, not built: 8 strings × 200 LEDs
   via ESP32-C6 PARLIO 8 lanes is feasible; codeword bank already supports
   1600; current WS recv limit is 4096 bytes, so a 1600-LED JSON frame
   (~8 KB) needs a compact binary frame format.

## How to continue this session

1. Confirm page shows `S14P-1916` and WS open.
2. Capture reader should already be running for
   `runs/s14p-1916-handheld/serial.txt`. If not, restart it with `LOGA`.
3. After Oliver runs handheld, retrieve the phone log and decode both page
   output and console frames. Compare missing lists, duplicates, and max
   shift.
4. If 1916 handheld is good, push to GitHub: the last push was `00c929c`;
   there are now local commits that need committing/merging first. Use the
   PAT at `~/LED_PAT.txt`.
5. If 1916 is bad, inspect the log: dark image → check AE lock messages
   (`AE locked to manual` / `AE lock not supported` / `AE lock failed`); motion
   flagged → note which guard tripped (conf/rem/step) and adjust
   `cwcGuardConf`, `cwcGuardRem`, `cwcGuardStep` over serial.

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
5. `arduino-cli upload --fqbn ... -p /dev/ttyACM0 firmware/poc_survey`
6. Verify boot banner; restart serial capture reader (flash resets port).
7. Oliver reloads phone; confirm page header + WS open.
