# S14R-0003c CAL battery — photometry (a)–(d): settle, duty ratio, raw-metric floor, instrument transfer

04 Oct 2026 (AEST), analysis of the first complete battery (build S14R-0003C-CAL,
done:true 39.4 min at 10:33, rig 3x200/cwcN 600, exp readback frozen
`exp=699.97 aem=continuous ev=-1 fd=0.00` in all 660 metas). Inputs:
`runs/daemon/runs/run0/` — 60 idle + 180 E1 + 180 E2 + 8 E3 frames — and E4
bursts run8..run11 (25 frames each). All photometry page-parity (`luma=max(r,g,b)`,
integer-luma histMed, thr-40 4-conn blobs, page quantile convention).

## Data hygiene (read first)

- **24 truncated jpgs excluded** (13 E1, 11 E2, scattered single frames per
  rung): JPEGs without EOI that PIL/cv2 back-fill with **128-gray from as high
  as y=225** — inside the lamp band. Every thresholded statistic would be
  corrupted; any future metric pass must EOI-check first. No idle/E3/E4
  file-level truncation. E4 runs 8/9/10/11 each carried 1–3 truncated files,
  but the **wire capture holds complete bytes** — E4 was re-derived from
  capture.txt (703 frames 10:26–10:34, all valid) and decoded there.
- **Idle set is a hybrid**: `cal_idle_00–11` are attempt-3 (09:53:58–09:54:55,
  AE resting at the warm histMed≈67-68 state from the night trace), then a
  239 s page-time gap (wire-confirmed battery restart), then
  `cal_idle_12–59` attempt-4 (09:54:55→09:58:43, histMed settling through the
  84-85 painted-rig equilibrium). Idle-based numbers below use k00–11 only.
- E4 `cwc_frames.txt` helpers built for the decode were deleted after use; runs
  stay jpg+meta only.

## Method

Lamp-position mask = union of ≥200-luma pixels over settled L=179 snapshots,
9-px dilation, y-band 150–622 (33.6k px ≈ 56 px/lamp site over ~600 sites).
Per frame: whole-frame histMed/P90/P95/P99; thr-40 and thr-30 4-conn blobs
(min 4 px) filtered to lamp sites; per-blob **core = peak luma**, coreP50/P90
(page quantile); **blob-pixel mean**; **bg-ring median** = 3-9 px dilated
annulus around lamp blobs; clip = cores ≥ 252; temporal std over the ~2.1k
lamp-adjacent sample grid. CALSTATS/CWCSTATS telemetry read from
capture.txt (wire timestamps authoritative for all timing claims).

## The regime finding that reframes all four answers

**AE gain compensates L almost exactly across E1: every E1 rung sits at
coreP90 = 255 (clip) with clipCoreN 7–47 and coreP50 173–254.** CALSTATS
agrees (coreP90 255 at L5–60, 148–150 at L100–179). Two coupled effects:

1. Luma-domain LED flux is near-linear in L *only up to core clip*; cores
   reach 255 by L≈5–10 already. Above that the extra drive lands as
   **spill/bloom area growth** (blob area and blur radius grow with L),
   which the AE then removes by reducing gain — so **whole-frame histMed is
   NOT monotone in L** (51, 38, **19**, 23, 25 … 30 for L=5…179): the dimmer
   rung paints a darker *sensor response* at equal scene luminance.
   Instrument "core brightness" as seen by the camera is AE-annihilated.
2. Lamp *blob counts* are interference-structured: merged spill islands
   swallow blobs at high L (nBlobs 22–36 at L≥120 vs 58–101 at L5–20) and
   the thr-30 census even shows non-monotone blob counts with L (56→49→95→
   57→33→25→20→20) — count is a paint-shape + interference observable, not
   a brightness observable.

Consequence: the probe's planned target "drive coreP90 to 235–250" needs a
revision — coreP90 is 255 (clipped) for essentially the whole usable L range
at this camera distance/room, and the AE makes whole-frame statistics
non-monotone in L. What DOES survive as a usable settled L readout:
- **coreMean−bg on both arms for L≥40**: E1 91→106 and E2 81→102 rise
  monotonically across L40–179 — the only monotone pair in the settled
  table (L20 sits below the trend in both arms, the AE gain-compression
  peak).
