# Phone-vs-console CWC detection gap report

**Scope:** `page/survey.html` in-page decoder vs `tools/cwc_pos_decode.py` console decoder, S14P-era direct-registration CWC decode path.

**Symptom to explain:** phone reports 193/200 missing `[27,46,90,91,131,150,156]` while console on the same frames reports 196/200 missing `[27,46,90,91]`. Gap LEDs: **131, 150, 156**.

## Executive summary

The page and console decoders are *structurally* aligned (direct per-plane registration, same codeword bank, same 9-of-18 scoring, same strongest-site-per-codeword dedup, same nominal gates). The concrete divergences are in **preprocessing details** and **peak-conditioning logic** that directly affect whether borderline core sites enter the candidate set and whether their registration is refined accurately. The most likely root cause of the observed gap is a **mask-threshold mismatch** (phone running an effective threshold higher than the console’s 150), compounded by the page’s stricter ±3 px local-max filter and a different parabolic-refinement guard.

---

## 1. Candidate-mask generation (high severity)

| | Page (`survey.html`) | Console (`cwc_pos_decode.py`) |
|---|---|---|
| Blur | Two passes of 3-tap box blur (`blur3`) | `cv2.GaussianBlur((5,5), 1.2)` |
| Threshold | `MASK_THR = CFG.cwcMaskThr \| 0 \|\| 175` | `args.mask_thr` (default `150`) |
| Fallback bug | **If `CFG.cwcMaskThr` is 0 or falsy, threshold falls back to 175, not 150.** | No equivalent fallback; default is 150. |

**Impact:** A 175 threshold is known to exclude edge-view / shallow-angle LEDs whose blurred-luma sits 184–191. S14-CWC-PLAN §10c documents that LEDs 20–25 and 150–156 were exactly this class. A phone running 175 while the console runs 150 would miss **150 and 156** (and similar tail LEDs), matching the reported gap. `localStorage` stickiness can keep a previous 175 value even after the shipped default is changed to 150.

**Recommended fix:**
- Remove the `|| 175` fallback or set it to the shipped default `150`.
- Explicitly clamp/validate `CFG.cwcMaskThr` at apply time so a stale `localStorage` value cannot silently override the console-matched default.

---

## 2. Parabolic sub-peak guard (medium-high severity)

| | Page | Console |
|---|---|---|
| Guard | Curvature form: `|left + right − 2·center| > 2·cwcNccPeakMargin` | Peak-vs-shoulder form: `center − max(left,right) >= cwcNccPeakMargin` |
| Effective when | A symmetric peak with both shoulders close to center can fail the page guard even though it is a clear peak. | A peak only needs to stand above its neighbours. |

**Impact:** The page guard is stricter for broad, low-contrast peaks and may keep the integer pick on more planes under handheld motion, while the console refines to sub-cell shifts. Sub-cell registration errors of ~1–3 px can push a weak core site out of the ±4 px full-res refine window or move the score map enough that a neighbour’s codeword wins the argmax. This contributes to inconsistent phone-vs-console argmax at LEDs 131/150-class edge sites.

**Recommended fix:** Align the page guard with the console’s `peak − max(shoulder)` formulation, or run both and verify they produce identical refinement decisions on every plane of every run.

---

## 3. Local-max filter before greedy suppression (page only) (medium severity)

Page `cwcDecode` applies a ±3 px local-maximum filter on `bestS` **before** amp/margin gates and suppression:

```js
for (let dyy = -3; dyy <= 3; dyy++) {
  for (let dxx = -3; dxx <= 3; dxx++) {
    if (nb >= 0 && bestS[nb] > v) { ok = 0; break; }
  }
}
```

Console does **not** have this filter; it sorts all masked pixels by score and uses a 7 px suppression window only.

**Impact:**
- A true LED whose score map peaks 2–3 px away from the blurred-luma local maximum can be rejected on the phone while accepted on the console.
- Conversely, a bloom-skirt pixel can survive on the phone if it happens to be a local max, then steal the codeword via strongest-site dedup.
- This explains **131** potentially disappearing on the phone even though the console recovers it, or the phone finding a different site for the same codeword.

