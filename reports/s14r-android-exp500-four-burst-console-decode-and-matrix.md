# S14R-0000 android bursts (exp=500 regime), 4-burst console decode + cross-burst per-id matrix

Bursts: `runs/daemon/runs/s14r-and-r2 r3 r4 r5` (wire capture.txt >=17:56, today, android, each 24 planes
+ master, 25/25 magic-clean, exp=500.05 aem=continuous ev=-1 fd=0.00 on every frame of every burst).
Console decode via `tools/cwc_pos_decode.py` **subprocess only** (one burst per process, scratch
`/tmp/improve_and/`): gates mask 100 / amp 40 / margin 6, `--fullres-rad 4`, `--bank tools/codewords_12of24.json`,
`--n 600`. PHONE CWCSTATS verified ABSENT for the whole 17:56-18:10 window (last S14R-0000 CWCSTATS 17:43:08;
store drained without decode lines) — console is the only decode for these bursts.

Hygiene (all four): max claimed id 599 (cwcN=600 honored, no cap phantoms); min confirmed amp 40.1-45.0
(gates honored); interpreter-side instrument (stacksig rebuilt from the CLI's own `direct_shifts.json`, points
scored only, no (N,H,W) tensor in-kernel) reproduces CLI amp with max|d| = 0.0 / 0.0 / 0.0 / 0.1 on 20 random
confirmed ids per burst; frames fed via synthesized `<tag>_frames.txt` from the pulled jpgs+metas, 25/25 decoded.

## Per-burst counts

| burst | confirmed /600 | amp med [min-max] | per-plane law k (amp/(255-wall)) | master histmed | s1/s2/s3 |
|---|---|---|---|---|---|
| r2 | 429 | 79.0 [40.3-166.7] | 0.87 | 100.5 | 144/120/165 |
| r3 | 549 | 89.0 [45.0-188.2] | 1.03 | 104.5 | 178/181/190 |
| r4 | 492 | 115.6 [40.1-223.1] | 0.94 | **74.5** | 169/180/143 |
| r5 | 387 | 71.9 [40.2-150.8] | 0.75 | 83.5 | 124/114/149 |

Union: **583 distinct ids confirmed at least once** (97.2%); never-seen 17. Hit histogram: 4-of-4 = 269,
3-of-4 = 178, 2-of-4 = 111, 1-of-4 = 25, 0-of-4 = 17.

## Cross-burst per-id matrix

Per burst fitted H(t→r2-ref) on shared confirmed ids (RANSAC 4 px): inlier fit resid med 2.0 / 1.8 / 0.8 px
(r3: 153/407 inliers, r4: 126/355, r5: 324/344); mapped per-id position spread across bursts med **5.7 px**,
p90 23.0 (168 ids >10 px; extremes >100 px = per-burst eaten-codeword/wrong-site wins, the base-gate 3-4%
class — never a fixed-site offset). Within-id amp spread on the 269 4-of-4 ids: med 59.2, p90 81.7 — amp is
NOT a stable cross-burst identity metric even with per-plane k normalization; wall-relative form is.

Conditional stability P(hit in u | hit in t): r3↔r4 96/86, r3↔r2 95/74, r5↔r3 95/67, r5↔r2 80/89,
r4↔r2 72/83, r5↔r4 64/81 (base rates 72/92/82/64). The matrix is burst-symmetric within ±10% except r3↔r5
and r4↔r5 pairs, where r5 (weakest burst, 387) drags both directions — an exposure/brightness property of
that view, not identity structure. Full per-id table: `/tmp/improve_and/matrix_600.csv`
(id, 4 bits, per-burst amps, ref-frame anchor, spread).

Never-seen 17 (2.8%): string-1 11 (74, 130, 188-198 block), string-2 5 (267, 314, 316, 396, 399), string-3 1
(578). Anatomy: 5 bright-wall (ring bg ≥200 in ≥3 bursts: 74, 130, 267, 314, 316), 11 of the 12 bright-wall/
mixed sit at anchors whose master blob peaks 255 in r2/r3/r5 (lamps present, ON-planes clipped — exposure),
2 low-contrast in all views (191, 578), 2 view-dark (396, 399: bg 14/7, peak 20/9 in r3/r4 but 255 in r2/r5).
Anchor positions interpolated from nearest confirmed ids (≤12 id-neighborhoods, 1/dist weights).

## Bright-wall buckets, miss% per burst (anchored misses; margin 6, mask 100)

| bucket | r2 (n) | r3 (n) | r4 (n) | r5 (n) |
|---|---|---|---|---|
| <80 | 15% (82) | 3% (122) | **37% (177)** | 15% (107) |
| 80-119 | 18% (67) | 5% (58) | 17% (108) | 35% (62) |
| 120-159 | 35% (142) | 8% (103) | 9% (176) | 36% (115) |
| 160-199 | 27% (120) | 13% (147) | 6% (137) | 31% (111) |
| 200-239 | 35% (188) | 10% (168) | — | 50% (183) |
| >=240 | — | — | — | 41% (22) |

