# S14R-0003f — Comparison-based (two-image) tear detection: analysis and verdict

Era: S14R-0003e follow-up. Date: 05 Oct 2026. Companion to `s14r-0003e-tearguard-design.md`
(the per-frame tear guard — read that first for the single-frame metric).

Question on the table (operator): **"Is comparing each image to the last a good method of
detecting image corruption? If the 2nd image was determined to be corrupted, then the first 2
would both need to be retaken because it wouldn't know which was corrupted."**

Short answer: comparing consecutive frames **does** detect tears, but by itself it cannot tell
WHICH frame of the pair is torn — the operator's concern is correct. However, a *third* frame
(the next one) resolves the ambiguity with a large measured margin, and there is a better
pairwise comparison available that has **no** ambiguity at all: the existing same-plane
regrab. Details and numbers below.

Corpora used (read-only): `runs/daemon/runs/run8..run11` (exp 699.97, 3 known tears:
r8:p07 y=191, r10:p05 y=383, r10:p21 y≈575) + `runs/daemon/analysis/e4_0003d_extract/`
(exp 300, 1 known tear: r12:p15, tear band y=271). 5 runs × 24 coded planes = 120 frames,
115 consecutive pairs, 8 of which "touch" a torn frame. All measurements:
`pairwise_measurements.json` + `pairwise_analysis.py` in this directory.

---

## 1. The ambiguity problem

When the tear-guard 3-arm statistic is applied to a **pair** (per-row means of frame N minus
frame N+1, tear-guard 3-tap adjacent-max), the 8 pairs touching a torn frame occupy the top
of the distribution — but the frames themselves are indistinguishable *from the pair alone*:

| pair (gray arm) | max diff | band y |
|---|---|---|
| r8 p06→p07 (p07 torn) | 29.5 | 191 |
| r8 p07→p08 | 29.1 | 191 |
| r12 p14→p15 (p15 torn) | 18.5 | — |
| r12 p15→p16 | 17.5 | — |
| r10 p20→p21 (p21 torn) | 6.5 | 575 |
| r10 p21→p22 | 6.6 | 575 |
| r10 p04→p05 (p05 torn) | 4.0 | 383–398 |
| r10 p05→p06 | 4.2 | 383 |

Frame p07 raised the pair diff whether it was first or second in the pair — proof that a
consecutive-pair test flags the *event*, not the *guilty frame*.

### The chain trick works (measured)

Frame N is the torn one iff it sits **off-gain against BOTH its neighbours in the same
below-band region**. The decisive statistic: the signed mean difference of row values in a
6-row window just below the band, frame N vs neighbour:

| torn frame | (N−1) minus N | N minus (N+1) | verdict |
|---|---|---|---|
| r10 p05 (y=383) | **+15.1** | **−14.2** | N reads low vs BOTH → N torn |
| r10 p21 (y=575) | −76.8 | +77.0 | N reads high vs BOTH → N torn |
| r12 p15 (y=271) | −12.1 | +12.5 | N reads low vs BOTH → N torn |
| r8 p07 (y=191) | +36.9 | −35.8 | N reads high vs BOTH → N torn |

Every torn frame produces **equal-magnitude, opposite-sign** offsets against its two
neighbours — i.e. the neighbours agree with each other and disagree only with N. On 30
randomly sampled clean consecutive-pair triples, the same statistic never exceeded
**1.41 luma** — ≥8× below the smallest tear signal (12.1). The pair-argmax band row also
agrees with the tear row (383, 575, 575, 271); one caveat below on the letterbox edge.
So: **ambiguity is real but resolvable with one extra frame — the chain separates cleanly.**

### What the ambiguity costs (at the measured 0.13% tear rate)

Per 24-plane burst, expected tears = 24 × 0.00125 ≈ 0.03. Costs *per tear event*:

- **Current scheme (per-frame self-check, regrab in place):** 1 regrab of the SAME plane;
  the rig still holds that plane's paint, so ≈ 1 plane period (+70–100 ms settle).
  Zero collateral, zero ambiguity.
- **Consecutive-pair test, no chain:** 2 frames must be retaken (the operator is exactly
  right), and worse, the earlier frame's plane is **no longer painted** by the time the pair
  is judged — it needs a repaint round-trip as well. Up to 3 regrabs/repaints for one tear
  (both members of pair N−1/N and of pair N/N+1 trip).
