# S14P-1910 handheld-3 review — brief (30 Sep, console-side; no page/firmware/docs edits)

## Headline
Shipped decoder (parity console, §10d refinement, gates 60/25): **168/200, zero dups**, amp med 134.7,
pitch med 21.1 px, missing 32 (29 besides the tripod-hidden [46,90,91]).
Offline probe that swaps ONLY the registration input — the page chain's totals replaced by direct
per-plane phaseCorrelate (same sqrt-luma+Hanning recipe the console uses for GT), decoded with the
identical scorer/gates/mask — gives **194/200, zero dups**; adding mask 175→150 and margin 25→10
gives **197/200, zero dups, missing exactly [46,90,91]** — the tripod gate result itself.

## Where motion sat vs budget
- Chain totals span dx 4.3 / dy 3.6 px over 18 planes (~5.7 px net) — **~5× tighter than 1908's ~30 px
  pan**; GT inter-plane steps med 1.2 (max 2.2) px, smooth slow drift, **no jerk events**; phaseCorrelate
  conf 0.76–0.84 on all planes and chain conf 0.72–0.90.
- Per-plane chain remainders med 0.47 (max 3.18) px — the ~5 px §4 residual budget was **never breached**.
- Chain-vs-GT error med 1.25 / max 2.28 px. Root cause of the misses: not budget breach but this
  1–2 px bias corrupting per-plane stacked profiles, which at 20 px pitch flips the codeword race on
  crowded pairs and fills the map with ghost-amp sites (the 1908 mechanism at lower amplitude).
- MOTION FLAGGED was over-conservative here: nothing in this run violates the thresholds it checks.

## Mechanism census (29 misses, all repaired offline by the GT-registration probe unless noted)
1. **Codeword-steal/suppress at crowded d6 pairs — 22 of 29** (16,22,25,30,39,44,51,53,55,57,68,74,
   75,85,98,100,108,109,120,122,127,147,149,153,175,179 minus the pure-margin ones): ghost magnets
   codewords 82 (48 pre-dedup sites at 40/10 on the GT map), 197 (17), 164 (16), 145 (13) claim
   OTHER LEDs' true sites 1–6 px away and suppress them; some claims die only in dedup AFTER
   consuming the suppression window. Also 24 lost its site to codeword82 by ONE pixel.
2. **Chain-stack margin collapse** (subset of above where the site was offered and no steal occurred:
   e.g. 25 margin 2.7→57, 57 margin 1.5→78, 85, 100, 179): profile noise from ±1px mis-stacking.
3. **Mask-kill at this pose** — 50, 155, 179 (+47 at stricter gates): edge-on tiny cores; blur luma
   143–172 under the 175 mask; the tripod pose lights the same LEDs fat (blur >200). Mask 150/140
   recovers them; the plan's codeword-103 phantom measured amp 40 on this run — not a threat here.
4. **27 — the only true remaining miss**: d6-pair with codeword 116 at (159,436), 6 px away, both
   real and lit; margin squeezed to 11.8. Fold geometry, not motion.
5. **46/90/91**: tripod-hidden trio; no lit core at any candidate in this pose either.
6. Health: exposure pinned (exp=300.03 all frames, ev −1), k 1.048–1.073, master clean, 19/19 frames.

## Lever ranking (recovered / ghost numbers, measured on THIS burst)
| lever | count | dups | notes |
|---|---|---|---|
| shipped chain 60/25 | 168 | 0 | baseline, 29 missing-ex-trio |
| gates sweep on chain 45..90 × 10..40 | 153–177 | 0 | noise-floor insensitive — never the fix |
| **L1: per-plane direct phaseCorrelate tots** | **194** | 0 | +26 recovered, path-resid med 1.6 px, ON-plane lit frac med 1.00 |
| L1 + mask 150 (60/25) | 195 | 0 | +LED50; path-suspects 10→14 (curve zones) |
| L1 + mask 140 | 196 | 0 | +LED47 (amp 147.7/marg 55.2); skip if mask floor concerns |
| margin 25→10 on L1+150 | **197** | 0 | +LED27 (margin 11.8); amp gate redundant (all ≥76) |
| L5 retake-guard | — | — | flags ZERO planes (tightened: p09/p14); loss is systemic, retakes would NOT have helped |
| 2-segment re-anchor probe | 178 | — | per-segment stacks lose codeword dimension (14/55 cross-seg agreement) — rejected |

## Recommendation — the accept-at-this-motion recipe
**Keep the §4 handheld gate reachable at THIS motion level by replacing chain totals with direct
per-plane phaseCorrelate registration in BOTH decoders** (page cwcChain pre-shift seed = 0,
remainder measured vs master — the plan §3 already says plane-vs-master never degrades with raw
shift): page + console decode with gates 60/10–60/25 and mask 150 → expect **194–197, zero dups,
missing ⊆ {27,46,90,91}**. Dup/ghost risk stays the shipped dedup (strongest-site-per-codeword);
the ghost magnets claim fewer sites once profiles are clean. Expect the recipe to hold up to ~2×
this motion (6–10 px/net, steps ≤4 px) since budget wasn't the binder. 27 remains a fold-crowding
case — accept as the fold-class exclusion with 120 (pitch authority), or fix with the §8 export list.
Do NOT adopt per-segment re-anchoring; the shipped motion guard did not misfire on data of this
class, keep it as-is.

## Verdict
**YES — the §4 handheld gate can pass at this motion level**, not by tightening operator discipline
(already 5× better than 1908) but by decoupling per-plane registration from the chain: direct
plane-vs-master phaseCorrelate totals + mask 150 + gates 60/10 reproduces the tripod gate 197/200
zero-dup offline on run 3. Structural next steps queued in the plan (§5 re-anchor, §9 landmarks,
§11 ack pacing) remain valuable for WORSE motion, but are not required for runs of this class.