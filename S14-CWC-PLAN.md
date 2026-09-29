# S14 plan — CWC LED position detection (Oliver + Nellie, Sep 2026)

State of play, 29 Sep: one burst = the whole calibration. Identity lives in
the light pattern (9-of-18 codewords, Twinkly-style); the camera only reads
bits + positions. S13's per-LED hole survey is retired from the page. The
movement/registration machinery is built and handheld-proven (r6); the
remaining work is bulk decode of the 150-px string and the point cloud.

## 1. Agreed capture protocol (19 frames: master + 18 planes, S14N primer)

1. p00 frame on for 1s — AE settles.
2. Grab frames p00 to p18 as quickly as possible. at 50% duty, b=150: plane 
    p paints LED i iff bit p of LED i's codeword is 1. Constant per-frame 
    load keeps AE stationary.
3. Grab all-on master frame as quickly as possible to minimise AE changes. 
4. Per plane: Starting with p17 and then working backwards to p00, register 
   each plane to master (seeded, master-anchored) → per-LED bit
   read = plane luma / (master luma × per-plane gain k from non-LED
   pixels). NOT a local-ring average — rings sit on neighbour bloom (r6
   lesson).
6. Decode: nearest codeword within d 4; gates: per-plane ON count exactly
   N/2, per-LED weight 9-of-18, decode distance ≤1, serpentine continuity
   (S13 impostor guard); failing LEDs → interpolated (§5).
7. Master once per burst; mid-burst re-anchor only on health triggers.


The off reference frame is DROPPED (agreed 27 Sep): NCC registration is
gain-invariant; the point set comes from the pile-up Σₚ(master − planeₚ)
— 9 coherent hole samples per LED, SNR ×3 over the single master−off
diff; collocation detection = NOR-weight < 9. Optional all-off diagnostic
grab at burst end stays bench-only (noise floor), not part of the decode.

## 2. Codeword bank (built, verified)

`tools/codewords_9of18.json`: 1600 codes, weight 9 of 18, d_min 4 (6
within the first 150), every plane exactly 800 ON for N=1600 (75 for
N=150), duty 50% per LED. Greedy max-min + column balance
(`tools/cwc_feas2.py`; the 7-of-14 record is kept there as the
impossibility note: C(14,7)-bound d≥4 caps at 429, greedy exhausted at
156). **Prefix property (measured):** the first N codewords keep d_min AND
exact N/2 per-plane balance for every N tested (100–1600) — N is just an
index count; no re-selection for smaller strings. Page embeds the bank
inline (page/codewords.js is the regeneration source).

Frame budget: 20 captures ≈ 12 s/burst at the measured 0.5 s/grab cadence.
50% duty at 1600 px ≈ 1.2 A/lane (safe); **cap master/plane brightness at
b≤150 (1.77 A)** per the polyfuse ladder; detection is
brightness-insensitive (S13: 150/150 at b=120/160/200/255).

## 3. Bench-verified movement/registration facts (r1–r6 evidence)

- **Registration is mandatory and master-anchored.** Unregistered diffs
  die at 2 px of shift (wrong-blob capture, not silence);
  registered (phaseCorrelate per frame) hold 100% to ≥6 px, 75–79% at
  8–10 px on integer residual. Median residual 0 px, 40/40 exact on
  random offsets ±10. Plane-vs-MASTER stays accurate at raw shifts far
  past half-pitch (40–80 px synthetic: residual ≤0.65 px) — raw shift
  does NOT degrade the estimate; plane-vs-plane chaining DOES bias
  (~0.5–1.5 px, one 86 px blow-up at conf 0.31). Hence: register every
  plane against the master, seeded by the previous composed transform
  (T_k = Δ_k ∘ T_{k−1}; Δ_k measured vs master — no chain accumulation,
  NO prediction, seed is always the last MEASURED transform).
- **Residual budget, not a shift limit**: ~5 px residual keeps the bit
  read clean while under the blob core radius; 10–12 px (½ pitch) risks
  neighbour bloom → wrong bit (d_min 4 still corrects one); 45 px = the
  serpentine gate's catch radius. Phase correlation is global — large
  raw shifts are fine while confidence holds.
- **Handheld measured (26 Sep, real phone)**: per-frame drift 1–2 px
  median (max 2.2) at 3.3 fps unpaced; net whole-burst drift 7–14 px
  over ~6 s; one failed NCC lock in 38 pairs. Single-burst handheld
  VIABLE at 3.3 fps; 5 fps would shrink the budget further but is not
  required (phone serves 6.5–6.9 fps unpaced; gap-200 pacing measured
  2.3–2.7 fps — pace rVFC-to-rVFC if 5 fps is ever wanted).
- **AE is the hazard, not motion** (r6): one-time ~0.5 s re-meter after
  the master→plane field change (+17–20 luma, invisible to getSettings),
  then HOLDS (±1.9 luma p03–p17); p00 caught mid-transition (−10).
  All-on tripod bursts still pop ~3/20 frames >15%. Mitigation is
  sequence-level: S14N primer + console-side master×gain read. Android
  manual exposure lock is a second-stage lever (iOS: not exposed).
