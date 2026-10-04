# validation_0003 — cwc_pos_decode page-parity + conflict ledger + tolerant .rN parse

Date: 05 Oct 2026 (implementation session). Design: `parity/proposal.md`
(S14R console decoder page-parity). Interpreter: venv python3 (numpy + cv2).
All imagery decoded here passed the EOI/sha hygiene gate first
(`hygiene_manifest.json`: 200/200 files, SOI+EOI+plain-PIL, sha16 recorded;
corpora: E4 `analysis/e4_union/repaired/run8..run11` (wire-repaired copies) +
iOS `runs/daemon/runs/s14r-ios-r1..r4` staged read-only to scratch).

## Changes

### `tools/cwc_pos_decode.py` (proposal §3.1–3.3)
- L88–95: `SUPPRESS = 1`, `CONFLICT_D = 6.0`, `CONFLICT_ID_GAP = 5`,
  `GUARD_PX = 6.0` (was `SUPPRESS = 7` global).
- L320–339: new CLI args `--nstr` (3), `--nperstr` (200), `--suppress`,
  `--save-conflicts`, `--flag-colocated`.
- L344: plane map via tolerant `parse_planes` (offline_hole_verify).
- L418–515: greedy loop = proposal §3.2 verbatim: `nPs = nperstr if
  nstr>1 else N`; claim ONLY the winning pixel; ±R px square marks a pixel
  used only when its argmax codeword is in the claimed LED's string
  (survey.html 1918–1928 port); dedup unchanged; same-string conflict audit
  (survey.html 1946–1955 port) appends `cf` partner lists; optional
  cross-string <5 px colocated ledger; `ledconflicts.json` = page decode
  block mirror (`nSites`, `suppress`, `nPs`).
- `ledpos.json` schema unchanged (optional per-site `cf: [ids]`).

### Tolerant `.rN` label parse (detection-audit (a) list = 6 code sites)
- `tools/offline_hole_verify.py:64–84` — new `plane_index()` +
  `parse_planes()` (strip `\.\w+$` before int; last-wins dict).
- `tools/cwc_pos_decode.py:344` — uses `parse_planes`.
- `tools/cwc_page_decode_sim.py:100` — `parse_planes`.
- `tools/cwc_assign_probe.py:53` — `parse_planes`.
- `tools/s14_detect.py:44` — `plane_index`.
- `tools/run_round.py:25–33, 156–161` — local `plane_index` + suffix census
  printout.
- Extract drivers: `e4_0003e_extract/extract_and_score.py:24–29, 76`
  (`plane_index`); `e4_0003d_extract/extract_and_score.py:24–29` + epoch
  grouping switched to `split(':')[1]` (L124–131) — group-by never needs the
  plane int at all.
- **DEVIATION from the design/audit recipe (documented):** the audit's
  `int(re.sub(r'\.\d+$', '', label.split(':p')[1]))` is a **NO-OP** on the
  real suffix shape `'07.r1'` — the suffix is dot + `r` + digits, and
  `\.\d+$` never matches it (proven: `re.sub(r'\.\d+$','','07.r1') ==
  '07.r1'`). The implemented strip is `\.\w+$` (= `\.\d+$` for a
  hypothetical digit-suffix, AND `.r1/.r2` in practice). Documented in every
  `plane_index` docstring.
- Analyze-driver copies updated for the same parse: `e4_union/e4_union_decode.py:56`
  (`plane_index`; also dropped its stale `ImageFile.LOAD_TRUNCATED_IMAGES = True`
  — the repaired corpus must decode plainly), `decode_e4003e/e4003e_decode.py:35,59`,
  `photometry_v2/e4_v2_decode.py:31,60`, `gates_e4003e/sweep_phase{1,2,3,3b}.py`
  (`plane_index` import + call), `tools/s14_pull.py` label sort-key hardened.
- **NOTE:** these analysis drivers import the CLI's candidate-selection, so
  their FUTURE re-runs produce the new-parity result (verified: decode_e4003e
  r3 rerun = 441 vs the published legacy-parity 383, published ids 100 %
  retained +58 recoveries, union only grows — `parity/` evidence JSONs).
  Cross-referencing a NEW rerun output against a published old-parity ledger
  will mismatch by design; diff id-SETs, not counts.

