# Implementation brief — improve S14P handheld LED detection

## Background
Nellie ran a console-side parameter sweep on existing handheld CWC image sets to find changes that can be ported to the phone page. This is the implementation hand-off.

## What to build
Reduce the per-LED suppression window in the page decoder (`cwcDecode` in `page/survey.html`) from the current 7 px square to **3 px or 5 px** (test both). This is the only change expected to give a material detection-rate improvement; other knobs are already close to optimal.

## Evidence
- Runs used: `s14p-1908`, `s14p-1911`, `s14p-1913`, `s14p-1917` handheld bursts (recovered from git where overwritten). AE-lock dark runs (`1914/1915`) were excluded.
- Decoder: `tools/cwc_pos_decode.py` with monkey-patched `SUPPRESS`.
- Visible-LED counts (excluding known hidden 46, 90, 91):

| config | 1908 | 1911 | 1913 | 1917 | mean | min |
|--------|-----:|-----:|-----:|-----:|-----:|----:|
| baseline (sup7) | 186 | 196 | 185 | 194 | 190.2 | 185 |
| **sup2 (5 px)** | 191 | 197 | 192 | 197 | **194.2** | 191 |
| **sup3 (3 px)** | 191 | 197 | 192 | 197 | **194.2** | 191 |
| sup1 (1 px) | 192 | 197 | 195 | 197 | 195.2 | 192 |
| sup4 | 191 | 197 | 185 | 196 | 192.2 | 185 |

- Other families produced ≤ +1 LED on average:
  - mask-thr best `mask135` → 191.0 mean
  - blur best `blur5_s1.5` → 190.5 mean
  - peak-margin best `pm0.1` → 191.5 mean
  - full-res radius 2–8 all identical → 190.2 mean

## Code change
In `POC LED survey/page/survey.html`, inside `cwcDecode`, replace the suppression rectangle (around line 1371):

```js
const x0 = Math.max(0, x - 3), x1 = Math.min(W - 1, x + 3);
const y0 = Math.max(0, y - 3), y1 = Math.min(H - 1, y + 3);
```

with:

```js
const R = 1; // try 1 first, then maybe 2 if QA shows duplicates
const x0 = Math.max(0, x - R), x1 = Math.min(W - 1, x + R);
const y0 = Math.max(0, y - R), y1 = Math.min(H - 1, y + R);
```

If you prefer a CFG-tunable version, expose `CFG.cwcSuppress` and default it to `1` or `2`.

## QA steps
1. Bump build to **S14P-1919** in `page/survey.html` and `firmware/poc_survey/poc_survey.ino`; update `tools/cdp_1904_check.py` expected stamp.
2. Pack, compile, upload, confirm page header `S14P-1919` and WS open.
3. Run one handheld burst and pull `cwc_frames.txt`.
4. Decode on the console with the **same** suppression radius you ship in the page:
   - Edit `tools/cwc_pos_decode.py` line 71: `SUPPRESS = 3` (or 5).
   - `venv python3 tools/cwc_pos_decode.py runs/s14p-1919-handheld --save-json --save-overlay`
5. Accept if phone-confirmed count is within 1–2 LEDs of console and no duplicate LED IDs appear.
6. Check for impostors: in the overlay, verify the extra recovered LEDs sit on the string and not on top of unrelated neighbour boxes. Known hidden LEDs (46, 90, 91) may be “recovered” on top of neighbours — that is expected and acceptable to flag, not trust.

## Optional follow-up
Add a post-decode spatial audit (page-side or in `cwcShowResults` / export):
- If two accepted LEDs are within ~6 px but their IDs differ by more than ~5, flag the weaker one (lower `margin`) for operator review.
- This catches the bloom-skirt impostors the smaller suppression window re-introduces, without hard-rejecting legitimate fold/collocated LEDs before the operator can see them.

## What NOT to change
- `cwcAmpGate` (60) and `cwcMarginGate` (10): keep these. Raising margin trades away real gains.
- Blur kernel, mask threshold, full-res radius, peak margin: already tuned; leave them.
- Do not spend time on gain-estimator variants or larger blur kernels — sweep showed they are flat.

## References
- Full report: `POC LED survey/reports/handheld-detection-sweep-20261001.md`
- Raw data: `/home/nellie/.hermes/cache/scratch/led_sweep/sweep_summary.json`
- Saved overlays for top configs: `/home/nellie/.hermes/cache/scratch/led_sweep/compare/`
