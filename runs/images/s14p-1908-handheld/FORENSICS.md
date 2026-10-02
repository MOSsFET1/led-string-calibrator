# S14P-1908 handheld burst — console-side forensics (30 Sep)

Burst: operator HANDHELD on the phone page (build S14P-1908), 19/19 frames
decoded (cwc:r1:p00..p17 + master), pulled late via LOGA+BRAMP
(pull_handheld.py). Frame store was ship-once-cleared at burst start, so this
ring holds ONLY this burst. No cwc_stats.json — CWCSTATS rides in the frames
log (chain + decode counts read from there).

## What was run
- `tools/cwc_pos_decode.py <run> --amp-gate 60 --margin-gate 25 --save-json
  --save-overlay` → 39/200 LEDs, ZERO dups, amp med 105.1. Overlay shows the
  39 boxes scattered over the frame; consecutive-id pitch median 167.7 px —
  NOT serpentine (tripod pitch ~20-28 px): the claims do NOT form the string.
- Replica of the page's chained decode (sim_mask175.py with the SHIPPED
  chain from CWCSTATS used as tots) → 152 raw sites / 100 distinct LEDs.
- Replica with tots = phaseCorrelate-rounded shifts → 196 raw / 142 distinct.
  (phaseCorrelate GT and the page chain AGREE within ~1-3 px over all planes.)
- Replica with tots = 0 → 34 raw / 22 distinct. So ±1-2 px tot rounding swings
  the accepted count 22→142: the decode runs at its noise floor on this burst.

## Findings
1. **Motion**: real handheld pan totalled ~±30 px (dx 28.5, dy 25.4 across 18
   planes, page-chain/phaseCorrelate agree); per-plane remainders 3-9.5 px
   (median 4.5). At cadence 0.24-0.32 s that is 3-5x the planned 1-2 px/frame
   drift. Residual budget ~5 px was consumed plane-to-plane.
2. **Registration tracked it** (chain conf 0.57-0.81, phaseCorrelate conf
   0.55-0.82) but only to ~±3-7 px — at/over budget. NOT a chain failure.
3. **Bit-read profiles degraded**: at decoded sites the per-plane stacksig
   profile is not bimodal with GT warp (ON/OFF means same sign in a few,
   spread 32-209); with tot=chain 15/27 claimed profiles bimodal. Common-mode
   content mismatch (LED core in plane vs background at master after a
   ±3-8 px registration slip at pitch ~20 px) → huge positive stacksig energy
   everywhere: EVERY codeword finds amp 140-230 somewhere (blur luma at that
   argmax can be 43!). Gates (amp 60/margin 25) then admit ghost claims;
   console got 39, page claimed 118 — both at noise-floor sensitivity.
4. Tripod reference (1906-phone-pos1): page-recipe NCC conf 0.95 at (0,0) all
   planes; handheld page-recipe NCC confs 0.37-0.65 at (0,0) — the chain's
   remainders recovered only a fraction of the real motion.
5. Burst health itself was good: exposure pinned (exp=200.02 every frame),
   k gains 1.02-1.06 (dim room), 19/19 frames, master post-500ms-flush clean,
   labels unique. NOT a firmware/protocol failure.

## Conclusion
The poor result is expected behaviour of the current recipe when handheld
motion exceeds the ~5 px residual budget: registration keeps up coarsely, the
codeword score field fills with common-mode noise, gates lose discriminability
(margin med 30 here vs 121 tripod), and the claimed count becomes unreliable
(39 console vs 118 page on the SAME data). No firmware/protocol defect.

## Recommendation
Re-shoot with movement discipline (slow, small movements; brief pauses) and
add an in-page residual/conf HEALTH GUARD on the result view (flag any plane
with |tot_p - tot_{p+1}| remainder > ~5 px or conf < 0.6, so a degraded burst
is visible before the operator walks away). §5's re-anchor to fresh master on
health triggers and §9's blob-landmark registration lever are the queued
escalations if slow-motion handhelds still fail. Do NOT treat this run's
LED positions as data.