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

## 4. Single-LED toggle test (S14O/S14P — GATE PASSED 30 Sep)

**Purpose**: validate the bit-read path end-to-end on one known LED
(zero errors on tripod) before scaling to the full 200-LED decode.

- **Sequence** (CFG `cwcTestMode=1`, `cwcTestLed=0`): P00 (1 s hold)
  → P01..P17 (70 ms settle clamp) → master all-on grabbed after a
  500 ms pipeline flush (the S14P-1902 fix: an immediate master grab
  delivers the PREVIOUS plane's content).
- **Backwards registration chain (operator spec, 30 Sep, IN-PAGE now)**:
  P17 registers direct to master; P16 is pre-shifted by P17's TOTAL
  then registered against master for its remainder; that accumulated
  TOTAL seeds P15, ... down to P00. Integer NCC on the 128-wide
  decimated grid, sample-at convention. The page runs cwcChain for
  EVERY burst (test + full); totals ship in CWCSTATS `chain`.
- **In-page position decode (S14P-1904..1906)**: after the master grab
  the page scores every codeword against every masked site —
  stacksig = master − k_p·plane (k_p = per-plane median gain),
  greedy accept best-score-first, amp gate `cwcAmpGate` 60, d6
  margin gate `cwcMarginGate` 25, ±3 px local-max + suppression,
  then STRONGEST-SITE-PER-CODEWORD dedup (S14P-1906; with only one
  physical string active, multi-site claims are bloom ghosts —
  keep max amp; on real multi-string installs the pass is off and
  per-lane paint + per-string code blocks identify instead, §11).
  The result shows ON THE PHONE: a static master image with a box
  per detected LED — the operator's view of decode quality. Sites
  ALSO ship (CWCDEC chunks + CWCDECS summary → `<run>/cwc_dec.json`)
  for the console cross-check.
  **MASK LESSON (30 Sep sweep, the 20–25 miss root cause)**: the
  candidate mask was blur≥200 and leds 20–25 + tail 150–156 never
  became candidates — their true sites score FAR ABOVE the gates
  (amp 90–124, margin 56–78) but blur-luma 184–191 (shallow-angle
  edge cores). No amp/margin gate pair can recover a pixel the mask
  never offers (measured: 20–24 absent at ALL 7 sweep points down
  to amp 25/margin 10). MASK_THR -> 175 (CFG `cwcMaskThr`) =
  mid-gap: weakest needed luma 180, strongest phantom 167. NEVER
  <= 160: a codeword-103 phantom at (89,631), 280 px from its true
  site, survives there even after strongest-site dedup.
  Result validated on pos1 (lights-on): 196/200 claimed, zero dups,
  missing only [16,46,90,91]; pos3 (dim): 197/200 missing
  [46,90,91] — 16/46/90/91 have margin 0.0 at every candidate
  (their argmax pixels are owned by other codewords' cores; needs
  a per-codebook nearest-site 1:1 assignment, an algorithm change,
  not a gate change).
- **Analysis** `tools/cwc_analyse.py --test-led N`: scores EVERY
  candidate site against the test LED's codeword and takes the best
  read (A2 fix: 'deepest pile-up hole' picked an arbitrary LED on a
  live string where every LED has a hole). Registration = the page's
  chained totals when shipped, else per-plane direct phase correlate.
  Site candidates = threshold LADDER on the blurred master (254/250
  any-area + 224≤60 px + 200≤40 px comps): a single 200 threshold
  merged bloom SKIRTS into giant components (the 30 Sep gate-run
  lesson — 'best site' landed mid-skirt at master luma 21 and read a
  coin-flip codeword), while a flat 254 misses cores that sit in a
  near-saturated zone (good sites read 18/18 with blurred luma only
  231/236). Synthetic-validated BOTH ways after the fixes: clean =
  18/18 PASS naming the right site; one corrupted bit = FAIL with
  exactly that plane flagged.
- **Tripod gate**: 18/18 bits, residual < 1 px, ON/OFF ratio > 1.5×.
- **Page builds**: **S14P-1904** (canvas-ownership fix — captures
  snapshot procCx, not the display canvas; scanning flag live:
  idle overlay frozen + manual paint buttons locked + status LED dark
  during bursts; chain + in-page decode + result view + drv? retry),
  **S14P-1905** (default nPx/cwcN 200 everywhere; decode ships the
  full sites list, 1904 shipped a summary only).
- **Status (30 Sep)**: GATE PASSED on S14P-1904. LED 0: 18/18 PASS at
  THREE sites (twin-site PASS = mirrored-string expectation, printed
  automatically); LED 1: 18/18 PASS (different codeword); LED 25:
  honest FAIL 16/18 at its true pixel, failing EXACTLY the 3 planes
  where LED 24 (site 3 px away) is ON and LED 25 OFF = bloom crosstalk
  from the lit neighbour (§5 item 6). No false claims (d_min 6 gate
  held). Full round on 1905: 209 sites / 170 LEDs of 200 claimed,
  phone-vs-console same-id 205/209 (§10).

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
- **Bloom crosstalk between adjacent codeword blocks (measured 30 Sep)**:
  a neighbouring LED 3 px away that is ON in a plane where the target LED
  is OFF contaminates the target's core read (LED 25 vs LED 24: fails
  exactly the 3 planes of their codeword difference; the site reads
  16/18 and is honestly REJECTED — no false claim, d_min 6 held).
  Handling: accept the honest reject (weight < 9 → weight-0 class
  I = interpolated per §8), or increase the margin gate locally; the
  1904+ decode's ±3 px local-max + suppression already prevents BOTH
  LEDs claiming one pixel. Physical separations fix the cause: string
  2 unplugged while debugging (operator, 30 Sep), per-string code
  blocks + per-lane paint for the multi-string installs (§11.4).
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

