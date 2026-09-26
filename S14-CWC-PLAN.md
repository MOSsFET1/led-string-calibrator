# S14 plan — CWC LED position detection (Oliver + Nellie, Sep 25 2026)

> **27 Sep amendment — the off reference frame is DROPPED (agreed, Oliver +
> Nellie).** §3 step 3 (All OFF → grab reference) is REVISED: the protocol is
> now master + 18 planes = 19 frames. Rationale (all verified against the
> real bank/pulls): NCC is gain-invariant so registration never needed the
> off frame; the point set moves to the pile-up Σₚ(master − planeₚ) — 9
> coherent hole samples per LED vs 1, SNR ×3 over the single master−off
> diff; the per-LED bit read uses per-plane diffs against the pile-up's
> local background (18-frame evidence per LED); collocation detection
> becomes NOR-weight < 9 (ON-mode read has no 0-of-18 signal from an
> unpainted pixel) — OR-weight 11–18 / NOR-weight 0–7 both cleanly off the
> single-LED weight of exactly 9. Bright-background handling is sign-based:
> OFF-planes always subtract light, so static background cancels, dynamic
> background dilutes by √18, and brighter-than-master events pile POSITIVE
> (invisible to the dark-hole read). Failure class: a background source
> mostly DIMMER than its master-frame state accumulates a negative impostor
> — caught by the standing domK/d10out/serpentine guards. Built + validated
> in page S14J (see S14-BENCH-SESSION.md). Optional all-off diagnostic grab
> at burst end remains bench-only (noise-floor measurement), not part of
> the decode.

Direction set by Oliver (24 Sep): drop intensity-level encoding; identify LEDs
by binary plane codes; capture protocol: all-on 1 s (AE warm-up) → all-on
frame → all-off reference → all-on 1 s → N plane frames. All runs BOX-driven
while the phone is unavailable (returns in days).

## 1. Corrected scheme: 9-of-18 (the 7-of-14 claim was wrong)

The 24 Sep discussion pitched 7-of-14 (C(14,7) = 3432 ≥ 1600). Combinatorially
impossible for 1600 codes: two 7-of-14 codewords at d ≥ 4 cannot share a
6-plane subset, so the count is bounded by C(14,6)/C(7,6) = 3003/7 = 429 —
measured greedy exhausted at 156. Corrected candidates (greedy max-min +
column balance, `tools/cwc_feas2.py`):

| scheme | 1600 codes? | d_min | columns | duty | frames (+master+off) |
|---|---|---|---|---|---|
| 7-of-14 | no (156) | — | — | — | — |
| 8-of-17 | no (1196) | — | — | — | — |
| **9-of-18** | **yes, 15 s** | 4 | **800/plane exact** | 50% | 20 |
| 10-of-20 | yes, 91 s | 4 | 800/plane exact | 50% | 22 |

**Decision: 9-of-18.** Codewords banked at `tools/codewords_9of18.json`
(1600 codes, d_min 4 = single-error correction, every plane exactly 800 ON,
duty 50% per LED). 18 planes + master + off = **20 frames total for ALL
1600 LEDs** — vs 1600 per-LED paint+diff cycles in the S13 hole scheme; the
frame count is the win: 20 captures + two 1-s paint brackets ≈ **12 s per
burst at today's measured 0.5 s/grab cadence** (10 s of grabs at 2 Hz + 2 s
of brackets; 6 s at 5 fps if the phone bench proves it).

Frame budget notes: 50% duty at 1600 px = ~1.2 A/lane (safe); the all-on
master at 1600 px is ~2.35 A/lane at b=200 → **cap master/plane brightness at
b≤150** (1.77 A) per the polyfuse ladder; detection is brightness-insensitive
(S13 matrix: 150/150 at b=120/160/200/255).

## 2. Bench step 1 DONE — camera-movement tolerance (artificial offsets)

Oliver's step, done offline on existing pulls (no phone/box):
`tools/delta_sweep.py` + `tools/delta_sweep_reg.py`. Master synthesised as
pixelwise max over each run's 16 pulled pair frames (ring keeps only pairs;
real master cross-check pending a pull that still holds one). Fast detector
(`detect_holes_fast`, cv2 CC + array merge) asserted 16/16-identical to the
page-mirror detector (`offline_hole_verify.detect_holes`) at offset 0.

**Unregistered (pair shifted vs master, diff as-is)**:

