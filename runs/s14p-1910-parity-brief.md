# S14P-1910 console parity brief — cwc_pos_decode.py × page cwcChain + §10d (30 Sep)

**Scope**: `tools/cwc_pos_decode.py` only (console decoder). No page, firmware,
serial or S14-doc edits. Everything offline from pulled `cwc_frames.txt` runs.
Artifacts: `runs/s14p-1910-parity-check.json` (machine-checked numbers),
per-run refreshed `ledpos.json`/`led_overlay.png`.

## 1. What the console did before (characterisation)

- **Registration**: `cwc_analyse.reg_residual` = `cv2.phaseCorrelate` on
  **sqrt-luma** at FULL resolution + Hanning → float (dx, dy) per plane,
  **direct** plane-vs-master (no chain, no decimation, no integer grid).
- **Warp**: `warpAffine(plane, +[dx,dy])` bilinear; stacksig = `mb_reg − k·regd`
  on the warped float copies; score at integer sites; gates 90/30.
- **Deltas vs page `cwcChain`** (§10 recipe): no backwards chain (no
  integer pre-shift by previous total), no 128-wide decimation, no ±12-step
  search, no sample-at convention, no K=W/128 integer quantisation (a float
  correlate has NO ±2.8 px step error — but also no shared convention with
  the page), gain k inverted (see below), and no sub-peak capture.

## 2. Why it scored worse on 1908 (root causes, measured)

1. **Sign/convention inversion**: the NCC chain's (dx,dy) is SAMPLE-AT —
   plane content aligns master when sampled at +(dx,dy). The old console fed
   that into `warpAffine(plane, +shift)`, which moves content +shift AGAIN.
   Measured on the actual 1908 planes (page-port NCC as referee): today's
   `warp(+ret)` left residuals of **+6.3 px (p17), −12.7/−25.4 px (p09)**;
   the page-style integer `shift_at(+sh)` left **0.00 px on every plane**
   (conf 0.65–0.81); `warp(−ret)` was worse (conf → 0.00). On the tripod
   (shifts ≈ 0) the error is invisible — the failure is motion-specific.
2. **Gain convention inverted**: console k = med(master)/med(plane) ≈ 1.03;
   page `cwcDecode` k = med(plane)/med(master) ≈ 0.97. On saturated cores
   `255 − 1.03·p` vs `255 − 0.97·p` differ by ≈ ±8/plane, flipping
   neighbour claims at 20 px pitch (observed: LED0/1, 2/3, 4/5, 8/9, 10/11
   position-swaps before the gain fix; amps now match the page).
3. **The 'page-sim' comparators were unreliable**: `sim_mask175.py`'s `ncc()`
   correlates ref and cur over the **same** window (cross-term should use
   ref at (y,x) vs cur at (y+dy,x+dx)) and uses float phaseCorrelate-rounded
   tots (not the shipped integer chain). The FORENSICS numbers 100/142/22
   were therefore not apples-to-apples; on identical footing (console scorer +
   integer shipped chain) the same burst decodes to **136**.

## 3. What shipped in cwc_pos_decode.py (§10d, line-for-line)

- Backwards chain exactly as page: p17 direct NCC vs master on the
  K=W/128-decimated grid; p (16..0) pre-shifted by
  `Math.round(prev_total/K)` **integer steps** (sample-at), remainder NCC vs
  master; float totals = prev_total + (pick+Δ)·K.
- Integer NCC ±12 both axes, mean-centred; verified against the **shipped
  CWCSTATS chain**: 1908 handheld max deviation **0.006 px** on all 18
  planes (my conf 0.660–0.814 vs ship 0.654–0.811, ≤0.008 — residual
  JS-float vs float64 arithmetic), tripod 0.000 (conf −0.004).
- §10d sub-peak: 3-point parabola per axis on the 25×25 surface,
  `delta = (y₋−y₊)/(2(y₊+y₋−2y₀))` (y₋/y₊ = samples at −1/+1 on the
  displacement axis — **sign validated against synthetic sub-step motion**:
  22/48 axes improved, 0 degraded, median |fit−truth| 0.203 vs 0.283
  mirrored; residual integer-pick error it corrects ≈ 0.21 steps = 0.66 px),
  sampled at the vertex — equivalent to the plan's y0+Δ·(y₋−y₊)/2 form.
- Guards: per-axis conditioning floor (`cwcNccPeakMargin`) **0.05** conf
  units; clamp |Δ| ≤ 0.5 (clamps observed on degraded bursts); integer pick
  kept at ±12 boundary peaks and at corner/out-of-surface shoulders.
- Decode path: stacksig sampled at the plane's **integer total** on the RAW
  plane (page `cwcDecode` shift-at-sample convention, no warp copies);
  k_p from the page's **histMedian** (integer-histogram median, +0.5);
  gates/suppression/dedup unchanged.