**Recommended fix:** Remove the pre-gate local-max filter from the page and rely on the suppression window + strongest-site dedup, matching the console. Re-test with the decoder-gate harness (S14-CWC-PLAN §11.5 item 3).

---

## 4. Decimation / grid arithmetic (low severity, parity risk)

| | Page | Console |
|---|---|---|
| Decimated height | `Math.max(2, Math.round(128 * H / W / 2) * 2 \|\| 72)` | `max(2, int(round(DW * h / w / 2)) * 2)` |
| Scale K | `W / 128` | `W / DW` (`DW = 128`) |
| Surface dtype | `Float32Array` | `float64` |

For typical phone resolutions these evaluate to the same `dh` and `K`, but the `|| 72` fallback and `Math.round` vs Python `round()` can diverge on unusual aspect ratios. Float32 accumulation of the NCC surface can also introduce ~1e-7 level differences, negligible at the gate but worth parity-locking.

**Recommended fix:** Share the exact decimation helper between page and console (or generate a console reference grid and assert equality in the harness).

---

## 5. Dedup / site merging (no functional bug, but note)

Both decoders use the same strongest-site-per-codeword rule:

```js
// page
const byled = {};
for (const s of out) {
  if (!cur || s.amp > cur.amp) byled[s.led] = s;
}
```

```python
# console
byled = {}
for q in ledpos:
    if cur is None or q['amp'] > cur['amp']:
        byled[q['led']] = q
```

No divergence here. Note: `cwcShowResults` on the page re-counts multi-site claims from `dec.sites` (already deduped), so the green/amber colour logic is misleading on the current single-string bench.

---

## 6. ampGate / marginGate application (no functional bug)

Both apply the same thresholds after computing `amp = bestS / 9` and `margin = (bestS − bestRunner) / 9`. Default values are identical (60 / 10 / 150 / 0.05). The risk is **sticky `localStorage`** carrying a prior gate (e.g. marginGate 25) — this is already noted in the source comments and S14-CWC-PLAN §10e but remains an operational hazard.

---

## Most likely explanation for the reported gap

For the symptom **phone missing 131, 150, 156** while console finds them:

1. **Primary:** effective phone mask threshold > console’s 150 (fallback to 175 or a stale `localStorage` value). LEDs 150/156 are documented edge-core LEDs whose blurred-luma is 184–191 and are excluded by 175 but accepted by 150.
2. **Secondary:** the page’s ±3 px local-max filter and stricter parabolic guard shift or suppress weak core sites (e.g. 131) that the console retains.
3. **Tertiary:** subtle registration differences from the different peak guard push argmax scores across the d6 margin boundary for LEDs sitting next to strong neighbours.

---

## Recommended action list

| # | Action | Severity |
|---|---|---|
| 1 | Fix `MASK_THR = CFG.cwcMaskThr \| 0 \|\| 175` to default to the shipped `cwcMaskThr` value (150) and add a validation/clamp on CFG load. | High |
| 2 | Remove the page’s pre-gate ±3 px local-max filter to match console behaviour. | High |
| 3 | Align page parabolic-refinement guard with console (`peak − max(shoulder) >= margin`). | Medium-High |
| 4 | Add a harness test (S14-CWC-PLAN §11.5 item 3) that runs the same frame set through both decoders and asserts identical site lists. | Medium |
| 5 | Reset / version-gate `localStorage` cfg stickiness so a default change actually reaches returning phones. | Medium |

---

## Files referenced

- `POC LED survey/page/survey.html` — `cwcChain` (lines 1118–1196), `nccRefine` (1197–1228), `cwcDecode` (1229–1355).
- `POC LED survey/tools/cwc_pos_decode.py` — `register_direct` / `ncc_search` / `parabolic_refine` / `ncc_refine` / `main`.
- `POC LED survey/S14-CWC-PLAN.md` — §10b/§10c/§10d/§10e (mask threshold history, parabolic spec, sticky gate hazards).
