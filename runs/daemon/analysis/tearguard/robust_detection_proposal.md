# S14R-0003F — Scene/exposure-robust tear detector (proposal + benchmark)

Era: S14R-0003D/E tearguard follow-up. Date: 05 Oct 2026 (analysis session).
Corpus (196 clean + 4 torn, read-only):
- **exp700 regime** (exp 699.97): `runs/daemon/runs/run8..run11`, 100 frames, 3 torn —
  `cwc_r8_p07` y=191, `cwc_r10_p05` y=383, `cwc_r10_p21` y=575 (ground truth from
  s14r-0003e-tearguard-design.md).
- **exp300 regime** (exp 300.03): `runs/daemon/analysis/e4_0003d_extract/`, 100 frames,
  1 torn — `cwc_r12_p15` y=272 (ground truth from e4_0003d_extract/scored.json + gallery).

Benchmark tooling (this dir): `robust_bench.py` (score computation, page-exact arm()
parity proven — literal JS-loop port agreed to 6 decimals on 3 frames),
`robust_scores.json` (all per-frame arm stats, n=200), `robust_algo_parity.js` +
`dump_parity_frames.py` (JS vs python parity on 8 representative frames,
**8/8 exact**: all three dimensionless arms + SLVL agree to 2 decimals).

---

## 1. Why the absolute thresholds keep breaking

The 0003e thresholds (gray 16 / chroma 3.5) are calibrated on the row-jump
**absolute** scale. Two exposure-dependent effects scale that floor:

1. Row-mean noise (JPEG block texture, sensor read noise) scales with scene
   brightness/exposure: the clean gray-jump max moved 9.8 → 13.4 between exp 700 and
   exp 300, and the *chroma* clean-jump maxima moved 1.72 → 2.77 (rg) / 1.53 → 1.56 (bg)
   in this corpus. Any absolute threshold sits within ~1-2× of either regime's clean
   floor.
2. Torn-frame tear contrast ALSO scales with exposure: the weakest tear (r10_p05) scores
   gray 5.6 / bg 8.4 at exp 700 while the exp300 tear scores gray 20.5 / rg 29.3. Any
   single absolute threshold is squeezed between a **regime-scaled floor** and a
   **regime-scaled signal** — there is no single number that works for a new exposure
   unless an accident puts it there. This is exactly what happened twice.

**Measured cross-regime instability of the current absolute metric** (jump `|diff
smooth3(rowmean)|` max `Dmax` per arm):

| arm    | exp700 clean Cmax | exp300 clean Cmax | clean Cmax ratio (300/700) |
|--------|------------------:|------------------:|---------------------------:|
| gray   |        6.14       |        8.31       |  1.35 |
| rg     |        1.72       |        2.77       |  1.61 |
| bg     |        1.53       |        1.56       |  1.02 |

A detector whose clean floor itself moves 35–61% between two exposures cannot have one
fixed threshold. The fix must cancel the exposure scale factor from the statistic
itself.

## 2. Chosen metric: MAD-normalized chroma jumps + gray 2-D gate

**Definition** (per-frame, computed once on the RGBA bytes; same row pipeline as 0003e —
full width, full H, no subsampling, skipTop 120):

- Row-value arms (as 0003e): `gray = (R+G+B)/3`, `rg = R−G`, `bg = B−G`; 3-tap smooth
  `[1,2,1]/4`; jump series `d_i = 0.25·|(v[i−1]+2v[i]+v[i+1]) − (v[i]+2v[i+1]+v[i+2])|`
  for i ∈ [skipTop−1…h−4] (identical to the 0003e scan).
- **Robust scale**: for each arm, `MAD = 1.4826·median(|d_i − med(d_i)|)` over the
  scanned range. The dimensionless score is `rMAD = dmax / (MAD + 1.0)` (floor 1.0 in
  8-bit LSB units damps the rMAD→∞ singularity on artificially clean frames; it is the
  ONLY added constant and is exposure/scene independent by construction).
- **Gray level-shift arm (SLVL)**: at the gray jump argmax row `yc`, compare median
  gray of rows [yc−60, yc−20) vs [yc+20, yc+60), normalized by the frame's own gray-jump
  MAD: `SLVL = |med_bot − med_top| / (MAD_gray + 1.0)`.

**Decision rule (TEAR ⇔ any of):**

