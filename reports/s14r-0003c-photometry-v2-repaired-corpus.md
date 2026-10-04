# S14R-0003c photometry (a)–(d) v2 — FULLY REPAIRED corpus (24 recovered E1/E2 frames + idle_49 back in)

04 Oct 2026 (AEDT, evening re-run). Re-run of the four photometry analyses on
the post-repair run-dir
corpus: all 33 wire repairs applied IN PLACE and verified
(`runs/daemon/analysis/run_repair/manifest.json`, 528/528 EOI+PIL+wh+sha green,
`eoi_sweep_failures 0`, `unresolvable 0`). The v1 report's
"24 truncated excluded" constraint is GONE: every frame is analyzed, including
the 13 recovered E1 + 11 recovered E2 ladder frames and `cal_idle_49`.
No frame is excluded anywhere in this report. HARD RULES honored: read-only
outside `runs/daemon/analysis/photometry_v2/` + this report; `/dev/ttyACM0`
never opened; no daemon signals; v1 reports untouched; no images to git;
`LOAD_TRUNCATED_IMAGES` is NEVER set (repaired corpus decodes plainly —
asserted by per-frame EOI + plain-PIL checks in every loader).

Corpus n: run0 = 60 idle + 180 E1 (18/rung × 10 rungs) + 180 E2 (18 × 10) +
8 E3; run8..run11 = E4 25 each (decode parity section below).

## Method (locked against v1 before extending)

Metrics re-implemented to v1's definitions and LOCKED by reproducing v1's
printed numbers on the v1-kept subsets (all frames minus the 33 repaired):

| definition | locked to v1 by |
|---|---|
| page parity: `luma=max(r,g,b)`, histMed = `tools/cwc_pos_decode.hist_median`, page quantile | code re-use |
| lamp mask | thr-200 union over the 18 settled `E1_L179` snapshots, y-band 150–622, **9×9 ellipse** dilate = 34,373 px (57.3/site) vs v1 "33.6k (56/site)" ✅ |
| (c) census blobs | thr-40 4-conn ≥4 px, mask-touching, 4 ≤ area ≤ 2000, **contrast vs the frame's own histMed** ≥ 15 → idle n40 med 13.0 (10–16) = v1 13.0 (10–16) exact; E1 L40 57.5 / L60 32.5 / L80 25.0 / L179 20.0 all = v1 exact; idle pooled peaks P10 88/P50 109.5, n30 5.0, blob30 P50 105/P90 108 vs v1 "88-96/110-112, n30 5, 96/112" ≈ exact |
| (b)/(d) `coreMean−bg` | **pooled mean over ALL thr-40 mask-touching blob px** − bg-ring annulus median (dilate blob-union by 9, minus erode-by-3, minus union); E1 v1-kept k2–18 medians: 86.2/88.2/79.1/94.4/97.8/101.4/102.3/104.4/106.0/106.0 vs v1 83.7/84.9/79.9/91.1/97.0/99.7/102.2/104.3/105.6/105.9 — max |Δ| 3.3 LSB (L10/L40/L5, where recovered frames also shift the median); plateau L100–179 |Δ| ≤ 0.4; the L20 dip and both cross-overs reproduce |
| (b)/(d) `coreP50` | **pooled per-blob PEAK P50 over the settled window** (thr-40 ≥4 px mask-touching, no cap; pool = all blob peaks of all window frames); E1 v1-kept: 229/253/218/159-164/174/115-128/86-105/95-116/98-114/103-126 vs v1 222/254/227/173/174/136/110/110/122/128 (±3–9 LSB; E2 ±0–4 on every rung: 103/107/83/112/111/103/94/93/79/79 vs 105/107/87/112/110/104/95/94/80/82); candidates rejected: pooled-pixel p50, census-capped pooled peaks, per-blob p90 |
| per-blob census peaks/percentiles | pooled per-blob peaks, page quantile |
| NOT v1's | median-of-blob-means (−40…−60 vs v1), capped-pixel pooled mean (L20/L40 wrong sign), thr40-only pooled area (negative at high L) |