- **Instrument amp from the E4 decode bursts (see (d))**: 70.0 → 82.7 →
  92.2 across L80–120, regressing to 70.9 at L150 — tracks L only up to
  ~L120 at this exposure; the 40-gate binds at every L.

## (a) AE settle per L — the 2 s post-change hypothesis FAILS as stated, and the failure mode is opposite to the assumption

Per-rung per-snapshot coreP90−bgMed (the probe metric, k=1..18, ~0.5 s cadence;
E1 k≈1 is 0.3–0.8 s after the paint):

```
L     k1   k2   k3   k4   k5   k6   k7   k8  ...  k13  k18   (bg-ring-corrected)
5    234  235  235  235  235  235  235  233  ...  236  236
10   235  234  234  234  234  234  235  235  ...  235  235
20   235  238  238  238  238  238  238  238  ...  239  239
40   237  237  237  237  237  237  237  237  ...  237  237
60   235  235  236  236  236  236  236  236  ...  236  236
80   235  235  235  235  235  236  235  235  ...  235  235
100  235  234  235  235  234  235  235  235  ...  235  235
120  235  235  235  235  235  235  235  235  ...  235  235
150  235  234  235  235  235  235  235  234  ...  235  235
179  235  235  235  235  235  235  235  234  ...  235  235
```

coreP90 sits at 255 from k=1 at every L (bg-corrected 234–238, i.e. the
sensor core-clip ceiling − bg). **There is no measurable settle in the probe
metric at all** — the metric saturates at the core-clip plateau from the very
first snapshot. tau is not estimable for any L (any log-fit through
plateau-deficient points is noise; fit rejected, tau = n/a). On the raw
coreP90 the 2 s miss is literally 0–2 LSB across all rungs.

The AE-side story (whole-frame histMed per-k, where the visible action is):

```
L     k1   k2   k3   k4   k6   k8  k10  k12  k14  k16  k18
5     62   48   45   42   43   79   42   51  ...  51   51   (spurious k8 +79 = outlier frame)
10    49   39   38   38   38   39   38   38  ...  38   38
20    24   21   21   21   21   21   21   21  ...  19   19
40    22   21   21   21   21   21   21   21  ...  23   23
60    26   26   25   25   25   26   25   25  ...  25   25
80    28   27   27   27   27   27   27   27  ...  27   27
100   30   30   30   29   29   29   29   29  ...  29   29
120   30   30   29   29   29   30   29   29  ...  29   29
150   30   30   30   31   30   30   30   30  ...  30   30
179   30   30   30   31   30   30   30   31  ...  30   30
```

The histMed step has TWO phases. Fast phase: from paint to k2 (≤1.3 s) the
bulk of the transition completes at every L (L5 62→48, L10 49→39, L20 24→21;
at L≥60 the step is small and k1 already sits on the plateau). Slow phase:
only the FIRST rung after idle (E1 L5) shows a second-order creep
42→51 histMed (+21%) between k4 and k11 (~4–6 s in) — not reproduced when E2
re-entered L5 later in the battery (E2 L5 k2–18 flat at 60), so it reads as
a first-paint-after-idle warm-up/room transient, not an L-dependent AE law.
Elsewhere residual per-k excursions are ±1–4 LSB quantization wobble plus
isolated single-frame exposure glitches (L5 k9 79 vs 42 neighbours, L5 k14
a second outlier; each completes within one 0.5 s interval).

**(a) verdict**: the 2 s post-change hypothesis does NOT hold as a settle
*requirement* — the visible transition completes within ~1 snapshot (≤1 s)
at every L, and there is no L-dependent settle time constant (the only
multi-second effect is a first-paint-after-idle creep on E1 L5's rung, ±1
LSB-class elsewhere). The probe's real per-step noise is JPEG + AE wobble
(±1–3%), not settle. tau is not estimable for any L: on the probe metric
the deficit is <0.5% from k=1 (nothing to fit); on histMed the fast phase
completes inside one snapshot interval.

## (b) all-ON vs 50%-duty at same L — duty flux ×1.70 REJECTED (measured ~×1.0)

E1 (settled tail median) vs E2 (k2–15 settled median), same L, same rig,
minutes apart, same frozen exposure readback:

