# CLI suppression/ownership parity with the served page — S14R proposal

**Status:** design for `tools/cwc_pos_decode.py` (console decoder) to adopt the served phone page's
(`page/survey.html` `cwcDecode`, lines 1786–1958) suppression, dedup, and rival/conflict semantics.
Scope: ownership/masking bookkeeping only. Gates stay mask-adaptive / amp 40 / margin 6.

---

## 1. Parity deltas found (page vs CLI)

Both implementations share: adaptive mask rule, per-plane direct registration + full-res refine
(rad 4 stride 2, page `nccRefine` = CLI `ncc_refine`), page-parity `hist_median`, bilinear stacksig
at the plane's own float shift, d≥8-competitor margin, amp = score/12, and the strongest-site-per-
codeword (max-amp) dedup. Those are already identical. The deltas are all downstream of the greedy
accept:

| # | aspect | page `cwcDecode` (survey.html) | CLI `cwc_pos_decode.py` | effect |
|---|--------|-------------------------------|--------------------------|--------|
| 1 | suppression window | `R = cwcSuppress = 1` → **±1 px (3×3)**, line 1918 | `SUPPRESS = 7` → **±3 px (7×7)** global square, lines 88, 411-412 | CLI eats 13× more area per claim |
| 2 | who gets suppressed | only masked pixels whose **argmax codeword belongs to the accepted LED's string** (`argI[jj]/nPs == sOwn`, line 1926) | **every masked pixel** in the square, regardless of codeword/string (raw `used[y-3:y+4, x-3:x+4] = True`) | CLI eats gate-passing rivals of a *different string*; page confirms both |
| 3 | rival/conflict handling | post-hoc audit over final sites: same-string, `d < 6 px`, `\|Δled\| > 5` → `{a, b, d}` conflict entry, **both members stay confirmed** (lines 1946-1955) | **none** — eaten rivals vanish silently; `ledpos.json` has no conflict representation | parity gap: the page's conflict list IS the ownership ledger; the CLI has no ownership ledger |
| 4 | candidate mask blur | double 3-tap box blur (`blur3∘blur3`, line 1810) | `cv2.GaussianBlur(5,5,1.2)` (line 380) | page-validated equivalent (~0.4 LED mean) but not bit-identical; a strict-parity option only |
| 5 | dedup | `byled` max-amp, one site per codeword (1937-1942) | identical (419-424) | ✓ no change |
| 6 | per-string scoping | string break at `nPs = nPerStr` from the rig hello; suppression + audit are string-scoped (1827, 1919, 1950) | CLI has **no knowledge of nStr/nPerStr** — cannot express same-string at all | CLI needs `--nstr/--nperstr` |

The 0000-17h43 audit (43–59 % phantom recoveries) and the interference law (iOS: 100 % of
never-confirmed ids are contests, 108–343/burst; android set ~44 contest + 13 suppressed) both
implicate deltas 1–3 as the CLI's parity gap. The E4 sweep (`gates_e4003e`) already measured the
populations that page parity recovers: gate-passing rivals eaten at a free pixel **17–20/epoch**;
cross-string claims the page confirms both: dominant class every epoch; pairs the page *also*
suppresses (≤1.5 px same-string): only 9–17.

## 2. Proposed gate order (identical to the page)

```
mask (adaptive thr) → greedy descend by site best-score → amp gate 40
→ d8-competitor margin gate 6 → CLAIM pixel
→ suppression: ±1 px square, marks a pixel used ONLY if that pixel's argmax
   codeword is in the CLAIMED LED's string          (page line 1918-1928)
→ same-codeword strongest-site dedup (max amp)       (unchanged)
→ conflict audit: same-string, d<6, |Δled|>5 → conflict pair, both confirmed
                                                     (page line 1946-1955)
```

