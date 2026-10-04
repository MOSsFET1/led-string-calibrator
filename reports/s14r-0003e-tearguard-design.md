# S14R-0003e — Tear-guard design for the cal/survey camera pipeline (0004)

Era: S14R-0003C-CAL (0003c milestone battery). Date: 04 Oct 2026 (analysis session).
Corpus: `runs/daemon/runs/run8..run11` (E4 CWC bursts, 25 jpgs each, L∈{80,100,120,150},
exp 699.97 aem=continuous ev=-1) — 100 frames total, ALL wire-verified/repaired per the
standing IMAGE-HYGIENE GATE (s14r-0003c-wire-repair-inplace.md; 0004(g) FEND-terminated
writer). Read-only corpus: no run-dir file touched; analysis tooling + JSON live under
`runs/daemon/analysis/tearguard/` (text, trackable).

Ground truth (3 torn captures, visually + kbg-outlier confirmed):
`cwc:r8:p07` (hard luma cliff y=191), `cwc:r10:p05` (y=383), `cwc:r10:p21` (red-state band
y≈590). Controls: the other 97 frames (max per-plane kbg deviation from run median +3.3%,
row-jump ≤ ~10 luma units on JPEG-decoded frames).

---

## 1. Metric (final, page-computable, validated)

`TearScore(frame)` = max over 3 arms, each arm = max adjacent |diff| of 3-tap-smoothed
`[1,2,1]/4` row means of the frame's RGBA data (W×H ≈ 406×720):

| arm | row value | threshold |
|-----|-----------|-----------|
| gray | (R+G+B)/3 | **16.0** |
| rg | R−G (abs) | **3.5** |
| bg | B−G (abs) | **3.5** |

Tear = ANY arm over threshold; tear_y = argmax row of the first firing arm.
Full column resolution (no row subsampling — LED row structure oscillates ±24/row sampled;
full-width sums average it out exactly), no horizontal subsampling (all 406 columns).

Pseudocode (page-exact, runs on `procCx.getImageData(...).data` or Blob→createImageBitmap
pixels; ≤3.5 ms/frame on desktop node, ~10-30 ms budget on phone ≤100 ms):

```js
function tearCheck(d, w, h) {            // d = RGBA Uint8ClampedArray
  const u32 = new Uint32Array(d.buffer, d.byteOffset, w * h);
  const g = new Float32Array(h), rg = new Float32Array(h), bg = new Float32Array(h);
  for (let y = 0; y < h; y++) {
    let sg = 0, srg = 0, sbg = 0;
    for (let x = 0; x < w; x++) {
      const v = u32[y * w + x];
      const R = v & 255, G = (v >>> 8) & 255, B = (v >>> 16) & 255;
      sg += R + G + B; srg += R - G; sbg += B - G;
    }
    g[y] = sg / (3 * w); rg[y] = srg / w; bg[y] = sbg / w;
  }
  const arm = v => { let m = 0;
    for (let i = 1; i < h - 2; i++)
      m = Math.max(m, 0.25 * Math.abs((v[i-1] + 2*v[i] + v[i+1]) - (v[i] + 2*v[i+1] + v[i+2])));
    return m; };
  return { gray: arm(g), rg: Math.abs(arm(rg)), bg: Math.abs(arm(bg)) };
}
// TEAR  ⇔  gray > 16 || rg > 3.5 || bg > 3.5
```

Bench: `runs/daemon/analysis/tearguard/page_algo_bench.js` (dumped-frame RGBA harness,
page_algorithm verbatim; source dumps + per-frame values in `measure_raw.json`).

**Gray-arm trap (do not "optimize")**: the Uint32 RGBA word is byte-position-weighted
(B·2¹⁶ + G·2⁸ + R + A·2²⁴) — NOT a linear RGB sum. Summing raw u32 words silently changes
the metric (JS parity failed until R/G/B were masked individually). The chroma arms need
the masks anyway; gray costs one extra add. Doubles are exact throughout (row sums ≤ 1.7e12).

## 2. Threshold + measured FP/FN (100/100 corpus, wire-verified)

Clean distribution (n=97): D_gray 5.71–9.78 (med 7.87, p95 9.43, max 9.78 = r11_p23);
|D_GmR| max 1.72 (r11_p19); |D_BmG| max 1.53 (r10_p11). Tears: r8_p07
gray 26.65 / rg 13.99 / bg 9.28 → y=191; r10_p05 gray 7.83 / **bg 8.37** → y=383;
r10_p21 gray 9.58 / **rg 23.90** → y=575 (JS row resolution; vision said y≈590 band —
same 575-594 red-state edge, argmax sits at its shoulder).