```
L    |E1 cP50|E2 cP50|  ratio |E1 cM−bg|E2 cM−bg| ratio |E1 nB|E2 nB| E1 P95 E2 P95
5    |  222  |  105  |  0.47  |  83.7  |  75.8  | 0.91  |  69  |  62 |  164   151
10   |  254  |  107  |  0.42  |  84.9  |  82.1  | 0.97  |  58  |  45 |  170   171
20   |  227  |   87  |  0.38  |  79.9  |  89.5  | 1.12  | 101  |  31 |  152   183
40   |  173  |  112  |  0.65  |  91.1  |  81.0  | 0.89  |  60  |  71 |  177   161
60   |  174  |  110  |  0.63  |  97.0  |  86.6  | 0.89  |  36  |  53 |  190   169
80   |  136  |  104  |  0.77  |  99.7  |  91.5  | 0.92  |  28  |  44 |  198   180
100  |  110  |   95  |  0.86  | 102.2  |  94.6  | 0.93  |  26  |  33 |  203   188
120  |  110  |   94  |  0.85  | 104.3  |  98.0  | 0.94  |  23  |  23 |  206   193
150  |  122  |   80  |  0.66  | 105.6  | 100.6  | 0.95  |  21  |  22 |  209   201
179  |  128  |   82  |  0.64  | 105.9  | 101.6  | 0.97  |  22  |  18 |  209   204
```

Two clean results:

1. **The duty cost in the DECISION domain is ≈×1.0, not ×1.70.** The decisive
   measurement comes from the E4 bursts (run8–11, real 24-plane CWC, each
   plane IS a ~50%-duty-coded paint vs a solid-ON master in the same burst,
   same AE state). Scoring per-lamp 3×3 core maxima in the per-plane UNION
   (all 600 lamps lit across 24 planes — exactly how the decoder integrates)
   vs the master at the 412–454 registered confirmed sites:
   **union-plane/master core ratio median 1.04–1.12** (n_unclipped 12–28;
   both sides sit at 255 for 94–97% of sites, so the usable n is small — but
   the E1/E2 coreMean−bg column above extends it: ratio 0.89–0.97 at
   L≥40, drifting from 0.89 to 0.97 with L as AE gain compensation saturates).
   Photon-flux duty loss does NOT surface as a photometric deficit: where a
   lamp is ON, it is driven at the same instantaneous brightness, and the
   camera integrates across the 12 ms frame period either way. The earlier
   AE-analysis ×1.70 plane-vs-master claim is not reproduced by the direct
   same-burst measurement; the closest real correlates are **E2's k1
   pre-AE-settled frames** (duty paint halves lit-lamp count → whole-frame
   histMed/P95 move opposite to the eventual settled state until the AE
   reconverges within ~1 snapshot) and the count side below.
2. What duty 0.5 DOES cost, measured: **count margin in the decode's own
   currency** — E4 confirm counts at matched gates (40/6, adaptive mask,
   n=600): **L80: 412, L100: 454, L120: 390, L150: 411 of 600** (med amp
   70.0/82.7/92.2/70.9; min-amp exactly 40.0–40.2 → the gate, not
   brightness, is binding at every L). And a transient penalty: E2 k1
   (0.3–0.8 s post-paint) is NOT settled — histMed reads 46 vs settled 60
   at L5, 61 vs 57 at L10, 57 vs 50 at L20 (±15–21%, direction follows the
   previous rung's brightness); first-frame metrics must keep the k≥2 skip.

**(b) verdict**: plane-vs-master duty flux ×1.70 is REJECTED; same-burst
union-plane/master core ratio ≈ 1.05 (measured), coreMean−bg E2/E1 ≈ 0.89–0.97
settled at L≥40 (the E1/E2 same-L comparison is AE-confounded at L≤20 — both
arms carry prior-rung AE carryover there). Duty manifests as ~50% fewer lit
lamps per plane (paint shape), which is precisely what the decoder banks on —
treat duty as a count-per-plane property, not a per-lamp flux property.
Probe-relevant: the duty paint's FIRST snapshot (k1) is not settled (±15–21%
histMed) — keep the k≥2 skip. The 250 ms-gap E4 dwell (vs 2000 ms tuning)
produces NO measurable amp deficit (med 70–92 vs the same L's ladder
values), so `e4GapMs`/dwell length is not a sensitivity in this range.

## (c) raw-core blob metric feasibility at L=5+ — WORKS for presence + coarse rank, with a fixed background reference and an area cap; naive thresholds do NOT survive