Companion rule from finding (1), NOT part of the parity change: **no gate promotion below amp 40**
without the position guard (claim site ≤ 6 px of the id's registered/interpolated anchor). The amp
gate stays 40 for identity-grade counts in this proposal.

## 3. Minimal diff-level design for `tools/cwc_pos_decode.py`

### 3.1 Constants + args

```python
SUPPRESS = 1          # page cwcSuppress: ±R px same-string window (was 7 = ±3 global)
CONFLICT_D = 6.0      # page conflict audit distance (strict <)
CONFLICT_ID_GAP = 5   # page: |led_a - led_b| > 5 within one string

ap.add_argument('--nstr', type=int, default=3)      # page window._nStr (rig 3x200)
ap.add_argument('--nperstr', type=int, default=200) # page nPs; strings = nstr, per-string len
ap.add_argument('--suppress', type=int, default=SUPPRESS)   # page knob cwcSuppress
ap.add_argument('--save-conflicts', action='store_true')    # write ledconflicts.json
ap.add_argument('--flag-colocated', action='store_true',
                help='also ledger cross-string pairs <5 px (both stay confirmed; '
                     'the page confirms them silently — audit field only)')
```

Back-compat: `--nstr 1` ⇒ `nPs = N` ⇒ "same string" is everything ⇒ ±1 px global window (the
closest legacy-faithful behaviour). The old 7 px global window is reproducible with
`--suppress 3 --nstr 1`; its default use is retired.

### 3.2 Greedy loop (replaces lines 394–424; `nPs = args.nperstr if args.nstr > 1 else N`)

```python
ledpos, conflicts, colo = [], [], []
R = max(0, args.suppress)
for j in order:                                    # argsort(amap) desc — unchanged
    y, x = divmod(int(j), mask.shape[1])
    if not mask[y, x] or used[y, x]:
        continue
    i = int(argi[y, x]); amp = float(best[y, x]) / 12.0
    s2   = float(sc[np.where(Dfull[i] >= 8)[0], y, x].max())     # d>=8 competitors — unchanged
    marg = (float(best[y, x]) - s2) / 12.0
    if amp < args.amp_gate or marg < args.margin_gate:
        continue                                  # sub-gate rival: NOT suppressed, simply unclaimed
    used[y, x] = True                             # claim ONLY this pixel
    ledpos.append({'led': i, 'cx': x, 'cy': y,
                   'amp': round(amp, 1), 'margin': round(marg, 1)})
    # page-parity suppression: mark pixels in the (2R+1)^2 square used ONLY when
    # that pixel's OWN argmax codeword sits in the claimed LED's string
    s_own = i // nPs
    for yy in range(max(0, y - R), min(H, y + R + 1)):
        for xx in range(max(0, x - R), min(W, x + R + 1)):
            if mask[yy, xx] and not used[yy, xx] and (int(argi[yy, xx]) // nPs) == s_own:
                used[yy, xx] = True

# strongest-site-per-codeword dedup — UNCHANGED (lines 419-424)
byled = {}
for q in ledpos:
    cur = byled.get(q['led'])
    if cur is None or q['amp'] > cur['amp']:
        byled[q['led']] = q
ledpos = list(byled.values())

# page conflict audit, verbatim port of survey.html 1946-1955 (SAME-string only)
for a in range(len(ledpos)):
    for b in range(a + 1, len(ledpos)):
        sa, sb = ledpos[a], ledpos[b]
        if (sa['led'] // nPs) != (sb['led'] // nPs):
            continue
        d = math.hypot(sa['cx'] - sb['cx'], sa['cy'] - sb['cy'])
        if d < CONFLICT_D and abs(sa['led'] - sb['led']) > CONFLICT_ID_GAP:
            conflicts.append({'a': sa['led'], 'b': sb['led'], 'd': round(d, 2),
                              'kind': 'same-string'})
            sa.setdefault('cf', []).append(sb['led'])
            sb.setdefault('cf', []).append(sa['led'])
if args.flag_colocated:                            # ledger-only, never suppresses/dedups
    for a in range(len(ledpos)):
        for b in range(a + 1, len(ledpos)):
            sa, sb = ledpos[a], ledpos[b]
            if (sa['led'] // nPs) == (sb['led'] // nPs):
                continue
            d = math.hypot(sa['cx'] - sb['cx'], sa['cy'] - sb['cy'])
            if d < 5.0:
                colo.append({'a': sa['led'], 'b': sb['led'], 'd': round(d, 2),
                             'kind': 'cross-string'})
```

### 3.3 Outputs (schema-safe)

- `ledpos.json`: **unchanged schema** (list of `{led, cx, cy, amp, margin}`, plus optional per-site
  `cf: [partner ids]` only when a conflict exists) — all downstream union drivers keep working.
- `ledconflicts.json` (`--save-conflicts`): `{'conflicts': [...], 'colocated': [...],
  'nSites': int(mask.sum()), 'suppress': R, 'nPs': nPs}` — mirrors the page's
  `decode: {…, conflicts: dec.conflicts}` block so console output can be diffed against
  CWCDECS/CWCSTATS lines directly.
- Per-site `cf` lists + the pairs file together are the "conflict-flagged entries": a ≤5 px rival
  that passes amp+margin is now **confirmed and flagged**, never eaten.

### 3.4 What is deliberately NOT changed

- Dedup (§3.2 block) — already page-identical, same-codeword only.
- Amp/margin gates — 40/6 stays the promoted identity set; margin is inert on d8 banks (no id sits
  in [4, 8)) so do not sweep it.
- Mask machinery — same adaptive rule; only the blur differs. Optional strict-parity step: swap
  `cv2.GaussianBlur(5,5,1.2)` for the page's double 3-tap box (`blur3∘blur3`); page-validated
  within ~0.4 LED mean. Ship as `--blur box` behind a flag, default off, until the A/B in §4.5
  passes; otherwise leave Gaussian (counts move ±1 in the E4 baseline).
- Registration, scoring, `hist_median`, `round_half_up` — already bit-parity.
- amp<40 gate promotions — remain banned without the ≤6 px position guard (finding 1); not
  activated here.

## 4. Validation plan

Instrument: import the CLI's own `register_direct/ncc_refine/hist_median` (the proven E4
bit-exact-parity recipe in `runs/daemon/analysis/e4_union/e4_union_decode.py`), build score arrays
once per run, re-run only the (old vs new) candidate-selection loop, plus a real subprocess
`cwc_pos_decode.py --save-json --save-conflicts` run to certify the diff end-to-end.
Position guard for ALL counted gains: recovery site ≤ 6 px of the id's registered anchor
(E4: `_union_anchor.json` / union-confirmed site; iOS: cross-burst mapped anchor) — the
17h43 law says unguarded relax-gain is 43–59 % impostor, and the base-gate impostor rate is 3–4 %;
any recovery landing >6 px from its anchor and >5 px from every phone/console-confirmed lamp is an
impostor, not a gain, and the change must be reverted.

