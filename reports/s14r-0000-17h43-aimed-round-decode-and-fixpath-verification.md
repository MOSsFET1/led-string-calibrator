# S14R-0000 round-2, 17:43 operator-aimed burst — console decode, parity, bright-wall rebuild, round-1 fix-path verification

Investigation of the SECOND real 12-of-24 round (`runs/daemon/runs/s14r-0000-17h43/`, 24 planes + master,
wire-extracted, 25/25 SOI+EOI+PIL clean, wire-vs-disk byte-equal on all 25). Operator aim: iPhone pointed
at the rig (different viewpoint from round-1's 16:16 burst). Operator ground truth: 600 installed
(strings 1-3). Phone decoded CWCDECS 17:43:08: **328 confirmed / 31 conflicts**, gates amp40/margin6/
suppress1, `sitesMasked 210003`, plane k[] 0.776-0.805. Console baseline (`tools/cwc_pos_decode.py`,
same gates, n=600): **317/600**. Round-1 reference: console 283/600, phone 309.

## Decodes, parity, hygiene

- **Console (CLI gates 100/40/6, fullres-rad 4): 317/600.** Instrument amp parity on those 317:
  med/p10/p90 = 0.0/0.0/0.0 vs the sweep (round-1's id-driven instrument reproduced exactly on fresh data).
- **Phone 328 vs console 317:** shared 297, phone-only 31, console-only 20. Shared-position parity:
  L1 median **0.0 px**, p90 2.0 — same as round-1 despite a different viewpoint and 10× steadier
  registration (direct per-plane shift mag med **0.26 px** vs r1's 7.1; chain conf 0.928-0.976 vs 0.79-0.93).
- Console-only 20 ids all sit at their phone/anchor sites (zero impostors at baseline).
- Of the 11 shared ids placed >6 px from the phone (3.7% of shared), 6 are members of the phone's own
  conflict list / intermingled pairs (L0, L119, L531, L449, L552, L441 near L442) and 5 are isolated
  wrong-site wins (L22, L44, L323, L325, L579) — the L563-class from round-1, now ~3-4% in BOTH rounds
  at baseline gates. Baseline console-only ids: 3 of r1's 14 and 5 of r2's 20 sit ≤5 px from a confirmed
  phone lamp (colocated/edge-blob second sites), none are far-orphan impostors at amp40.
- All 31 phone conflicts same-string, d 2-5.7 px, every conflicted id still confirmed — same as r1.
- Max claimed id 599 → **cwcN=600 honored** (no cap phantoms). Bank 12-of-24 Golay as shipped.
- The 560-599 string-3 tail is systematically sick from this viewpoint: 23 of its 40 slots show
  instrument amp < 8 at their interpolated anchors with ZERO phone confirmations, vs 5/40 in round-1 —
  view- or colocation-dependent blindness, not identity failure (same codewords confirmed from r1's angle).

## Exposure: the r1 evBias lever FIRED, not by CFG but by the operator/AE

- Burst exposure is a single stable value in both rounds (wire metas, `aem=continuous ev=-1`):
  r1 `exp=699.97` → r2 **`exp=200.02`** (×3.5 shorter ≈ −1.8 EV), no mid-burst drift (AE stable).
- Per-plane k (page & console agree to 0.001): r1 ≈ 0.579 (0.532-0.626) → r2 **0.776-0.805**.
- Master-frame histMedian 83.5 → 169.5; anchor ring background med **161 → 197**: the operator aimed
  at a *brighter* wall region, so the round did NOT get the wall relief the fix path called for — the
  relief came entirely from exposure.
- **Law re-fit (amp vs ring background, confirmed ids):** med amp/(255−bgr) = **0.97** (n=348) vs r1's
  0.538. corr with headroom 0.67 unchanged. The r1 law `amp ≈ 0.54×(255−wall)` was EXPOSURE-SPECIFIC, not
  universal: the same headroom now yields ×1.8 the amplitude. Per-plane algebra explains the shape:
  with ON planes clipped at 255, score/12 → k̄·(255−wall) at the site, so the law's constant is essentially
  the per-plane gain k̄ (0.58 → 0.79 measured; 1.8× observed lift includes reduced ON-side clipping,
  missed-id ON-peak med 255 → 251).
- Verdict on r1 prediction 1: mechanism CONFIRMED (exposure down raises amp per headroom, monotonically,
  and now crosses gate 40 for wall ≤ ~214); the r1 report's amp-70-at-wall-120 number overstated what
  exposure alone buys at fixed wall (at exp 200 the observed 200-239-bucket amp med is 49, not 70 —
  because this view's wall is brighter). Ev-bias bookkeeping advice stands: exposure is still
  `ev=-1`, page-controlled; re-apply-at-burst-start + lower wall fill remain the operators' levers.

## Bright-wall bucket tables rebuilt on the aimed viewpoint (console sweep, 594 instrument anchors)

Miss % by ring-background bucket at each amp gate (margin 6, mask 100):

| bucket | n | amp40 | amp25 | amp20 | amp15/3 |
|---|---|---|---|---|---|
| <80 | 27 | 70% | 37% | 33% | 14% |
| 80-119 | 64 | 48% | 29% | 28% | 20% |
| 120-159 | 88 | 32% | 22% | 20% | 13% |
| 160-199 | 130 | 30% | 13% | 9% | 6% |
| 200-239 | 267 | **54%** | 26% | 20% | 14% |
| >=240 | 18 | 72% | 44% | 44% | 0% |

vs r1 at amp40: 26 / 19 / 29 / 43 / **80** / 100%. The bright-bucket death dropped 80→54% at the same
gates (exposure effect), and amp25 pulls it to 26%. amp med of confirmed ids per bucket (r1→r2):
77.8→110.4 (80-119), 59.7→91.8 (120-159), 60.1→58.7 (160-199), 47.3→49.0 (200-239): the aimed wall is
brighter, so identical-gate counts are NOT identity-matched — bucket medians are the right template,
raw miss % alone is not.

## Mechanism split of the 283 console misses (instrument, anchors = console ∪ phone = 348, self-check 0.0 px)

| mechanism | n | own amp med | bgr med | prof_on med | reading |
|---|---|---|---|---|---|
| amp<40 | 211 | 17.2 | 213 | 57 | under gate at anchor (bright wall) |
| contest | 22 | 24.0 | 202 | 75 | stronger rival codeword at the site; zero phone-confirmed |
| suppressed/starved | 19 | 48.8 | 173 | 60 | healthy at anchor (amp 42-87, margin 8-28), beaten at anchor by argmax-site ownership + ±3px suppression; ALL 19 phone-confirmed (the phone's conflict-tolerant window keeps them) |
| mask | 24 | 1.5 | 71 | 29 | no modulation → truly absent/hidden |
| margin | 7 | 50.7 | 158 | 40 | instrument-thin d≥8 margin; 4 of them phone-confirmed |

amp<40 bands: 0-8: 51 · 8-25: 79 · 25-40: 80. Mask-class: zero phone-confirmed.

## The round-1 fix-path predictions against this fresh round

**1. "Amp gate 40→25 keeps margin 6 = pure gain" — REFUTED as a blanket rule; TRUE for the at-anchor
population.** Counts reproduce (r1 +97, r2 +131, zero lost both rounds), but the gains decompose as:

| round | combo | recovered | near-site (true) | dup-at-foreign-lamp | far-orphan (>6 px from anchor & phone) |
|---|---|---|---|---|---|
| r1 | 100/25/6 | 97 | 33 | 7 | **57 (59%)** |
| r1 | 100/20/6 | 106 | 34 | 8 | 64 |
| r2 | 100/25/6 | 131 | 66 | 9 | **56 (43%)** |
| r2 | 100/20/6 | 158 | 73 | 13 | 72 |
| r1 | 100/15/3 | 132 | 32 | 15 | 85 |
| r2 | 100/15/3 | 202 | 73 | 20 | 109 |

At relaxed gates an eaten codeword wins at some OTHER bright site (argmax-per-site) and its true site
stays starved — impostor counts are majority of nominal gains in both rounds. Round-1's report claimed
"+97, zero regressions" and matched an instrument prediction in COUNT only; a position audit (this
round's new instrument-vs-sweep parity check at anchor/phone sites) was missing and would have shown
~60% of those "recoveries" as impostors. A systematic family exists: string-3 ids 560-599 claim a chain
of string-1/2 lamp sites x 156-240, y 224-311 in BOTH rounds (10 ids wrong in both: 115, 565, 567, 575,
580-582, 585, 591, 597). Base-gate (amp40) impostor rate is only ~3-4% — the corruption is CREATED by
the relaxation, in both viewpoints.

Instrument-side prediction for r2 amp25 (at-anchor amp ≥ 25, margin ≥ 6): 93 ids; the sweep realized
131; 71 of the 93 land at/near the predicted site; 60 realized-only split into 6 true near-site hits,
2 eaten-codeword dup placements onto a foreign lamp's site, and 52 orphan-site wins whose anchor-site
amp is dead (< 25; 34 of them < 8) — e.g. L588 "recovered" at (176,224) with instrument amp 1.9, anchor
330 px away. Position audit is mandatory for counting relaxations: raw counts overstate true recovery.

**2. Suppression redesign (page-parity conflict list)** — quantified on r2: the recoverable-real class
is ~19 ids (amp ≥ 40, margin ≥ 6, phone-confirmed, eaten) + 22 contest-class; CLI redesign still pays,
slightly smaller than r1's 57 (aimed view separates blobs better). Page behavior (conflicts flagged,
members still confirmed) remains the correct model.

**3. Keep mask 100 / cwcN=600** — verified: mask 80 at amp25 = 449 vs 448 (±1); max id 599; phone k parity.

**4. "Evidence-absent" bookkeeping flag** — r2 fresh count: 24 mask-class + 11 amp<8-with-no-codeword-
structure (prof_sep < 5) = ~35 ids with no identity evidence from this viewpoint; consistent with r1's
~35-55. All phone-blind too. Ship the flag so these stop counting as decoder misses.

**5. Bright-bucket table as the regression template** — rebuilt above on this viewpoint; r1's shape
reproduces (miss% rises with wall luma, monotonically at every gate), with the amp40 bright-bucket
penalty softened 80%→54% by the exposure change.

## What would actually move the number (evidence-backed)

1. **Exposure + aim (operator lever, unchanged but recalibrated):** at exp 200 (ev=-1), amp ≈ 0.97×
   (255−wall) ⇒ amp 40 needs wall ≤ 214, amp 70 needs wall ≤ 183. The 200-239 bucket holds 45% of
   anchors — reframing to drop wall fill ~60 luma + another ~0.5-1 EV of exposure crosses most of the
   211 amp<40 class through the EXISTING gate. Wire exposure is stable per burst — AE is not the risk;
   applyEvBias-at-burst-start is still the 10-line page change that makes CFG-controlled exposure real.
2. **Amp gate change: hold at 40 for identity-grade output.** If a relaxed gate is adopted for counting,
   pair it with the position guard (site within ~6 px of the registered lamp position) — otherwise ~45-60%
   of the counted gain is impostor placements (verified both rounds). The guard recovers the true gains
   (66 at r2 amp25) while rejecting the 56 far-orphans and 9 foreign-lamp dups.
3. **CLI suppression/ownership redesign** (same-codeword dedup + ≤5 px rival-site demotion to
   conflict-flagged entries, page-style): ~19 real ids + removes the base-gate 3-4% impostor class.
4. **Re-audit r1's sweep tables with the same position audit** before anyone cites "+97 pure gain" —
   round-1's gate-sweep recovery numbers overstate true recovery by ~2.9×.

*Evidence artifacts: /tmp/invest_r2/ (sweep_driver.py, evidence.py, sweep_out.json, evidence.json,
mech.json, wrong_site_audit.json, phone_328.json, phone_decode_r2.json, wire_metas.json; per-plane
scoring ran only in subprocesses; no (ids,H,W) tensor in-kernel; run dir untouched, no git).*