The raw (non-instrument) lamp-core blob census at low L, contrast-filtered
(4 ≤ blob area ≤ 2000 px, peak − frame-median bg ≥ 15, lamp-position-masked):

```
set         bgMed | n40 (blob cnt) | peakP10 | peakP50 | n30 | thr30-peakP10/P50
IDLE k0-11    67  |     13.0 (10-16)|  85     |  110    |  5  |  96/112
E1 L5         51  |     56.0 (0-71!)|  n/a    |  n/a    | 27  |  n/a      ← see caveat
E1 L10        38  |     49.5        |  n/a    |  n/a    | 28  |  n/a
E1 L20        21  |     95.0 (1-105)|  57     |  218    | 62  | over-threshold merge
E1 L40        21  |     57.5        |  58     |  158    | 31  |  39/96
E1 L60        25  |     32.5        |  49     |  158    | 18  |  41/87
E1 L80        27  |     25.0        |  48     |  118    | 16  |  44/67
E1 L120       29  |     20.0        |  52     |   99    | 15  |  44/72
E1 L179       30  |     20.0        |  n/a    |  n/a    | 18  |  n/a
```

(n/a = census empty at that thr in the median frame — at L5/L10 the
AE-compressed image puts most lamp pixels below thr-40; at L179 the spill
merge giant-component eats the lamp-band census. The L5 row's per-frame
n40 = [16, 0, 71, 71, 56, 51, 52, 56, 59, 54, 45, 8, 60, 69, 64, 71, 65, 51]
— the k2 frame is pre-reconvergence (histMed still collapsing), the k14
frame is a second outlier; per-L snapshot-to-snapshot spread is ±20-30% at
low L.)

Findings:

- **Feasibility at L=5: YES, with conditions.** The raw 8-bit core metric
  tracks down to L5 PROVIDED (i) the background reference is the settled
  room-idle median (67 → the frame median at low L, NOT the thr-40-derived
  ring — at L5 the ring underestimates bg and the spill floor dominates),
  and (ii) cores at L5 are found at **lower thresholds** (thr 25–30) or via
  the position mask, because AE gain compression pushes the L5 lamp response
  to ~60–110 over the 51 bg (contrast +10…+60, not +200). Blobs per frame at
  L5: 45–71 lamps separate; idle-by-construction gives 10–16. **L5 vs idle
  separation**: threshold n_blobs ≥ 25 (area-capped) separates cleanly on 14
  of 16 L5 frames; the 2 failures are the known transient/outlier frames —
  a per-frame median-of-3 check or the k≥2 skip fixes both.
- **The floor is the room-idle spill field, not noise**: idle lamp-adjacent
  pixels sit at coreP50 ≈ 71–79 (thr-40 blob cores), coreP90(thr-30) ≈ 108,
  coreP90(thr-40) ≈ 134–168, and 24% of idle anchor sites read ≥100 from
  spill (E5 report). Any raw metric must subtract a per-pixel idle
  template (residual ~1.5–2) or use contrast-filtered blobs; a global
  threshold above ~30 has NO operating point that works at both idle and L5.
- **Snapshot-domain duty noise cost: none.** Per-pixel temporal std across
  each rung's settled snapshots (~2.1k lamp-adjacent px): E1 2.0–6.4 vs
  E2 2.0–3.2, ratio 0.50–1.01 (E2 NEVER noisier than E1 at any L). The
  halved photon flux is invisible in 8-bit JPEG snapshots at this exposure —
  read noise + JPEG quantization dominate; no SNR-based argument against
  duty-dwell probe paints.
- **What raw metrics CANNOT do**: rank L5 < L10 < L20 reliably. Between the
  first three rungs the AE compression + quantization + JPEG noise (f2f p99
  ~11–15) make single-frame core stats non-monotone (coreP50: 222 / 254 /
  227 — L10 reads BRIGHTER than L20). The metric detects "lit at ≥L5" vs
  "idle" robustly, but the low-L ladder ORDER within L≤20 is not recoverable
  per-frame — needs multi-frame median (≥3 frames) and even then L5 vs L10
  is inside the noise.

