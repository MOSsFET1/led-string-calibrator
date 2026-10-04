# S14R-0003E E4 decode — union(L), loss census, optimal L, opportunity list

Written 04 Oct late, subagent decode campaign on tonight's
`S14R-0003E` battery (exp 699.97, aem continuous, ev −1, 406×720,
first E4-only battery at the retuned tear thresholds 26/8.5).

## Provenance

- **Source: run dirs alone** — `runs/daemon/runs/s14r0003e-r2..r5/`
  (jpg + meta.json each; 24 coded planes `cwc:rN:p00..p23` + 1 master,
  labels `cwc:rN:*`). All 100 frames wire-verified BEFORE analysis
  (`analysis/e4_0003e_extract/scored.json`: EOI + PIL + WH + sha vs
  capture.txt — 100/100 green). No wire re-derivation was needed and
  none was done; `capture.txt` was only the sha reference.
- **Driver**: `analysis/decode_e4003e/e4003e_decode.py` — the proven
  photometry_v2 run-dir loading pattern over the CLI's own machinery
  (`tools/cwc_pos_decode.py`: register_direct + ncc_refine fullres-rad 4,
  frozen 12-of-24 bank, N=600, amp 40, margin 6, adaptive mask
  min(100, max(45, 1.12·histMed)), suppress 7 px, max-amp-per-codeword
  dedup). Tonight's run dirs carry no `<tag>_frames.txt`, so the CLI's
  own `main()` cannot be invoked directly; parity of the driver
  machinery was instead proven **EXACT vs the CLI-identical v2 outputs
  on 0003c**: on run8 AND run9 the driver reproduces the shipped
  `run8_ledpos_v2.json`/`run9_ledpos_v2.json` id sets bit-for-bit
  (identical ids, amp, margin, and site coords, max Δ amp 0.0,
  Δ margin 0.0, Δ site 0), and re-derives the reference per-L counts
  412/454 exactly. The CLI cannot be run on tonight's dirs only because
  of the missing frames.txt input format — the decode path is the same
  proven one.
- **r5 torn flags are FALSE** (meta torn:1 + tm on p03/p04/p06/p08/p17)
  — chroma-threshold artifact at the retuned thresholds; visually clean
  and decoded like every other plane. Measured confirmation: their
  per-plane gains k sit inside the epoch's family (0.633 vs range
  0.633–0.651; the 0003c true tears were k outliers 2.0–4.1). No
  planes dropped anywhere.

## Task 1 — per-epoch confirmed + stats (n=600)

| epoch | L | confirmed /600 | % | amp med [min] | margin med [min] | histMed → eff thr | wall med | k = amp/(255−wall) | mask px | walk sites | pre-gate contested |
|---|---|---|---|---|---|---|---|---|---|---|---|
| r2 | 80 | **423** | 70.5 | 68.5 [40.2] | 40.2 [15.4] | 40.5 → 45.4 | 209.7 | 1.585 | 142080 | 121779 | 121235 |
| r3 | 100 | 383 | 63.8 | 64.3 [40.0] | 37.1 [14.0] | 50.5 → 56.6 | 212.1 | 1.457 | 140638 | 122102 | 121612 |
| r4 | 120 | 385 | 64.2 | 65.8 [40.0] | 37.9 [7.7] | 55.5 → 62.2 | 212.8 | 1.577 | 140243 | 121411 | 120906 |
| r5 | 150 | 398 | 66.3 | 68.2 [40.2] | 39.6 [9.5] | 54.5 → 61.0 | 214.2 | 1.622 | 140274 | 120337 | 119790 |

All gates exactly honored (min amp ≥ 40.0, min margin ≥ 7.7); max
confirmed id < 600 in every epoch (no cap phantoms). Per-plane k
(kbgs) tight in every epoch (r2 0.58–0.61, r3 0.49–0.56, r4 0.57–0.59,
r5 0.63–0.65) — no torn/truncation-style k outliers anywhere.

Rig fixed across epochs: site-identity check on the 352 ids confirmed
in all four epochs → anchor L1 displacement med **0.0** (the max 277 is
one suppressed-neighbor relabel, 1/1056 pairs; all others ≤ 4).

## Task 2 — union(L) + comparison vs 0003c reference

Unions by led id tonight (pairs / triples / 4-burst):

