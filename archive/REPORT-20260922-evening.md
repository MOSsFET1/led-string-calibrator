# Day-2 experiment report — Sep 22 evening (Nellie)

Operator: results of tonight's session (S9 page + S10 box, your "go"). The
one-line summary: **the CFG channel was silently broken for every daytime
experiment — found and fixed; tonight's rerun got clean data, and two of
your three questions now have evidence-based answers.**

## What the experiments found

### 1. Your dark-pair idea (CFG.darkPair) — the AE race: INCONCLUSIVE (capture race)

The sequence ran (field pin → die-on grab → black grab, ~300 ms apart, per
colour, exposure logged per grab). BUT the on/off grabs came back
byte-identical (mean abs diff 0.0) — both grabs resolved from the same
video frame. The grabs are resolving faster than the LEDs can change state
through the WS round-trip. The frames also show the AE did NOT re-converge
during the dark window (exp stayed 300, med luma dropped to 11–14 = scene
still readable). The idea itself is NOT refuted — the measurement needs a
grab-gap (e.g. wait 250 ms between the paint and each grab, rather than
grabbing both immediately). One real observation: in the dark window the
whole scene went dark but the DIES were still visible as small bright
points (13 px ≥200 luma at the die position) — so an under-exposed
die-only image is achievable; the AE lag you hoped for does exist.

### 2. Brightness matrix — now with clean data: contrast, not brightness

All four combos verified and differenced offline. With AE pinning exposure
to the field, accent brightness 80→255 changed NOTHING material (die
residual peaks 41–44 in every combo; same 1-px blob). Field brightness
80→255 likewise. Conclusion stands from yesterday, now with proper data:
**the lever is die-vs-field contrast in the DIFFERENCE domain, not
absolute brightness.** The winning detector remains temporal-diff; the
detection margin is what needs work (merging bloom-split blobs, local
reference).

### 3. 2-colour field (CFG.field2) — flag reached the box but page evidence
ambiguous: the all2 frames show mean R 59 / G 64 / B 77 — B is the LOWEST
channel, consistent with a red+green field, and the blue-die diff vs the
all2 reference showed no >40 blob (die peak was ~40 vs diffThr 40 — right
AT threshold, inconclusive rather than negative). Needs one more run at
higher accB or lower diffThr.

## The root cause found today (affects everything): the CFG channel

The box embeds the CFG string in its drv? JSON WITHOUT escaping quotes —
`{"darkPair":1}` inside `"cfg":"..."` produced invalid JSON, the page
silently dropped it, and BOTH the cfg and any directive in that poll were
lost. This is why: the accB sweep "never applied" (byte-identical frames),
the first darkPair/field2 runs looked "ok" but ran with defaults, and
random combos "FAILED" while others passed. **S10 (flashed) escapes the
JSON properly.** Verified live: cfg application now logs on the page.

## Transport finding

Page sessions degrade after ~30–60 min: ack retries stretch surveys from
25 s to 2+ minutes (t951→t1572 gaps tonight). A page reload resets it.
Tonight's runs were all post-reload, so the stall didn't bite — but for
the handheld product this argues for the page auto-reloading its WS
session on repeated ack-retries (a small S11 item).

## Where this leaves the detector

- Temporal-diff vs the survey's own all-on frame: proven, keep.
- Die margins in this scene sit at 0.5–1.9× the bar. Next lever is NOT
  brightness: it's (a) merging bloom-fragmented blobs before dominance,
  (b) per-die local reference (die neighbourhood minus the same patch in
  the all-on frame), (c) possibly the dark-pair diff (once the capture
  race is fixed) which showed a 13-px die visible in an under-exposed
  frame.
- Next build (S11) candidates, in order: blob merge, dark-pair grab fix
  (settle between on/off grabs + byte-diff guard), field2 verdict rerun
  with diffThr=25.

All raw data in runs/night-20260922-172601/ (frames, logs, evids).