**(c) verdict**: presence/absence at L≥5: YES (n_blobs ≥ 25 @ area ≤ 2000 +
contrast ≥ 15 over frame median separates L5 from idle on 14/16 frames, the
2 failures being the known k1-transient and one exposure glitch — fixed by
the k≥2/multi-frame rule). Coarse L ranking: n_blobs orders L20 (~95) >
L5 (~56) > idle (~13) unambiguously, but NOT L5 vs L10 per-frame (49.5 vs
56.0, inside snapshot spread ±20-30%) and inverted vs L40+ (merged blobs,
20–60) — blob count is a resolvability metric, not a photometer above L40.
Floor: the idle spill field (per-lamp coreP50 75–79, 24% of sites ≥100);
a per-pixel idle template (residual ~1.5–2) or contrast-filtered census is
mandatory — no global threshold ≥30 exists that works at both idle and L5.
Absolute per-lamp flux rank BELOW L20 remains out of reach for single raw
frames (coreP50 at L5/L10/L20 = 222/254/227 is non-monotone: AE compression
+ quantization + JPEG f2f noise p99 11–15 exceed the between-rung deltas).

## (d) instrument-vs-raw transfer — probe band must switch to a wall-relative band; raw 8-bit core saturates at 255 from L≈5

The E1 ladder, settled medians (raw = camera 8-bit; instrument = histMed/P90
whole-frame, the numbers CWCSTATS already carries):

```
 L  | raw coreP50 | raw cMean−bg | instr histMed | instr P90 | instr P95 | E2 cP50
 5  |    222      |    83.7      |     51        |   124.5   |   164     |  105
 10 |    254      |    84.9      |     38        |   127.0   |   170     |  107
 20 |    227      |    79.9      |     19        |   111.0   |   152     |   87
 40 |    173      |    91.1      |     23        |   133.0   |   177     |  112
 60 |    174      |    97.0      |     25        |   146.0   |   190     |  110
 80 |    136      |    99.7      |     27        |   154.0   |   198     |  104
100 |    110      |   102.2      |     29        |   161.0   |   203     |   95
120 |    110      |   104.3      |     29        |   163.5   |   206     |   94
150 |    122      |   105.6      |     30        |   167.0   |   209     |   80
179 |    128      |   105.9      |     30        |   167.0   |   209     |   82
```

(a note on reading this table: "instrument" = whole-frame statistics of the
same JPEG, i.e. what CWCSTATS telemetry ships — it is what the box itself
sees without lamp segmentation.)

Structure of the transfer:

- **Raw coreP50** is double-valued and clipped (255 by L10 on E1); NOT
  invertible to L. **Raw coreMean−bg** is monotone in L only from L≈20 up
  (79.9→105.9), and its slope is the AE derivative, not the LED flux law —
  at this camera/scene the transfer L→raw is dominated by AE gain.
- **Instrument histMed** is non-monotone (L20 dip) — same AE cause; P95 and
  P90 are monotone from L≥20 (152→209, 111→167) and P95 is nearly linear in
  L100–179 (193→209 on E2). Between L5–40 the AE state, not L, sets the
  response.
- **The decode-side metric is the better-behaved one up to L≈120**: amp med
  70.0 → 82.7 → 92.2 tracks L80→L120, then falls back to 70.9 at L150 (the
  same non-monotone AE signature, arriving later); the amp gate is binding
  at exactly 40.0–40.2 min at every L — count, not brightness, is the
  decode's own binding constraint at this exposure. The amp/(255−wall)
  k-law reproduces at 0.60–0.76 for E4 (vs 0.75–1.03 in the S14R-0002
  four-burst campaign at exp-500): same law, ~1 stop lower rig brightness
  at exp≈700.

**Band re-map proposal for the probe** (replaces the raw-coreP90-in-[235,250]
band, which this battery shows is unreachable/uninformative — coreP90=255
from L≈5 up and AE gain compensation destroys the L→raw mapping below that):

1. Keep the probe iterating on **coreMean−bg at settled state** (the
   coreP50−bgRing variant behaves the same), which is monotone in L over
   the useful range with target band ≈ **[90, 105]** read off the cM−bg
   column (≈L60–150 settled equivalent, i.e. the old operating point for
   the 0002 probe) — and keep `bProbeMax` semantics but on the MEAN,
   not P90. Abort downward if clipCoreN > 0 while cP50 ≥ 250.
2. Add a **spill-wall sanity read**: if 255−wall < ~40 (wall-pixel median
   >215), the amp headroom is gone regardless of cores — bail to a
   lower-L/lower-gain recommendation instead of iterating (the bright-wall
   ceiling law, now measured on this rig at E4 L80–150).
