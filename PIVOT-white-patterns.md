# Pivot decision: colour → white-pattern detection (Sep 22/23, Nellie)

## The evidence that closes the colour era

Operator's S12 field report: large blue patch on the right with nothing
blue in it. Offline analysis of the pulled frames CONFIRMS the mechanism
and adds a second, worse one:

1. **Cross-channel leakage into the scan channel.** During the BLUE scan,
   the field is red+green — but on camera that field's blue-excess is NOT
   zero: mean −48, d90 −15, max +17, varying ±40+ between the reference
   and accent grabs (AWB gain re-balance). The die residual (peak ~101)
   competes with a moving ±40 noise floor: that is the right-side patch
   (2620 px, bbox 44% of frame width, upper-right quadrant only — the
   same region the operator saw). Same risk for green (mean −68, d90 −21).
   The complementary field fixed CHANNEL SATURATION but introduced
   FIELD-COLOUR WOBBLE: the hue-excess of the field itself moves between
   grabs, and that movement is indistinguishable from a die in a
   whole-frame diff.
2. **Saturated die hues mis-classify** (green die read as 'R' at page
   level; hue-of-mean is meaningless in the bloom).

Verdict (operator + evidence): colour as the DISCRIMINATOR is unreliable
on phone cameras (clipping, AWB re-balance, channel bleed). Pivot to
**white-on/off patterns** — Twinkly's proven scheme — with everything we
have built since kept: temporal diff, complementary-field-style exposure
discipline (constant-luminance paints), box/transport machinery, frame
store, offline pipeline.

## The new detection scheme (S14)

Every survey frame is now BINARY: LEDs are either bright-white (ON) or
dark (OFF). The die = the pixel set that changes ON↔OFF between a pair of
grabs taken ~2Δ apart.

- Pair design per Twinkly practice: alternating halves. Survey = a
  sequence of K bit-planes; in bit-plane t, every LED whose index bit t
  is 1 is ON. A die's K-bit signature = its identity (log2(N)+1 planes
  cover 200 px). The all-ON plane anchors position; the all-OFF plane
  measures ambient.
- Detection per plane pair: luma diff, threshold, connected components,
  the S11 merge, and the S13 area-cap (≤1% of frame) + dominance bar.
- Camera motion: same exposure as ever (AE pinned by the ~half-ON
  population — total luminance constant across bit-planes BY DESIGN, so
  AE/AWB see a static scene while the pattern flips: better than the old
  build's per-pixel march where AE saw full-on→black swings).
- The dark-pair experiment (proven AE window: ~50% recovery in 300 ms)
  gives the ON/OFF pair timing: paint ΔP, settle ≥300 ms, grab.

## Why this is not going back to the dead end

The per-pixel diff march failed because identity = "which single LED is
lit" needed 100 s of tripod stillness and a march tracker. Binary
patterns carry identity in the CODE, each plane is self-contained, a
shaky frame degrades one bit of one blob's signature (recovered by
parity/second pass), and 200 px need ~9 planes ≈ 20-30 s. The all-ON
plane's blob positions + per-plane ON/OFF membership give the 2D point
cloud; occluded dies interpolate from ID neighbours.

## S14 work list (build next session)

1. Firmware: 'bits' command — paint from a bitmask (bit t of LED i) at
   brightness b (compacts 150 px to ~19 bytes vs 1537 B of hex).
2. Page survey: K = ceil(log2(npx)) + 1 planes + all-ON + all-OFF;
   per-plane grab; per-die signature assembly by blob position tracking
   across planes (nearest-blob matching with the previous plane's
   positions; positions come from the all-ON plane).
3. Detector: luma-diff between plane t and plane t+1 is NOT needed —
   each plane is compared to the all-ON and all-OFF planes directly
   (ON if diff vs OFF > thr at the die position; OFF if diff vs all-ON
   < −thr). Robust to AWB: luma only, no hue.
4. Acceptance: area cap + dominance + per-die margin log.
5. Keep: complementary-field code dormant behind CFG (compField=0
   default), dark-pair (bit-plane timing uses the same dpGap machinery),
   WS auto-recycle, frame store, EV/log pulls.

## What we keep from the colour era

The frame store + offline pipeline (this pivot decision is EVIDENCE-BASED
because of it), the EV-verified trigger loop, the AE-pump measurements,
the AE-lag window, and the status-LED/transport fixes.