- **Consecutive-pair test + chain attribution:** 1 retake (guilty frame only), but detection
  is one frame *late* — the rig has already moved past plane N, so a repaint is needed
  anyway. No extra retakes of innocent neighbours.

In aggregate over a burst these are all small (≈ 10–20 ms per burst average), but the
*structural* costs are not: late detection forces plane repaints and out-of-order regrabs on
the wire, and the pair test cannot run on the first or last plane of a burst (no second
neighbour). The current in-place regrab has none of those problems.

---

## 2. The content-change confound: measurable, but small compared with real tears

Consecutive planes differ *by design* (different coded pattern). What does that do to a
row-diff profile vs what a tear does?

- **Plane-change diff (clean pairs):** gray arm median 2.09, p95 2.87, **max 3.66** over
  107 clean pairs. Chroma arms: |R−G| max 1.03, |B−G| max 1.27. The diff concentrates
  in LED-occupied rows (sparse, spiky), and global AE micro-shift between consecutive grabs
  is negligible: full-frame mean-gray shift ≤ **0.58 luma** on every clean pair (the AE has
  settled by burst time — consistent with the 4 clean p00s after the worst epoch step).
- **Tear diff:** raises the same profile to 4.0–29.5 (gray) / 14.1 (R−G) / 9.6 (B−G),
  **coherently across the full width** at one row, with one whole band moving to a different
  gain/colour state (that is what the ±12–77 luma band offsets in §1 are).

Key observation: **the 50%-duty coded paint barely moves the row means.** Plane-to-plane
paint change is essentially invisible at row-mean resolution, so pairwise scores come out
close to the per-frame guard's values (r8:p07 pair 29.5 vs self-score 26.65). The real
content confound is therefore small *in this corpus* — but it is not free:

- **Caveat 1 (margin):** the weakest tear pair (r10:p05, gray 4.0–4.2) sits only **1.1×**
  above the clean-pair max (3.66). A gray-arm-only pair test would need retuning exactly the
  way the absolute single-frame thresholds did. The chroma arms carry the separation
  (R−G 14.1 vs 1.03 = 13.7×; B−G 6.4 vs 1.27 = 5.0×), same per-channel necessity the single
  frame guard documented.
- **Caveat 2 (argmax trap):** without masking the letterbox, the pair-argmax can land on the
  bottom edge (y=703–718) instead of the tear band (the r8 case) — the pairwise profiles
  must mask the same fixed letterbox rows (skipTop 120 + bottom crop) as the per-frame scan.
- **Caveat 3 (spiky profile breaks median-normalisation):** normalising the pair profile by
  its own median FAILS — clean pairs have max/median ≈ 28 (LED rows only), overlapping torn
  pairs (22.2). Don't normalise by the profile median; normalise by global level (AE shift,
  or full-frame mean |diff|), or rely on the chroma arms which have few clean outliers.
- **Caveat 4 (sample size):** one battery's exposure settings (exp 699.97 across L=
  80–150, plus the exp-300 extract). The clean pairwise baselines are *probably*
  intensity-dependent too — the same weakness the operator complained about, in pairwise
  form.

---

## 3. The natural same-content pair: the regrab

The existing regrab path already produces the perfect pairwise test. When anything suspects
a tear, the page holds the paint and regrabs **the same plane**: grab vs regrab see the
identical painted scene. Any coherent horizontal band difference then convicts the FIRST
grab unambiguously — no content change is possible, so no ambiguity, and thresholds can be
tight.

