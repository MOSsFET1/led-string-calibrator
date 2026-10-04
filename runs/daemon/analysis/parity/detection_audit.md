# S14R-0003E-era detection pipeline audit — end to end

Date: 05 Oct 2026 (audit session). Scope: tear-guard detection pipeline (page-side
`cal.html` guard + console decode side `tools/cwc_pos_decode.py` family) as shipped in
builds 0003D/0003E. Read-only audit: no run-dir imagery decoded wholesale, no serial,
no git mutations. Evidence is file:line quotes; corpus numbers re-derived from the
extract `scored.json` files, not copied from reports.

Corpora used:
- `runs/daemon/analysis/e4_0003d_extract/scored.json` (100 frames, exp 300.03, runs r9–r12)
- `runs/daemon/analysis/e4_0003e_extract/scored.json` (100 frames, exp 699.97, runs r2–r5, carries `torn_shipped`/`tm_shipped`)
- `runs/daemon/capture.txt` (63.6 MB, the 0003E battery at lines ~67086+, 5 `torn` FRAME lines)
- `runs/daemon/runs/s14r0003e-r5/*.meta.json` (disk metas)

---

## (a) The claimed benchPull bug — VERIFIED for 0003D; ALREADY FIXED in 0003E; tolerant `.rN` parse NOT landed (but is currently dormant)

**The bug was real in 0003D.** HANDOFF-S14R.md L402–404:

> KNOWN BUG for next build: benchPull ships a FIXED 5-field FRAME json — torn:1/tm
> extras NEVER reach the wire/disk meta (flag loss at ship, cal.html ~:992)

**Fixed in 0003E** (pushed c3ea9f7 per HANDOFF L67). Current `page/cal.html`:

- L449–451 (benchCapture):
  ```
  // 0003E FIX: carry the FULL stored meta (not a fixed field list) so the
  // tear guard's torn:1/tm extras survive to the wire and <label>.meta.json
  // (0003D shipped {t,label,W,H,exp} only — flags were silently dropped).
  ```
- L996–999 (benchPull ship loop) now ships the extras:
  ```js
  const meta = JSON.stringify({ t: f.t, label: f.label, W: f.W, H: f.H,
                                exp: f.exp,
                                torn: f.torn, tm: f.tm });
  ```

**Wire-level proof the fix held** — `runs/daemon/capture.txt` L74552 (battery 23:06:02):

```
[PHONE] FRAME {"t":"69.234","label":"cwc:r5:p03",...,"torn":1,"tm":{"jL":13.4,"jR":8.2}}
```

5 such lines (`r5:p03/p04/p06/p08/p17`), and the daemon's disk meta is verbatim:
`runs/daemon/runs/s14r0003e-r5/cwc_r5_p03.meta.json` contains the same `torn: 1, tm {...}`
object. So the flag now survives to `<label>.meta.json` exactly as the design intended.

**Tolerant-label parse: NOT landed.** `tools/cwc_pos_decode.py` L320 is still the naive parse:

```python
planes = {int(f['label'].split(':p')[1]): f['img'] for f in frames if ':p' in f['label']}
```

A label `cwc:r8:p07.r1` → `split(':p')[1]` = `'07.r1'` → `int()` raises ValueError
(crash), matching the design warning (s14r-0003e-tearguard-design.md §4, L140–146).
The same naive pattern lives in **5 console tools / drivers**:
`tools/cwc_pos_decode.py:320`, `tools/cwc_page_decode_sim.py:100`,
`tools/s14_detect.py:44`, `tools/cwc_assign_probe.py:53`, `tools/run_round.py:152` —
plus the analysis drivers `decode_e4003e/e4003e_decode.py:58` and both
`e4_0003{d,e}_extract/extract_and_score.py`. No file strips a `.\d+$` suffix anywhere.

**However the crash is dormant**: the shipped implementation chose *silent replace +
persistent-tear meta* instead of the design doc's `.rN` label-suffix convention — the
wire contains **zero** suffixed labels (`grep 'label.*\.r[12]' capture.txt` → 0), and
`cal.html` has no `.rN` suffix code (labels are plain `cwc:rN:pNN`, L935). Either land
the tolerant parse (cheap, protects any future suffix) or formally retire the `.rN`
convention in the design doc so the "never ship the guard without it" obligation dies.
Note the duplicate-label policies that DO exist (longest-group wins in both extract
scripts, skip-if-exists in the daemon) already tolerate re-ships; what nothing tolerates
is the suffix **in the int()**.