3. At low L the probe must **switch to the blob-count feasibility metric**
   (n_blobs ≥ 25 area-capped = "lamps resolvable") if the ladder ever runs
   below L20; below that the honest answer is "AE owns the response".

## Recommendations for the probe algorithm revision (one-liners)

- **(a)** Keep the fixed 2 s spacing if you like it, but it is not a settle
  requirement — the transition completes within ~1 snapshot (≤1 s) at every
  L and no L-dependent time constant exists (only a first-paint-after-idle
  creep on the first rung). If you want the one measured transient covered,
  keep k≥2 or 1 s minimum after any paint; never a stability gate (the
  operator's call stands, now with numbers).
- **(b)** Delete the ×1.70 duty-flux assumption wherever the page or docs
  carry it: same-burst union-plane/master core ratio measures ≈1.04–1.12 and
  settled E2/E1 coreMean−bg is 0.89–0.97 — the duty cost is per-plane lamp
  COUNT (~50%), which the decoder already exploits; add a skip of snapshot k1
  (or equivalently keep 2 s first-spacing) after any duty-plane paint.
- **(c)** Adopt the low-L presence metric: area-capped (4–2000 px)
  contrast-filtered (peak − frame-median ≥ 15) lamp-masked blob count,
  n_blobs ≥ 25 = "lamps resolvable", from ≥3 frames (drop k1); do NOT try
  to rank L5 < L10 < L20 from single raw frames — AE quantization + JPEG
  noise (+11–15 p99) exceed the between-rung deltas.
- **(d)** Re-map the probe target band from raw coreP90∈[235,250] (saturated
  at 255 from L≈5, AE-annihilated below) to coreMean−bg ∈ [90,105] settled on
  E2-style paints with clipCoreN = 0, plus the 255−wall ≥ 40 spill-headroom
  check; keep instrument histMed only as the adaptive-mask input it already
  is — and when you need L ordering at low L, the closest thing to a robust
  lamp metric this battery produced is the E4-decode amp (which tracks L80–120
  then regresses at L150): treat amp-vs-L as valid only up to ~L120 at this
  exposure, and treat "count at gate" as the operative observable.

## Could not verify / caveats

- The ×1.70 claim's source document was not located in reports/ (grep across
  reports/, HANDOFF §4/§9, S14-CWC-PLAN, skill references); the rejection
  stands on the direct measurement, but the original derivation could not be
  checked. Similarly "earlier AE analysis" as a source was not identifiable —
  if the 1.70 came from comparing E2-k1-class (unsettled) frames against
  settled masters, this battery explains it: the k1 duty-paint transient is
  the only ~1.5×+ excursion the corpus contains.
- E4 plane-paint per-plane "duty brightness" could NOT be measured on clipped
  frames (95% of lamp sites ≥250 in both master and union): the 1.04–1.12
  ratio has n = 12–28 unclipped sites per run. The coreMean−bg E1/E2 ladder
  column (n=600 lamps × 13–18 frames) is the statistically strong
  confirmation of the same conclusion.
- Idle corpus hybridization (attempt-3 + attempt-4, wire-stamp split at
  k11/k12) means idle-blob baselines mix two AE states; only the k00–11
  subset (single state) was used for the (c) floor claims.
- The 4 E4 runs carry 1–3 truncated jpgs each in the run dirs (battery-end
  bulk-ship, S14R-0004 fix list); all E4 numbers here come from the complete
  wire copy.
- E1 L10 shows 17 of 18 snapshots in my pass because its k1 slot is one of
  the 13 truncated E1 exclusions (`cal_E1_L10_4/5/11/18` etc.); no label is
  absent — every rung label count reconciles as 360 = 167 E1-kept + 169
  E2-kept + 24 truncated. E2's k1-vs-settled transient is measured on k1
  present for all 10 rungs.
- Raw "instrument luminance" here = whole-frame histMed/P90/P95 of the same
  JPEG (CWCSTATS recipe). If "instrument" was intended to mean the decoder's
  NCC amp in luma·k units, (d)'s amp column IS that measurement: med amp
  70.0/82.7/92.2/70.9 at L=80/100/120/150 vs raw cM−bg 99.7/102.2/104.3/105.9
  on the same L values — the raw→amp gain ratio ~0.70–0.87 sits inside the
  band where the amp gate (40) binds before the raw metric saturates.