- **Tripod AE-freeze across long bursts**: exposure readback pinned
  (exp=500.05 aem=continuous) across 16 frames / 7.9 s.
- **Comp machinery proven in-page** (S14J harness, headless Chromium +
  mock box): round trip identity conf 1.00, known shift recovered within
  half a grid cell, warp-back reduces error 87→30; the S14B-5 grid-unit
  fix (NCC ×W/128) is in. White-noise content is pathological for the
  decimated NCC — quote quantization-aware gates; real content is smooth.
- **Bit read (r6 verdict, supersedes the c2f6ecc 'blocked' note)**:
  per-core reads against a LOCAL RING average fail (rings sit on
  neighbour bloom); against the registered master × per-plane gain k
  (median over non-LED pixels) reads go cleanly bimodal (ON ≈ 1.0, holes
  ≈ 0.45–0.5) and 13 cores decoded STRICTLY. Remaining limit ~17%
  per-bit error after top-9 normalisation — bulk decode needs that
  roughly halved (target ≪ d_min−1 = 3).

## 4. Single-LED toggle test (S14O/S14P — CURRENT WORK)

**Purpose**: validate the bit-read path end-to-end on one known LED
(zero errors on tripod) before scaling to full 150-LED decode.

- **Sequence** (CFG `cwcTestMode=1`, `cwcTestLed=0`): P00 (1 s hold) →
  P01..P17 (70 ms settle clamp) → master all-on grabbed ASAP (<100 ms
  after P17). Backwards registration console-side: each plane → master
  (P17 seeded from master, then P16 seeded from P17 …).
- **Analysis** `tools/cwc_analyse.py --test-led 0`: reads `cwc_stats.json`
  (testMode/testLed/testBits), finds the LED's hole in the pile-up,
  reads per-plane luma at the LED (master×gain, r6 recipe, top-9
  normalised), gates ON/OFF at 0.5, prints per-plane reads + verdict.
  Validated on synthetic runs (clean = 18/18 PASS; one corrupted bit =
  FAIL with exactly that plane flagged).
- **Tripod gate**: 18/18 bits correct, registration residual < 1 px on
  all planes, bimodal ON/OFF ratio > 1.5×. Handheld: same protocol,
  residual budget ~5 px, zero decode errors still the target.
- **Page build**: **S14P-1901**; firmware `PAGE_BUILD`: **S14P-1901**
  (1900→1901: the test branch no longer calls benchPull itself — the
  shared S14L auto-ship ships the store exactly once; double-ship found
  by the mock+CDP harness 29 Sep).