```
rg:   rMAD_rg > 6.0
bg:   rMAD_bg > 3.5
gray: (rMAD_gray > 8.0) AND (SLVL > 15)
```

Rationale:
- On **chroma** arms, clean frames have essentially NO jump outliers beyond the frame's
  own texture: clean `rg_rMAD` max = 2.43 (exp300), `bg_rMAD` max = 1.41 (exp300) —
  while *both* regimes' tears that carry a chroma tear score ≥ 5.5 on that arm. **The
  clean/tear chroma separation is >2.2× in the SAME statistic, at BOTH exposures — with
  NO second gate needed.** Jump-only normalization already solves the retuning problem
  for the chroma arms.
- On the **gray** arm, jump normalization alone is NOT enough (clean `gray_rMAD` reaches
  5.62 at exp300 — the LED row-oscillation residual scales with texture too — while the
  weak r10_p05 tear scores only 3.57). But a real gray tear ALSO shifts the row-mean
  LEVEL below the tear: adding `SLVL > 15` as a co-gate removes every elevated-jump
  clean frame (they sit at y≈304-316 where the AE-ladder's brightness step inflates both
  jump and shift for ~15 clean frames, exp700 SLVL up to 27 but gray_rMAD ≤ 3.96 — the
  AND keeps them out).

### Why (rMAD, SLVL) is exposure-robust

Both are ratios of exposure-scaled quantities. Measured across the two regimes:

| statistic | exp700 clean max | exp300 clean max | clean max ratio |
|-----------|-----------------:|-----------------:|----------------:|
| rg_rMAD   | 1.49 | 2.43 | 1.63 |
| bg_rMAD   | 1.38 | 1.41 | 1.01 |
| gray_rMAD | 3.96 | 5.62 | 1.42 |
| gray_SLVL (over all clean) | 27.3 | 7.1 | 0.26 |
| gray_SLVL (among rMAD>4 clean only) | (none) | 7.09 | — |

The chroma rMAD clean maxima move ≤1.63× between regimes (vs 1.61× for the raw Dmax —
the *normalization* is what fixes the separation RATIO, not the scale). The critical
property is the **clean-vs-tear separation is >2.2× at both exposures for the chroma
arms**; the gray arm's 2-D gate achieves ≥1.86× separation at both regimes too. The
threshold can sit at ~1.9× clean and ~0.5× tear **simultaneously in both regimes** —
headroom that the absolute metric could only have in a single exposure.

### Measured benchmark (this rule, exact)

**Tear frames — per-arm scores & rule verdict:**

| label | regime | rg_rMAD | bg_rMAD | gray_rMAD | gray_SLVL | rule | fires |
|-------|--------|--------:|--------:|----------:|----------:|-----:|-------|
| cwc_r8_p07    | exp700 | 12.29 | 8.06 | 22.53 | 38.4 | **2.56** | gray (y=191) |
| cwc_r10_p05   | exp700 | 1.16  | 7.55 | 3.57  | 21.7 | **2.16** | bg (y=383) |
| cwc_r10_p21   | exp700 | 20.45 | 1.06 | 4.09  | 8.5  | **3.41** | rg (y=575) |
| cwc_r12_p15   | exp300 | 24.29 | 5.49 | 17.09 | 31.6 | **4.05** | rg (y=271) |

**Clean score distribution:**

| regime | n | med | p95 | max | max-frame |
|--------|--:|----:|----:|----:|-----------|
| exp700 | 97 | 0.432 | 0.475 | **0.495** | cwc_r11_p13 |
| exp300 | 99 | 0.346 | 0.440 | **0.473** | cwc_r10_p16 |
| both   | 196 | 0.397 | — | **0.495** | — |

**FP = 0 / 196. FN = 0 / 4.** Margin ratios per regime (min tear score / max clean
score): exp700 **4.36×** (2.157/0.495), exp300 **8.56×** (4.05/0.473).
Combined margin: tearMin/cleanMax = 2.157/0.495 = **4.36×**. **The same rule, same
numbers, both regimes.**

### Sensitivity analysis (rule headroom)

Uniform global scaling of ALL FOUR constants by k:

