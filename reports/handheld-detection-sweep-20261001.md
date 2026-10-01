# Handheld CWC detection sweep — S14P-1917 baseline alternatives

**Date:** 2026-10-01  
**Goal:** Try console-side parameter changes that can be ported to the phone page to improve LED detection rate over the S14P-1917 baseline.  
**Runs used:** four handheld CWC bursts with inter-frame movement (no AE-lock dark runs):
- `s14p-1908-handheld` (recovered from git `2ac118e`)
- `s14p-1911-handheld-r4` (recovered from git `2d128f2`)
- `s14p-1913-handheld`
- `s14p-1917-handheld` (the requested baseline)

Excluded: `s14p-1914/1915` AE-lock dark-image runs (per operator instruction).

**Decoder:** `tools/cwc_pos_decode.py` with monkey-patched blur kernel / suppression radius / gain estimator.

## Baseline visible-LED counts

Visible LEDs = 200 minus the 3 operator-confirmed hidden LEDs (46, 90, 91).

| run | baseline confirmed | baseline visible |
|-----|-------------------:|---------------:|
| 1908 | 186 | 186 |
| 1911 | 196 | 196 |
| 1913 | 185 | 185 |
| 1917 | 195 | 194 |
| **mean** | **190.5** | **190.2** |

## Best single-knob changes

All numbers are visible-LED counts (higher = better). “Min” is the worst run.

### Suppression radius (biggest lever)

The page and console currently use a 7×7 suppression window (`x±3, y±3`). Reducing it is by far the most effective change.

| config | 1908 | 1911 | 1913 | 1917 | mean | min | portability |
|--------|-----:|-----:|-----:|-----:|-----:|----:|:------------|
| **sup1** (1 px) | 192 | 197 | 195 | 197 | **195.2** | 192 | trivial: change `x0/x1, y0/y1` |
| **sup2** (2 px) | 191 | 197 | 192 | 197 | 194.2 | 191 | trivial |
| **sup3** (3 px) | 191 | 197 | 192 | 197 | 194.2 | 191 | trivial |
| sup4 | 191 | 197 | 185 | 196 | 192.2 | 185 | trivial |
| sup5 | 191 | 197 | 185 | 196 | 192.2 | 185 | trivial |
| sup7 (current) | 186 | 196 | 185 | 194 | 190.2 | 185 | — |

**Verdict:** `sup1` is best on raw visible count, but the tiny window risks accepting the same physical LED twice (bloom skirt split into two codeword claims). `sup2`/`sup3` give nearly the same gain with a little more safety margin. The current 7 px window is clearly too aggressive for handheld geometry.

### Other knobs (smaller gains)

| family | best config | mean | min | note |
|--------|-------------|-----:|----:|------|
| mask threshold | `mask135` | 191.0 | 184 | small gain on some runs |
| blur kernel | `blur5_s1.5` | 190.5 | 186 | minor, not consistent |
| full-res radius | `frad2..8` | 190.2 | 185 | current `frad4` already optimal |
| peak margin | `pm0.1` | 191.5 | 187 | slight gain on 1908/1913 |
| gain estimator | `gain_p75` | 190.5 | 185 | negligible |

None of these match the suppression-radius effect.

## Combined configs tested

| config | 1908 | 1911 | 1913 | 1917 | mean | min |
|--------|-----:|-----:|-----:|-----:|-----:|----:|
| sup3 + mask135 | 195 | 197 | 191 | 197 | 195.0 | 191 |
| sup3 + mask135 + pm0.01 | 195 | 197 | 193 | 196 | 195.2 | 193 |
| sup3 + mask140 + amp55 | 195 | 197 | 192 | 197 | 195.2 | 192 |
| sup1 | 192 | 197 | 195 | 197 | 195.2 | 192 |

All are within ~1 LED of each other. The simpler change (`sup2` or `sup3`) is preferable for portability.

## The impostor problem

Reducing suppression increases raw detections, but some of the “recovered” IDs are suspicious:

- LED 46 (known hidden) is often claimed within 2 px of LED 53 or 58.
- LED 90/91 (known hidden) are often claimed within 2 px of LED 26 or 173.
- LED 16, 27, 5 are sometimes claimed on top of unrelated neighbours.

These are classic bloom-skirt / impostor claims. With the current strongest-site-per-codeword dedup, a hidden LED’s codeword can steal a nearby bright pixel because the suppression window no longer blanks it.

A post-decode conflict filter (reject the weaker claim when two accepted LEDs are < ~6 px apart and their IDs are far apart) removes most of these, but also removes some legitimate collocated/fold-back LEDs the operator mentioned exist. A filter aware of the physical route would do better, but that is not a single-run phone-portable fix.

## Practical recommendation

1. **Port `sup2` or `sup3` to the page first** — change the suppression rectangle in `cwcDecode` from `x±3, y±3` (7 px) to `x±1, y±1` (3 px) or `x±2` (5 px). This is the highest-impact, lowest-risk page change.
   - Page location: `page/survey.html`, lines ~1371–1375.
2. **Keep amp=60, margin=10**; raising margin to 12+ trades away some real gains to avoid impostors.
3. **Add a post-decode spatial audit** (page-side or export-side) that flags when two accepted LEDs are closer than ~6 px but with non-consecutive IDs, so the operator can confirm rather than silently trust.
4. Leave blur, full-res radius, peak margin at current defaults — they are already close to optimal.

## Files generated

- Raw sweep data: `/home/nellie/.hermes/cache/scratch/led_sweep/sweep_summary.json`
- Saved decodes for top configs: `/home/nellie/.hermes/cache/scratch/led_sweep/compare/`
