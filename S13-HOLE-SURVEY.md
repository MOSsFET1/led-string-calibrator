# S13 — hole survey: all-on master − single-LED-off pairs (operator scheme, Sep 23)

## The scheme (operator's proposal, adopted)

Turn ALL LEDs on white at `holeB` (default 200), wait `holeSettle` (1 s) for
the camera AE to adjust, grab the MASTER frame; then per LED i: turn off just
LED i, wait `holeGap`, grab the PAIR, and diff master−pair to isolate LED i
as a DARK HOLE in the bright field. A bright #i box is drawn on the phone
camera view for review (misses drawn red M#i; a miss never erases a box).

## Why this beats the dead ends (evidence carried over)

- The per-pixel march died on camera motion lighting up every edge in every
  pairwise dark↔bright diff. Here BOTH pair frames are the same bright field
  (one LED of 150 toggles <1% of scene luma) — the AE-pump whole-frame-shift
  blob class (S12's 13,373-px blue blob) is excluded BY CONSTRUCTION.
- Contrast, not brightness: the residual is the LED's own contribution
  against a near-zero outside tail on a still bench — dominance margins far
  above the 0.5–1.9× accent-era margins.
- One LED per pair, self-contained: a bad pair wastes one LED, not a march.
  No tracker, no anchor, no ladder (the operator's "one lever" rule holds
  trivially — a miss has brightness, not geometry, as its lever).

## Implementation (S13-1900)

- FIRMWARE: stamp-only change (`PAGE_BUILD` S13-1900). The existing `frame`
  command already paints arbitrary per-LED arrays ("W" = white at b,
  null = black). `poc_survey` flashed 65% program storage.
- PAGE (page/survey.html): `holeSurveyRun()` behind `CFG.holeSurvey` (default
  1). Detection reuses the bench-proven `detectDiffBlobs` (signed diff,
  bloom merge, outside-tail d10 dominance) + the S13 area cap
  (`holeAreaFrac`, 1% of frame) as the giant-blob rejection. The master is
  a MEDIAN of 3 grabs (per-pixel); `holeMasterEvery` (default 10) re-grabs
  the master to bound slow drift/AF hunt; 0 = the operator's pure scheme.
- Review boxes: `holeBoxes` Map, double-stroked (dark under, bright over) so
  they read on the bright field; redrawn by the idle loop after the survey
  (staleness dims); cleared by Clear boxes.
- Evid (EV pull): `{build, px, allB, thr, domK, foundN, missedN, found[], missed[]}`.
  At 150 px the found+missed lists fit the 1100 B ship cap; at 200 px, if the
  evid ships "too big", the full verdict is in the page log (LOGP) — the
  per-LED lines are `h<i> FOUND pk.. n.. @(x,y) d10..` / `h<i> miss (blobs:..)`.
- Frame store: master grabs + every found LED's pair + every `holeFrameEvery`-th
  pair (16-slot ring; FRAMP pull). Full pair coverage would overflow — the
  per-LED log is the complete record.

## Console + offline verification

- tools/hole_session.py (system python3): STAT gate (requires S13-1900) →
  single-burst `CFG=..\nSCAN` → EV-change verification → FRAMP/LOGP pulls →
  runs/hole-<stage>-<ts>/. Stages: baseline (standing defaults), matrix
  (b 120/160/255 ×2, gap 150/600 ×2, masterEvery 0/25 ×2, standing params
  restored after).
- tools/offline_hole_verify.py (session kernel / numpy+PIL): decodes the
  pulled frames, RE-RUNS the hole detection independently on master−pair
  luma, and cross-checks against the page evid (agreement classes OK/OFL/PG!/MISS).
  Ground-truth filter for the matrix: accepted positions must coincide across
  repeat runs AND fall in the string's geometric band (old truth: band
  y≈1050–1250 at the tripod's previous position — re-derive from the E0
  found-list before judging any run).

## Experiment matrix (this session)

1. E0 baseline at standing defaults (b=200, gap 300, masterEvery 10, thr 30,
   domK 8). Operator reviews the boxes on the phone BEFORE the matrix runs.
2. Brightness: 120 / 160 / 255 ×2. Expectation to test: AE pins the bright
   field in every pair, so contrast (and detectability) should be
   brightness-insensitive EXCEPT for the hole's own depth (a dimmer field
   makes the LED's contribution a larger FRACTION of scene luma → deeper
   hole); watch foundN + offline peak depths.
3. Gap: 150 / 600 ×2. Catches latch/frame-staleness misses (150) vs
   extra-drift margin (600). The dark-pair era measured AE recovering only
   ~halfway in 300 ms — but that was a full-black transition; here the pair
   frame is near-identical to the master, so a short gap should suffice.
4. masterEvery: 0 (pure original) vs 25 vs standing 10. Tests whether
   master staleness is a real miss source at 150 px (~2 min runs).

Decision rules: constants are set INSIDE measured gaps, never at edges; a
parameter with no effect across 2+ verified runs is dropped back to standing;
a repeatable miss pattern (same LEDs every run) is geometry/occlusion, not
detection — verify against the phone image before touching thresholds.