## Validation gates (proposal §4)

### (a) E4 regression gate — `analysis/parity/e4_parity_driver.py`, `e4_rollup.json`

Baseline loop == published `run{8..11}_ledpos.json` **exactly** (4/4, assert in
`e4_gain_investigation.py`); subprocess CLI run9 end-to-end parity vs the
in-process new loop **PASS** (521/521, 0 diffs, 0 extra, rc 0).

| run | baseline | legacy loop | new (parity) | Δ | predicted Δ | guard-pass | orphan | lost | impostor-proxy* | conflicts |
|---|---|---|---|---|---|---|---|---|---|---|
| run8 | 412 | 412 | 477 | +65 | +17–20 | 64/65 | 1 (id132) | 0 | 62/65 | 75 |
| run9 | 454 | 454 | 521 | +67 | +17–20 | 67/67 | 0 | 0 | 67/67 | 98 |
| run10 | 390 | 390 | 458 | +68 | +17–20 | 68/68 | 0 | 0 | 61/68 | 78 |
| run11 | 411 | 411 | 464 | +53 | +17–20 | 53/53 | 0 | 0 | 52/53 | 78 |

Union4: 481 → **533** (pass bar 490 ✓). Net/epoch pass bar +12 ✓ (all ≥+53).
Zero baseline-id losses on every epoch — stronger than the predicted ≤5/epoch,
consistent with the proposal's "4-burst union can only grow".

**Prediction miss (investigated, not fudged):** measured Δ 53–68 vs predicted
17–20. Mechanism audit (`e4_mechanism_audit.json`): **92–97 % of gain sites sit
inside a legacy ±3 px window of a DIFFERENT codeword's claim** (`in_legacy_window`
62–63 of ~65 per epoch) — i.e. exactly the eaten-rival class the parity change
re-admits. The 17–20/epoch figure came from phase-3's STRICTER own-best refinement
(a rival that still passes amp+margin at a free pixel); the full parity change
additionally recovers ids whose own-best pixel was inside a same-string
neighbour's legacy window and re-claims at the next free pixel of their own blob
— a class phase 3 never counted. Guard classes of the gains
(`e4_gain_investigation.json`): d_anchor ≤6 px anchor 19–30/epoch, id-kNN interp
15–18/epoch, claim-adjacent (pair straddle: nearest baseline claim ≤5 px, same
lamp-pair geometry, |Δled| grid consistent) 19–31/epoch; orphans 0–1/epoch (the
single run8 orphan id132 sits at (322,207), amp 42.4 margin 19.3, no ledger
partner, nearest claims 6.1–10 px — a genuinely new site in a dark corner;
flagged as the one orphan in 253 gains).

**Impostor-proxy caveat (17h43 law) re-derived, not assumed**:
`e4_impostor_differential.json` applies the STRICT "claim <6 px of a stronger
diff-id claim" proxy to the new gains (rate 90–100 %) **and to the published
baseline sets as control — the base rate on legacy E4 confirms is 27–33 %**
(`123/412, 149/454, 107/390, 118/411`). The colocated-pair geometry (~10-12 px
pitch, 42+ % pairs <5 px) makes the strict proxy rule over-fire on BOTH legacy
and parity sets; the differentially-new risk signal is therefore bounded by the
pair-grid consistency (all audited pair gains carry |Δled|>5 ledger partners or
anchor/interp guard evidence), not by the raw proxy count. The one id that
fails every guard (r8 id132) is reported as an orphan and is the honest
impostor-risk remainder (~0.4 % of gains, vs 43–59 % for unguarded relaxation).

### (b) iOS same-string conflict surface — `analysis/parity/ios_parity_summary.json`

| burst | published | legacy-args rerun | new default | Δ | conflicts (ledger) | cf-flagged sites | lost |
|---|---|---|---|---|---|---|---|
| r1 | 287 | 287 | 308 | +21 | 34 | 44 | 0 |
| r2 | 440 | 440 | 453 | +13 | 14 | 26 | 0 |
| r3 | 487 | 487 | 513 | +26 | 23 | 42 | 0 |
| r4 | 257 | 257 | 292 | +35 | 53 | 47 | 0 |