- **Status (29 Sep)**: S14P-1901 flashed. Test mode captures 18 planes
  + master; the ship path is HARNESS-PASSED (mock box + headless
  Chromium: CWCSTATS=1, FRAME=19, FEND=19, labels run-tagged). The 28
  Sep 'frames don't ship' failure was CONSOLE-side, not the page: the
  pull reader broke on the FIRST `[PHONE-LOG] end` (the LOGP ring
  pull's own logend precedes the frame stream) + the directive-slot
  race; both fixed in `s14_bench.py`. The analyser's label mismatch +
  stub bit read also fixed (synthetic-validated: clean = 18/18 PASS,
  one corrupted bit = FAIL on that plane). Phone page down at 29 Sep am
  (STAT GONE) — reload + run the tripod round (forensics:
  `S14-BENCH-SESSION.md` §S14O/S14P).

## 5. Failure handling (per-layer retakes; interpolation is first-class)

- **Per-plane ON count** — exactly 800/1600 (75/150 in the bench burst);
  corrupted plane → retake that plane (~1–2 s).
- **Per-LED weight** — 9 of 18; d_min 4 silently corrects one bad frame;
  decode distance >1 → re-read that LED from the affected planes.
- **Registration confidence + health triggers** — confidence gates each
  frame; path-length/confidence threshold → re-anchor to a fresh master
  and resume. Knock/jump detector: |T_k − T_{k−1}| vs running median.
  Mid-burst 90° flip = burst abort + redo (not a similarity).
- **Serpentine continuity** — decoded IDs must trace the string's path
  (S13 impostor guard); locally broken path → targeted re-read.
- **Burst sanity** — frame count/timing checksums catch gross events →
  full retake (cheap at ~12 s).
- **Dead/hidden/stuck-ON** — weight 0 (or 18) ≠ 9 → flagged, position
  interpolated from serpentine neighbours (S13 px95 path; never a
  threshold change).
- **Collocated LEDs** (two LEDs, one point): reads = OR of two codewords
  (weight 11–18) → detected by weight alone, but pair identity is NOT
  recoverable from one burst (brute force: unique in 2/300 random ORs).
  Resolution: serpentine inference → disambiguation burst (fresh sub-
  code, same 19-frame protocol) → manual pin. Stored once, both IDs.

## 6. Config: selectable strings × LEDs (100–1600)

`nStr × nPerStr` (or single `nLeds`); the codeword prefix property makes
smaller strings FREE (index count only). Box carries the knob (NPX=,
hello ships it); CFG gains nStr/nPerStr so the map editor + decode gate
know the string structure (string s owns IDs [s·nPerStr, (s+1)·nPerStr),
folds inside a string, transitions are known breaks). Same camera
framing at smaller N = larger pitch = EASIER reads.

## 7. Orientation

One burst = one orientation, locked; the map is tied to the orientation
it was shot in. Between-frame rotation is absorbed by the similarity
registration; a mid-burst 90° flip is the health-triggered burst-abort
case (master-to-master correlation ≈ 0 is the unambiguous signature).
Processing geometry is aspect-agnostic (long side 720, windows × frame
diagonal, nothing squashed). Resolution asymmetry is the real cost:
landscape capture gives the short side only ~406 px — if wide installs
need it, request landscape-native capture (getUserMedia/screen-
orientation change, not an algorithm change).

## 8. Point cloud format — `ledcloud/2` (AGREED SPEC, 27 Sep)

Self-contained JSON; array order IS the LED id. Consumers are trivial
('canvas = k × (box.aspect, 1), sample points[id]').

```json
{
  "schema": "ledcloud/2",
  "created": "2026-09-27T21:40",
  "build": "S14M-1900",
  "strings": 8, "perString": 150,
  "box": { "aspect": 3.42, "rot": 0 },
  "mm": { "w": null, "h": null },
  "points": [ [0.0123, 0.0044, "C"], [0.0131, 0.0046, "C"], ... ]
}
```

- **Coordinates**: x/y floats in [0,1] of the CLOUD'S OWN bounding box —
  NOT camera pixels (camera frame is scaffolding; decoded, it leaves the
  data). Box declared in the header (aspect = width/height; rot reserved
  for a future cloud-axes alignment; today the cloud inherits the master
  frame's orientation). Consumers never rescale.
- **Box rule**: computed over ALL points — every LED gets an XY, even a
  never-detected one. Outliers can't inflate it: rejection is structural
  (below), not box-based.
- **Every LED present**: points[id] = [x, y, class], class C = confirmed
  (weight 9-of-18, distance ≤1, serpentine-consistent), I = interpolated
  (folds hidden/dead/never-revealed/gate-rejected; per-LED REASON lives
  in the burst log), X = collocated (one XY, both IDs).
- **Rejection at decode time** (operator-confirmed): serpentine gate
  (≤ holeGateK 4 × median pitch from ID neighbours' midpoint), decode
  distance ≤1, weight 9-of-18, per-plane count exactly N/2. The cloud
  layer never thresholds.
- **Floats** now; integer 0-4095 grid is the pre-agreed downgrade if a
  firmware consumer appears. Provenance (created/build) rides along;
  unknown fields ignored. `mm` null until a scale reference is shot.
- Export = ONE conversion at generation (capture px → cloud box), so
  every consumer downstream is conversion-free.

## 9. Open items (29 Sep)

1. **S14P tripod round** (§4): phone page reload → test-mode burst →
   pull → `--test-led 0` PASS (18/18, residual <1 px, ratio >1.5x),
   then handheld repeat.
2. **Bulk decode** of the 150-px string: per-bit error must fall ≪ 3
   (from ~17% at r6) — primer + master×gain reads + top-9 norm.
3. **Point cloud export** (§8) + in-page decoder once decode is solid.
4. Deferred levers (only if evidence demands): Android manual exposure
   lock (iOS-safe fallback impossible), 12-of-18 higher-duty codewords
   (re-run feasibility only for a distant large install), sub-pixel
   registration refinement (parabolic peak) if real residuals near 1 px,
   blob-landmark registration if correlation conf breaks on real scenes.

## 10. 29 Sep evening — first full position round (S14P-1903, clean master)

One burst (`tools/run_round.py runs/s14p-1903-pos1 --cwc-test-mode 0
--cwc-n 150 --b 150 --comp 0`): LOGA arm + CFG/BURST + auto-ship pull in
ONE serial session. 19/19 frames, master dip-test CLEAN (ratio 0.96 =>
master IS all-on; the 1903 master-grab 500 ms flush did its job),
registration residual 0.03 px median, conf 0.83–0.89.

Position result: 177 distinct LED sites (union of per-codeword multi-site
matches, amp ≥ 60/plane, d6 margin ≥ 25) — boxes `runs/s14p-1903-pos1/
sites_union.png`, `led_1to1.png` (94-site strongest-claim subset). The
site-per-codeword multiplicity (80 LEDs with exactly 2 sites) is the
MIRRORED-STRING structure: lane1's paint is mirrored to all 8 lanes, and
2 physical strings sit in frame — one codeword legitimately has 2-3
sites. Serpentine path tracing separates strings LATER; identity decode
must NOT force one-site-per-code (1:1/argmax attempts plateau at
94–111/150 with mis-assignments — the twin sites ARE the data).

Next steps (in order):
1. Re-run the single-LED toggle test on the 1903 build (§4): the clean
   master should finally give the 18/18 tripod gate.
2. Positions-only point set: take the site union, keep amp/margin gates,
   output ledcloud/2 with class from the site multiplicity + strength.
3. Serpentine string tracing over the union (positions only, no ids).
4. THEN per-LED bit read + identity decode (needs the strings separated).
