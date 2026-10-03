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
   — 30 Sep update, PROMOTED: parabolic sub-peak (→1910, see §10d);
     REJECTED: drift-velocity prediction (operator, 30 Sep: predicts an
     oscillatory ~8-12 Hz tremor signal that reverses inside its own
     estimate window, and a wrong predicted offset is fed into the warp
     unobserved — worse-then-silent; never implement).

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
assignment (ALGORITHM-CHANGE QUEUE, not gate tuning) — **QUEUE DROPPED
30 Sep pm: operator confirmed 46/90/91 are physically HIDDEN**; the
console's honest unclaim was correct behaviour — ship the trio class I
per §8, no algorithm work needed. Supersession
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

### 10d. 1910 SPEC — parabolic sub-peak registration (SPECIFIED, not built)

Forensics (30 Sep) showed the chain's K/2 = ±2.8 px quantisation is the
dominant warp-residual term under motion: ncc() picks an INTEGER
decimated step and scales by K = W/128 = 5.625, so a real 4 px
remainder snaps to 0 or 5.625. 1910: capture the 25×25 NCC score
surface (already computed at every step), then fit a 3-point parabola
through the peak along each axis: Δ = (y₋−y₊)/(2(y₊+y₋−2y₀)) ∈
(−0.5,+0.5); final shift = (dx*+Δ)·K, float totals, integer pre-shift
unchanged for the next plane's seed. Guards: apply ONLY when the peak
stands over its shoulders (margin ≥ ~0.05 conf units; else keep the
integer pick — flat shoulders give garbage fits), clamp |Δ| ≤ 0.5,
and keep the integer pick at ±12 boundary peaks. Limits: sharpens the
CHOSEN peak only — a lattice side-lobe lock (~20 px) is not fixed by
interpolation; that class belongs to blob-landmark registration (§9
lever). Parity rule: the identical refinement lands in
tools/cwc_pos_decode.py in the same build, so console/page agree
line-for-line (cwc_decode_sim gate, §11.5). Then re-attempt the §4
handheld gate with the discipline recipe (slow small pans, total
drift ≤ ~15 px); drift-velocity prediction is REJECTED (see §9/§10
deferred levers, 30 Sep).

### 10e. 30 Sep evening — out-of-hours box reboot + pending handheld r4

