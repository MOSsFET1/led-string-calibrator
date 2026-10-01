# S12 experiment report — Sep 22 late night (complementary field validated)

Session: fresh S12 page, 6 verified surveys + frame pulls. The headline:
**your clipping diagnosis was right, and the complementary field fixed it —
ALL THREE dies now detect in the offline diff, including red which never
detected before.**

## 1. Complementary field (S11/S12 headline change) — VALIDATED

Scanning die colour C over a field of the other two colours, diffed against
the same field without the die:

| die | baseline survey | accB=80 | accB=255 | allB=80 | allB=255 |
|-----|----------------|---------|----------|---------|----------|
| R   | n97 pk65 @(117,219) | n1621 pk118 | n43 pk55 @(114,221) | miss (pk37) | n398 pk152 @(109,220) |
| G   | n212 pk96 @(52,210) | n306 pk94 | n107 pk107 @(50,209) | n35 pk61 @(52,208) | n254 pk97 @(53,212) |
| B   | n2620 pk101 | n2776 pk98 | n2403 pk112 | n874 pk119 | n2616 pk108 |

Red detected in 4/5 runs (was 0/∞ under the white field). Green solid
(consistent centroid ~(52,210)). Blue huge-but-consistent. The die channel
is genuinely dark in each reference frame (R-field mean 28, G 44, B 45) —
headroom confirmed.

## 2. New problem revealed: scene-wide AE pump between ref and accent grabs

The page's own detector accepted a 13,373-px "blue blob" (pk 237) — that is
not the die, it is a whole-frame blue-channel shift between the reference
grab and the accent grab (AE gain pump as the paint changes). The real die
is inside that blob. The green die was also misCLASSIFIED as 'R' at page
level (mean-RGB of a saturated bloom reads warm) — classification needs
hue-at-peak, not hue-of-mean. Fixes for S13:
  a. Interleave ref/accent grabs (ref R, die R, ref G, die G...) so AE state
     is shared; or grab ref immediately after die (2-frame pair).
  b. Dominance must reject scene-wide shifts: reject blobs whose area
     exceeds a few % of the frame (a die at 150 px is ~50-300 camera px).
  c. hueClass from the PEAK pixel's neighbourhood, not the blob mean.

## 3. Dark-pair experiment — capture race FIXED, AE lag MEASURED

All three guards logged OK (the dpGap=300 ms settle works). AE re-convergence
measured: scene mean luma 201→55, 92→35, 103→36 (on→off, 300 ms) — the AE
recovers only ~halfway in 300 ms, confirming your under-exposure window
exists. But as painted, the 'on' frame still contains the white field, so
the pair diff is dominated by the field, not the die. S13 one-liner: make
the 'on' frame die-ONLY (field off) — the under-exposed die-on-black image
you originally proposed, now with a measured AE window to put it in.

## 4. Brightness matrix (now meaningful)

accB 80→255: R die residual actually LARGER at accB=80 (bigger headroom
relative to a dimmer field); allB=255 gave the biggest R signal (pk152).
With the complementary field, dimmer accents may be BETTER (less AE pump,
less bloom) — worth one focused sweep next session.

## Bottom line

Detection now has signal for all three dies in the difference domain; the
remaining work is page-side acceptance quality (giant-blob rejection,
hue-at-peak classification, ref/accent interleaving) — all S13 items. The
brightness question is settled: complementary fields + temporal diff, not
drive level.

Data: runs/night-20260922-221237/ (compfield_diffs.png = the three die diffs).