# S14R-0001 iPhone POI bursts (r1–r4, 20:30–20:36) — console decode, exposure-motion measurement, union + cross-round matrix

Four consecutive iPhone bursts on the 0001 build (3×200=600 installed, 12-of-24 bank), 25 jpgs each
(24 planes p00–p23 + master, 406×720, wire metas exp='' — exposure state was NOT read back; the
operator tapped the camera view for POI exposure per the new 0001 feature and saw no visual
feedback). This report console-decodes all four bursts with `tools/cwc_pos_decode.py`'s exact
machinery (n=600, gates mask100/amp40/margin6, fullres-rad4), measures whether exposure actually
moved between taps, builds the per-burst + union counts and bucket curves, and the cross-round
per-id matrix. Position-relaxation counting follows the 0000-17h43 report's position-guard rule.

## Hygiene + instrument parity

- 25/25 jpgs per run, magic-clean (SOI+EOI) and PIL-clean, all 406×720. Decode staging built in
  `/tmp/improve_ios/<run>/cwc_frames.txt`; **run dirs untouched**. Image-source parity: staging
  (wire->disk jpg) vs repo jpg decode max|diff| = 0.0 on every master.
- EXIF in these 406×720 jpgs is a Photoshop-resize stub (only PixelDimension tags) — no camera
  exposure metadata survives the wire. All exposure motion below is measured from pixels.
- **Instrument parity on the cross-build reference**: re-running this machinery on
  `s14r-0000-17h43` reproduces the published console numbers **exactly** — 317 @ 100/40/6 and
  448 @ 100/25/6 (reg shift mag med 0.26 px, as documented). The machinery below is the same one
  that produced those verified numbers.

## Console decodes (CLI gates mask100, margin6; amp gate as listed)

| burst (wire span) | 40/6 (base) | 30/6 | 25/6 | 20/6 | 15/3 | 25/4 | max id |
|---|---|---|---|---|---|---|---|
| r1 20:30:38–20:31:49 | **287** | 393 | 437 | 456 | 483 | 439 | 599 |
| r2 20:31:52–20:32:54 | **440** | 534 | 545 | 549 | 560 | 548 | 599 |
| r3 20:32:57–20:34:13 | **487** | 547 | 553 | 559 | 569 | 554 | 599 |
| r4 20:34:15–20:35:33 | **257** | 365 | 408 | 423 | 451 | 416 | 597 |

Registration (direct per-plane, fullres rad 4): shift mag med 3.12–3.17 px, max 4.2–9.5; axes
refined x/y 9/8, 1/8, 9/13, 13/11 of 24. cwcN=600 honored (r4's tail misses to 597, no cap
phantoms anywhere). Per-burst counts span **257–487 (±40%) at effectively identical photometry**.