**Second latent ship-path bug found (new):** `cal.html` L923 truncates fractional
thresholds before comparing:

```js
torn = (m.jL >= (tearGet('thr') | 0)) || (m.jR >= (tearGet('thrRed') | 0));
```

`8.5 | 0 === 8` — the retuned `thrRed: 8.5` (TEAR, L521) compares as **8** at runtime.
All 5 false flags of the 0003E battery have `tm.jR` in [8.0, 8.2] (see (c)) — every one
of them is **directly attributable to this truncation**, not to the 8.5 value. The
extract docstring (`e4_0003e_extract/extract_and_score.py` L10–11) already suspected it.

---

## (b) e4_0003d_extract scoring tooling — what it says about detection quality at exp 300 vs 700

Tooling itself is sound: exact port of `tearScan()` (block-mean ×7, luma = max(r,g,b),
skipTop 120), era-gated on daemon start + `exp 300.03` string, longest-group dedupe,
EOI/PIL/W-H hygiene per frame (all 100 green). Re-derived stats from `scored.json`
(96 planes / 4 masters):

| regime | clean jL max (offline) | clean jR max | torn (offline @18/6) | page-side behaviour |
|---|---|---|---|---|
| exp 699.97 (calibration, 0003c) | 10.6 | 2.4 | 3 (calibrated) | n/a (pre-guard) |
| exp 300.03 (0003D live) | **13.4** (med 12.0) | 3.3 | **1 = `cwc:r12:p15`, jL 29.2 / jR 16.5, y≈272, vision-confirmed** | **61/96 planes regrabbed, 15 exhausted retries shipped FLAGGED** (HANDOFF L394–397) |
| exp 699.97 (0003E battery) | 15.4 (med 13.05) | 3.6 | 0 | 5 planes page-flagged, **all 5 FALSE** (decode: k 0.633 inside family 0.633–0.651; no k outliers) |

Detection quality at exp 300 was good *offline* — 99/100 clean ships, the one true tear
caught with margin 2.2× (gray) / 5.0× (red) over clean max — but the guard's *page-side*
cost exploded: thresholds calibrated at exp 700 (clean 10.6) did not transfer to exp 300
(clean 13.4, first-attempt page scores reaching ≥18 on 63% of planes).

Three algorithm implications:

1. **Both arms scale with exposure regime.** Clean jL grew 1.26× and jR 1.38× going
   700→300. Fixed scalars are regime-bound (confirmed again 700-side in 0003E: offline
   clean max 15.4 vs the 10.6 calibration). Per-battery retune (the standing ≥1.3× rule,
   HANDOFF L399–401) is necessary but currently uncomputable — see (c).
2. **The offline JPEG rescore is NOT an oracle for the page's red arm.** On the 5
   0003E-flagged frames, page `tm.jR` = 8.0–8.2 while the offline decode of the *same
   final-attempt JPEG* scores jR 2.5–2.9 (jL agrees within ~1.3: 13.4 vs 14.7 etc.).
   Chroma information of a 1-px tear line is largely destroyed between the live canvas
   read (`getImageData`) and the shipped JPEG (4:2:0 chroma subsampling + ICC/PIL decode
   mismatch documented at cal.html L592–597). Consequence: **offline rescoring cannot
   validate or refute page tear flags on the red arm** — which is exactly the arm where
   the 0003E false flags live. Equivalence was only ever proven on lossless untagged
   PNGs in a harness, never on the live grab → shipped-JPEG path.
3. **Regrab selection biases offline "clean max" low.** Shipped frames are best-of-≤3
   attempts; the page-side first-attempt distribution (the thing thresholds must clear)
   is never recorded anywhere — 0003D's 61-regrab storm arm attribution is unknown, and
   `grep tearguard|regrab capture.txt` → 0 lines (silent regrabs are wire-invisible,
   HANDOFF L404–406).

---

## (c) Is the 26 / 8.5 retune validated? — only in the negative direction; headroom math is thin on the tear side