- **~19:01 box reboot (unattended) + TLS wedge**: after the reboot every
  TLS accept failed `mbedtls_ssl_setup -0x7F00` (ALLOC_FAILED — the
  rebooted box's heap cannot fund a second TLS session alongside the
  RAM-malloc'd pageGz, poc_survey.ino L910-914). The phone page could
  not reconnect; its Burst button stayed correctly greyed ('WS:
  closed') for minutes with zero phone traffic. Operator power-cycled
  ~19:4× and reloaded the phone: WS healthy. Operational read: a
  multi-minute greyed Burst = WS down, not the auto-upload (ship is
  ~40–60 s; the bench doc's 'greyed a few minutes = upload' note is
  corrected in S14-BENCH-SESSION.md).
- **New handheld r4 DECODED (console, 20:14–20:17)**: recovered from the
  bench ring after the r4 ship (replay into runs/s14p-1911-handheld-r4/).
  Phone claimed 187 @ gates 60/25 (page localStorage sticky; 1911 ships
  marginGate 10). Console: **191/200 @ 60/10**, 188/200 @ 60/25, zero
  duplicate claims. Missing @10: [5,16,22,46,67,90,91,109,114] —
  46/90/91 = the standing hidden trio, the rest pending the full-res-NCC
  re-score (r3 precedent: dec-NCC quantisation class). Motion was honest
  (net vs median ≈ 6.1 px) but the 1909 guard still captioned it
  'MOTION FLAGGED, unreliable' (every plane's direct-conf 0.767–0.885
  < the 0.90 default themed on legacy chain conf) — so 187/191-class
  passing handheld rounds are NOT no-data; the guard re-theme (CFG-only)
  is now the standing blocker for §4 acceptance.

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

## 12. Implementation status (02 Oct 2026, S14P-1928)

Frame-bits replaces JSON paints for CWC rigs — with CFG `nStr=8,
nPerStr=200` (1600 LEDs) a single JSON paint cannot reach the box: it
must ride the new WS BINARY command `frame-bits` (client→box, exactly
206 B: magic 'B', ver 1, brightness byte, flags byte, u16 LE epoch,
then a 1600-bit plane, bit j = LED global id j, LSB-first per byte;
lane = j/nPerStr, px = j%nPerStr; ids ≥ nStr·nPerStr ignored;
off-rig lanes/tails forced black; per-lane write, NO mirroring;
malformed → `{"err":"frame-bits shape"}`). The ack is the normal JSON
latch-ack echoing the message's u16 epoch. Full current-state detail —
build box/page/QA stamps, per-string polyfuse clamp, hello shape,
build cycle, open work — lives in `HANDOFF-S14P.md`; this section
records what the plan's earlier sections look like as-built.

- **§6 nStr/nPerStr note: IMPLEMENTED.** CFG keys `nStr` (1–8) /
  `nPerStr` (1–200) applied box-side AND page-side (mid-session
  re-CFG ok); sCfg budget widened 128→640 B (real max CFG is 413
  chars; verifier `tools/verify_s12_cfg.py`); hello reply now =
  `{"ok":true,"id":N,"fw":"poc_survey","px":nPx,"nStr":nStr,
  "nPerStr":nPerStr}`; per-string polyfuse clamp on frame-bits
  brightness (maxB = max(20, floor(255·2/(nPerStr·0.0142)))), byte-
  identical to the old fuseClampB at nPerStr=200.
- **§1/§2 capture protocol: SUPERSEDED-IN-PRACTICE (page as-built).
  Correction to §1 as written:** there is NO all-off reference frame
  anywhere in the burst and P00 is not an all-on primer — the primer
  IS coded plane P00 itself, a 50%-on plane (exactly N/2 LEDs lit)
  HELD 1,000 ms for AE settle; P01..P17 follow at 100 ms settle each
  (floor 70, CFG `cwcSettle`); the reference is a fast all-ON master
  (JSON `all`) grabbed with only 70 ms of flush before AE re-meters;
  decode diffs planes vs THAT master. 19 frames = 18 coded planes +
  master. The "off reference frame is DROPPED" paragraph stands; the
  "p00 all-on AE-settle" reading of §1 does not match the page.
- **QA: bit-exact multi-string PASS** (mock box + fake camera,
  `tools/cdp_1904_check.py`, exit 0): burst-3 case CFG nStr=8
  nPerStr=25 (200 ids across 8 virtual strings) — 18 frame-bits
  latches, every plane exactly N/2 lit rig-wide, and bit-exact
  per-lane proof (LED 57 = display L3 pixel 7; its latch sequence
  matched codeword 000110010111110100 across all 18 planes).
- **Real-rig round: PENDING.** All 8-string behaviour above is
  QA-verified on the mock only; the real 8×200 rig has not been shot
  yet. First real round: CFG nStr=8 nPerStr=200, reload, confirm
  header stamp + hello nStr/nPerStr in a STAT, one burst, decode,
  conflict audit. Handheld S14P-1922 reference: phone 193/200, 5
  spatial conflicts flagged (a26/b115, a28/b117, a77/b103, a78/b102,
  a147/b160), chain conf 0.777–0.900, no motion flag; 1917-era
  baselines phone 190/200, console 195/200.
- **§10-era gate defaults (1922→1927, sweep-validated 02 Oct)**:
  page defaults are now mask 100 / amp 40 / margin 6 with full-res
  ±4 px NCC refine ON — console-led optimisation, see §13. The §10b
  era values (mask 175→150→100 history, amp 60, margin 25→10) remain
  accurate history; 10b's "never ≤160" mask rule is superseded at the
  NEW amp-40 regime (mask 100 is console-optimal there; the ≤160
  phantom risk was measured at the old amp-60 regime).

## 13. 02 Oct console-led optimisation + capture-only corpus (as of S14P-1928)

Evidence: `reports/console-decode-20261002-run2-6.md` +
`reports/console-decode-20261002-sweep-results.json` (450 rows), QA
`tools/cdp_1904_check.py` check 10, repair provenance
`runs/daemon/repair_report.json`.

**Baseline (console, first REAL multi-string-ish round set)** — CFG
`cwc=1 cwcN=400 nStr=2 nPerStr=200` + gates mask 100 / amp 40 /
margin 6, six 19-frame rounds 12:40–12:45 captured box-perspective
(~3 m off-axis, 9.5–16 px/LED): runs 2–6 = **354/309/358/389/300 =
1710/2000 (85.5%)** at fullres rad 4. Round spread tracks per-round
luma, not identity: zero LEDs missed in all 5 rounds, 35 missed ≥3
of 5, misses viewpoint-scattered (worst ids 394–399, 246/248, 78–82)
— no dead band, no codeword-structure failure, and at N=400 no
over-claim/phantom evidence. Missing-LED repair: the daemon writer
damaged 8 of 95 frames (7 truncated + r2 p01, which was both
morning-blocked and PIL-broken; r2 p00 was the other morning-blocked
label) — all **95/95 re-derived byte-exact from
`runs/daemon/capture.txt`** (host window 12:40–12:45 verified, PIL
406×720, SOI/EOI checked); damaged originals preserved under
`runs/daemon/runs/truncated_backup/` + `run2/morning_0843_backup/`.
The 5-frame 12:40:26–40 r1 tail (labels `cwc:r1:*`) lives in
`runs/daemon/runs/run1af/` — jpg+meta only, NOT a decodable run
(morning r1 in git stays untouched, still truncated p01/p07/p13 as
committed). No CWCSTATS/CWCDEC exists for this burst anywhere in
capture.txt — phone decode stats never shipped before the daemon died;
console numbers are today's only decode ground truth.

**90-combo gate sweep** (mask {100,125,150,175,200} × amp {40,60,80} ×
margin {6,10,14} × fullres {off, rad4}, totals /2000 over runs 2–6):

| mask | amp | margin | fullres | total | note |
|------|-----|--------|---------|-------|------|
| 100 | 40 | 6 | rad4 | **1710** | promoted set — today's optimum |
| 100 | 40 | 10 | rad4 | 1708 | margin 10 trades run6 (−4) for r4/r5 (+1) |
| 125 | 40 | 6 | rad4 | 1704 | |
| 150 | 40 | 6 | rad4 | 1682 | |
| 150 | 60 | 10 | rad4 | 1458 | old default — −252 |
| 100 | 40 | 6 | off | 1197 | best fullres-OFF combo |

Read-across: **the promoted set IS the sweep optimum** (top total,
top-or-tied min per run at min 300); amp 60→40 is the single biggest
lever (amp 80 collapses: best amp-80 combo 1068); fullres rad-4 is
worth +513 (1710 vs 1197) — mandatory off-tripod; mask 150+ loses
28–456 at amp 40 (worst: mask 200 / margin 14).
**Shipped as compiled defaults in S14P-1927/1928** (page CFG + firmware),
not left to CFG: mask `cwcMaskThr` 100 (was 150), amp `cwcAmpGate` 40
(was 60), margin `cwcMarginGate` 6 (was 10), fullres refine on.

**S14P-1928 capture-only corpus choreography** (QA check 10 PASS,
38/38): the burst's paint/grab/settle choreography is UNCHANGED
(P00 1 s primer 50%-on → P01..P17 at `cwcSettle` → fast all-on
master) but the in-page decode/reg/ship stack is bypassed — frames
accumulate in benchStore ACROSS bursts (run-numbered labels
`cwc:rN:*`), zero decode lines / zero CWCDEC-CWCDECS-CWCSTATS / zero
auto-ship, Burst re-enables promptly after each burst; the operator
ships the accumulated store with the page's **'Send frames (all)'**
button (one benchPull, non-destructive — the store keeps its frames,
cleared only at the next decode-mode burst start / reload). Store
overflow in capture mode DROPS THE OLDEST frame with a loud log line
naming the lost label (no silent wrap). Rationale: the box's HTTPS
was dying all afternoon (see the TLS-heap wedge below); console-side
decode of raw frames is the reliable path while the page stays a
camera (this is how the 1710 baseline + sweep corpus was captured).

**Box TLS-heap wedge (open, undiagnosed)**: esp-tls-mbedtls
`mbedtls_ssl_setup -0x7F00` (ALLOC_FAILED, no heap for a new TLS
session) storms killed the box's HTTPS twice today — ~08:32–08:43
(the S14P-1926 incident window) and 12:57:15→12:58:03, right after
which the daemon died (last capture 12:45:10 `[PHONE-LOG] end`,
daemon gone by 12:58, never recovered on its own). Same signature as
the 30 Sep archive wedge (archive/S14-BENCH-SESSION.md: reboot clears
it). **Working recipe (02 Oct, proven twice)**: RTS→EN pulse via the
bench serial (a flash-reset tap reboots the box) → boot banner replays
(`=== poc_survey S14P-1928: ...`), page can reconnect. Recovery drill:
restart the daemon, verify `=== daemon start ===` + `[LOGA] persistent
arm ON` in its output, then drop directives in `runs/daemon/cmds/`.
Root cause (heap fragmentation vs leak vs socket pressure) is NOT yet
diagnosed.

**Codeword-bank feasibility (02 Oct, computed + verified)**:

- 9-of-18 @ 1600 codes, d≥5: IMPOSSIBLE — constant-weight sphere bound
  = floor(C(18,9)/82) = 592 < 1600 (greedy reached only 206).
  d_min 4 is the ceiling for the 9-of-18 bank; 1710/2000 is what
  one-bit-correcting identity buys at 3 m off-axis.
- 12-of-24 @ 1600 codes, d≥8: FEASIBLE — C(24,12) = 2,704,156
  enumerated; the extended Golay [24,12,8] weight-12 subcode holds
  2,576 codewords, pairwise d_min 8 VERIFIED on the full subcode; the
  first-1600 and first-400 prefixes both keep d_min 8 (prefix property
  holds for the new bank). d≥9 impossible (sphere bound 600 < 1600).
  Burst cost: 24 planes per burst instead of 18 (+6 planes ≈ +30%
  burst and decode time).
- **OPERATOR DECISION (02 Oct): the 12-of-24 era is designated
  S14R-0000** — a NEW R-line, the next build after S14P-1928. The P
  line (9-of-18) is closed at S14P-1928; S14R work starts from the
  Golay subcode bank + the S14P-1928 capture-only console pipeline.

**Open items (as of S14P-1928)**:

1. **TLS-heap wedge root cause** — undiagnosed (see above; reboot/RTS
   recipe is the workaround).
2. **Phone-vs-console parity for runs 2–6** — UNVERIFIABLE: no
   CWCSTATS/CWCDEC ever shipped (capture-only mode, then the daemon
   died 12:58 before any later telemetry).
3. **First REAL 8×200 multi-string rig round** — still pending
   (today's 5 rounds ran nStr=2 at cwcN=400 box-perspective).
4. **Phone 1600-id result-view UI** — how a 1600-id map should
   render/select is still open (200-id boxes fine).
6. **Test-LED demo** — RAN AND COMPLETED, 14:38–14:39 under S14P-1928:
   mechanical end-to-end PASS. See HANDOFF-S14P.md open item 6 for the
   02-Oct-evening operator-corrected interpretation (strings 1-3 connected;
   bank-cap gap produced claims to id 1570; sub-600 confirms substantially
   real; decode domain clamp = standing fix). Operator decision (verbatim):
   no new one-LED-lighting UI — the existing all-on button already shows
   where to cut a string.
7. **ledcloud/2 §8 export tool** — CWCDECS carries the §8 field
   names; the converter is still not built (console verdicts stay
   authoritative for cloud export).

## 14. Test-LED demo (02 Oct, 14:38–14:39, S14P-1928) — mechanical PASS, honest verdict

The queued demo CFG was delivered and the test-mode path ran to
completion (runs/daemon/capture.txt, `=== daemon start ===` 14:28
session; CFG acked 14:28:29, first test burst telemetry 14:38,
completed round 14:38:18→14:39:15; global id 599 = string 3 pixel
200; the bank is the 9-of-18 P line — 12-of-24 is S14R-0000, §13).

- **CFG delivered**: `{"cwc":1,"cwcN":600,"nStr":3,"nPerStr":200,
  "cwcSuppress":1,"cwcMaskThr":100,"cwcAmpGate":40,"cwcMarginGate":6,
  "cwcTestMode":1,"cwcTestLed":599}`.
- **Completed-round telemetry (CWCSTATS 14:39:15)**: build S14P-1928,
  n = 18 planes, testMode 1, testLed 599,
  testBits = [3,4,6,8,11,12,13,15,17] (LED 599's codeword ON planes),
  chain conf 0.817–0.902, confirmed 275, conflicts 33 (gates amp 40 /
  margin 6, suppress 1).
- **HONESTY NOTE (mandatory reading)**: the physical rig has FEWER
  installed strings than the CFG claimed — the operator visually
  confirmed only one 200-LED string lights on all-on. With
  cwcN = 600 > installed, the decoder over-claimed via the same
  d = 4 cousin-phantom class as the S14P-1927 morning incident
  (cwcN = 1600 → 646 = 392 real + 254 relabels; here cwcN = 600 →
  275 with 33 conflicts). **275 is NOT a detection rate**, and the
  result-view 'led 599' chip was a phantom relabel of a real
  string-1 lamp, NOT proof that string-3 pixel 200 lit.
- **Verdict**: the test-mode path (CFG → burst → codeword-bit paint →
  decode → highlighted chip) works END-TO-END MECHANICALLY;
  interpreting its output requires installed-count == cwcN (the
  decoder must never be pointed beyond what is physically wired).
- **OPERATOR DECISION (recorded verbatim)**: no new
  one-LED-lighting UI feature — the existing all-on button already
  shows where to cut a string.

## 15. The S14R era as-built (02 Oct, S14R-0000 → S14R-0002)

The R-line replaced the P line AFTER §13's designation. State at this
writing: **S14R-0002 is committed at origin/main = 3f22c2a, QA PASS
(exit 0, check 12), embed roundtrip 47408-byte exact — and NOT YET
FLASHED** (the box still runs S14P-1928; the flash is the next physical
action, then the first real 0002 burst). Verified 03 Oct: re-compile
1,342,722 B = 68% (min_spiffs, RAM 18%); `tools/verify_embed_s14r.py`
all-True (build stamps agree, 12OF24 bank served, 9OF18 retired,
S14R-0001 UI ids kept, S14R-0002 probe/adaptive-mask ids present); bank
recomputed d_min 8 / weight 12 / per-plane ON exactly 800 at N=1600 /
800 consecutive complementary pairs. Era corpora live under
`runs/daemon/runs/s14r-*` (gitignored imagery; metas/ledpos packs
tracked); commit history: 8925ef8 (0000) → 8c3fe3d (0001) →
e5534dc/1e7b843/c7e4aeb/b406a61 (round + corpora investigations) →
3b22b35+3f22c2a (0002).

### 15.1 S14R-0000 — 12-of-24 Golay bank (commit 8925ef8)

- **Bank swap**: CWC 9-of-18 → extended-Golay [24,12,8] weight-12
  subcode, per §13's feasibility: `tools/codewords_12of24.json` +
  `page/codewords.js` (1600 codes as ON-plane index lists, weight
  exactly 12, d_min 8, emitted as **800 complementary pairs** so every
  plane paints EXACTLY N/2 lamps at every even prefix — §2's exact-duty
  requirement without per-plane selection; generator
  `tools/cwc_bank_gen.py`, checker `tools/cwc_bank_check.py` rc 0).
  d_min 8 ⇒ the identity gate requires a **d8 margin** (score minus
  best competitor / 12 > `cwcMarginGate`), and d≥9 is impossible
  (sphere bound 600 < 1600) — this bank is the ceiling, as computed in
  §13. The 9-of-18 bank is retired from the page (kept at
  `tools/codewords_9of18.json` as the P-line record); CLI decoders go
  `--bank`-aware (12of24 default, 9of18 legacy parity).
- **Protocol delta**: 24 planes → `frame-bits` message is now exactly
  **246 B** (6 B header + 240 B = 1920-bit plane), consistently in the
  page, firmware and mock; burst = **25 frames** (primer-plane P00 held
  1 s + P01..P23 + fast all-on master). QA STAMP S14R-0000 full
  10-check PASS exit 0 (bit-exact LED 57 across 12/12 ON planes,
  per-plane 100/200 lit at the 8×25 burst-3 shape, capture-only 25-frame
  + bulk 50/50 intact); compile 1,333,698 B = 67%.
- **Console decode**: `tools/cwc_pos_decode.py` amp normalisation
  follows the bank weight (score/12), registration/direct-shift
  machinery unchanged.

### 15.2 First real 12-of-24 rounds + round-1 miss investigation (16:16 / 17:43)

- **16:16 round** (report
  `reports/s14r-0000-16h16-miss-classification.md`): phone 309 / console
  **283/600** at gates mask 100 / amp 40 / margin 6; multi-phone parity
  on the 269 shared ids: |d| median **0.0 px** (p90 2.0). cwcN=600
  honored (max id 599 — §14's cap-gap phantom class is dead while the
  bank maps only wired ids). **277 misses classified by instrument**
  (stacksig-rebuild, CLI-amp parity exact): 191 amp<40 (ring med 224 —
  the bright wall), 57 suppressed (colocated, within 3 px of a stronger
  claim), 25 truly-absent/hidden, 3+1 margin/contest. **Measured law:
  amp ≈ 0.54 × (255 − wall)** at this exposure (ON planes pinned at
  sensor clip 255; modulation depth capped by (255−wall)) — miss rate
  rises monotonically with wall luma, 200–239 bucket misses 80%.
- **17:43 operator-aimed round** (report
  `reports/s14r-0000-17h43-aimed-round-decode-and-fixpath-verification.md`):
  phone 328 / console **317/600**; parity shared-L1 med 0.0 px again
  despite re-aim; registration 10× steadier (direct shift med 0.26 px).
  Exposure moved exp 699.97 → 200.02 (AE, not CFG): the wall-law
  constant lifted **0.538 → 0.97** (= the per-plane gain k̄, as the
  algebra predicts), bright-bucket (200–239) miss **80% → 54%** — the r1
  law was exposure-specific, not universal.
- **amp25-without-guard REFUTED** (the round-2 position audit):
  relaxation counts reproduce (+97/+131) but **43–59% of the gains are
  impostor placements** (far-orphan sites >6 px from any registered
  lamp, amp ~1–2; a systematic string-3 560–599 family claims
  string-1/2 lamp sites in BOTH rounds). True at-anchor gains: +33
  (+34 at amp20). Rule: **hold amp 40 for identity-grade counts**; any
  relaxed count must pass a ~6 px own-anchor position guard; r1's
  "+97, zero regressions" line is corrected in-file.
- The 560–599 string-3 tail went systematically sick from the 17:43
  viewpoint (view-dependent blindness, confirmed fine from r1's angle)
  — per-view "evidence-absent" bookkeeping (~35–55 ids/round) was
  recommended to stop counting sensor/view limits as decoder misses.

### 15.3 Android exp500 corpora (r2–r5, commit c7e4aeb)

Console decode of 4 Android bursts (`runs/daemon/runs/s14r-and-r2..r5`,
exp=500.05, all hygiene checks green): **429 / 549 / 492 / 387 of 600;
union 583 (97.2%)**; hit histogram 4-of-4 = 269. 17 never-seen ids
anatomised (5 bright-wall, 2 low-contrast, 2 view-dark, rest on-clip).
**The monotone wall law BREAKS at this exposure**: r4 (darkest view,
master histMed 74.5) misses hardest in its darkest bucket — **77 anchor
misses are MASK-class** (master blur < 100; lamps still read blob peaks
136) → the adaptive-mask-thr candidate. Amp is NOT cross-burst-comparable
(within-id spread med 59 even normalised); homography is required for
cross-burst mapping (translation-only false-spreads 200 px). Colocated
pairs (<5 px) 161 across bursts, 42% same-string — the page's
conflict-tolerant model (flag, both confirmed) stays the right
semantics.

### 15.4 S14R-0001 — operator UI + (iOS) exposure round (commit 8c3fe3d)

- **Operator UI**: Burst button directly under the camera canvas
  (burstRow), 8 one-press string buttons `bStr1..bStr8` (operator
  nStr override mid-session; a box CFG nStr still drives the rig),
  Survey button removed, 'Capture only' label, wake-lock requested
  automatically at boot/on visibilitychange (separate wake button
  GONE), LED-number font 13→10 px and chips 2 px closer. Compile
  1,335,810 B = 67%; QA PASS exit 0 with new check 11.
- **Exposure levers at burst start** (`applyEvBiasAtBurst`): Android
  re-applies the CFG `evBias` AT BURST START (the r1 finding: it was
  applied once in the camera-ready callback only, so CFG changes never
  took effect; 03 Oct: rig runs `evBias` −1 everywhere — −3 retired
  before its first real burst); iOS runs the **POI tap lever** (`cwcPoi*` — iOS honours
  pointsOfInterest, never exposed exposureCompensation).
- **iPhone POI corpora r1–r4** (commit b406a61, report
  `reports/s14r-0001-ios-poi-console-decode-exposure-and-cross-round.md`):
  console **287 / 440 / 487 / 257 of 600; union 534 (89%)**, 66 ids
  never in any burst — **100% rival-eats** (every one of the 600 had a
  masked site amp ≥ 8 — usually ≥ 40 — somewhere; zero mask-class, zero
  amp-starved). THE FAILURE AXIS FLIPPED from wall luma to codeword
  interference in the dense swarm; miss% tracks how much string sits
  behind each anchor, not wall luma (r2/r3 bucket tables INVERTED vs
  0000). Gate relaxation on this corpus: gains **97–100% impostor**
  (own-anchor guard-pass 6/472 total) — amp 40 stands. **POI tap was
  PIXEL-INERT** (no measurable AE change across all 4 bursts) AND the
  wire carried `exp=''` — **the exposure read-back gap** made the
  feature unverifiable from metadata. Fixed in 0002 (§15.6).
- r2↔r3 was one aim (95.2% of 413 shared ids re-place ≤5 px); r1 and
  r4 were separate re-aims — cross-aim per-id disagreement is viewpoint
  + interference, not identity churn.

### 15.5 CLI identity lever (queued, the top open item)

Evidence-ranked across all corpora (§15.2–15.4): the CLI's
`tools/cwc_pos_decode.py` argmax-per-site + ±3 px suppression +
strongest-codeword dedup **eats contested codewords** — contest +
suppressed classes run 108–343 ids/burst on the iPhone corpus and
~57/set on Android, while relaxed gates buy impostors. The redesign —
**same-codeword dedup + demote rival-site claims ≤5 px to
conflict-FLAGGED entries (page-parity), instead of silent suppression**
— is designed and evidence-backed, NOT yet built. Keep amp 40 /
margin 6 / adaptive mask; treat per-burst counts via mechanisms +
union, never raw totals; per-id amp is aim-relative, never cross-round.

### 15.6 S14R-0002 — pre-burst brightness calibration + adaptive thresholds (commits 3b22b35 + 3f22c2a)

- **Pre-burst BRIGHTNESS PROBE** (the headline; page keys `bProbe*`,
  tuning evidence `tools/tuning_s14r0002.json`): a solid all-ON paint at
  `bProbeStart` 120, ~2 s between steps (`bProbeDelayMs`) so the AE
  re-converges, 2–3 MULTIPLICATIVE iterations (L×target/measured,
  clamped ±1.5×/step), metric = **lamp-core P90 (5×5 core max) minus
  the local background-ring median** — drive cores to the
  JUST-BELOW-CLIP knee: band **coreP90 235–250** (`bProbeMin/Max`)
  with lamp-pixel clip fraction **≤5%** (`bProbeClipHi`, cores ≥ 252;
  `bProbeCoreMinLuma` 170 defines a core). After the final level:
  HOLD it and wait a FIXED `waitSettleMs` 2000, then fire — the
  operator-simplified hold, NO settle gate. Calibration context: ALL
  14 corpora bursts overdrove (lamp-core clip 17.1–56.0% at fixed
  bBurstB 150, coreP90 = 255) — the knee band is new ground, chosen
  because clipped cores strangle amp at (255 − OFF_lamp) while spill
  keeps growing (§15.2's mechanism, inverted to a target).
  - Every CWC burst runs it when `bProbe: 1` and paints its planes at
    the CHOSEN level (bBurstB keeps a telemetry-fallback role); the
    burst is probe+settle-gated ('probe done:' precedes 'cwc burst:' by
    ≳1.5 s).
  - **`PROBE` serial directive** = the standalone calibration run (no
    burst): per-step telemetry `{L, P90, clipPct, histMed}`, rig
    returns to black, telemetry ships as a `"probeOnly":true`
    CWCSTATS/BSTATS via the next pull.
- **BSTATS/CWCSTATS carry the probe + exposure telemetry**:
  `{bright, histMed, clipPct, probeIters}` + `coreP90`+`probeSteps`
  + **`expAtBurst`** — the 0001 exposure read-back gap (§15.4, `exp=''`)
  is closed; exposure state is verifiable from every burst now (the
  0001-POI-unverifiable lesson).
- **Adaptive mask threshold** (`cwcMaskAdaptive 1`,
  `cwcMaskK 1.12`, `cwcMaskFloor 45`): page and CLI in exact parity —
  `thr = min(cwcMaskThr, max(cwcMaskFloor, cwcMaskK × histMed(master)))`;
  the CLI ships `--mask-thr auto` semantics (adaptive default ON, box
  CFG still overrides the page keys). A/B evidence (identical CLI
  machinery, gates 40/6, fullres rad 4): **and-r4 (histMed 74.5) thr
  100→83.4 = 492→514 confirmed, +22, −0 regression**; and-r2/ios-r2
  saturate at 100 (byte-identical sets 429/440); and-r5 (histMed 83.5
  → thr 93.5) 387→386 — the documented cost is hairline site 398
  losing an argmax contest (same class as the base-gate 3–4%
  interference misses; the threshold saturates at 100: +0 net beyond
  the gain).
- **Operator UI round 2**: the dark square behind result-view LED
  numbers REMOVED (text-only white labels on a 2 px black stroke);
  Android evBias stays −1 (−3 intent retired 03 Oct: never applied in a real burst, all calibration corpora at −1, iOS ignores it); POI tap gains visible feedback
  (4 s crosshair at the metered point).
- **QA: RESULT PASS, exit 0** — checks 1–11 kept green (stamp, CFG +
  burst shape, direct registration, decode, canvas, test branch,
  multi-string bit-exact, replay, capture-only ×2 = 50 frames + bulk
  send, operator UI) and NEW check 12 exercises the probe end-to-end
  on the fake camera: PROBE directive telemetry ships, a bProbe burst
  fires only after the probe completes + the settle hold elapses, and
  paints at the probe-chosen brightness. `verify_embed_s14r.py`
  roundtrip PASS (47408); compile 1,342,722 B = 68% (RAM 18%;
  re-verified 03 Oct pre-flash). Commit 3f22c2a additionally
  quarantined drained/dup run dirs and added run7 residue for corpus
  provenance.

### 15.7 Where the S14R era stands / what is next

1. **FLASH S14R-0002** (parent's action right after this doc task),
   then the **first real 0002 burst** — the first evidence the probe +
   BSTATS telemetry work on the real rig (expect lamp-core clip in the
   single digits and BSTATS {bright…} populated; the 14-burst corpora
   all overdrove at fixed b=150).
2. **CLI suppression/ownership redesign** (§15.5) — the top identity
   lever for the 100%-rival-eat iPhone class.
3. **POI tap real-rig validation** — was pixel-inert in 0001; 0002's
   crosshair feedback + exp readback make it measurable now.
4. **TLS-heap wedge root cause** — undiagnosed; `-0x7F00`/`-0x7780`
   storms recurred in the 06:44 wedge of today's capture.txt; RTS→EN
   reset recipe stays the workaround.
5. Adaptive-mask second-order costs: watch hairline sites (the 398
   class) when re-sweeping; `cwcMaskAdaptive 0` remains the fixed-thr
   fallback.
6. ledcloud/2 §8 export converter — still not built (console verdicts
   stay authoritative).