| pair | ∪ | | triple | ∪ | |
|---|---|---|---|---|---|
| r2+r3 (80+100) | 430 | | r2+r3+r4 | 440 | |
| r2+r4 (80+120) | 435 | | r2+r3+r5 | **442** | |
| r2+r5 (80+150) | **437** | | r2+r4+r5 | 438 | |
| r3+r4 (100+120) | 409 | | r3+r4+r5 | 420 | |
| r3+r5 (100+150) | 418 | | **4-burst union** | **443** | |
| r4+r5 (120+150) | 405 | | never seen in any L | **157** | |

**Verdict: tonight lags the 0003c reference everywhere except L80, and
the L-ladder ordering is overturned.**

| L | tonight | 0003c ref | Δ |
|---|---|---|---|
| 80 | 423 | 412 | **+11** |
| 100 | 383 | 454 | **−71** |
| 120 | 385 | 390 | −5 |
| 150 | 398 | 411 | −13 |

| union | tonight | ref | Δ |
|---|---|---|---|
| 80+100 | 430 | 458 | −28 |
| 80+120 | 435 | 456 | −21 |
| 80+150 | **437** | 443 | −6 |
| 100+120 | 409 | 475 | −66 |
| 100+150 | 418 | 462 | −44 |
| 120+150 | 405 | 456 | −51 |
| 4-burst | 443 | 481 | −38 |
| never-seen | 157 | 119 | +38 |

The reference's operating point (L=100 best single, L=120 best
complement, r9∪r10 = 475) does NOT reproduce: tonight the best single
is **L=80**, the best pair is **80+150** (437), the best triple
80+100+150 (442), and pairs centered on L100 are tonight's WORST
(100+120 = 409). The 0003c L100 advantage (+42 over next best) was not
re-created by this battery; L100 is tonight the WORST single epoch
(−71).

## Task 3 — per-epoch loss census (misses, 600−confirmed)

| L | misses | mask-stage | contest (rival eats) | of which walk-eaten (amp≥40 at a codeword-owned pixel) | suppressed (7 px window) | amp-gate | margin-gate |
|---|---|---|---|---|---|---|---|
| 80 | 177 | 0 | 131 | 25 | 46 | 0 | 0 |
| 100 | 217 | 0 | 170 | 25 | 47 | 0 | 0 |
| 120 | 215 | 0 | 175 | 25 | 40 | 0 | 0 |
| 150 | 202 | 0 | 170 | 25 | 32 | 0 | 0 |

(0003c reference census for comparison: mask 0–1, wall/amp 1–24,
suppressed 23–45, contest/other 3–35, no-anchor 119.)

Read:

- **Mask-stage ≈ 0 at every L** — same as 0003c: the adaptive mask rule
  is not binding at exp 699.97 (eff thr only relaxes to 45–62).
- **amp-gate and margin-gate deaths ≈ 0** — every miss has ≥40 of
  amp at its best-evidence site or is eaten before gate test. The
  photometric gates are NOT what tonight loses ids to.
- **The miss side is ~85% contest/suppression** — the interference
  law, not photometry: 131–175 contest ids + 32–47 suppressed ids per
  epoch. That is much heavier than 0003c's contest/other (3–35):
  tonight's misses carry large NEGATIVE margins at their best pixel
  (median ≈ −80) — a rival codeword owns the evidence pixel outright.
  Every walk-eaten contest id has an amp ≥ 40 at a pixel owned by a
  rival; the CLI's argmax walk then discards it.
- **Never-seen ids are not photometric-dark either**: their best-evidence
  amps sit at 44–56 (p25–p75) — above the amp gate — but with massive
  negative margins. They are rival-contest victims, consistent with
  the never-seen hard floor being colocated/interior lamps whose
  evidence is interleaved with a stronger occupant's.

Which lever to attack per L: **the contest/suppression redesign
(page-parity conflict handling in the CLI) is the top single-burst
lever at EVERY L tonight** — worth ~1.3–1.4× over the amp lever
(which measures zero recoverable population tonight: no missing id
fails amp-gate at an unsuppressed in-mask owned site in any epoch).

## Task 4 — OPTIMAL L verdict for the CLEAN-SET (single-burst) goal