**Where it came from:** `page/cal.html` L514–523 TEAR comment — 26 = 1.94× the 0003D
exp-300 clean max 13.4; thrRed 8.5 motivated by 0003D's true tear r12:p15 scoring
jR 16.5 (→1.94× margin claimed) against a "clean max 2.9" jR. HANDOFF L67 records the
build; **no report validates the pair against a true tear positive** — the 0003E battery
(exp 699.97) contained zero offline tears, so 26/8.5 has never once fired on a real tear.

**Headroom math, re-derived:**

| quantity | value | margin vs threshold |
|---|---|---|
| offline clean jL max @700 (0003E) | 15.4 | 26/15.4 = **1.69×** |
| offline clean jL max @300 (0003D, post-regrab-selection) | 13.4 | 26/13.4 = 1.94× |
| tear gray `r12:p15` @300 (offline) | 29.2 | 29.2/26 = **1.12×** |
| tear gray-band species `r10_p21` @700 (page recipe jL 28.5, cal.html L601) | 28.5 | 28.5/26 = **1.10×** |
| tear gray `r8_p07` species @700 | 57.5 | 2.2× |
| offline clean jR max @700 | 3.6 | 8.5/3.6 = 2.36× (runtime effective 8 → 2.2×) |
| **weakest known tear = red-only species `r10_p05` @700 (jR 8.5)** | 8.5 | **8.5/8.5 = 1.00× — zero margin** |

Two conclusions:

- The retune bought clean-side headroom by spending nearly all tear-side headroom:
  two of the three canonical tear species now sit within ≤1.12× of `thr`, and the
  red-only species — the reason the red arm exists — sits exactly at `thrRed 8.5`
  (and, with the `\|0` truncation, *below* the effective threshold 8 → guaranteed FN;
  only `8.5 ≥ 8` saves it today). A slightly weaker repeat of the 0003c tear anatomy
  ships unflagged and poisons photometry (kbg outlier with no marker).
- The retune also optimized the wrong objective: a false flag costs only retry latency
  (all 5 0003E flags decoded clean; union/decode absorbed them trivially), while a
  missed tear costs a poisoned photometric frame with no marker. FP are cheap, FN are
  expensive — thresholds should bias sensitive, not quiet. 61 regrabs at 0003D was an
  annoyance (~+20 s/burst), not damage.

**Headroom for the NEXT exposure regime:** if the regime moves shorter (exp ~150),
offline clean jL extrapolates to ~17 (1.26× per 700→300 step observed) and the
page-side first-attempt distribution plausibly ~+5 above that (the 0003D storm
evidence) → ~22 page-side vs `thr` 26 = **1.18× — below the project's own standing
≥1.3× rule**. The rule cannot even be evaluated today: page-side clean distributions
are unmeasured at every regime (see (b) implication 3). Next regime needs
telemetry-driven retune, not another doubling-by-guess.

---

## (d) Ranked next changes for the DETECTION pipeline

1. **Fix the `\|0` threshold truncation (cal.html L923).** Compare with the actual
   numbers: `torn = (m.jL >= tearGet('thr')) || (m.jR >= tearGet('thrRed'))` (tearGet
   already guarantees a number). Rationale: `8.5|0=8` caused all 5 of 0003E's false
   flags and silently rewrites any CALCFG fractional retune — the primary tuning
   channel. Validate: rerun `page_algo_bench.js` over the 0003E frames → expect 0/96
   flags; node --check; next battery CWCSTATS `tears:0`.
2. **Land the tolerant `.rN` parse — or retire the `.rN` convention.** One-line change in
   all 5 tools: `int(re.sub(r'\.\d+$', '', f['label'].split(':p')[1]))` + explicit
   last-wins (regrab) on duplicate plane indices. Or, cheaper: patch the design doc §4
   to declare the meta-flag path (`torn:1/tm`, now proven end-to-end) the permanent
   convention and delete the `.rN` requirement — the shipped page has no suffix code,
   so the naive parses are dead weight only if the doc still promises them. Validate:
   unit test with `cwc:r8:p07.r1/.r2` labels → plane dict still 24 entries; rerun the
   e4003e driver → 423/383/385/398 unchanged.