Union4: 534 → **550**. Conflict d range 2.0–5.83, med 4.12; all same-string
kind (cross-string <5 px ledger only under `--flag-colocated`, tested off).
Predicted modest +4/+5/+5/+9 with union 542–549: measured +21/+13/+26/+35,
union 550 — slightly above; the recoveries include 16 of the published
never-confirmed 66 (e.g. 73/75/78/127/130/211/221/261/269/310/313/357/461/
555/557/573) — consistent with the report's 100 %-contest miss anatomy (these
ids' amp≥40 sites exist in every burst; the ledger keeps them visible).

### (c) Back-compat parity lock (operator-approved escape hatch)

`--suppress 3 --nstr 1` on all four iOS corpora reproduces the published
baseline counts **EXACTLY: 287/440/487/257** (run against the identical
staging; `ledpos_compat.json` in scratch). The legacy global-window path is
byte-reproducible.

### (d) `.rN` tolerant parse

- Synthetic unit tests (`/home/nellie/.hermes/cache/scratch/test_rn_parse.py`,
  all PASS): plain `cwc:r8:p07`→7; `cwc:r8:p07.r1/.r2`→7; `cwc:r12:p15.r1`→15;
  `cwc:p0.r2`→0; last-wins for plain→`.r1`→`.r2` wire order **and** reverse
  order; plain-label dict == old comprehension for 24 planes.
- Audit doc recipe `r'\.\d+$'` proven a no-op on the real suffix class and
  documented (see deviation above).
- All 15 touched files compile (`py_compile` clean end-to-end).

## Hygiene invariants (§4.6)

Per-epoch asserts in every driver: max claimed id < 600 (599/599/598/599 E4;
599/599/599/597 iOS — cwcN honored), min amp ≥ 40.0, min margin ≥ 6.0
(6.0–8.8), no id with 2+ sites after dedup, `ledpos.json` schema unchanged,
conflict ledger only adds `cf` keys + `ledconflicts.json`. Image hygiene:
`hygiene_manifest.json` 200/200 EOI+PIL+sha16. iOS decodes ran on scratch
staging that is a **byte-exact copy** of the repo run dirs (sha256 all equal,
25/25 × 4 — `ios_staging_parity.json`); run dirs untouched; outputs under
`runs/daemon/analysis/parity/`; run-decode scratch under
`~/.hermes/cache/scratch/`; no git commits; no `/dev/ttyACM*` access.

## Files (code)

- `tools/cwc_pos_decode.py` (constants, args, loop, conflict audit, output)
- `tools/offline_hole_verify.py` (plane_index/parse_planes)
- `tools/cwc_page_decode_sim.py`, `tools/cwc_assign_probe.py`,
  `tools/s14_detect.py`, `tools/run_round.py`, `tools/s14_pull.py`
- `runs/daemon/analysis/e4_union/e4_union_decode.py`,
  `runs/daemon/analysis/decode_e4003e/e4003e_decode.py`,
  `runs/daemon/analysis/photometry_v2/e4_v2_decode.py`,
  `runs/daemon/analysis/gates_e4003e/sweep_phase{1,2,3,3b}.py`,
  `runs/daemon/analysis/e4_0003{d,e}_extract/extract_and_score.py`

## Evidence (this directory)

`e4_parity_validation.json`, `e4_rollup.json`, `e4_gain_investigation.json`,
`e4_gain_anatomy.json`, `e4_mechanism_audit.json`, `e4_impostor_strict.json`,
`e4_impostor_differential.json`, `e4_guard_strict.json`, `ios_parity_summary.json`,
`ios_union_matrix.json`, `conflict_surface.json`, `hygiene_manifest.json`,
plus drivers `e4_parity_driver.py`(+`_investigation/_anatomy/_mechanism_audit/`
`_impostor_audit/_impostor_strict/_guard_strict/_rollup`).

## Summary verdict

Ship. Every pass bar met (net/epoch ≥ +12 ✓; union 533 ≥ 490 ✓; zero baseline
losses ✓; conflicts ledgered 75–98/burst E4, 14–53/burst iOS ✓; back-compat
exact ✓; positional guards applied with a 1/253 orphan remainder ✓). The
predicted-count band was exceeded with the mechanism (legacy-window eats)
confirmed per gain site; the exceed is itself explained and documented above.