| offset | b=200 baseline | b=120 run |
|---|---|---|
| 0 px | 100% | 100% |
| 2 px | 12.5% | 6.2% |
| 4–10 px | ≤2% | ≤2% (misses grow: 208/640 at 10 px) |

Cliff at 2 px in both: a shifted pair's diff sprouts a positive crescent at
every LED rim; the true hole loses to impostors (no misses, all wrong-capture).

**Registered (cv2.phaseCorrelate per frame, roll back, then diff)**:

| offset | b=200 baseline | b=120 run |
|---|---|---|
| 0–6 px | 100% | 100% |
| 8 px | 78.9% | 100% |
| 10 px | 75.6% | 100% |

Registration itself: median residual 0 px, worst 0 px, 40/40 exact on random
offsets up to ±10 (sqrt-compressed luma, Hanning window). The b=200 dip at
8–10 px is pre-registration residual (integer-pixel roll-back leaves ≤1 px
error → 75–79% survive the 6 px tolerance); b=120's tighter holes tolerate it
fully. Next: sub-pixel refinement (parabolic peak on the correlation surface)
should close the 8–10 px band to ~100% everywhere.

**Conclusion: every plane frame must be registered to the master before its
diff.** Integer-pixel registration makes the scheme robust to ≥6 px of
movement per frame — far beyond real inter-frame drift (worst observed real
cross-run shift: 4 px). Hand-held capture becomes viable.

## 2b. Beyond-tolerance movement: detect + retake (Oliver's question)

The failure signature at large offsets is wrong-blob capture, not silence —
and the CWC structure turns most of it into *visible* decode errors. Layered
checks, each with its own retake granularity:

1. **Per-plane ON count** — every plane must show exactly 800/1600 LEDs ON
   (75/150 in the 150-px burst). A corrupted plane read shifts the count →
   retake that plane (~1–2 s).
2. **Per-LED weight** — each LED must read 9 of 18 planes ON. d_min 4
   corrects a single bad frame silently; a decode distance > 1 → re-read
   that LED from the affected planes.
3. **Registration confidence + master re-anchors** — phase-correlation
   confidence gates each frame; a master re-grab every ~6 planes measures
   real drift master-to-master. Beyond threshold → roll back to the last
   good master and resume from there.
4. **Serpentine continuity** — decoded IDs must trace the string's physical
   path (the S13 impostor guard); a locally broken path → targeted re-read.
5. **Burst sanity** — frame count/timing checksums catch gross events
   (knock, AE jump) → full retake, cheap at ~12 s per burst.

Limits, stated honestly: retake cannot fix a physically hidden LED (px95
class — hidden flag + interpolation stays), and mid-burst scene changes
(passer-by) show up as plane inconsistency → detected, then re-run.

## 3. Capture protocol (Oliver's spec, implemented as the box sequence)

1. All ON 1 s — AE settles (phone AE adaptation measured in the ~0.2–0.5 s
   class; 1 s is generous).
2. Grab master frame (registered positions + per-plane reference).
3. All OFF → grab reference as fast as possible (before AE climbs).
4. All ON 1 s — AE returns to the master's exposure.
5. 18 plane frames at 50% duty, b≤150: plane p paints LED i iff bit p of
   LED i's codeword is 1. Constant per-frame load keeps AE frozen.
   Codewords: codewords_9of18.json — 1600 codes, weight 9 of 18 planes,
   d_min 4, columns balanced exactly 800/plane (verified); the first N
   are prefix-valid for any N (100–1600), first 150 balanced 75/plane.
6. Per plane: register to master (phase-correlate) → diff vs off-reference →
   detect → per-LED bit read at expected (x, y) ± tolerance.
7. Decode: nearest codeword within d 4 (syndrome-free popcount table);
   serpentine-continuity gate on the decoded ID path (impostor guard from
   S13); LEDs failing confidence → interpolated (§6); hidden/dead flagged.
8. Master+off grabbed once per burst; mid-burst re-anchor only on health
   triggers (§6); CFG knob can force an extra anchor.

## 4. Dead, hidden, and collocated LEDs

**Dead or physically hidden LED** — its hole never appears (dead: never
painted; hidden: painted but occluded, e.g. px95 whose accepted blob was
proven to be an off-path scene feature). The read for that LED is 0-of-18
(weight 0 ≠ 9) → flagged, and the map position interpolates from its
serpentine neighbours (h−1, h+1 anchor the point; h−2/h+2 check). Same path
as the S13 px95 handling; never a threshold change. A *stuck-ON* LED (reads
18-of-18) is the mirror signature — flag + interpolate.