3. **Ship page-side tear telemetry (per-burst, in CWCSTATS):** per-plane first-attempt
   jL/jR (or p95/max over the burst), regrab count, exhausted-retry count, arm+y of any
   final flag. Rationale: closes the two audit gaps at once — makes the standing
   "retune to ≥1.3× clean max" computable from the wire (today it needs an offline
   rescore of a *selected* population), and gives the silent-replace path the audit
   trace HANDOFF L404–406 says is missing. Costs ~40 bytes/burst. Validate: one
   battery; correlate CWCSTATS tear fields vs offline rescore (jL must correlate —
   implication (b)2 says jR will NOT, which is itself the P4 datum).
4. **Recalibrate the red arm against live-canvas statistics, not JPEG-decoded corpus.**
   Measured divergence on identical frames: page jR 8.0–8.2 vs offline 2.5–2.9 (≈5.4
   units, 4:2:0 chroma subsampling of a 1-px line + ICC decode path). Either (a) scan
   the snapshot that will actually ship (decode the `toDataURL` JPEG via
   `createImageBitmap` before scanning; ~10–20 ms of the ~260 ms plane budget) — makes
   offline rescoring a valid oracle again and removes the ICC/subsampling asymmetry —
   or (b) keep the live scan and set `thrRed` strictly from telemetry (item 3). Also
   decide `thrRed` on margins, not storm suppression: today's 8.5 leaves the
   red-only tear species at 1.00× margin; ~7–7.5 with live-path stats would restore
   ≥1.3× both ways if the live clean jR headroom permits. Validate: harness comparing
   live-RGBA vs decoded-JPEG arms across the 100-frame 0003E corpus; FN regression
   against the three 0003c tear species must stay 0.
5. **(Console decode side, cross-reference) conflict/ownership redesign** — already
   fully ranked with guarded populations in
   `runs/daemon/analysis/gates_e4003e/report.md` (+17–20 guarded recoveries per epoch,
   674–868 gate-passing rival claims eaten; margin and mask knobs measured inert; amp
   relaxation creates 22–36 impostor-proxies). Not repeated here; it dominates
   console-side gains but is a decode-quality change, not a guard change.

Not worth changing (measured): `skipTop` 120 (argrow clustering 186/192 is the benign
y≈279 half-frame step family — all 0003D/0003E clean argrows sit at 186, no top-strip
contamination), retry cap 2 (0.13% tear rate, retries cost only latency), and the 3-arm
`gray/rg/bg` design-doc metric remains the *better-measured* alternative if item 4
forces a redesign — its corpus margins were 1.64× worst-clean / 2.39× weakest tear,
versus the shipped 2-arm pair at ≤1.12× on two tear species.

---

## Evidence index

| fact | source |
|---|---|
| 0003D fixed-5-field bug | HANDOFF-S14R.md L402–404 |
| 0003E fix (full meta) | page/cal.html L449–454, L996–999 |
| flags on wire | runs/daemon/capture.txt L74552, L74591, L74673, L74755, L75122 |
| flags on disk | runs/daemon/runs/s14r0003e-r5/cwc_r5_p03.meta.json (verbatim torn/tm) |
| naive parse still present | tools/cwc_pos_decode.py L320 (also 4 other tools, see (a)) |
| no `.rN` labels on wire | grep `label.*\.r[12]` → 0 |
| `\|0` truncation | page/cal.html L923; TEAR.thrRed 8.5 at L521 |
| 0003D storm + 1 true tear | HANDOFF-S14R.md L394–399; e4_0003d_extract/scored.json (r12:p15 jL 29.2/jR 16.5) |
| 0003E 5 false flags | e4_0003e_extract/scored.json (torn_shipped=1, tm jR 8.0–8.2; offline jR 2.5–2.9); decode_e4003e/report.md (k in-family) |
| clean distributions | this audit, re-derived from both scored.json (0003D: jL≤13.4/jR≤3.3; 0003E: jL≤15.4/jR≤3.6) |
| calibration corpus tears | tearguard-design §2; cal.html L500–502 (57.5 / 28.5 / 8.5 page recipe) |
| retune provenance, no positive validation | cal.html L514–523; HANDOFF L67; no tear-positive exists at 26/8.5 |
| standing ≥1.3× rule | HANDOFF-S14R.md L399–401 |