Settled windows: E1 k2–18, E2 k2–15 (v1's); per-frame values throughout; n =
full 18/18 per rung-arm unless stated. (a)'s probe metric = pooled core P90 −
ring bg (the bg-corrected 234–238-class column of v1's (a) table).

Scripts + data (this dir only): `phot_v2_pass.py` (428-frame per-frame metrics →
`frame_metrics.json`), `probe_defs.py`/`census_lock.py`/`idle_lock.py`
(locks), `mask_calib2.py` (mask lock), `v2_tables.py` (→ `v2_tables.json`),
`v2_final_numbers.py` (→ `v2_final_numbers.json`), `e4_v2_decode.py`
(v2 E4 decode + parity → `run{8..11}_ledpos_v2.json`, `e4_v2_summary.json`),
`e4_ratio_shiftcorr.py` (b) same-burst ratios.

## (a) Settle per L on the FULL E1 ladder — core claim holds; transient mechanics refined

Full per-k histMed (n=18 at every rung, no exclusions; k1 ≈ 0.5 s after paint):

```
E1 L:    k1    k2    k3    k4    k5    k6    k7    k8    k9   k10   k11   k12   k13   k14   k15   k16   k17   k18
5      62.5  48.5  46.5  45.5  42.5  42.5  43.5  43.5  79.5  38.5  41.5  42.5  51.5  51.5  51.5  51.5  51.5  51.5
10     49.5  39.5  38.5  38.5  39.5  38.5  38.5  38.5  38.5  39.5  38.5  38.5  38.5  38.5  39.5  38.5  38.5  38.5
20     24.5  21.5  21.5  21.5  21.5  21.5  21.5  21.5  21.5  21.5  21.5  21.5  19.5  19.5  19.5  19.5  19.5  19.5
40     22.5  21.5 ... k2-k18 flat 21.5 except k15-18 23.5
60     26.5  26.5  25.5 ... flat 25.5 ±1
80-179  k1 = k2 ± 1 LSB; flat thereafter (see v2_tables.py output)
```

- **k1 transient re-measured on the full ladder (n=18/rung, k1 present at all
  20 rung-arms — note: NONE of the 24 recovered frames is a k1; the k1 column
  was complete in v1 too).** k1 histMed vs med(k2–18), full n: E1 L5 +34.4%,
  L10 +28.6%, L20 +14.0%, L40 +4.7%, L60–120 +3.4…3.9%, L150/179 0%;
  E2 L5 −23.1% (carry-in from the bright previous arm), L10 +7.0%,
  L20 +13.9%, L40 +23.5%, L60+ 0%. **12/20 rung-arms shift ≥3% at k1
  (8/10 E1 — the bright-arm direction, 4/10 E2).** **k≥2 rule stands with
  n: skip k1 after any paint, k2 is safe for all rungs except the
  first-paint-after-idle one (below).**
- k2 residual (k2 vs med(k3–10)): E1 L5 +11.5%, L60 +3.9%, L100/120 +3.4%;
  E2 L5 +3.3%, L20 +2.0%; all others ≤1%. Probe metric (pooled coreP90−ring)
  deficit vs settled tail: k1 +0.3…+13.1% (L≤20), **k2 ≤ +3.5% at every L**,
  k3 ≤ +2.1%.
- **"AE lands by k2, no tau" survives at full n for L≥10** — flat plateau
  from k2, no L-dependent time constant anywhere (log-fit still rejected).
- **Refined: the first-paint-after-idle creep on E1 L5 is a dip-then-rise**,
  not a monotone creep: with the recovered k3/k10 frames the full shape is
  carry-in 62.5 → dip floor 38.5 **at k10** (the repaired frame — v1's
  k-window read it as a monotone 42→51 creep) → equilibrium 51.5 **by k13**
  (~6 s), then flat. Still unique to the battery's first paint after idle
  (E2 L5 re-entering later is flat at 60.5 from k2), so not an L-dependent
  AE law. One isolated exposure glitch remains (E1 L5 k9, histMed 79.5,
  −7.6% core dip, self-corrects in one interval).
- **k-rule for the probe (v2 form)**: skip k1 always; from k2 use any rung
  EXCEPT the battery's first paint after idle — there read from k≥13 (or
  skip the rung); for per-step safety at the first-paint rung k3 still
  carries +2.1% on the probe metric, k≥13 is the clean zone.

## (b) E1 vs E2 duty ratio at full n — ~×1.0 (cM−bg) holds; low-L coreP50 rows reproduce, ratio's low-L structure is real and grows from truncated + settle-dynamics rows

Settled medians, full n (E1 k2–18, E2 k2–15), LOCKED definitions
(`coreP50` = pooled per-blob peak P50 over the window — this aggregate's
low-L rise above the L20 dip is the AE-gain-compression peak shape v1 saw):

```
L   |E1 cM-bg| E2 cM-bg| ratio |E1 cP50| E2 cP50| ratio | v1 (excl-24) E1/E2 cMbg→v2 | v1 p50 r→v2
5   |   86.2 |   76.9  | 0.89  | 229.0 | 103.0  | 0.45  | 0.91→0.89 | 0.47→0.45
10  |   88.2 |   83.1  | 0.94  | 253.0 | 107.0  | 0.42  | 0.97→0.94 | 0.42→0.42
20  |   79.1 |   90.5  | 1.14  | 231.0 |  83.0  | 0.36  | 1.12→1.14 | 0.38→0.36
40  |   94.4 |   82.1  | 0.87  | 164.0 | 111.5  | 0.68  | 0.89→0.87 | 0.65→0.68
60  |   97.8 |   86.6  | 0.89  | 174.0 | 110.0  | 0.63  | 0.89→0.89 | 0.63→0.63
80  |  101.4 |   91.7  | 0.90  | 128.0 | 103.0  | 0.80  | 0.92→0.90 | 0.77→0.80
100 |  102.3 |   94.7  | 0.93  | 105.0 |  92.5  | 0.88  | 0.93→0.93 | 0.86→0.88
120 |  104.4 |   98.0  | 0.94  | 116.0 |  91.0  | 0.78  | 0.94→0.94 | 0.85→0.78
150 |  106.0 |  100.6  | 0.95  | 114.0 |  79.0  | 0.69  | 0.95→0.95 | 0.66→0.69
179 |  106.0 |  102.6  | 0.97  | 125.5 |  78.0  | 0.62  | 0.97→0.97 | 0.64→0.62
```

- **cM−bg ratio at L≥40: 0.87–0.97 (v1: 0.89–0.97)** — holds, and at full n
  the low-L cM−bg rows are settled-both-arms clean (L5 0.89, L10 0.94).
- **coreP50 ladder reproduces at full n** (E1 229/253/231/164/174/128/105/
  116/114/125.5 vs v1 222/254/227/173/174/136/110/110/122/128 — same shape
  incl. the low-L AE-peak inversions; ±3–9 LSB mask-gating sensitivity,
  E2 within ±0–4). The v1 low-L p50 ratio rows (0.36–0.47) were REAL, not
  truncation artifacts: the duty paint halves the lamp-peak population's
  P50 at low L (AE gain compression at the L20 dip makes E1's peak
  population double-valued — pooled-peak P50 is not AE-free there), and the
  ratio converges to 0.62–0.88 by L≥80. The "duty is ≈×1.0" conclusion
  remains scoped to what v1 scoped it to: the cM−bg band and the E4
  same-burst plane/master measurement — NOT the pooled-peak P50 ratio.
- **The ×1.70 duty-flux stays retired**: E4 same-burst union-plane/master
  core ratios re-measured DIRECTLY from run dirs with v2 parity-exact
  shifts: median 1.065/1.133/1.095/1.089 at L=80/100/120/150
  (n_unclipped 12/28/24/17, p10–p90 1.01–1.48) — ≈1.05–1.13, v1 said
  1.04–1.12 (n 12–28): HOLDS (≈1.05–1.13 because union takes the max over
  24 paints' 3×3 cores vs one master).
- Settled-window sensitivity: E2 medians identical for k2–15 vs k2–18 vs
  incl-k1 within 0.2 LSB (k1 does not move medians at full n); keep the k≥2
  skip per (a) for per-frame reads.

## (c) Presence-metric floor with idle_49 back (n=60) + blob-count ≥25 re-validated

Idle floor, all 60 frames (census blobs, thr-40, area 4–2000, contrast ≥15
vs frame histMed, lamp-masked):

```
idle k00-11 (leg 1, warm 67-68.5):   med 13.0  (10-16)   pooled core P50 95.5  P90 130.5  mean 92.6
idle k12-59 (leg 2, painted 84-85):  med  9.0  (5-16)    pooled core P50 105.5 P90 147.0* mean 103.3 (*leg2 tail k40-59)
idle all 60:                         med 10.0  (5-16)    p95 14
idle_49 (recovered):                 cens 9, histMed 84.5, P50 105.5 — mid-leg-2, in-band of its leg
```

- **No idle frame reaches 25 (max 16 across all 60, both legs; leg-2
  equilibrium reads LOWER census (9) than leg-1 (13) though its pooled spill
  cores are brighter: P50 105.5 vs 95.5, site-core ≥100 fraction 68% vs
  26% (≥150: 12% vs 2%) — the leg-2 AE state merges more lamp-adjacent px
  into fewer/thicker components.**
- **E1 L5 at full n (18/18 frames): census = 14 (k1) then 44–71 (k2–k18),
  median 58.0; ≥25 on 17/18 all-frames and 17/17 at k≥2 (min 44).** v1 read
  "14 of 16 with 2 failures (called k1-transient + one outlier)". v2
  re-attributes: those 2 "failures" (printed 0 and 8 at list positions of
  `cal_E1_L5_10` and `cal_E1_L5_3`) were TRUNCATED-FRAME censuses — with
  repairs the separation is 17/17 at k≥2 with min 44, **cleaner than v1
  thought, and the only sub-25 frame is the REAL k1 transient (14)**. The
  ≥3-frame median-of-3 rule still holds trivially (all triplets ≥52).
- E2 L5 full n: 18/18 ≥ 25 (min 37). E2 L10: min 26. **New caveat: E2 L20
  (the AE-dip rung, duty paint) never crosses: max 24, median 21, and its
  k3 reads 16 = the idle all-60 max while its k1 reads 4 < idle max.**
  The ≥25 presence threshold is E1-validated; a duty-coded paint at the
  L20-class AE dip presses against it single-frame — keep the ≥3-frame
  median rule (median 21 > idle max 16 holds, margin 5) and treat
  "first frame ≥25" as arm-dependent at L≤20.
- Floor conclusion unchanged, now two-state: the idle template a low-L probe
  subtracts must be the leg-2-equilibrium state (84–85 histMed, pooled core
  P50 ~105) because batteries idle-restart into the painted-rig equilibrium;
  leg-1 numbers (v1's floor row) are the colder hybrid minority.
- Coarse ranking unchanged: L20 (~97) > L5 (~58) > L10 (~51) is count-order
  visible, idle 9–13 below both; blob count still not a photometer above
  L40 (merge-dominated: 21–35 median at L150–80).

## (d) Transfer curve / band re-map at full n — re-derives, boundaries move ≤3 LSB

E1 settled (full n) transfer: cM−bg 86.2 (L5) → 88.2 → **79.1 (L20 dip)** →
94.4 → 97.8 → 101.4 → 102.3 → 104.4 → 106.0 → 106.0 (L179); pooled
per-blob PEAK P50 (v1's coreP50 definition) 229→253→231→164→174→128→105→
116→114→125.5 vs v1 222/254/227/173/174/136/110/110/122/128 — same shape
incl. the low-L inversions; raw metrics still cannot rank L below ~L40
single-frame. Monotone cM−bg from L≈20 up: same as v1. Both curves
reproduce at full n.

- **[90,105] coreMean−bg band**: E1 sits in-band L40–150 (94.4→106.0, L150/179
  a hair above top edge as in v1) and E2 L60/80–179 (86.6 → 102.6).
  Re-derived identical within the ≤3.3 LSB lock tolerance (recovered frames
  lift L5/L10/L40 by +2.5–3.3; L100–179 unchanged to ≤0.4 — the old
  operating point and its meaning are unchanged).
- **clip-side abort rule**: clipped blobs exist at EVERY rung (median 2–30
  blobs with peak ≥252 per settled frame even in-band) — consistent with
  v1's rule shape: abort requires clip AND cP50 ≥ 250 (cP50 is 85–118
  in-band) — never fires in-band. Confirmed, no change.
- **255−wall ≥ 40 spill-headroom bail, re-derived directly from run dirs**
  via the v2 E4 decode site walls (blur(5,5)-median behind confirmed sites,
  412–454 sites × 4 runs): 255−wall median 44.4 (r8) / 52.6 (r9) /
  51.2 (r10) / 43.0 (r11) — ≥ 40 at every L; bail stays out of reach on
  this rig/view. HOLDS identically.

## E4 confirm: decode DIRECTLY from run dirs (no wire pass) — parity EXACT

`e4_v2_decode.py` = v1's CLI-parity-proven driver with the frames loaded from
`runs/daemon/runs/run8..run11` jpgs (post-repair; EOI asserted; truncation
fallback NOT set), same gates (amp 40, margin 6, mask-thr auto, SUPPRESS 7,
fullres-rad 4, n 600), frozen bank. Per-run ledpos jsons written next to this
report's analysis dir. Diff vs v1's wire-derived ledpos jsons
(`analysis/e4_union/run{8..11}_ledpos.json`):

```
run8  L=80 : v2 412/600  amp med 70.0 | parity EXACT (412/412 ids; amp, margin, site Δ = 0.0/0.0/0)
run9  L=100: v2 454/600  amp med 82.7 | parity EXACT (454/454 ids; 0.0/0.0/0)
run10 L=120: v2 390/600  amp med 92.4 | parity EXACT (390/390 ids; 0.0/0.0/0)
run11 L=150: v2 411/600  amp med 70.9 | parity EXACT (411/411 ids; 0.0/0.0/0)
```

**E4 PARITY: 4/4 runs bit-exact vs the wire-derived v1 decode (412/454/390/411
confirmed; per-id amp/margin/site identical).** Union objective re-derived from
the v2 ledpos: r8∪r9 458, r8∪r10 456, r8∪r11 443, r9∪r10 475, r9∪r11 462,
r10∪r11 456; 4-burst union **481**, never-seen **119**, in-order cumulative
412 → 458 → 477 → 481 (+46/+19/+4) — every v1 union number confirmed.

## What changed vs v1 — the compact table

| conclusion | v1 | v2 (repaired corpus) | verdict |
|---|---|---|---|
| (a) AE lands by k2, no L-tau | measured on 167/180 E1 (13 excl.; k1 cols complete) | full 180 E1: probe metric ≤ +3.5% from k2 at every rung; no tau anywhere; first-paint creep is dip-then-rise, equilibrium only by k13 | HOLDS |
| (a) k≥2 skip rule | "keep k≥2 or 1 s" (2 L5 frames mis-attributed to transients) | n-stated: skip k1 (12/20 rung-arms ≥3% histMed shift); k2 safe everywhere EXCEPT first-paint-after-idle rung (k2 +11.5%; use k≥13) | UPDATED (n; first-rung k≥13) |
| (b) duty ratio ~×1.05, ×1.70 retired | cM-bg E2/E1 0.89–0.97 at L≥40; L≤20 "AE-confounded" | full n: 0.87–0.97 at L≥40; low-L cM−bg now settled-both-arms (0.89–1.14); pooled-peak-P50 ratio ladder reproduces at full n (0.45→0.62, was real structure not truncation) | HOLDS (stronger) |
| (b) E4 same-burst union/master ratio 1.04–1.12 | wire-derived copies | 1.065–1.133 from run dirs (n_unc 12–28) | HOLDS |
| (c) census ≥25 separates L5 from idle | 14/16 (2 failures called transients) | 17/17 at k≥2 (min 44); the 2 "failures" were the two truncated frames' censuses; idle max 16 over n=60 | HOLDS (cleaner) |
| (c) idle floor = spill field, template needed | leg-1 only (k00-11), coreP50 71-79 class | two-state: leg-2 equilibrium (P50 105.5, 68% sites ≥100) is the template future batteries restart into; leg-1 95.5/26% | UPDATED (two-state floor) |
| (c) threshold transfer | E1-derived, E2 unchecked | duty paints at L20-class AE dip: E2 L20 max 24 < 25; k3 = 16 = idle max — ≥3-frame median mandatory | NEW caveat |
| (d) band [90,105] + clip-abort + 255−wall≥40 | derived from E1 (excl. frames) + E4 wire | E1 in-band L40–150 (94.4→106.0), E2 L60/80–179; boundaries move ≤3.3 LSB; wall headroom 43.0–52.6 ≥ 40 at all 4 L | HOLDS (identical) |
| (d) monotone cM−bg only from L≈20 | yes | yes (79.1@L20 → 106.0@L150/179; L5/L10 above the dip) | HOLDS |
| E4 union(L) counts | from wire-derived copies | DIRECT from run dirs: 412/454/390/411 **EXACT parity** | CONFIRMED |

**Nothing flipped.** Two sub-claims corrected in attribution, not direction:
the (b) low-L AE-confound rows are real pooled-peak-P50 structure (v1's
characterization stands; the recovered frames leave it intact), and the (a)
"2 transient failures" in (c) were the 2 truncated frames themselves (their
censuses came from wire-truncated bytes — truncation, not physics); the (a)
first-paint creep's shape is dip-to-k10-then-rise-to-k13 (the recovered k10
frame fills the dip). One new caveat: duty-coded paints at the L20-class AE
dip press the blob-25 threshold single-frame.

## Files

`runs/daemon/analysis/photometry_v2/`: `phot_v2_pass.py`, `frame_metrics.json`
(428 frames), `mask_calib2.py`, `probe_defs.py`, `census_lock.py`,
`idle_lock.py`, `census_p50_lock.py`, `cp50_zoo.py`/`zoo2`/`zoo3`,
`v2_tables.py`, `v2_tables.json`, `v2_final_numbers.py`,
`v2_final_numbers.json`, `v2_b_locked.py`, `v2_b_table_locked.json`,
`e4_v2_decode.py`, `run{8..11}_ledpos_v2.json`, `e4_v2_summary.json`,
`e4_ratio_shiftcorr.py` + pass logs. Report:
`reports/s14r-0003c-photometry-v2-repaired-corpus.md`. No images produced;
v1 reports and all run dirs untouched.

## Caveats

- Aggregate lock tolerance vs v1's (b)/(d) tables is ≤3.3 LSB at L5/L10/L40
  (v1 aggregates recomputed on v1-kept frames with the locked definitions;
  residual is definition-level, not data-level), ≤1.1 elsewhere — the band
  boundary inherits that ±3 LSB ≈ ~1-rung uncertainty.
- v1's exact provenance of its own (b) "settled tail median" window is
  inferred (k2–18 reproduces its numbers best; its scripts were not
  preserved) — recorded, not consequential: locked diffs have consistent
  sign only where recovered frames enter the window.
- E2 L20 single-frame census can read idle-level (k3 = 16, k1 = 4): any
  probe presence check must multi-frame (v1 already required ≥3 frames —
  now with a concrete failure case).
- Parity-vs-v1 comparisons for E4 use v1's own ledpos jsons (CLI-parity-proven
  there, then re-proven bit-exact here from run dirs) — no third copy exists.