**Collocated LEDs (two LEDs, one camera point)** — both paint the same
(x, y). Their reads are the OR of two codewords (weight 11–18 measured over
random pairs; single LED = 9), so collocation is *detected* by weight alone.
However the pair is NOT uniquely recoverable from one burst: brute-force
all-pairs decode of random OR-reads gave 1 unique solution in only 2 of 300
cases (median ~70 candidates, worst 848) — a 9-of-18 OR collapses pair
identity. Designed resolution, cheapest first:
1. **Serpentine inference** — on a string, consecutive IDs are adjacent in
   3-D; a gap between decoded IDs i and i+2 at one point strongly implies
   the missing ID i+1. Decodes both IDs at that point in most cases.
2. **Disambiguation burst** — when inference can't resolve (several IDs at
   one point), re-shoot ONLY those LEDs with a fresh code assignment
   (different sub-code), same 20-frame protocol at the affected points.
3. **Manual pin** — the app's map editor (S13's manual workflow) as fallback.
Collocated points are stored once in the point cloud with both IDs attached.

## 4b. Selectable strings × LEDs-per-string (100–1600)

Config is `nStr × nPerStr` (or a single `nLeds`); total N selects the map
size. The codeword design makes this FREE — verified on the bank:

- **Prefix property (measured):** the first N codewords of the 9-of-18 bank
  keep d_min 4 (hereditary) AND perfect column balance — every N tested
  (100/150/200/400/800/1200/1600) lights exactly N/2 LEDs per plane, spread
  0. The greedy's balance scoring made every prefix self-balanced, so N is
  just an index count: no re-selection, no regeneration.
- **Frame budget is N-independent:** 18 planes + master + off = 20 frames
  whether N=100 or N=1600 (the scheme's core win over per-LED pairs).
- Plane sanity check becomes "exactly N/2 ON" (was 800); per-LED weight
  check unchanged (9 of 18); serpentine gate gets the topology from config:
  string s owns global IDs [s·nPerStr, (s+1)·nPerStr), folds inside a string,
  and string transitions are known breaks in the ID path.
- Box already carries the knob (NPX= sets nPx; hello ships it to the page);
  CFG gains nStr/nPerStr so the map editor and the decode gate know the
  string structure. Smaller N at the same camera framing = larger pitch =
  EASIER detection (bigger blobs, more residual headroom).

## 4c. Portrait vs landscape — yes, with one rule and one gap

The page is already orientation-agnostic where it matters: processing
dimensions come from the TRACK's real aspect (`sizeForVideo` scales the long
side to 720 and the short side to match — "nothing is ever squashed"), every
detector uses W and H from that geometry (windows are fractions of the frame
DIAGONAL — `holeWinFrac` × hypot(W,H)), and no S13/S14 code assumes
taller-than-wide. The point cloud is stored in whatever frame the capture
used, so a landscape capture just yields a landscape map. Both orientations
work TODAY for the S13 flow.

S14-specific rules to hold:
1. **One burst = one orientation, locked.** Handheld rotation BETWEEN frames
   is absorbed by the similarity registration (rotation is part of the ECC
   model); a MID-BURST 90° flip is not a similarity — it's the
   health-triggered re-anchor case (confidence collapses, burst aborts, redo
   in the new orientation). The map is tied to the orientation it was shot
   in; a later burst in the other orientation needs its own master (master-
   to-master correlation is the drift alarm AND the orientation check —
   90° shows up as near-zero correlation, unambiguous).
2. **Resolution asymmetry is the real cost.** Today the long side is capped
   at 720 px: portrait proc = 406×720, landscape proc = 720×406. For a
   wide facade (the landscape case), the short side only gets 406 px of
   horizontal detail — at distance that's the pitch squeeze multiplied. If
   wide installations need it, request landscape-native capture (ideal:
   1280×720 track in landscape = full 1280 across) — a getUserMedia/
   screen-orientation change, not an algorithm change.

## 5. Frame cadence (measured, not guessed)

Survey-mode pulls show median 0.50 s between frame grabs (min 0.40 s) —
phone-side JPEG acquisition is the floor today, not the box. Exposure was
pinned across 16 frames spanning 7.9 s (`exp=500.05 aem=continuous` in every
header), so the AE-freeze assumption holds over bursts far longer than ours.