| k    | cleanMax | tearMin | FP | FN |
|------|---------:|--------:|---:|---:|
| 0.75 | 0.660 | 2.876 | 0 | 0 |
| 0.85 | 0.583 | 2.538 | 0 | 0 |
| 1.0  | 0.495 | 2.157 | 0 | 0 |
| 1.15 | 0.431 | 1.876 | 0 | 0 |
| 1.30 | 0.381 | 1.659 | 0 | 0 |

Margin is exactly invariant to uniform-scaling (it's a ratio) — the *binding
constraints* that set how far k can move in each direction are:
- lower bound on k: as gates drop, the clean frames with the highest per-arm rMAD/SLVL
  approach 1.0 (cleanMax hits 0.66 at k=0.75, hits 1.0 around k≈0.52 by extrapolation of
  the max-chroma (rg 2.43/6 = 0.405 bound); the first clean frame to FIRE does so near
  k≈0.5 — NOT near the operating point).
- upper bound on k: the weakest tear (r10_p05, bg 7.55/3.5=2.16) needs
  `3.5·k < 7.55` → k < 2.16.

**Operating point recommendation: keep the constants exactly as above; k=1.0.** If a
future corpus shift demands a retune, scale all four constants by the SAME k (not
individually) — that preserves the 4.35× margin by construction.

### Per-arm detail at exp700 (for the 0003e comparison table)

| arm (dimensionless) | exp700 clean med/max | exp300 clean med/max | exp700 tear min | exp300 tear min |
|----------|---------------------|----------------------|-----------------|-----------------|
| rg_rMAD  | 0.99 / 1.49 | 1.31 / 2.43 | 1.16* | 24.29 |
| bg_rMAD  | 0.87 / 1.38 | 0.93 / 1.41 | 7.55 | 5.49 |
| gray_rMAD| 3.45 / 3.96 | 5.07 / 5.62 | 3.57* | 17.09 |
| gray_SLVL| 23.4 / 27.3 | 5.1 / 7.1 | 8.5* | 31.6 |
| (r10_p05 gray 2-D gate) | — | — | fails (0.45) | — |

(*r10_p05's gray arm values illustrate why the gray arm needs the 2-D gate and why it
is NOT the tear's firing arm — it fires on bg_rMAD at y=383.)

## 3. Page-exact JS pseudocode

Same performance envelope as 0003e (row-loop ≈3.5 ms desktop node, ≤10-30 ms phone
budget; all new work is O(h) row ops, no per-pixel cost). Mirrors the 0003e conventions
(Uint32 little-endian RGBA word: `R = v&255, G=(v>>>8)&255, B=(v>>>16)&255` — **NOT**
raw-sum, masks required, the 0003e "byte-position-weighted trap" applies verbatim).

```js
function robustTearCheck(d, w, h) {          // d = RGBA Uint8ClampedArray (getImageData byte order)
  const SKIP = 120, FLOOR = 1.0;             // skipTop rows; MAD floor (8-bit LSB units)
  const T = { rg: 6.0, bg: 3.5, grayJ: 8.0, grayS: 15.0 };   // ALL FOUR move with one global k
  const u32 = new Uint32Array(d.buffer, d.byteOffset, w * h);
  const g = new Float32Array(h), rg = new Float32Array(h), bg = new Float32Array(h);
  let o = 0;
  for (let y = 0; y < h; y++) {              // row sums (identical to 0003e block)
    let sg = 0, srg = 0, sbg = 0;
    for (let x = 0; x < w; x++) {
      const v = u32[o + x];
      const R = v & 255, G = (v >>> 8) & 255, B = (v >>> 16) & 255;   // byte masks REQUIRED
      sg += R + G + B; srg += R - G; sbg += B - G;
    }
    o += w;
    g[y] = sg / (3 * w); rg[y] = srg / w; bg[y] = sbg / w;
  }
  // 0003e page-exact jump series d_i over i in [SKIP-1, h-4]
  function jumps(v) {
    const j = new Float32Array(h - 3);
    for (let i = 1; i < h - 2; i++)
      j[i - 1] = 0.25 * Math.abs((v[i - 1] + 2 * v[i] + v[i + 1]) - (v[i] + 2 * v[i + 1] + v[i + 2]));
    return j;
  }
  const jG = jumps(g), jR = jumps(rg), jB = jumps(bg);
  const lo = SKIP - 1;                        // index 0 of j <=> row-pair (1,2); lo keeps rows >= SKIP
  function scan(j) {                          // {max, argmaxIdx, MAD} over the scanned range
    let m = 0, mi = -1; const a = [];
    for (let k = lo; k < j.length; k++) { const d = j[k]; a.push(d); if (d > m) { m = d; mi = k; } }
    const med = medianF32(a);
    const dev = Float32Array.from(a, x => Math.abs(x - med));
    return { max: m, i: mi, mad: 1.4826 * medianF32(dev) };
  }
  const sG = scan(jG), sR = scan(jR), sB = scan(jB);
  // gray arm level shift at the gray-jump candidate row yc
  const yc = sG.i + 1;
  const seg = (a, b) => { const t = []; for (let y = a; y < b; y++) t.push(g[y]); return t; };
  const shift = Math.abs(medianF32(seg(Math.min(h, yc + 20), Math.min(h, yc + 60)))
                       - medianF32(seg(Math.max(0, yc - 60), Math.max(0, yc - 20))));
  const sLVL = shift / (sG.mad + FLOOR);
  const rgR = sR.max / (sR.mad + FLOOR);      // dimensionless chroma-arm scores
  const bgR = sB.max / (sB.mad + FLOOR);
  const gR  = sG.max / (sG.mad + FLOOR);
  const tear = rgR > T.rg || bgR > T.bg || (gR > T.grayJ && sLVL > T.grayS);
  return { tear, rgR, bgR, gR, sLVL, yc,
           arms: `rg ${rgR.toFixed(2)} bg ${bgR.toFixed(2)} gray ${gR.toFixed(2)} sLVL ${sLVL.toFixed(1)}` };
}
// medianF32: sort a copy; middle element (n&1) or mean of two middles.
```

JS↔python parity: **8/8 frames exact** (4 clean incl. worst-case clean from each
regime, 4 torn) — `robust_algo_parity.js` vs `robust_bench.py`, all arms agree to
2 decimals (Float32 vs Float64 medians differ ≤0.02 on every arm).

## 4. Rejected candidates

| candidate | one-line reason |
|-----------|-----------------|
| **(a) gray arm self-normalization only** (rMAD_gray > const) | clean `gray_rMAD` scales with scene/exposure too (3.96 → 5.62 across regimes) and the weak r10_p05 tear only reaches 3.57 — no single constant separates; needs the SLVL co-gate (which is what we ship). |
| **(a-local) local ±45-row MAD instead of global MAD** | rLOC clean maxima move 2.99→5.51 across regimes (vs 3.57→13.9 for rMAD): WORSE cross-regime stability than the global scale, because the local window is too short to absorb the LED panel step at y≈304-316. |
| **(b) p99.5 percentile reference** (`rP = dmax / (p99.5+floor)`) | clean rP 1.07-1.44, tear rP 1.59-2.28 combined margin only 1.39× vs 4.36× for the chosen rule; tears at 720-row resolution are too close to the clean p99.5 (the jump spectrum is heavy-tailed). |
| **(c) SLVL (level shift) as a STANDALONE arm** | clean max at exp700 = 27.3 (the y≈316 AE-ladder panel step) vs tear min 8.5 (r10_p21) — margin 0.31, unusable alone; only viable as the gray arm's 2-D co-gate (which is what we ship). |
| **(d) chroma-coherence gate** (gray+rg+bg jumping at the same row) | measured row agreement: r10_p05 fires bg at y=383 but gray argmax at y=330 (|dy|=53); r10_p21 fires rg at y=575, gray at 575, bg at 191. The chroma arms are NOT row-coherent in 2 of 3 exp700 tears — coherence would DISCARD true tears; it is a corroborating signal, not a gate. |
| **(e) cross-reference (second-grab comparison)** | out of scope for the in-page metric (would double the burst's wire/capture cost); note only: the regrab pipeline that 0003e specified (≤2 retries, `.rN` labels) is unchanged by this proposal — same retry policy slots in above the dimensionless rule. |
| **absolute Dmax with per-regime constants** | the status quo that failed twice (clean Cmax ratio gray 1.35× / rg 1.61× between regimes); it is a special case of "tunable per-exposure" and loses by construction. |
| **z-score = (dmax − med) / MAD** instead of ratio | r10_p05's bg jump is +7.55σ but a tear must fire even when the frame's own jump median is inflated by the panel step; the simple ratio has wider margin (5.34× chroma-only) — z-score was measured, loses. |

## 5. Recommended threshold policy

Single set of four constants, NO per-arm or per-exposure tuning:

```
TEAR ⇔ rg_rMAD > 6.0  or  bg_rMAD > 3.5  or  (gray_rMAD > 8.0 and gray_SLVL > 15)
```

Headroom math (at k=1.0):

- **chroma arms**: the clean maxima are rg 2.43 / bg 1.41 → thresholds 6.0/3.5 sit at
  **2.47× / 2.48×** the worst clean. The weakest chroma tears are rg 12.3 / bg 5.49 →
  the threshold sits at **0.49× / 0.64×** of the weakest tear. Both sides hold
  simultaneously at BOTH exposures (clean rg max 1.49@700/2.43@300, tear rg min
  24.3@300/12.3@700; clean bg max 1.38@700/1.41@300, tear bg min 8.06@700/5.49@300).
- **gray 2-D arm**: worst clean (rMAD 5.62, SLVL 7.09 — different frames) — the AND
  gate never fires because clean frames with gray_rMAD>4 have SLVL ≤ 7.09 (n=1, r10_p16)
  and clean frames with high SLVL (27.3, the y≈316 step) have rMAD ≤ 3.96. The weakest
  gray-2-D tear (r8_p07: rMAD 22.5, SLVL 38.4) clears both gates at **2.8×/2.6×**.
- **overall margin** = tearMin/cleanMax = **2.157/0.495 = 4.36×** (both regimes:
  2.157/0.495 exp700, 4.05/0.473 exp300 — the weaker regime pair governs).

**Do NOT retune exposure-by-exposure.** The only legitimate retune trigger is a NEW
corpus where a clean frame's rMAD exceeds ~2 (then scale all four constants by the same
k), or a new tear class (e.g. an all-three-arms-coherent gain jump — none observed in
4/4 tears to date).

Byte-order trap (same as 0003e): the Uint32 word is B·2¹⁶+G·2⁸+R+A·2²⁴; masks per
channel REQUIRED (`v&255`, `(v>>>8)&255`, `(v>>>16)&255`) — summing raw u32 silently
changes the metric.

## 6. Residual caveats

1. **Sample size**: 4 torn frames total (3 exp700 + 1 exp300). The margins (>4.3× on the
   binding frame, per-arm ≥2.2× clean-side / ≥2.6× tear-side at both regimes) make
   single-sample sensitivity unlikely to matter, but the tear-side minima are exactly
   that — 4 samples. Future batteries should keep logging `TEARGUARD {label, arm, y,
   tries}` and the per-arm rMAD/SLVL values (cheap, 4 floats in the meta) so the
   clean/tear separation can be re-verified for free on every new corpus.
2. **The gray 2-D gate is an AND of two ratios** — there is no guarantee a NEW tear
   type with a big jump but SLVL ≤ 15 exists (none seen in 4/4); the chroma arms are the
   primary safety net (they carry r10_p05 and r10_p21 alone).
3. **SkipTop=120 unchanged** — the y≈107 master/top-region structure stays excluded, as
   in 0003e. If a tear ever lands above row 120 this detector (like 0003e) is blind to
   it; no evidence that happened (master frames clean in both regimes).
4. **exp300 clean rg_rMAD max 2.43** (cwc_r11_p00) is the binding clean observation; at
   2.47× below the rg threshold it's the tightest margin in the design. If a future
   corpus shows a clean rg_rMAD > 3, that is the first signal to investigate — not a
   reason to go back to absolute thresholds.

---

Appendix: score JSON `robust_scores.json` (n=200), scripts `robust_bench.py`
(+ literal page-arm() parity gate), JS parity `robust_algo_parity.js` +
`dump_parity_frames.py` (8/8 exact). No run-dir imagery touched; no daemon/serial
interaction; no git actions.
---

## 0004 page implementation

Implementation: page/survey.html BUILD S14R-0004. The rule ships as
`robustTearScan(imgData)` — a line-for-line port of `robust_algo_parity.js`
(byte masks per channel, single implicit smoothing in the 4-tap jump formula,
fractional thresholds {rg 6.0, bg 3.5, grayJ 8.0, grayS 15.0}, skipTop 120,
floor 1.0). `robustTearPair(imgA, imgB)` runs the same scan on the per-pixel
|a−b| diff of two grabs (channel-wise; alpha pinned to 255).

Validation (this session, offline, node 22):
- Harness (committed here): `page_impl_dump_frames.py` (200 corpus frames ->
  untagged RGBA .raw, regime-tagged filenames), `page_impl_port.js` (the
  page's own tear-guard functions extracted verbatim from the packed
  BUILD page, incl. the byte-mask / single-smoothing traps) and
  `page_impl_validate.js` (node: parts 1+2 below). Reproduce with:
  `python3 page_impl_dump_frames.py && node page_impl_validate.js`.
- Head-to-head: the page port vs `robust_algo_parity.js` on the same 8
  parity frames — **8/8 identical to 2 decimals** (all arms + SLVL).
- **Single-frame rule: FP 0 / 196, FN 0 / 4.** Same firing rows/arms as the
  benchmark tables above (r8_p07 rg/gray y=191, r10_p05 bg y=383,
  r10_p21 rg y=575, r12_p15 rg y=271; r8_p07's sLVL 40.8 vs the paper's 38.4 —
  this harness uses the JS-parity seg() ordering, matching
  robust_algo_parity.js exactly).
- Margins per regime (tearMin/cleanMax of the combined score/threshold ratio):
  exp700 **5.47x** (0 FP / 0 FN on 97 clean + 3 torn), exp300 **10.0x**
  (0 FP / 0 FN on 99 clean + 1 torn). Combined 2.157/0.405 = **5.33x**
  (the paper's 4.36x used 0.495 as cleanMax — that number included the
  gray-only rMAD of a frame whose SLVL gate does not fire; under the page's
  actual decision rule the worst clean frame scores 0.405).
- Scan cost (desktop node, 406x720): mean **4.1 ms**, max 17.4 ms per scan —
  inside the <=30 ms page budget. Three scans per plane budgeted
  (p00: 2 self-scans + 1 pair; others: 1 self-scan + 1 pair).

p00 double-grab certification + chain telemetry (page code):
- p00 is grabbed twice on one held paint; the pair-diff scan plus both
  single-grab self-scans gate a third grab; two agreeing grabs certify, the
  odd one out is discarded; three-way disagreement ships `p00` flagged
  (torn:1, tm {arm:'pair', ...}, tries:2).
- Offline characterisation on real p00 frames (NOT a live-rig validation —
  stand-ins: two same-paint grabs from different runs of the same exposure):
  identical-image pair scores 0/0/0 (sLVL 0/0 from MAD 0); cross-run same-paint
  p00 pairs score rg<=0.60 bg<=0.55 gray<=0.64 sLVL<=0.9 — far under the
  thresholds (chroma gates at 6/3.5, gray at 8&15). Same-paint pair diffs are
  >9x under the binding gate, so a genuine tear step in either grab has the
  full rule range to itself. Cross-exposure p00 pairs stay clean too
  (rg 1.66 bg 0.84 gray 3.25, sLVL 1.3 — gray rMAD 3.25 < 8 so no fire).
  Control: two different planes (p00 vs p01) also stay clean in the diff
  (largest diff arm is gray 1.51 — bit-pattern flips do not dominate the diff
  row means). **Live calibration of the pair rule on the rig is still owed at
  the next battery** (real back-to-back grab noise, not cross-run stand-ins).
- p01..p23 chain pair-diffs (vs the previous shipped frame) are TELEMETRY
  ONLY: logged + shipped in each frame's meta (`src.chain`), never a regrab
  trigger; different-plane diffs score high by construction (they compare
  different codeword patterns) — batteries use them for chain consistency,
  not pass/fail.

Page wiring (BUILD S14R-0004):
- regrab: self-scan only, <=2 extra grabs of the SAME held paint (<=3 total
  incl. first), label suffix `.r1`/`.r2` in the FRAME meta label; persistent
  tear ships torn:1 + tm {arm, y, rMAD scores, tries}.
- telemetry: per-plane first-attempt arm scores in the frame meta (`src.self`),
  chain pair-diff scores (`src.chain`), burst totals + p00 status in a `tear`
  object on CWCSTATS (also a minimal CWCSTATS in capture-only mode); battery
  re-verification of the separation is therefore free.
