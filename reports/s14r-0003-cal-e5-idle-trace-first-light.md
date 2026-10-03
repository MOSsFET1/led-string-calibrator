# S14R-0003 CAL battery — E5 idle trace, first light

03 Oct 2026 (AEST), battery attempt 1, 22:19–22:34 Sydney, phone-on-tripod,
rig powered but not painted (pure idle room + rig-glow). Run dir
`runs/daemon/runs/run0/`. Note on inputs: the two parallel jpg name sets
(`cal_*` / `cal_idle_*`) in-dir are md5-identical salvages of the same wire
labels; per PROVENANCE the wire re-derivation is byte-exact for all 29 labels,
so this analysis uses the correct salvaged frames for the two store-truncated
labels (03, 20) as well as the 27 clean ones — the quarantine `.trunc` are
store-time remnants only. Caveat: file mtimes for labels 03/04/20/21 and
24–28 are re-derivation artefacts; all spacing below uses meta page-uptime
`t` (wire_ts absent from metas). Exp readback byte-identical in all 29 metas:
`exp=799.98 aem=continuous ev=-1 fd=0.00` (aem continuous, no AE-lock).

## Recipe (page-parity)

Replicated from `tools/cwc_pos_decode.py` / page `survey.html`: PIL RGB
decode → luma = max(r,g,b) per pixel → histogram of INTEGER luma → cumulative
walk past half the pixel count → v + 0.5. Regions on frame 00, with the
09:44 all-ON master (same aim) as the lamp map: **wall** = y 120–180,
x 0–60 (above and left of the registered lamp band; lamp anchors span
x 46–405, y 177–586, bulk 88–319); **dark floor** = column strip
x 340–406, y 100–500 (right of the lamp band).

## Headline numbers (27 clean + 2 salvaged = 29 frames, t 17.4→501.8 s)

| stat | dense 00–23 (t 17–41 s, ~1.0 s apart) | wake 24–28 (t 127–502 s) |
|---|---|---|
| whole-frame histMed | **67.5** (range 66.5–68.5; 21/24 frames at 67.5) | 75.5–79.5 |
| whole-frame P90 | 122–124 (mode 123) | 134–138 |
| whole-frame P95 | 132–134 | 136–150 |
| wall-region median | 61–64 (mode 62) | 65–75 |
| dark-region p05 | 6.0 | 6–7 |
| dark-region p01 | 2 (occ 1, 3) | 2–3 |
| registered-anchor 5×5 mean (583 ids) | spread 1.03, per-site std 1.13 | 68.6–76.5 |
| exp readback | frozen (identical string) | frozen (identical string) |

## AE-invisible gain (pixel stats vs frozen readback)

Pixels move while the readback string never does. Within the dense 24 s
epoch: histMed ±1 LSB (≈±1.5 %), P90 ±2 (≈±1.6 %), wall median 61→64
(9 distinct integer values, ≈±4.8 % — the most continuous probe). Across the
epoch boundary (23→24, after the ~86 s page-time gap): histMed 67.5→79.5
(**+18 %**), P90 +15, wall +13 — with `fd=0.00` and exp still read back
unchanged. Conclusion: on this phone AE adaptation does NOT surface in the
exposure readback for these idle scenes; pixel-domain photometry is the only
AE proxy, and wall-region median is more sensitive than (semi-quantised)
whole-frame histMed. Caveat: part of the wake-up step may be real scene/
aim change between epochs; the dense-epoch ±3 % wobble is the clean AE-gain
noise floor.

## Frame-to-frame JPEG variance (dense 00–23, 23 consecutive pairs)

mean |Δluma| per pair 1.89–2.19 (mean of means **2.03**); p99 14–15;
single-pixel max 123–223 but isolated sparkle (p99 is the usable bound).
Luma-stratified pair-std: 2.5 in blacks (<20), 3.5–4.8 in midtones;
at registered anchors (5×5) std 1.13. Sparse-gap pairs run mean |Δ| 4.3–11.0
(real scene/room + gain drift). Dense median template reproduces any dense
frame at mean |err| **1.4–1.6** (p99 ~11); same template vs wake frames
degrades to 9.4–11.4 (epoch mismatch — templates should be epoch-matched).

## Dark-frame usability

- The right-hand strip is **not** an off-state floor: its 40×8 block means
  span 7→95 (strongly structured spill from string-3 far columns + wall
  texture). Lamp sites are NOT dark in idle: median anchor 68.7 vs 51.7 on a
  control column (+17 spill); 48 % of anchors above wall+10, **24 % ≥100**;
  top-quintile anchors mean 113.
- Verdict: region-based floor subtraction is out; a **per-pixel median
  template** from the idle stack works within an epoch (residual ~1.5, p99
  ~11) and is usable for amp baseline subtraction, but a true rig-unpowered
  dark capture tonight is still the right reference — the spill baseline
  (median +17, quartile ≥100) is lamp-position-dependent, not a constant.

## Predictions for tonight's E1–E4 (exp pinned ≈800, resting histMed 67.5)

1. **Adaptive-mask rescue finally unsaturated**: master histMed < 89 →
   effective thr = min(100, max(45, 1.12×67.5)) ≈ 75.6 on E4 master frames;
   every prior corpus saturated at 100 — count effect expected small but this
   is the first measured exercise of the regime.
2. **Clip headroom**: idle spill anchors peak ≈138; the L=120 morning probe
   put core P90 at 237 (exp-500 room), so E1's L=179 (≈1.5×) approaches that
   level and spill-island anchors (already ≥100 in idle) are first to clip;
   whole-frame P90 123–138 stays far below the 225–245 frame-p95 band —
   expect no whole-frame clip, island-local clipping only at high L.
3. **Low-L amp budget**: ~half the anchors are spill-lit (baseline +17 to
   +113), so E4 low-L rows (5–40) and E2 must judge amp against LOCAL
   background, not frame floor; recon noise (f2f p99 ~14) plus the ±3 %
   invisible-gain wobble set the measurable-step floor — steps smaller than
   a few luma will drown; readback exp must NOT be trusted as an AE proxy
   (frozen while pixels drift ±3–18 %).

## Could not verify

- Wall-clock spacing between salvaged frames (mtimes poisoned by
  re-derivation; page-uptime t is authoritative and smooth 17.4→501.8 — no
  evidence of a page reload, but a reload mid-trace cannot be fully excluded).
- Physical room changes across the epoch boundary (readback-blind; treated
  as AE gain + possible scene drift combined).
- Rig shape "3×200" is from CALSTATS telemetry (matches operator's morning
  confirmation); not re-verified optically.
- Tonight's restart CALCFG (22:30, LOGA re-armed, `idleCount:0` → E5
  dropped from the restart order) read from `capture.txt` lines — daemon
  untouched, no serial access, this report is the only write.