### 4.1 E4 corpora `runs/daemon/runs/run8..run11` (repaired copies under
`runs/daemon/analysis/e4_union/repaired/run8..run11/`; 25 jpgs each; baselines 412/454/390/411,
4-burst union 481)

| metric | baseline | predicted with parity change | direction |
|---|---|---|---|
| per-burst confirmed | 412 / 454 / 390 / 411 | **+17–20/epoch** (guard-passing eaten rivals at free pixels, measured in gates_e4003e) ⇒ ≈ 429–432 / 471–474 / 407–410 / 428–431 | ↑ |
| 4-burst union | 481 | +15–30 ⇒ **≈ 496–511** | ↑ |
| baseline-id regressions (lost confirms) | — | **≤ 5/epoch** (page-parity keeps the ≤1.5 px same-string window; anything lost must be auditable as page-suppresses-too) | ~0 |
| impostor share of gains | — | ≤ base rate ~3–4 % after the ≤6 px guard (vs 43–59 % for unguarded relaxation) | ↓ |
| conflicts | none today | page-class list, ~tens/burst; cross-string keep-both becomes the dominant new-confirm class (as on page) | new ledger |

Pass criteria: every gain guard-PASS, ≥ +12 net/epoch, union ≥ 490, baseline ids lost all
inside ≤1.5 px same-string pairs (page suppresses those too).