## 4. Threshold measurement (cwcNccPeakMargin)

Per-axis margin = peak − max(in-axis shoulders) on the real surfaces:

| run | axes | min | median | max |
|---|---|---|---|---|
| 1906 tripod | x (18/18) | **0.2464** | 0.2496 | 0.2523 |
| 1906 tripod | y (18/18) | **0.1735** | 0.1760 | 0.1801 |
| 1908 handheld | x (8/18 active) | 0.0011 | 0.0452 | 0.1280 |
| 1908 handheld | y (4/18 active) | 0.0025 | 0.0425 | 0.0755 |

**Default 0.05**: ~3.5–5× below the tripod's minimum good-peak margin
(refinement active on 36/36 healthy axis-fits) and above the median
degraded-burst margin (fits refused on exactly the flat-shoulder planes).
For cross-agent reconciliation: the page default should be set to **0.05**
(`cwcNccPeakMargin = 0.05`); if the in-flight build picked a different
value (e.g. the spec's "~0.05"), the tripods above say 0.04–0.06 all hold
the tripod at 36/36 — the sensitive knob is handheld behaviour, where 0.05
rejects the worst 24/36 axes.

## 5. Validation (all offline, decode_run only)

**Tripod 1906-phone-pos1** (`--save-json`, gates 90/30):
- **197/200, zero dups, missing [46,90,91] — unchanged.** Pitch median
  20.2 (min 4.1, max 76.5 vs 75.0 before).
- **LED16 present site (135,571)** amp **130.5** margin **75.2** — verdict
  unchanged (present; the plan's 137.1 was the pre-parity console gain —
  the page's own CWCDEC for LED16 is 128.9, which the new console matches
  within 1.6). **LED25 (166,424)** amp 137.2 margin 85.9 (page: 144.5@y423;
  both runs' sites have master=255 at y424 and codeword25-fingerprint
  137.2 there).
- Positions: median delta **0.0 px**, 197/197 shared, **4 claims >1 px**
  (LED120 by 69 px, LED57 by 3, LED173/197 by 2). LED120: old-console
  claim (186,303) now decodes as codeword-120's NEGATIVE (score −139.9 vs
  +139.9 for 120) at both candidate sites, and the page's own shipped
  CWCDEC already claimed 120 at (253,301) amp 142.9 — the new result is the
  page-agreeing one; the other three are 1–2 px quantisation flips toward
  the page (new==page on all three).
| handheld | count | pitch med | dups |
|---|---|---|---|
| before (float-warp console) | 39 | 167.7 | 0 |
| after §10d refined (default) | **122** | **21.4** | 0 |
| after, --no-refine (integer ship chain) | 136 | 20.0 | 0 |
- Accepted claims now form serpentine runs (longest consecutive run 10;
  run-length histogram 1:17, 2:8, 3:3, 4:3, 5:4, 6:2, 7:1, 9:1, 10:2) —
  vs. before, where ids were scattered (167.7 = noise-floor claims).
- Per-plane refined shifts: move ≤ **0.67 px** from the integer chain
  (median 0.42); vs phaseCorrelate GT (FORENSICS recipe) the refined
  chain deviates **med 1.28 / max 2.90 px** — the same error class as the
  shipped integer chain (med 1.26 / max 2.62): refinement is
  active on only 12/36 axes here (flat shoulders refused) and does not
  invent motion.
- Remaining honesty note: the §10d lever sharpens the chosen peak only;
  on this degraded burst the counts (122–136 vs phaseCorrelate-tots 180)
  still reflect a motion burst past the ~5 px residual budget — a
  disciplined re-shoot per the plan remains the real fix; the console no
  longer AMPLIFIES it (before: 39 scattered claims, now: coherent
  strings).

## 6. Regression-guard verdict

No tripod metric moved beyond tolerance except the intended page-parity
corrections (amp/margin numbers now matching the page's own CWCDEC, 4
position flips toward page-shipped sites). Count, missing set, dup count,
LED16/LED25 presence verdicts all preserved — **no revert needed**.

## 7. Hand-off notes

- Console CLI: `cwc_pos_decode.py <run> [--amp-gate 90 --margin-gate 30]
  [--peak-margin 0.05] [--no-refine]`; the JSON artifact
  `runs/s14p-1910-parity-check.json` holds per-plane tables (refined vs
  integer shifts, per-axis margins, GT deviations) for the cwc_decode_sim
  gate check against the 1910 page build.
- The 1908 run's shipped `ledpos.json`/overlay are now the REFINED (60/25)
  result; the old-console artefacts remain in git history and in
  `scratch/ledpos_190*_before.json` snapshots.
- sim_mask175.py's same-window NCC bug is documented here; if that script
  is ever re-run, its ref-window indexing needs the (y,x)/(y+dy,x+dx) fix
  (out of my file scope).