## 10b. 30 Sep — the gate + the 200-LED rounds (S14P-1904..1906)

Builds (page embedded → one flash each): **1904** carried the session's
code-review bundle (§4 list) + the operator's backwards registration
chain + the IN-PAGE position decode + the on-phone boxed-master result
view + drv? poll retry + non-blocking STAT. **1905** raised the default
string length to 200 (firmware nPx + page cwcN/npxin + tool defaults)
and fixed the decode ship (full sites list now reaches
`<run>/cwc_dec.json`; 1904 shipped a summary only). **1906** lowered the
candidate-mask threshold to a tunable CFG `cwcMaskThr`=175 and added the
strongest-site-per-codeword dedup — the operator's miss report (20–25
NOT occluded; with only string 1 active, duplicates erroneous) resolved
by the sweep: the misses failed the MASK (blur-luma 184–191 < 200), not
the gates — no amp/margin gate pair can recover a pixel the mask never
offers (20–24 absent at all 7 sweep points down to amp 25/margin 10).
Recipe validated CONSOLE-SIDE on both runs: 196/200 (pos1) + 197/200
(pos3 dim), zero dups, 20–25 and 150–156 all claimed; residual
[16,46,90,91] = margin-0.0-everywhere cases needing a per-codebook
nearest-site 1:1 assignment (an algorithm change, queued).
**Ghost anatomy (settled with pixels)**: the persistent "far twins"
(led30 far claim, led120's) sit INSIDE the translucent drawer organiser
— clipped specular glints (raw 255, blurred 203/221, no discrete LED
core) that strengthened at lights-off; they are object reflections —
NOT a second lit string (string 2 is out of circuit per the operator's
rig state). The strongest-site rule (amp 155 vs 110) removes them at
zero cost; a run-runner subagent misread their persistence as "string 2
still connected" — corrected here. The handoff-era 'never force 1:1'
rule above applies ONLY to real multi-string installs; on this single-
string bench strongest-site-per-codeword IS the operating rule.

- **Toggle-test GATE PASSED (§4)** on 1904: LED 0 18/18 PASS ×3 sites
  (twin-site note fires), LED 1 18/18 PASS, LED 25 honest-reject at
  16/18 with the exact crosstalk plane set (§5 — call unproven until
  the same segment is re-shot after the mask fix; LED 25 now claims at
  (167,423) amp 115.5 margin 74.0 on pos1 with the 1906 recipe).
  Console registration residual 0.04 px median; page chain totals all
  (0.0, 0.0) conf 0.946–0.967 — tripod, correct.
- **In-page decode counts by round** (gates amp 60 / margin 25):
  pos1 209/170 dups 29-ghost; pos3 (dim, 14:29) 199/160 dups 29-ghost;
  with the 1906 recipe the console re-decodes 197/160-equivalent at
  zero dups (counts above). The page side of 1906 (mask 175 + dedup in
  cwcDecode) shows the same on the phone at the NEXT shot.
- Harness `tools/cdp_1904_check.py` (mock box + headless Chromium + fake
  camera) validates the real page pre-flash: stamp ALIVE, CFG+BURST via
  drv? directives, 19/19 frames, chain + decode logged, result canvas
  non-blank + saved, CWCDEC sites list + CWCDECS summary + CWCSTATS
  < 4096 B, zero page errors — both burst modes (test mode: 2nd burst).

Next steps (in order):
1. Unplug string 2 → re-shoot the 200-LED round: every codeword should
   claim exactly ONE site; tune gates from measured amps/margins.
2. Handheld repeat of the toggle test (§4 budget ~5 px residual).
3. ledcloud/2 export from the single-string site set (§8 classes).
4. THEN §11 (S14Q build): WS async-send spike → box-driven capture.

### 10c. 1905-pos1 miss list (30 Sep, RESOLVED by the 1906 recipe)

The 1905 page (gates amp 60 / margin 25, mask blur≥200) missed 30 LEDs:
[6, 20, 21, 22, 23, 24, 29, 44, 46, 47, 50, 60, 70, 71, 74, 75, 76, 83,
90, 91, 94, 97, 100, 101, 109, 110, 111, 122, 150, 156]. **Root cause
(sweep-settled): NOT occlusion (operator-corrected) and NOT the gates —
the CANDIDATE MASK.** Leds 20–25 + 150–156's true sites score far above
the gates (amp 90–124, margin 56–78) at blur-luma 184–191; the mask
never offered those pixels. The 1906 recipe (mask 175 + 60/25 gates +
strongest-site dedup) re-decodes pos1 at 196/200 and pos3 (dim) at
197/200, zero dups — the miss list collapses to [16,46,90,91] (pos1) /
[46,90,91] (pos3): margin-0.0-everywhere cases whose argmax pixels are
owned by other codewords' cores → per-codebook nearest-site 1:1
assignment (ALGORITHM-CHANGE QUEUE, not gate tuning). Supersession
note: the earlier occlusion theory in this section's first version and
the parallel-session 'string 2 still connected' call are both corrected
by 10b's ghost anatomy (drawer glints).

**PHONE-SIDE VALIDATION (30 Sep 14:58, S14P-1906, runs/
s14p-1906-phone-pos1)** — first 1906 phone camera round; the
phone-side evidence §10b awaited. run_round 19/19 frames, labels
unique; page decode 194 LEDs (gates 60/25, mask 175) → cwc_dec.json;
console re-decode **197/200**, zero duplicate codeword claims on BOTH
sides; missing [46,90,91] (margin-0-everywhere, argmax pixels owned by
neighbours' cores). **LED 16 RECOVERED** phone-side (amp 137.1, margin
77.2 — the mask fix bought it; it was gone at mask 200 this morning).
All gate conditions met: 197 ≥ 195; amp max 250.8 / med 173.2 (no 255
clip — overshoot in the seen skirt); mask candidates 19,865; every of
20–25 + 150–156 present (amp 117–207, margins 76.7–134.5); lowest
margin of the round 58.2 vs gate 25. **LED 25 VERIFIED** (167,424),
amp 136.9, margin 83.9 — its toggle 16/18 honest-fail is bloom
crosstalk, NOT a real defect. ONE page-vs-console divergence:
codeword 120's PAGE argmax (253,301) sits 67 px right of the console
verdict (186,303 — on-pitch between 119 and 121); it is a specular
REFLECTION of LED 120 (operator saw the misplaced box on the glare):
a mirror image carries codeword 120's EXACT 9-of-18 pattern, so the
code scores high at the glint, and this shot the page's blurred-luma
mask let the glint through while the console mask (rounding at the
175 boundary) did not. 1 site of 194; the other 193 shared IDs agree
median 0.00 px, max 10.0. Rule: console verdicts stay authoritative
for cloud export; the page-side mask divergence joins the
cwc_decode_sim gate list (§11.5 item 3).

[SUPERSEDED FORENSICS — kept for the record. The occlusion call below was
WRONG (operator-corrected: 21–25 not occluded); the real root cause is the
candidate mask, see 10b/10c above. The corridor observation itself was real
(the corridor does mask thin at 255) but it was not what kept 21–24 out —
their pixels die at the 200 mask threshold, and the 1906 recipe recovers
them. Amber-codeword interpretation also superseded: with only string 1
active those multi-site claims are ghosts, not mirror twins.]

209 sites / 170 LEDs — 30 LEDs unclaimed: [6, 20, 21, 22, 23, 24, 29,
44, 46, 47, 50, 60, 70, 71, 74, 75, 76, 83, 90, 91, 94, 97, 100, 101,
109, 110, 111, 122, 150, 156]. Forensics on the 21–24 group (the
operator's flagged cluster): the route between decoded neighbours
19 (171,538) and 25 (167,421) passes the multimeter/PSU occlusion +
the desk-loop near-saturation zone; the corridor masks to only ~12 px
at 255 and the segment's plane pixels read coin-flips (9–11/18)
against EVERY candidate — the segment is occluded/defocused there,
not unlit (the strand IS visible in the master crop). The console
decoder misses the same LEDs — physical, not a decode-gate bug.
Handling: honest reject → class I interpolated from serpentine
neighbours (§5/§8). The amber (multi-site) codewords are the
mirrored-paint twin claims (29 codes this round, mostly string 2's
ball + skirt pairs — the suppression keeps them OFF string-1 pixels).
After the string-2 unplug + re-shoot, re-read this list from the
fresher run before touching gates.

## 11. AGREED ARCHITECTURE — box-driven capture (S14Q design, 29 Sep night)

Operator proposal, Nellie-verified 29 Sep. ONE firmware build carries all
of it; the box becomes the sequencer and the phone the capture+decode
client (the deployable product shape: phone + box, no console in the
loop). Replaces the page-driven burst loop for the decode era; the
19-frame protocol, primer/master structure, decode gates and
cloud format are UNCHANGED.

### 11.1 Who holds what

- **The box is the single authority for the codeword bank** — 1600
  codes as 18 × 1600-bit masks (~3.6 KB flash; program 66% of 1.94 MB).
  The phone holds NO bank. Per-frame payloads carry the effective
  per-LED state to the phone, so there is no cached-copy consistency
  problem and no bank-hash handshake. The phone REBUILDS the effective
  bit table from the payloads as the burst runs and decodes against
  that (with d≤1 correction). Bank regeneration then never needs a page
  or firmware repack — it is box data.
- **Pattern payload = 1 bit per LED: exactly 200 bytes.** 1600 LEDs
  (8 lanes × 200) → 25 bytes per lane, byte block = lane, bit = LED
  index. ~3.8 KB of pattern data per burst. No further compression pays:
  the codewords are deliberately high-entropy.
- Per-string codeword blocks ride the same per-lane paint change
  (string s = codes 200·s+i), so identity falls out of the decode with
  NO serpentine second pass (§9.4 of the ledcloud spec; measured bank
  facts in tools/cwc_bank_check.py output: block d_min 4, per-plane ON
  exactly 100 within every 200-block, cross-block d_min 4).

### 11.2 Capture flow (event-driven, NOT poll-paced)

Tonight's 1.5 s is solely the page's drv? poll interval
(setInterval 1500) — a polling artifact, not physics; measured capture
cadence is 0.32–0.53 s/plane. The new flow removes the poll from the
timing path:

1. Box latches pattern p (FRAME_MS ≤ 40 ms pacing).
2. Box pushes a CAPTURE command to the phone OVER WS (unsolicited),
   payload = the 200-byte pattern (+ frame type: primer/master/plane,
   + expected-lit count).
3. Phone selects/exposes, grabs, ACKs {frame_type, plane_idx, grab_ts,
   gross checks}. Box then latches p+1 and pushes the next command.
4. After the master ack the phone computes the point cloud IN-PAGE and
   returns ONE ~12 KB JSON (points + per-LED confidence + per-plane
   gross-check results).
5. The 19 JPEGs / 1.4 MB / LOGP-LOGA-sDrv-pull-recipe layer exists ONLY
   as the bench debug tap (benchStore + BRAMP stay built and paid for,
   never in the operating path).

- Transport spike (HARD GATE before the build): the raw esp_https_server
  has no WebSocket library — unsolicited send needs
  httpd_ws_send_frame_async on the stored sockfd. Bench-verify it works
  on the C6 harness EARLY. Fallback = raise the poll rate to ~150 ms
  during capture mode (+≤150 ms/plane, ~2–4 s/burst, acceptable).

### 11.3 Timing rules (AE / pipeline flush)

- Per-plane budget measured with WS push: latch ≤40 + push 15 +
  content-matched grab 30–100 + ack 10 ≈ 0.10–0.19 s → 2–4 s/burst
  (2–3× faster than tonight's ~0.4 s/plane).
- **Master at plane gain — AGREED (operator)**: grab the master early,
  immediately after the last plane's ack. AE does not change within a
  frame, so its master pixels are IDENTICAL in brightness to tonight's
  late master (per-LED core brightness is per-pixel and exposure-driven;
  the 2× global mean only influences AE's NEXT decision). Early master
  lands at plane gain: k_p ≈ 1.0 by construction instead of the measured
  0.84–1.18 spread — one less noise term in the bit read. LED-core
  saturation is UNCHANGED between early and late masters (the early
  master only widens already-saturated cores; harmless).
- **Master flush = stable-pair + count rule (phone-side, no positions
  needed)**: keep grabbing until two consecutive grabs agree (pipeline
  flushed) AND the bright-source count ≈ 2× a plane's (catches the
  r4/r5 stale-master failure: a stable pair of p17-content frames agree
  with each other but count 1×, not 2×). Fixed 500 ms flush remains as
  the hard fallback. OPEN bench item: is a flat ~250 ms flush sufficient
  (tonight only brackets <100 ms bad / 500 ms clean)?
- **Per-frame gross checks by the phone (aggregate, pre-decode)**:
  plane lit-count ≈ expected N/2 (±bloom tolerance), toggle-count vs the
  previous plane's payload ≈ Hamming(code_{p-1}, code_p). Catches
  no-power, half-lit, stuck pattern in-burst (~1 s) instead of minutes
  later console-side. Per-LED placement/verify gates remain DECODE-time
  (the phone cannot localize LED i before the point set exists).
- **The box must never dictate capture timing** — it latches and
  commands; the phone owns camera-pipeline decisions (the ownership rule
  that underlies the master bug fix).
- sCfg (128 chars) is too small for the choreography: CAPTURE commands
  ride a NEW directive message class; sDrv stays single-slot for bench
  compatibility.

### 11.4 Box-side hardware/firmware deltas (ONE build)

- Capture state machine (latch → push → ack → advance; plane-retake on
  missing/failed ack).
- Bank embed 18 × 200-byte masks (~3.6 KB), bank-hash printed in hello.
- **Per-lane paint**: pxColour[] is today ONE array mirrored to all 8
  lanes; the burst path needs per-lane masks (required for per-string
  codeword blocks).
- Result store ≥ 12 KB (150 points × x/y/class/conf) — sEvid 1280 B is
  not enough; SRAM has 269 KB free.

### 11.5 Gates before the flip (in order)

1. WS async-send spike on the C6 (transport decision above).
2. Positions-only round on 1903 (§10 steps 1–3) — no firmware change.
3. **JS decoder gate**: the phone's decode must reproduce the console
   decoder's output on the SAME captured frames (cwc_decode_sim gate)
   before any box-driven trust. The decoder is the schedule risk; the
   plumbing is not.
4. Then the S14Q build: box-driven capture + per-lane paint + per-string
   blocks + in-page cloud, harness-validated (mock box + headless
   Chromium extended with the capture state machine), tripod then
   handheld.