**FP = 0 / 100, FN = 0 / 3.** Normalized margin S = max(arm/threshold): clean max **0.611**
(r11_p23), 2nd 0.600; tear min **2.39** (p05 via bg). Headroom: 1.64× against the
worst-clean, 2.39× against the weakest tear, per-arm ≥2.0× clean / ≥2.4× tear. Thresholds
are JPEG-pipeline-calibrated (q0.5 decoded bytes = what benchCapture ships); the page-side
pre-JPEG grab differs by ≲1 luma on row means — absorbed by the ≥1.64× headroom.

**Per-channel necessity is proven, not precautionary**: r10_p21's gray score (9.58) sits
BELOW the clean gray max (9.78) — luma-only misses it, |G−R| fires at 23.9 vs clean 1.72
(13.9×). r10_p05 has NO channel cliff above luma noise on R−G (1.32) or gray (7.83) but
B−G steps 8.37 vs clean 1.53 (5.5×) — the tear replaced a red-lit band with a
green/cyan-shifted one below y=383. r8_p07 fires all arms. One metric fits all three
because an iOS sensor/AE tear re-gains ONE or TWO channels per band, never all three
coherently.

Cross-check: per-plane kbg flags r8_p07 (+60% vs run median) and r10_p21 (+143%) but
r10_p05 only +12% (rank 23/24) — overlapping the clean spread (r9_p12 is +3.3% clean) —
kbg is corroborating evidence, NOT a detector. The row-metric strictly dominates it.

## 3. Timeline + trigger analysis

Cadence (page t, wire metas): capture-side plane dt median 260–279 ms (min 204, max 497)
→ 25 frames in 6.4–6.9 s; wire relay is a SEPARATE, slower process (one post-epoch
benchPull: 100 FRAME groups relayed 10:27:10→10:33:12, dt med 4 s, 3–6 s — the ~3.5 s/frame
serial ceiling; the torn frames' capture-side dt is statistically indistinguishable from
their clean neighbors: 245/301 ms around r8_p07 vs 279 median).

Tear positions: epoch index 7 (r8, t+2.0 s into burst), 5 (r10, t+1.4 s), 21 (r10,
t+5.5 s) — TWO different depths inside the SAME epoch, one mid-burst in another; the four
p00s (all after the worst AE step of the battery: 10 s rig-black + duty dwell between
epochs) are all clean; every tear has ±1 clean neighbors and a same-index plane in other
runs with an essentially identical clean profile. aem=continuous and exp frozen at 699.97
in all 100 metas (readback never tracks gain — weak witness; but there is no exposure
correlate at all).

**Finding**: no trigger correlate exists at this cadence. The tears are per-grab
asynchronous events — consistent with a metering/readout race inside the phone camera
pipeline (the iOS-side equivalent of a sensor/AE mid-readout tear) — at ≈3/2400 grabs
(0.13%) with no correlation to: position-in-epoch (early planes after paint: NO — p21 is
late; after the 10 s gap: NO — all four p00s clean), the scene-luma plane steps at 50%
duty (96 clean coded planes + 4 clean masters across the same steps), or relay load
(shipping is post-epoch, wire-serial, invisible to the capture cadence). Pre-burst AE
settle and dwell-level guards have nothing to gate on: the 2 s duty dwell already ends
with AE visually settled (97 clean frames prove the cadence is safe), and the tear fires
mid-burst. The honest attribution: single-frame sensor/AE reconfiguration race, mechanism
not instrumentable from the page; the guard is therefore DETECT-AND-REGRAB per frame.

## 4. Placement + regrab policy (0004 scope)

**Placement: per-frame post-grab detect + immediate regrab** — inside `expE4`'s plane loop
(cal.html:748–755), wrapped around `grabPlane()` before `benchCapture(...)`. The grab is
procCx canvas pixels (pre-JPEG), the check is <10 ms against a ~260 ms plane period, and a
retry repeats the exact pending paint state (the plane's `frameBitsPaint` is still held on
the rig — the retry is `grabTimeout(); lumaOf()` while the same plane displays; settle
re-wait 70-100 ms). The 70 ms master grab gets the same check + retry (keeps
"fast before AE re-meters": ≤2 retries ≈ +400 ms worst case). The E1/E2 ladder snapshots
(E3/E5 are full-frame stats already) may reuse the same guard later; E4 is the 0004 scope.