**5 fps (200 ms/grab) is not yet proven** — the gap is phone-side JPEG
capture+upload. It is plausible (12–16 KB frames) but needs a measured test:
the burst paints are trivially fast (1600 px ≈ 48 ms + reset), so 5 fps
reduces to "can the phone grab+ship a frame in 200 ms". Bench it before
promising; 2–3 fps is the safe fallback. Cadence is NOT tied to AE: at 50%
duty every frame loads the field identically, and S13 evidence shows
exposure stays pinned for 8 s — so grabs can be as fast as the camera
allows, with the all-on → off → all-on bracket protecting the master/off
pair only.

## 6. Handheld reality — chained registration, single-burst target

*Oliver's corrections (25 Sep), both accepted:* all drift numbers above are
TRIPOD-bound — "worst 4 px cross-run" says nothing about handheld, where
whole-burst displacement can be tens of px and slow rotation is likely.
Design target changed to match: **one burst = the whole calibration**; LEDs
not confidently revealed by that burst get interpolated positions (flagged),
not retake bursts. Retakes (§2b) remain only for gross failures (knock, AE
jump, burst checksum).

Why the per-frame strategy must change for handheld: registering every frame
to the burst-start master (translation-only) absorbs global translation but
NOT rotation/scale — a 2° hand rotation displaces points 200 px off-centre by
~7 px, past the 6 px read window, and phase correlation itself degrades at
large direct displacements. Handheld design:

1. **Seeded registration (Oliver's refinement, 25 Sep)** — maintain the
   composed transform T_k; before estimating frame k's motion, pre-shift it
   by T_{k−1} (warpAffine, bilinear — sub-pixel capable), then register the
   pre-shifted frame against the MASTER. The estimator always works in the
   small-signal regime (it only sees one frame's worth of new motion plus
   residual), while every estimate stays anchored to the master — small
   search AND independent per-frame errors (no chain accumulation: T_k =
   Δ_k ∘ T_{k−1} composes, but Δ_k is measured against the master directly).
   NO prediction: never extrapolate from previous deltas (Oliver's explicit
   rule — predictions can go wrong); the seed is always the last MEASURED
   composed transform, and the estimator re-measures frame k's true drift
   from that starting point. JUMP/knock detector unchanged: the inter-frame
   increment |T_k − T_{k−1}| jumping well above its running median.
   (Synthetic A/B 25 Sep: seeded == unseeded accuracy on clean content even
   at 28 px total drift — seeding is kept for the confidence/search regime
   and the knock detector, not for raw accuracy.)
   **Point-2 revisited (Oliver, 25 Sep): the half-pitch read-limit is about
   RESIDUAL, not raw shift — and content toggle is the real hazard.** Measured
   on 150-LED synthetic planes (JPEG q80): plane-vs-MASTER estimates stayed
   accurate at raw shifts far past half-pitch (40, 80 px: residual ≤0.65 px;
   confidence holds ~0.44–0.6 even with only 50% of LEDs lit). Raw shift does
   NOT degrade the estimate — but random plane-to-plane content does: chained
   plane-vs-plane correlation was *biased* (~0.5–1.5 px err, conf 0.33, one
   synthetic sparse-content case blew up to 86 px est at conf 0.31). This is
   exactly why the scheme registers every plane against the MASTER (the
   seeded design): the master's stable full blob-set locks correlation, while
   half the plane's content toggles underneath. Residual budget after
   registration: ~1.5 px worst synthetic, well under the 5 px read budget —
   but REAL handheld + real scene must confirm (tripod real-scene conf was
   0.98; synthetic black-background conf is far lower, i.e. the real scene's
   bright content HELPS the correlator).
2. **Similarity (rotation+scale+translation), not translation-only** —
   cv2.findTransformECC (EUCLIDEAN/AFFINE) per frame; still cheap at 406×720.
3. **Mid-burst re-anchor becomes a health-triggered fallback, not a
   cadence**: if the composed-transform path length or per-frame confidence
   crosses thresholds (fast rotation, knock), re-grab master+off mid-burst
   and re-anchor there. Normal bursts never pay for it.
4. **Motion blur is a non-issue at the measured 500 µs exposure** (hand
   moves µm-scale in 0.5 ms); displacement between frames is the enemy, and
   shorter frame period shrinks it — 5 fps is now motivated twice (cadence
   AND drift), strengthening the case for the phone grab-rate bench.

Single-burst confidence model: a LED is CONFIRMED iff weight 9-of-18, decode
distance ≤ 1, and serpentine-consistent; otherwise INTERPOLATED from its
confirmed serpentine neighbours (hidden/dead LEDs land here naturally). The
map records which class every LED is in — interpolation is a first-class
output, not an error path. An optional second burst later refines
interpolated LEDs only (a CFG knob, not the design).

**What the "6 px" actually is (Oliver's question, 25 Sep) — a layered
residual budget, not a shift limit.** Raw inter-frame shift is absorbed by
registration; what matters is the RESIDUAL after correction:

- ~5 px: bit-read stays clean while the RESIDUAL — the registration
  estimate's error, not the raw shift — stays under the blob core radius
  (~2–5 px). A 40 px shift with a perfect estimate leaves 0 px residual;
  the residual budget is what the bench measures. Not applicable: with
  master-anchored registration each frame's estimate error is independent;
  with chained registration the estimate errors compose down the chain
  (~√N). (Oliver's circle picture is the right model — the old "raw shift
  degrades the read" claim was wrong and was removed.)
  exactly right for exact estimates — "correctly detected and shifted back"
  is the entire difficulty, and the estimator's accuracy, not the shift
  size, is what lands in the read window.)
- ~10–12 px (½ pitch): beyond this a read can grab a neighbour's bloom →
  wrong bit. d_min 4 still corrects a single corrupted frame per LED.
- ~45 px: S13's serpentine gate (holeGateK=4 × pitch) catches gross
  mis-assignment; weight/plane-count checks catch systematic failures.
- Raw shift size itself: phase correlation is global — the Δ-sweep held
  100% at 6–10 px offsets with integer roll-back (the 8–10 px dips were the
  ≤1 px integer residual, sub-pixel refinement's job), and would hold at
  much larger shifts *provided the correlation stays confident*.
- What erodes correlation confidence (the true limit): 50% of LEDs toggle
  between consecutive frames (content change on top of motion), JPEG noise,
  rotation (needs the ECC similarity model, not translation-only), and
  scene content leaving the frame — unrecoverable by any transform.
- Chained small steps keep each estimate small and confident (error grows
  ~√N over the chain) — why the handheld design optimises for small
  per-frame motion rather than tolerating big jumps. If real handheld
  residuals exceed the budget, the upgrade path is blob-landmark
  registration (detected LED centroids as features — a rich landmark set
  at 1600 px) instead of image correlation.

## 7. Open items for the first box burst (150-px string)

- **Bench kit built (25 Sep, awaiting Oliver's return):** page `S14A-1900`
  adds box-driven BURST (all-on N-frame burst, full-res frames in a
  dedicated ring, CFG `bBurst{N,Gap,Hold,B}`) + BRAMP pull; console driver
  `tools/s14_bench.py` (burst/pull/analyse) + runbook
  `S14-BENCH-SESSION.md`. Measures phone cadence (5 fps question) and
  handheld per-frame motion (chained-registration budget). Firmware
  directive parser extended (BURST/BRAMP); compiled 66% min_spiffs; flash
  + phone reload pending (box unplugged — Oliver out).

- Map first 150 of the 1600 codewords onto px0–149 (column balance only
  matters at 1600; note per-plane counts on 150 for the burst log).
- New firmware sequence cmd (burst mode): master/off/planes + per-frame
  phone capture triggers over WS, no per-LED dwell (frames, not pairs).
- Page build stamp bump S13x → S14 series; repack page BEFORE compiling.
- Cross-check a real pulled master vs the synthetic max once a pull with a
  master exists.
- Sub-pixel registration refinement (close the 8–10 px residual).
- Evid/log per-frame pulls: ring keeps only the last 16 frames — pull each
  frame (or shrink payload) so a burst survives the ring.

## 5. Context

- S13 hole survey: 149/150 at b=200, 150/150 at b=120/160; px95 proven
  physically hidden (impostor accept at (154,628) = static scene feature,
  25 px off-path; lower-b "detects" = h94 bloom fragments). Serpentine gate
  + hidden-flag carried into S14 decode.
- S13A page bug fixed (cfg-before-directives, bUsed painted-value capture).
- Tooling added this session: `delta_sweep.py`, `delta_sweep_reg.py`,
  `cwc_feas2.py`, `select_codewords.py` (7-of-14, kept as the impossibility
  record), `codewords_9of18.json`, `detect_holes_fast` (cv2, 16/16-equal).
- Offline verify fix: dominance d10 hoisted out of the per-blob loop
  (loop-invariant; 3.5 s → 10 ms/frame on noisy diffs, identical numbers).