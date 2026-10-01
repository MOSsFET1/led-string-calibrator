# Strategy research: camera mapping of an LED string (Sep 21 2026, Nellie)

Predecessor: poc/cal_page/POC-TEST-PLAN.md (B96→B119 era). That paradigm is
judged a dead end: per-pixel OFF/ON diffs are hostage to camera motion (every
edge lights up on the diff), the march is sequential O(N) time, and the whole
~100 s session must survive on a steady tripod.

## How the commercial SOTA does it

**Twinkly (Ledworks S.r.l., patents from 2016, e.g. "Lighting system and
method of controlling the system" cited in US8080819B2):**
- Mapping = a "quick sequence of encoded light flashes"; the app determines
  the position AND the identification code of each LED from the sequence
  (official brochure: "By a quick sequence of encoded light flashes, the app
  is able to determine the position and corresponding identification code of
  each single LED. A sophisticated reconstruction algorithm combines views
  made at different angles to create the best 3D model in a bunch of
  seconds.").
- App flow (help center): hold phone still, countdown, string flashes an
  encoded pattern; each LED's in-app state: red → orange (XY found, one
  view) → green (3D, multiple views). "Easy Mapping" = single scan, 2D.
- User-facing failure handling: "red LEDs remain → rescan, moving slightly
  to change the image section" = overlapping sub-scans stitched by ID.
- 500–700 LED strings mapped this way on plain phone cameras (Wi-Fi/BT).

**Lightwork (PWRFL, openFrameworks/OpenCV, desktop):** one-at-a-time
sequential firing, webcam on a tripod — README warns "Any movement will
throw off your mapping results". Same paradigm as our march; stereo mode =
2 captures for depth. The industry knows the motion problem; the commercial
answer is coded flashes, not motion-tolerant diffs.

**marimapper (TheMariday, open source):** phone-camera photos of blinking
LEDs → COLMAP structure-from-motion → true 3D + LED normals. Offline,
laptop-driven, minutes per map. Proof that SfM on bright point features
works, but heavyweight for an in-browser POC.

**Govee permanent lights:** manual segment/zone mapping in the app — no
evidence of camera auto-mapping. WLED: no camera mapping at all.

**Academic adjacent field (OCC / visible-light positioning):** LEDs blinking
OOK codes, per-pixel cross-correlation over 2N frames gives per-LED ID +
position; frequency-division needs ≥2 frames per bit per LED and hundreds of
distinct frequencies — unusable over WS2812 at phone framerates.

## Candidate strategies

A. **All-on skeleton:** one frame, every LED lit, threshold + connected
   components → complete 2D point cloud in one shot; order assigned by
   nearest-neighbour chaining from px0. Crossing serpentines ambiguous →
   disambiguate with 1–2 coded frames.
B. **Binary-coded burst (Twinkly's paradigm):** frame t lights the subset of
   LEDs with bit t = 1 (plus a px0-only frame and an all-on master frame).
   Each blob's ID = its across-frame on/off signature. log2(N)+2 frames:
   200 px → ~10 grabs ≈ 10–20 s. Identity carried by the light pattern, not
   by march order — no anchor tracker, no ladder, no shift compensation.
   Redundancy via parity/2nd-pass frame; ambiguous signatures re-verified
   per blob (localised retry, not a restart).
C. **CDMA refinement of B:** random k-of-n patterns, Hamming-distance
   matching. More frames for the same coverage; only if bit errors prove
   common.
D. **Frequency-division:** ruled out (above).
E. **Handheld video + SfM (marimapper-style):** motion becomes signal, but
   browser-side SfM is its own project. Parked as a later 3D path.
F. **3D from repeated coded bursts:** after B, repeat the burst from 2–3
   handheld angles; per-LED IDs make cross-view correspondence trivial
   (match by ID, not SfM) → triangulate per ID.

## Why B (built on A's all-on frame) kills the motion problem

- Identity lives in the code, so any single frame may be blurry/shifted; we
  need stability within one ~100 ms frame and rough re-visibility across
  ~2 s, not 100 s of tripod stillness.
- No pairwise diffs → the "every edge lights up on the diff" failure class
  disappears. Ambient suppression stays (EV bias on Android, dim-time
  discipline, dominance bar as the blob gate).
- Brighter display ⇒ shorter AE shutter ⇒ less motion blur.
- Per-blob independence: a miss corrupts one blob's signature, not the
  march. Occluded LED = absent from all frames → interpolate from ID
  neighbours (old U4 becomes trivial).
- Overlapping sub-scans (framing limits) stitch exactly, by shared IDs.
- 3D later almost free (F).

## Colour + bright-background technique (Oliver, Sep 21 — ADOPTED)

In a multi-string system: light all strings EXCEPT the one being surveyed
bright white (exposure control), and light the surveyed string with a
stationary colour wave so its pixels can be differentiated.

Verdict: adopt, with two corrections:
1. Hue is a SEGREGATION + VERIFICATION channel, not the identity channel.
   Hue is 1-D and fragile where we have already been burned (B96: green-die
   red-channel bleed from AWB gain + cross-talk + demosaic; bleed peaks
   overlapped real red peaks). The binary coded burst stays the ID layer;
   hue labels neighbours, confirms IDs per-frame, and separates the
   surveyed string (saturated hues) from the white backdrop (low
   saturation).
2. The white backdrop is servo'd, not maximised: measure the white blobs'
   camera peak each frame and servo backdrop brightness to the minimum that
   holds the peak at ~85–90% (never clipped to flat white). Benefits: pins
   AE on iOS WITHOUT exposureCompensation (the whole iOS exposure problem
   evaporates), stabilises AWB (hue readings only mean something when WB is
   pinned), and makes the coded burst a small histogram perturbation the
   AE meter ignores.

Synthesis worth benching later (single-string case): bit=0 LEDs lit DIM
WHITE instead of off, bit=1 lit in colour — every LED visible in every
frame, total luminance near-constant, AE frozen, code read as hue/brightness.
Riskier read than on/off (Twinkly's proven scheme) → bench A/B, not a
commitment.

## Decisions (Sep 21, with Oliver)

- New paradigm: coded-burst survey (B) bootstrapped by the all-on frame (A).
- NOT ported from the old build (all never reliably worked; make them
  unnecessary rather than fixed):
  - phone→box log shipping (floods, drain races, NVS commits). Logs live on
    the phone page (in-memory ring + download button); the box receives
    only ONE bounded evidence summary per burst (serial-visible).
  - pause/resume (a burst is 10–20 s; Abort + rerun instead).
  - persistent session state ("2nd scan times out" cannot exist).
- Feature order, one at a time, each verified before the next:
  - F1: all-on survey frame + exposure/clipping study + 3 accent LEDs
    (R/G/B) + the reusable blob detector + the page/box machinery
    (auto-connect, auto-camera, compact logging, log download, start/abort,
    console automation via serial directives). Tripod OK; final system
    handheld.
  - F2: binary coded burst IDs (on/off in a bright background).
  - F3: colour wave as per-frame verification + merge splitting.
  - F4: multi-angle sub-scan stitching (by ID).
  - F5 (later): 3D from repeated bursts; constant-luminance burst A/B.
- New folder: led-display/POC LED survey/ (firmware poc_survey, page,
  runs/, docs). Old cal_poc2 stays flashable for rollback.