**Tonight's winner: L=80.** 423 confirmed, and the best pair complement
to it is L=150 (union 437), with L=100 the third leg (442). Amp medians
do NOT drive the verdict (all four sit 64–69, same range as 0003c):

| L | amp med | amp p5 | margin med |
|---|---|---|---|
| 80 | 68.5 | 44.9 | 40.2 |
| 100 | 64.3 | 42.3 | 37.1 |
| 120 | 65.8 | 42.1 | 37.9 |
| 150 | 68.2 | 41.8 | 39.6 |

The 0003c statement (L=100 best single, L=120 best complement) is
**overturned tonight** — but with an important caveat: tonight reproduces
the reference's *amplitude structure* exactly (the wall law k ~1.5–1.7,
amp med 64–69 vs 70–92 at 0003c) yet does not reproduce its L100 peak
(454 → 383, same driver, same gates, parity-proven). The L→count link
in this regime runs through **suppression dynamics, not brightness**:
per-L suppression counts tonight are 46/47/40/32 and the contest class
is ~10× reference. Between two identical-photometry batteries one night
apart, the count ordering flips by >10% (423 vs 383 at the same two
Ls). A single fixed L therefore does NOT look like a stable operating
point at exp≈700; the count is dominated by which rival codeword wins
each shared 7 px window, and that ordering is not L-controlled
(measured: shared-352 ids' amp quartiles move <10% between Ls, yet
confirmed counts move +40).

**Clean-set recommendation**: if one L must be fixed, take **L=80**
(highest confirmed tonight AND the best complement anchor in every
pair table r2+rX = 430–437 > r3+rX, r4+r5). A small fixed set
{80, 150} → 437 tonight (best pair measured), {80, 100, 150} → 442.
But treat the L choice itself as weak evidence: the per-L spread
(383–423) is within the night-to-night variance this pair of batteries
has now demonstrated (454→383 at L100).

## Task 5 — OPPORTUNITY LIST, ranked by expected single-burst gain
(populations measured on tonight's frames)

1. **CLI contest-redesign (page-parity conflict handling)** — walk-eaten
   class at any L: **25 ids/epoch** have amp ≥ 40 at a codeword-owned
   in-mask pixel whose argmax is a rival; the CLI eats all 25; the page
   would flag ≤5 px rivals as conflicts instead of dropping. Measured
   cross-epoch recovery of these: 6–7 of the 25 are confirmed in some
   other epoch tonight (suppression-dynamics victims — recoverable
   without redesign by union), 18–19 are never-seen (structural).
   Expected single-burst gain if the redesign converts the rival-owner
   sites into conflict entries like the page: **up to +25 on a single
   burst** (all 25 have passing amp; the margin gate is the blocker the
   redesign would bypass — page-parity demotes rather than eats).
   This matches 0003c's "23 of 27" class anatomy: tonight's measured
   same-class population is 25 at every L (stable), of which 23–24
   never confirm under the current CLI anywhere in the battery.
2. **Union-pair operating point shift** — best pair 80+150 = 437
   (+14 over the best single) at tonight's data. If the schedule allows
   exactly two bursts: L=80 then L=150 (NOT L=80 + L=120 as 0003c's
   ranking suggested; L=150's unique contribution to r2 is 13 vs
   L=120's 14, but the pair union measured directly favors 80+150,
   driven by L150 confirming more of L80's suppressed class).
3. **Suppression-window arbitration (cwcSuppress 7 → per-codeword
   ownership)** — 32–47 ids/epoch are suppressed-class (their best
   codeword-owned in-mask pixel has amp 48–87 and margin 25–49 — clean
   evidence — but sit inside a confirmed site's 7 px window, i.e. by
   construction ≤ 3 px from a stronger rival's claimed site: the
   colocated-pair signature). Of tonight's r5 (L=150, the smallest
   suppressed class at 32), 14 are recovered by another epoch, 18 never.
   A per-codeword (rather than per-pixel) suppression window would
   recover the colocated subset; expected single-burst gain bounded by
   the epoch's suppressed count (32–47; the colocated-pair subset is
   most of it, since every suppressed id's evidence pixel sits inside a
   claimed window).