- Max retries: **2 extra grabs per frame** (3 grabs total incl. original). On persistent
  tear after retry cap: ship the BEST-attempt frame with label suffix `.r2` and emit
  `TEARGUARD {label, arm, y, tries}` on the wire (LOG-visible) — do NOT abort the burst
  (union-across-planes absorbs a torn plane; its kbg outlier is expected and the decoder's
  per-codeword completion tolerates up to 5 dropped planes measured).
- Label suffix convention (§5 collision-safe): attempt-0 ships the plain label
  `cwc:rN:pNN`; retry-1 (if attempt-0 failed the check) ships `cwc:rN:pNN.r1`; retry-2
  ships `.r2`. Suffix rides the FRAME meta label → daemon filename
  `cwc_r8_p07.r1.jpg` (daemon's `re.sub(r'[:*?"<>|]','_')` keeps the dot; skip-if-exists
  unaffected; distinct t values keep the groups separable in capture.txt).
  **Ordering constraint**: `cwc_pos_decode.py` does `int(label.split(':p')[1])` on plane
  labels — the suffix CRASHES a naive parser and duplicate labels last-win silently. 0004
  must land the tolerant parse (strip optional `\.\d+$` before the int; on duplicate plane
  indices prefer the LAST occurrence — the regrab — or, better, prefer the attempt whose
  TearScore is lowest, which the meta `t` + TEARGUARD lines make auditable) IN THE SAME
  BUILD as the guard. Until that lands, a suffix is wire-visible but decodes via
  last-wins only if the parser tolerates the suffix — never ship the guard without it.
- Rate telemetry: per burst, CWCSTATS gains `tears: n` (count of retry successes + shipped
  `.rN` frames); >2 tears in one burst logs a WARN (degradation signal) — battery continues.

## 5. Prevention beyond regrab: verdict = regrab-only is the right 0004 scope

- `cwcAeLock` (survey.html path, `exposureMode:'manual'` after p00): cal.html has NO
  cwcAeLock path today; the survey one is best-effort and log-confirmed unsupported on iOS
  ('AE lock not supported on this device' — iOS Safari exposes no exposureMode). The burst
  camera is an iPhone: the lever demonstrably does not reach the device class that tore.
  On Android it pins exposure DARK (recorded S14P lesson) — a photometric-regression risk
  against the 14-corp exposure corpus for a 0.13% event.
- POI: already applied at battery start (applyPoi, cal.html:318) and it did not prevent
  the tears — POI converges AE, it does not fence a mid-readout race, and the clean-p00
  evidence shows AE settling is not the failure moment.
- AWB lock: equivalent platform story (no iOS applyConstraints support), and the
  red-band tear anatomy (one channel re-gained per band) is a GAIN/luma event, not a
  white-balance decision the page could lock.
- The 3 tears' positions + clean neighbors + identical same-index clean profiles across
  runs mean NO page-side pacing change (settle, dwell, gap) has a target to fix: the
  mechanism is inside the camera pipeline between frames, not at its boundaries. Evidence
  path if that ever changes: the TEARGUARD/CWCSTATS counters accumulate per-epoch tear
  positions across future batteries — clustering at epoch starts would resurrect a
  pre-burst-settle argument with data; none exists today (3 samples, 3 different shapes).

## 6. Recommendation (one line)

Add the 3-arm row-discontinuity tear guard (gray 16 / |R−G| 3.5 / |B−G| 3.5, 0 FP + 0 FN
on 100 wire-verified frames, ≥2.3× margins both ways, <10 ms in-page) to E4's post-grab
path with ≤2 auto-regrabs and `.rN` label suffixes + tolerant decoder parse, regrab-only
in 0004 — AE/AWB lock prevention is dead on iOS and unwarranted at a 0.13% tear rate.

---
Appendix: measurements `runs/daemon/analysis/tearguard/measure_raw.json` (100 rows, all
arms + band stats + meta t), scripts `measure_tears.py` (python/cv2-side) + 
`page_algo_bench.js` (page-exact node harness; parity 5/5 frames exact vs python on all
arms). No run-dir imagery touched; no daemon/serial interaction; no git actions.