### 4.2 iOS corpora `runs/daemon/runs/s14r-ios-r1..r4` (25 jpgs each; baselines 287/440/487/257,
union 534; 100 % of never-confirmed are contests)

| metric | baseline | predicted | direction |
|---|---|---|---|
| per-burst confirmed | 287 / 440 / 487 / 257 | +4 / +5 / +5 / +9 (exactly the suppressed/starved row: amp≥40 marg≥6 site lost) | modest ↑ |
| union | 534 | +8–15 ⇒ ≈ 542–549 | ↑ |
| never-confirmed bookkeeping | silent misses | 108–343/burst appear as **conflict-flagged / contest rows** (d8-competitor margin negative ⇒ still fail margin 6; visible, not eaten) | new ledger |
| flagged ≤5 px rival pairs | eaten today (16:16 round: L400/L563-class, phone flagged, CLI ate) | ≈ the page's conflict count; cross-string pairs both confirmed like the page | parity restored |

The margin gate is what keeps iOS gains small — that is correct page parity: the page confirms the
same small set. The deliverable here is the *ownership ledger* (contests visible, not eaten), which
is exactly the page's conflict-tolerant semantics.

### 4.3 Android exp500 set `s14r-and-r2..r5` (baselines 429/549/492/387)

Predicted +2–7/burst from suppressed class (7/2/1/3) plus partial contest converts; colocation
pairs (<5 px, 45/33/37/46 per burst) flip from "one member eaten" to "both confirmed" for
cross-string, "both confirmed + flagged" for same-string — matching the page's proven-healthy
colocated-pair semantics.

### 4.4 Cross-check vs the phone (0000 rounds)

On `s14r-0000-17h43` the page shipped 328 confirmed / 31 conflicts (same-string d 2–5.7 px) vs CLI
317. Predicted CLI-with-parity: ≈ 330–336 confirmed and a 20–31-entry same-string conflict list;
per-pair agreement with the page's `{a, b, d}` entries within 1 px. This is the only run with the
phone's own conflict list on the wire — it is the direct semantic diff, not just a count diff.

### 4.5 Optional blur A/B (§3.4)

`--blur box` on all four corpora: accept only if per-burst |Δ| ≤ 2 confirmed vs Gaussian and no
baseline id regresses; otherwise keep Gaussian and record the delta (page vs CLI blur is the one
remaining non-ownership parity gap, documented, ~0.4 LED).

### 4.6 Hygiene invariants (every corpus, every run)

- max claimed id < cwcN (600) — no cap phantoms;
- min confirmed amp ≥ 40.0, min margin ≥ 6.0 — gates honored;
- no id with 2+ sites after dedup (same-codeword dedup intact);
- `ledpos.json` schema unchanged (diffable against today's baselines by id-set minus new adds);
- recovered-id site ≤ 6 px of its anchor (guard) — any recovery farther is an impostor and fails
  the change;
- no git mutations, no `/dev/ttyACM*` access, run dirs untouched (staging + outputs under
  `runs/daemon/analysis/parity/`).

## 5. Risks / notes

- The naive "shrink the window" variant (window 7→3 px, still global cross-string) is NOT the
  proposal: gates_e4003e measured its raw +50–62/epoch with ~93 % impostor-proxy (relabel storm).
  Parity comes from the **pair** of (same-string-only argmax test) + (conflict ledger), and the
  counted population is only the 17–20/epoch guard-passing class.
- `--nstr 1` fallback changes old behaviour (±3 px global → ±1 px global); legacy reproductions
  must pass `--suppress 3 --nstr 1` explicitly and should expect the documented eaten-rival class
  back.
- The greedy order (argsort desc over masked `amap`) is unchanged, so tie-breaks and score-desc
  semantics are untouched; only *which pixels survive to be claimed* changes.
- Downstream union drivers that treat ledpos.json as the full truth will now receive cross-string
  colocated pairs that older runs dropped; the 4-burst-union number can only grow, never shrink,
  under this change (verified per run in 4.1-4.3 pass criteria).