4. **Ladder-order / second-epoch choice for the probe** — the in-order
   cumulative union (running order L80→100→120→150: 423 → 430 → 440 →
   443) is the optimum order tonight too (matches the best of all
   orderings). Marginal steps: +7, +10, +3 — flatter than 0003c's
   +46/+19/+4; the "L=120 brings 17 unique ids" complement effect did
   not reproduce (tonight L120's unique contribution to any set is ≤14
   and zero over the 80+100+150 triple: 442 → 443).
5. **NOT worth attacking tonight (measured ≈ 0)**: mask-stage losses
   (0 at every L), amp-gate losses (0), margin-gate losses (0 on
   unsuppressed owned pixels). No photometric-gate knob move gains
   anything on this battery.

## Files

- `analysis/decode_e4003e/e4003e_decode.py` — driver (v2-parity pattern
  over tools/cwc_pos_decode.py internals + loss census)
- `analysis/decode_e4003e/s14r0003e-r{2,3,4,5}_ledpos.json` — per-epoch
  ledpos + stats + census (CLI `leds` list inside; run-dir provenance)
- `analysis/decode_e4003e/s14r0003e-r{2,3,4,5}_leds.json` — per-epoch
  confirmed-id lists
- `analysis/decode_e4003e/s14r0003e-r*_ledpos_leds.json` — legacy-name
  copies from the first driver pass (same led lists, no census)
- `analysis/decode_e4003e/opportunity_20261004.json` — per-epoch
  opportunity populations
- `analysis/decode_e4003e/summary.json` — counts, unions, per-L amp
  medians, loss census table, reference block

Hygiene: runs/ read-only (ledpos.json went beside the analysis dir, not
into the run dirs); no git commands; bench_daemon untouched;
/dev/ttyACM0 never opened; frozen bank used as-is; exp read directly
from metas (exp=699.97 all 4 epochs).

---

EXECUTIVE SUMMARY (S14R-0003E E4 battery decode, 04 Oct night — for the operator)

1. Decoded all 4 epochs (r2=L80/r3=L100/r4=L120/r5=L150, n=600, CLI-parity machinery; driver proven EXACT vs the shipped v2 decode on 0003c run8+run9 — identical ids/amp/margin/sites).
2. Confirmed: 423 / 383 / 385 / 398 (L80/L100/L120/L150). vs 0003c reference 412/454/390/411 → L80 +11, L100 −71, L120 −5, L150 −13.
3. Unions: best pair 80+150=437, best triple 80+100+150=442, 4-burst 443, never-seen 157. Every union is BELOW reference (−6 to −66; 4-burst −38, never-seen +38).
4. The 0003c operating point is OVERTURNED: L=100 (ref's best by +42) is tonight the WORST epoch; best single is L=80; L120's best-complement effect did not reproduce (adds only +1 to the 80+100+150 set).
5. Loss census flips the diagnosis: mask-stage 0, amp-gate 0, margin-gate 0 at EVERY L — zero photometric-gate losses. Misses are interference: contest 131/170/175/170 + suppressed 46/47/40/32.
6. Every contest miss carries amp ≥ 40 at a codeword-owned pixel with a rival's argmax on top (margins ≈ −80) — the known CLI-eats-contest-losers lever, measured tonight at 25 ids/epoch in the walk-eaten class plus the full contest mass.
7. Never-seen ids (157) are NOT dark: best-evidence amps 44–56 (above gate) with huge negative margins — rival-contest victims, same anatomy as the hard floor.
8. Clean-set verdict: fix L=80 (423 + best complement anchor in every pair); fixed set {80,150}=437 / {80,100,150}=442. But treat Lchoice as weak — same-photometry night-to-night spread (454→383 at L100) exceeds the per-L spread; suppression dynamics, not brightness, dominate counting.
9. Opportunity ranked: (1) CLI page-parity contest redesign, up to +25 single-burst, 18–19/25 never-seen otherwise; (2) pair 80+150 (+14 over best single); (3) per-codeword suppression window (32–47 colocated-window ids/epoch, clean amp/margin evidence); (4) ladder order still optimal in-battery; (5) photometric gates: nothing to gain tonight.
10. Hygiene: all 100 frames wire-verified (EOI+PIL+sha) pre-analysis; r5's 5 torn:false-error flags decoded in-place (k in-family 0.633 vs 0.48–0.65, no 0003c-style k outliers); runs/ read-only, no serial, no git, no daemon touches.