- What the corpus can say: there are **no actual grab-vs-regrab pairs on disk yet** (the
  regrab path hasn't shipped). Best available proxies:
  - consecutive-frame global gray shift ≤ 0.58 luma (AE drift between seconds-apart grabs
    is ~half a luma; a regrab seconds later should be in the same range);
  - same-index cross-run comparison (same *coded* content, different exposure settings):
    worst clean adjacent-step 6.56 luma — i.e. even across exposure changes the pair metric
    stays below the weakest same-run tear signal, and a same-scene regrab will sit far below
    that.
- Suggested confirmatory test on the regrab pair: same 3-arm band statistic with tight
  thresholds (gray ≈ 2–3, chroma ≈ 1–1.5 — to be set from live data), plus band
  localisation to log WHERE the first grab differed.
- This also upgrades `.rN` shipping: instead of "ship the retry blindly", compare grab vs
  regrab and ship whichever frame passes the single-frame scan *and* shows no band
  difference against its twin.

Cost: **one extra grab only when suspicion fires — which is exactly the regrab the current
design already performs.** Compared with the chain approach (§1): no waiting for a third
frame, no plane repaint, no first/last-plane blind spot, no wire reordering — for the same
expected extra-grab count (≈ 0.03 per burst at the measured tear rate).

---

## 4. Simple pairwise statistics that survived measurement

For a pair (grab A, grab B), page-computable over the same row means the single-frame scan
already builds (≈ +1 ms on a 10 ms budget):

1. Per-row mean-abs-diff profile, smoothed 3-tap [1,2,1]/4, on gray / R−G / B−G row values
   (the same three arms).
2. Band localisation: argmax row of the adjacent 3-tap step, with the letterbox masked.
3. Normalisation: by global AE shift or full-frame mean |diff| — **not** by profile median
   (fails: clean p50 28 vs torn min 22, see §2 caveat 3).
4. Global-shift floor: if |mean(grayA) − mean(grayB)| is large (≫ 1 luma), suspect AE
   movement, not a tear — clean pairs never exceeded 0.58 luma, so anything ≥ ~2 luma is a
   genuine AE event. Note a tear can also drag the global mean (2 of 4 torn pairs shifted
   3.6–9.6), so the shift is a *rejection gate for AE drift*, not a tear detector.
5. For the chain variant: signed windowed band offset vs each neighbour (§1 table) — the
   equal-and-opposite pattern across the two pairs is the tear signature.

---

## 5. Verdict and ranked recommendation

| method | ambiguity | extra cost per tear | false-alarm risk (measured) | verdict |
|---|---|---|---|---|
| Per-frame self-scan (current, absolute thresholds) | none | 1 regrab, paint held | 0 FP / 97, 0 FN / 3 — but thresholds are intensity-dependent (retuned twice) | **keep**, with the parallel self-normalising work as the trigger |
| Consecutive-pair diff, alone | **yes** — both frames retaken, guilty one needs repaint | up to 3 regrabs + repaints | gray-arm margin only 1.1× (tear min 4.0 vs clean max 3.66); chroma arms carry it | **do not gate shipping on this** |
| Consecutive-pair + chain attribution | none (sign-flip coherence, 4/4 tears, ≥8× margin) | 1 retake but late → repaint | chroma-based pair trigger 5–13.7× on this corpus | useful **offline**; in-page only if the trigger develops false negatives |
| **Same-content regrab diff** | **none** — any band diff convicts the first grab | 1 regrab, already the current flow | TBD live (no pairs on disk); proxies: AE shift ≤0.58, cross-exposure pair ≤6.56 | **ship as the confirmatory pairwise test** |

1. **The operator's concern is confirmed**: compare-to-previous alone cannot attribute a
   tear — both members of a tripped pair need retaking, and the innocent neighbour's plane
   must be repainted because the rig has moved on.
2. **A third frame resolves it** (the chain trick works with ≥8× margin) but adds latency
   and repaint cost, and cannot cover the burst's first/last planes.
3. **The best pairwise comparison is the one the pipeline already enables**: the same-plane
   regrab diff. No ambiguity, no content confound, no extra expected cost, and it makes
   `.rN` shipping evidence-based instead of blind.
4. **Do not use consecutive-plane diffs as the primary detector** — the content-change
   margin is thin (1.1× gray, no clean margin against r10:p05-class tears) and the baseline
   will drift with exposure/duty exactly like the absolute thresholds did.

### What needs live validation on the next battery

- **Real grab-vs-regrab pairs** (none exist on disk): distribution of the 3-arm pair
  statistic on *clean* same-plane regrabs, to set the confirmatory thresholds from data
  rather than proxies. This is the single missing measurement.
- AE drift over the actual seconds-long grab→regrab interval (proxy: 0.58 luma between
  consecutive planes; regrabs are further apart).
- Whether pairwise chroma clean maxima stay ~1 luma at other duty/exposure settings.
- Chain sign-coherence on live tears (4 samples so far, all consistent) and the letterbox
  masking for pairwise argmax (r8 case landed on the bottom edge).
- Phone timing budget for the pairwise pass (expect ≈ +1 ms over the single-frame scan;
  row means are shared).

Appendix: `pairwise_analysis.py` + `pairwise_measurements.json` (this directory) hold every
number quoted; scripts are rerunnable read-only. Run imagery untouched; no serial ports; no
git actions.