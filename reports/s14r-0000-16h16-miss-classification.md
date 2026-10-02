# S14R-0000 first real 12-of-24 round, 16:16 burst — why 277 of 600 lamps were missed

Investigation of the FIRST REAL 12-of-24 round (`runs/daemon/runs/s14r-0000-16h16/`, 24 planes + master,
wire-extracted, 25/25 SOI+EOI+PIL clean). Operator ground truth: 600 lamps installed (strings 1-3,
many hidden or colocated). Phone decoded CWCSTATS 16:19:57: **309 confirmed / 42 conflicts**, gates
mask100/amp40/margin6, `sitesMasked 131852`. Console baseline (`tools/cwc_pos_decode.py`, same gates,
same frames): **283/600**. Instrumented miss classification below; bright-background hypothesis
CONFIRMED with a measured law.

## Round hygiene (what is *not* the problem)

- **cwcN=600 was honored.** Max phone-claimed id = 599, zero claims >= 600; console max id 599.
  The yesterday-cap-gap phantom mechanism does not apply to this round.
- **Registration/identity parity is excellent.** On the 269 ids confirmed by BOTH phone and console,
  median |dx|+|dy| = 0.0 px (p90 = 2.0; 9 outliers are console wrong-site wins — see suppression).
  Per-plane direct shifts conf 0.897–0.947, shift mag med 7.1 px.
- **Phone and console decode the same banks correctly.** Bank verified weight-12 x1600 (12-of-24 Golay).
- All 42 phone conflicts are SAME-STRING (31 in string 2), d = 2.0–5.7 px, and every conflict id was
  still confirmed by the phone — consistent with colocated/hidden partners sharing one blob, not decoder confusion.

## Console↔phone delta (same gates)

- shared 269, phone-only 40, console-only 14.
- Of the console-only 14, ~9 are WRONG-SITE WINS: console placed L563 at (238,202) while phone put it
  at (184,519) next to L400 (d 2.24 px, phone conflict pair 400/563). The console's global ±3 px
  suppression + max-amp-per-codeword dedup lets an eaten codeword "win" at a distant bright site.
  Phone's design (small same-string window + conflict list, members still confirmed) is healthier.

## Mechanism split of the 277 console misses (id-driven instrument, validated)

Instrument: per-id site interpolated between confirmed string anchors (323 anchors incl. phone extras;
self-check max error 0.0 px), page-parity stacksig sampled ±4 px, own codeword + full competitor bank
scored at each offset. Validation: on the 283 console confirms, instrument amp − CLI amp median/p10/p90
= 0.0 (exact parity); own codeword wins at the best site for 314/323.

| mechanism | n | background (ring med) | own amp med | prof_on med | reading |
|---|---|---|---|---|---|
| amp<40 | 191 | **224** | 15.8 | 106 | decodable but under gate (bright wall) |
| suppressed | 57 | 168 | 26.6 | 107 | site within 3 px of a stronger claim (colocated) |
| mask | 25 | **22** | 1.7 | 12 | no modulation anywhere → truly absent/hidden |
| other | 3 | 201 | 42.4 | 102 | contest between codewords |
| margin<6 | 1 | 151 | 40.1 | 27 | marginal |

Amplitude-stage detail (191 amp<40): own amp 0–8 (near-zero modulation): 30; 8–25: 112; 25–40: 49.

## Test 1 — local background correlation (operator hypothesis): CONFIRMED

Ring median of master luma, annulus r≈8–20 px around each registered site:

| background bucket | confirmed | missed | total | miss % |
|---|---|---|---|---|
| <80 | 96 | 34 | 130 | 26% |
| 80–119 | 44 | 10 | 54 | 19% |
| 120–159 | 77 | 31 | 108 | 29% |
| 160–199 | 68 | 51 | 119 | 43% |
| 200–239 | 38 | **149** | 187 | **80%** |
| >=240 | 0 | 2 | 2 | 100% |

Missed background medians **207 vs 135** for confirmed (p25–p75 of misses 157–228). Miss rate rises
monotonically with background luma; 151 of 277 misses sit at wall >= 200.

**Measured law:** amp ≈ 0.54 × (255 − background). On confirmed sites corr = 0.67 with
amp/(255−bgr) med = 0.538; per-depth-bucket medians track it (depth 80–120 → amp 50.7, 160–200 → 79.8).
Mechanism: ON planes pin at sensor clip (see test 2), so the per-plane luma step a codeword can
produce at a site is ~= k×255 − wall ≈ 0.54×(255 − wall) after the page's k-normalised stacksig.
At wall >= 200 the depth is <= 30 ⇒ amp <= ~30 < gate 40 — those lamps cannot confirm at any margin
setting with today's exposure.

## Test 2 — clipping: ON planes are pinned at 255

Median ON-plane peak luma at best site: confirmed **255** (p90 255), missed **254** (p90 255);
234/277 missed ids have ≥1 ON plane peaking ≥254. In the bright-wall region (bgr ≥ 200, n=151) the
ON-plane peak median is 255. The LED+glow signal is clipped on the ON side; the OFF side floats with
the wall. Modulation depth is therefore capped by (255 − wall), and only exposure-down can widen it.

## Test 3 — where the loss happens