The monotone wall law of the earlier rounds is BROKEN at this exposure: r4 (darkest view, histmed 74.5, zero
≥200-bucket anchors) misses hardest in its DARKEST bucket (<80: 37%) — mask-stage (master blur < 100) kills
dim lamps against a dark frame; r4's 77 anchored misses are mask-class at wall med 21.5 while their master
blob peaks still read 136 (lamps present, mask floor too high for that view). r4's confirmed bucket amps confirm
the regime flip: med 165.6 at wall <80 vs 90.4 at 160-199. Bright-bucket death (200-239: 50% in r5 vs 35% r2)
still tracks the wall, softened by the 500.05 exposure vs r1's 699.97 (80% at r1).

## Miss mechanism split at anchors (instrument, exact amp parity with CLI)

| class | r2 | r3 | r4 | r5 | reading |
|---|---|---|---|---|---|
| amp<40 | 122 | 40 | 22 | 171 | bands 0-8 / 8-25 / 25-40: 27-52-43 (r2), 17-19-4 (r3), 11-9-2 (r4), 33-98-40 (r5) |
| contest (argmax≠self at anchor) | 16 | 4 | 8 | 16 | 130/162 (r2), 44/44 (r3), 73/85 (r4), 132/193 (r5) winners themselves confirmed in-burst |
| mask | 15 | 5 | **77** | 20 | dark-view signature (r4) + true-hidden minority |
| suppressed/starved (amp≥40, mg≥6, lost site) | 7 | 2 | 1 | 3 | CLI global ±3px suppression + per-codeword dedup |
| thin margin d≥8 | 11 | 0 | 0 | 3 | instrument-thin |
| anchored total | 171 | 51 | 108 | 213 | = per-burst misses (all anchored: 583 anchors + 17 interp) |

Exposure lever (per-plane law k_t×(255−wall) ≥ 40 predicted at anchor): crosses for **101/156** (r2),
**40/46** (r3), **31/31** (r4 amp-gate-class), **97/193** (r5) = **269/426 = 63%** of all amp-gate-class misses
across the four bursts would clear amp 40 with NO decoder change if ON-plane clipping were removed
(evBias re-applied at burst start + wall-fill/aim control). Consistent with r1/r2 round law refits
(k 0.58 @ 699.97, 0.97 @ 200.02, now 0.75-1.03 @ 500.05): the wall headroom multiplier is exposure-settable.

Colocated pairs (<5 px, both confirmed) per burst: 45/33/37/46 unique pairs, 161 across bursts, 42%
same-string, d med 4.1 px — interlaced adjacent lamps at 10-12 px pitch; the page's conflict-tolerant model
(flag, both confirmed) is the right semantics; CLI one-site-per-window silently drops one member of some.

## What guides the algorithm work (evidence-ranked)

1. **Exposure/on-plane clipping is 63% of the remaining gap** (269/426 amp-gate-class misses cross gate 40 at
   their own anchors by the wall law): evBias −2 re-applied at burst start (page-only ~10 lines) + operator
   wall-fill/aim discipline (r4 view). No gate change required.
2. **Adaptive/local mask threshold** for dark views: r4 loses 77 ids to mask-stage at wall med 21.5 whose
   lamps measure peak 136 on the master. Normalizing mask thr to per-frame histmed (or local contrast) is the
   single biggest r4-class lever; validate by sweep before promoting.
3. **Amp25 + position guard, not blanket relaxation**: the 25-40 band holds 43/4/2/40 = 89 recoveries; the
   17h43 audit proved 43-59% of unguarded relaxed-gate counts are far-orphan impostors — count only sites
   within ~6 px of a registerable anchor (this corpus's anchors: matrix_600.csv). The 8-25 band (159 ids)
   stays identity-grade-no; ship "evidence present, sub-gate" bookkeeping instead.
4. **Page-style conflict lists in the CLI** (demote, don't eat): recovers ~44 contest + 13 suppressed ids;
   colocated-pair semantics (flag both, keep both) already proven healthy on-page.
5. **Per-id ledger flags to ship**: never-seen-17 (bright-wall 5, low-contrast 2, view-dark 2, on-clip 8-11),
   plus the 396/399 view-visibility pair. These are sensor/view limitations, not decoder misses; ~35-55/round
   in every S14R round so far.

Artifacts: /tmp/improve_and/ (worker.py, analyze2.py, score_at.py, emit_anchors.py, anatomy.py, all_rows.jsonl,
analysis2.json, matrix_600.csv, anchors.json, mapped_*.json, mech_r2..r5.json, logs). Decoder outputs
ledpos.json + direct_shifts.json written by the CLI into each run dir (standard --save-json/--save-shifts);
synthesized <tag>_frames.txt (reconstruction glue for the frameless pulled dirs). Run jpgs/metas/capture.txt
untouched; no git.