**Relaxation + position guard (gains vs base, audited against the id's own masked anchor):**
25/6 gains +150/+105/+66/+151 with guard-PASS only 0/2/0/4 (0–2.7%) and **guard-FAIL 150/103/66/147
(97–100%)**, zero losses in every burst. Unlike 0000 (45–60% impostor share), essentially the whole
nominal gain here sits away from the id's own site: every missed id already has a ≥40-amp masked
site (see mechanism split), so relaxing amp just re-admits rivals' wins at other lamps. Keep amp 40
for identity-grade counting; raw relaxed counts on this rig are ~97–100% inflation.

## Exposure: did the tap actually move AE? — NO (measured from pixels)

| burst | master histMed | plane-med range | k̄ (per-plane gain) | max plane→plane drift | ON-peak med (p90) | anchor ring med | amp med (confirmed) |
|---|---|---|---|---|---|---|---|
| r1 | 160.5 | 126.5–136.5 | 0.788–0.851 | 5 | 248 (255) | 190.5 | 63.3 |
| r2 | 152.5 | 118.5–131.5 | 0.777–0.862 | 4 | 233 (255) | 203.2 | 61.8 |
| r3 | 160.5 | 142.5–148.5 | 0.888–0.925 | 5 | 233 (255) | 201.5 | 59.9 |
| r4 | 168.5 | 147.5–158.5 | 0.875–0.941 | 5 | 229.5 (255) | 182.0 | 59.4 |

Cross-build references (measured the same way): 0000-16h16 exp=699.97 → master histMed 83.5, ring
161, k̄≈0.58; 0000-17h43 exp=200.02 → histMed 169.5, ring 197, k̄ 0.776–0.805.

- **Burst-internal:** AE stable — plane-to-plane histMed drift ≤ 5 median units inside every burst;
  k̄ bands tight. No AE hunting.
- **Between bursts:** master histMed spans 152.5–168.5 across all four bursts (≈ ±0.1 EV total);
  ring-background med 182–203. The r2↔r3 pair is the same camera aim (below, 95.2% of 413 shared
  ids re-place ≤5 px after a (−2,−17) shift) and differs by 160.5 vs 152.5 histMed / 201.5 vs 203.2
  ring — indistinguishable from noise.
- Verdict: **the POI tap produced no measurable exposure change in any burst.** All four bursts sit
  at the exp≈200 class (like 17h43's ev=-1), nowhere near a 700-class step (histMed 83.5). The
  operator's report of "no visual feedback" is consistent with the pixels: exposure did not move.
  With `exp=''` on the wire this cannot be confirmed from metadata — **the exp read-back is the
  missing instrument**: the 0001 POI feature is unverifiable until exposure state is actually
  returned per frame.
- The amp/headroom law measured on claimed sites: med amp/(255−ring) = 0.769 / 0.769 / 0.502 /
  0.598 (r1–r4, corr −0.73…−0.84) vs 0.97 on 17h43 — the constant is again build/aim-specific
  (per-plane gains and ON-side clipping; ON-peak p90 = 255 in every burst), so bucket medians, not
  raw amp thresholds, remain the only cross-build template.

## Bucket curves (bucket = ring-bg of the id's masked best-anchor at BASELINE; n is identity-matched)

Miss % by bucket at base 40/6 / relaxed 25/6:

| bucket | n r1 | r1 40/6 | r1 25/6 | n r2 | r2 40/6 | r2 25/6 | n r3 | r3 40/6 | r3 25/6 | n r4 | r4 40/6 | r4 25/6 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| <80 | 78 | 62.8 | 28.2 | 27 | 55.6 | 14.8 | 223 | 21.5 | 9.4 | 143 | 65.7 | 35.7 |
| 80–119 | 189 | 70.9 | 39.2 | 251 | 36.3 | 13.5 | 130 | 25.4 | 10.0 | 179 | 62.0 | 37.4 |
| 120–159 | 157 | 52.2 | 26.8 | 125 | 26.4 | 6.4 | 110 | 18.2 | 7.3 | 113 | 53.1 | 26.5 |
| 160–199 | 133 | 28.6 | 14.3 | 126 | 15.1 | 6.3 | 95 | 11.6 | 4.2 | 122 | 48.4 | 30.3 |
| 200–239 | 43 | 23.3 | 14.0 | 71 | 2.8 | 1.4 | 42 | 2.4 | 2.4 | 43 | 44.2 | 16.3 |

The 0000 signature (miss% rises monotonically with wall luma) is **gone — the r2/r3 tables are
INVERTED (bright-bucket 2.8/2.4% vs mid-bucket 26–36%)**. Baseline-miss stats explain it: at the
missed ids' masked top sites amp_best med is 53.6–77.6 in every bucket of every burst — comfortably
over the 40 gate — while the d8-competitor margin is NEGATIVE at 78–100% of misses (median rival
excess −3.5…−86 amp units; e.g. r1: margin med −81.9/−80.9/−61.8/−43.3/−3.5 by bucket). On this rig
the binding failure is **codeword interference in the dense swarm**, and miss% tracks how much
swag overlaps behind each anchor — r2/r3's dense mid-swag anchors sit at ring 80–160 with clean
bright-wall anchors (margin med +24/+32, neg% 0 in the 200–239 bucket), r1/r4's anchors sit inside
the tangle. Wall luma has stopped being the failure axis at this exposure.

## Mechanism split of 600 (masked per-id top site, base gates)

| burst | confirmed | suppressed/starved (amp≥40, marg≥6, ≤6 px of a claim) | contest (amp≥40, marg<6) | amp 8–40 | mask-proxy (<8) |
|---|---|---|---|---|---|
| r1 | 287 | 4 | 308 | 1 | 0 |
| r2 | 440 | 5 | 155 | 0 | 0 |
| r3 | 487 | 5 | 108 | 0 | 0 |
| r4 | 257 | 9 | 334 | 0 | 0 |

**Zero evidence-absent ids**: every one of the 600 ids has a masked site with amp_best ≥ 8 (in fact
≥ 28) in all four bursts. The entire miss population is eaten/contested codewords — the CLI's
argmax-per-site + 7 px suppression loses them to brighter rivals — not absence, masking, or amp
starvation. This is the r2 report's contest/suppression class grown to 100% of the misses.

## Union + cross-round per-id matrix (base gates 40/6)

- Per-burst confirmed: r1 287, r2 440, r3 487, r4 257.
- Pairwise id overlap: r1∩r2 269, r1∩r3 263, r1∩r4 221, r2∩r3 413, r2∩r4 235, r3∩r4 234.
- **Union 534 / 600 (89.0%)**, 4/4-confirmed 207; multiplicity {4×: 207, 3×: 77, 2×: 162, 1×: 88}.
  At 100/25/6: union 581 (96.8%), 4/4 354, {1×: 24}; at 100/15/3: union 590, 4/4 395, {1×: 11}.
- **Never-in-any-burst: 66 ids** (29, 63–64, 73, 75–79, 82–83, 127, 129–131, 137, 188–193, 196–198,
  209, 211, 217–218, 220–221, 227–229, 261, 263–264, 266–269, 275–277, 279, 307, 310, 313–316,
  319–321, 357, 361–362, 397, 461, 505, 555–557, 573, 583, 589). But their masked cross-burst max
  amp: med 66.1, p90 77.1, max 103.2 — all ≥40 in at least one burst, none <8 anywhere: nothing on
  this rig is dark; those 66 are pure eats (rival wins), recoverable only by ownership redesign.
- Cross-burst geometry: r2↔r3 is one aim — RANSAC shift (−2,−17) puts 95.2% of 413 shared-id claims
  within 5 px (residual med 2.24 px), claim-cloud PCA major angles 107.5° vs 108.4°, and the
  masked-master NCC at that shift returns (0,0) steps conf 0.788. Every pair involving r1 or r4
  shows no shared rigid frame (best labeled-agreement 2.1–6.8%, cloud angles 7–17° off, room-NCC
  peaks near-tied: 481 cells within 0.9×max — the room's shelves/chairs pin the NCC, the swarm
  doesn't). So r2/r3 = one aim, two bursts ~1 min apart; r1 and r4 are separate re-aims. Cross-aim
  per-id disagreement is therefore viewpoint + interference, not identity churn — treat the matrix
  as aim-relative.

## What would actually move the number on this build (evidence-backed)

1. **CLI suppression/ownership redesign is now the whole game**: ~470+313 ids of contest/
   suppressed headroom across bursts, with ZERO mask-class and ZERO amp-starved ids. Same-codeword
   dedup + ≤5 px rival-site demotion to conflict-flagged entries (page-parity) is the only lever
   that addresses a 100%-interference miss population. Amp-gate relaxation is worthless here
   (97–100% of gains fail the own-anchor position guard) — hold 40/6 for identity-grade counts.
2. **Expose the exp read-back (exp='' today) before the next POI session** — with it, the "did the
   tap do anything" question is a one-line answer; from pixels we can only say exposure did NOT
   move across 4 bursts (±0.1 EV) while the operator tapped, i.e. the feature showed no effect.
   The AE itself is benign: converged, stable within bursts (drift ≤5 med units).
3. **Aim/framing, not exposure, separates buckets here**: at this exposure the lamps clip
   (ON-peak p90 = 255) and every id clears amp 40 somewhere; misses are swarm-overlap. Filling the
   frame with sparser rig regions (or dimming the room fill behind the swarm) attacks the real
   axis — local codeword competition.
4. **Count bursts by union + mechanism flags, not single-burst totals** (257–487 swing at constant
   photometry), and anchor cross-burst geometry on parallax-free pairs (r2↔r3) rather than the
   room NCC, which the room's furniture decoys (481 near-tied peaks).

## Verification trail

- Instrument reproduces the published 0000-17h43 console counts exactly (317 @40/6, 448 @25/6).
- Image-source parity 0.0 on all masters; 25/25 SOI/EOI/PIL per burst; cwcN honored (max id 599,
  r4 597).
- Base-gate claims re-derived twice by independent paths (CLI run + sweep runner) — identical
  counts (287/440/487/257).
- All tables above computed from `/tmp/improve_ios/{claims_rN,anchors_rN,bucket_audit,
  exposure,union_matrix,union_spread,pair_agree,final_aggregates}.json`; per-plane scoring ran only
  in subprocesses (fp16 (600,H,W) tensors on /tmp tmpfs, freed per run); no git, run dirs untouched
  (staging frames.txt + outputs live in /tmp/improve_ios).