Detection-stage (site-level): 25 (mask), i.e. only 9% — and they are the dark, no-modulation class.
Gate-level: **191 of 277 (69%) pass amp<40 with otherwise-healthy codeword evidence** (prof_sep > 5 for
90% of all missed ids). Code-level: only 1 (margin) + 3 contest. Suppression: 57.
So: the decoder sees them; the amp GATE (with today's exposure) is what rejects them.

## Test 4 — exposure-control inventory (page + firmware)

- `page/survey.html` CFG already has **`evBias: -1`** → `applyEvBias()` clamps to
  `track.getCapabilities().exposureCompensation` and applies via `applyConstraints({advanced:[…]})`.
  BUT it is called exactly **once, in the camera-ready callback** (line ~370) — never re-applied at
  burst start, so a CFG change mid-session does not take effect.
- **`cwcAeLock: 0`** default; `lockAeAfterP00()` (manual exposureMode freeze after p00) exists and is
  wired at p==0 but disabled. Metas: `aem=continuous ev=-1 fd=0.00` on every frame; this round's
  exposure was stable (`exp=699.97` on all metas — per-plane k drift 0.53–0.63 came from scene
  content, not AE drift).
- Firmware `poc_survey.ino` has no exposure keys: the phone camera is fully page-controlled;
  `sCfg` (640 B, idempotent-replay, 1926) already carries page CFG — **no firmware change needed**.
- **Minimal change to make exposure a first-class CFG field:** (a) call `applyEvBias()` at burst start
  (one line in the 24-plane burst routine before p00's settle, guarded to run only when the CFG value
  changed); (b) ship `CFG={"evBias":-2}` from the box before the next round; (c) optional UI stepper.
  Cost ≈ 10 page-side lines, no firmware build, Android-only effect (iOS ignores exposureCompensation —
  graceful). For bright walls the operator should also consider reframing (lower wall fill) — free.

## Gate sweep on the real pipeline (CLI-gating-block verbatim, n=600, parity-exact)

| mask/amp/margin | confirmed | recovered | lost |
|---|---|---|---|
| 100/40/6 (baseline) | 283 | — | — |
| 100/30/6 | 350 | +67 | 0 |
| **100/25/6** | **380** | **+97** | **0** |
| 100/20/6 | 389 | +106 | 0 |
| 100/25/4 | 385 | +103 | 1 |
| 80/25/6 | 380 | +97 | 0 |
| 100/15/3 | 412 | +132 | 3 |

Mask 80 = mask 100 ⇒ the mask stage is NOT binding this round. The relaxations never dropped a
baseline confirm until amp15 (−3). Recovery targets match the instrument's prediction exactly
(+97 = 57 suppressed-recoverable ∩ 49 in the 25–40 amp band ∩ others).

## Ceiling arithmetic and what is genuinely absent

- Site-level truth available to any decoder on THESE frames: 323 anchor sites (317 unique ids).
- Gate relaxation ceiling: ~389–412 (amp20/6 .. amp15/3).
- Truly-absent floor (no background, no modulation, prof_on ≈ 12, own amp ≈ 0–2): 25 mask-class +
  ~30 near-zero-amp ids ≈ 35–55, consistent with the operator's "many hidden or colocated"
  ground truth — those need exposure-down + possibly repositioning, not decoder work.

## Ranked recommendations

1. **Exposure down (highest value, operator's own lever):** re-apply `evBias` at burst start +
   set `CFG={"evBias":-2}` for the next round (Android). Law says wall 200→~120 lifts amp from
   ~15-30 to ~70+ for physically-present lamps ⇒ most of the 151 bright-wall misses cross the
   EXISTING amp-40 gate; expected +100–150. Cost: ~10 page lines, no reflash.
2. **Amp gate 40→25, keep margin 6 (proven today):** 283→380, zero regressions, margin 6 still
   guards d8 structure. If exposure-down lands first, re-sweep before adopting; keep 40 if the
   phantom corpus (9-of-18 legacy, --bank flag) ever revisits oversized banks.
3. **Suppression redesign (page-parity):** dedup same-codeword duplicates only; demote rival-site
   claims ≤5 px to CONFLICT-flagged entries instead of silently suppressing. Recovers part of the
   57-console-suppressed class (colocated partners the operator knows are installed) and removes
   the ~9 console wrong-site wins (L563@(238,202) class). Page already does this better than the CLI.
4. **Keep mask 100** (not binding); **keep cwcN=600** (honored; max id 599).
5. **Bookkeeping for the survey:** surface an "evidence-absent" flag for the ~35–55 no-modulation
   ids (mask-class + amp<8) instead of counting them as decoder misses.
6. Verify the package on the next round against phone CWCSTATS parity (console↔phone shared-count
   and the bright-bucket miss table above as the regression template).

*Evidence artifacts: /tmp/invest_s14r/ (evidence.json, mech.json, sweep_out.json, phone_decode.json;
per-plane scoring ran only inside subprocesses; no (ids,H,W) tensor in-kernel).*

---

## CORRECTION (02 Oct evening — superseded by the round-2 audit)

The amp-25 "gate relaxation" numbers in this report (+97 at amp 25, +106 at amp 20, +132 at amp 15/margin 3) were computed
against DETECTION COUNT ONLY. The round-2 impostor-position audit
(reports/s14r-0000-17h43-aimed-round-decode-and-fixpath-verification.md) re-audited this round's surviving sweep data:
43-59% of the relaxed-gate "recoveries" were impostor placements (far-orphan sites >6 px from any registered lamp, amp ~1-2,
placed on background between real lamps). TRUE near-site gains for this round: amp25 +33, amp20 +34, amp15/3 +32.

The exposure law as stated ("amp ~ 0.54*(255-wall)") was measured at this round's exposure (exp=699.97); the exposure-
parameterised form is amp ~ k_per-plane*(255-wall) with k 0.538 @ exp 700, 0.97 @ exp 200 (round-2, AE-shortened).

Do not adopt gate relaxation without the position guard (site within ~6 px of the registered lamp + <=5 px dedup of
confirmed-lamp claims). With the guard, true gains remain large and clean; without it, phantom placements inflate them.
