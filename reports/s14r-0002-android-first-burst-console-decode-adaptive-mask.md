# S14R-0002 android first burst (09:44, exp=500 regime) — console decode, legacy vs adaptive mask

Corpus: `runs/daemon/runs/s14r2-and-0944/` — 25 jpg (planes `cwc_r1_p00..p23` + `cwc_r1_master`)
+ 25 metas, wire capture 09:44-09:46, android, S14R-0002 build. Operator ground truth for this
burst: that dir only. Conventions reused exactly from
`reports/s14r-android-exp500-four-burst-console-decode-and-matrix.md` (subprocess CLI, one burst
per process, synthesized `<tag>_frames.txt` glue from the frameless pulled jpgs+metas, gates
mask/amp/margin = 100/40/6, `--fullres-rad 4`, `--n 600`, save json+shifts, no overlay, no repo
writes — all decoder outputs live in scratch `/home/nellie/.hermes/cache/scratch/s14r2b/`).

CLI note: the android campaign's `--bank tools/codewords_12of24.json` flag no longer exists in
`tools/cwc_pos_decode.py` — the 12-of-24 bank (n_codes 1600, ids 0..599 = `codes[:600]`, dmin 8)
is the hardcoded default, so bank identity is unchanged. `--peak-margin` left at default 0.05;
suppression window SUPPRESS=7 px hardcoded (per-LED site suppression + strongest-site-per-codeword
dedup).

## Sanity + hygiene

25/25 jpgs magic-clean (FFD8FF) and PIL-decodable; every meta reads exactly
`exp=500.05 aem=continuous ev=-1 fd=0.00`; labels complete p00..p23 + master. Bank check:
1600 codes, all weight 12 (distinct-plane sets, dmin 8), `codes[:600]` = the n=600 id space.
Hygiene: max claimed id 599 both decode passes and on the phone per-LED list (cwcN=600 honored,
**no cap-gap phantoms, no ids >=600, no double-claim sites**); min confirmed amp 40.0 (gate
honored exactly), min margin 8.2 (above gate 6). Interpreter-side instrument (stacksig rebuilt
from the CLI's own `direct_shifts.json`, points scored only, no (N,H,W) tensor in-kernel)
reproduces CLI amp+margin on ALL 321 confirmed ids with max|d| = **0.06 / 0.06** (same parity
class as the android report's 20-id sampling).

## Exact CLI lines run

```
PY=/home/nellie/.hermes/hermes-agent/venv/bin/python3
$PY tools/cwc_pos_decode.py <scratch>/runA --tag cwc --n 600 --amp-gate 40 --margin-gate 6 \
   --mask-thr 100 --mask-adaptive 0 --fullres-rad 4 --save-json --save-shifts          # pass A (legacy)
$PY tools/cwc_pos_decode.py <scratch>/runB --tag cwc --n 600 --amp-gate 40 --margin-gate 6 \
   --mask-thr 100 --mask-adaptive 1 --mask-k 1.12 --mask-floor 45 --fullres-rad 4 \
   --save-json --save-shifts                                                           # pass B (adaptive)
```

(`runA`/`runB` scratch dirs each carry the synthesized `cwc_frames.txt`; repo untouched.)

## Wire probe telemetry (verbatim, capture.txt 09:44:35)

```
[09:44:35] [PHONE] CWCSTATS {"build":"S14R-0002","n":24,"comp":0,"testMode":0,"testLed":0,"nStr":3,"nPerStr":200,"bright":120,"histMed":47,"clipPct":0,"probeIters":1,"coreP90":237.4,"probeSteps":[{"L":120,"P90":237.4,"clipPct":0,"histMed":47}],"expAtBurst":"exp=500.05 aem=continuous ev=-1 fd=0.00",...}
[09:44:35] [PHONE] CWCDECS {"ampGate":40,"marginGate":6,"suppress":1,"sitesMasked":143157,"confirmed":373,"conflicts":44,"strings":3,"perString":200}
[09:44:35] [PHONE] CWCDEC ... (373 per-LED entries, ids 0..599, amp min 40 med 65.6 max 142.5)
```

Non-{} telemetry confirmed: full 24-plane chain (conf 0.909-0.972), decode block, 44 conflict
pairs. Note the CWCSTATS `histMed: 47` is the PROBE frame's luma; the decode master's page-parity
histmed on the pulled jpg is **99.5**.

## Counts

| pass | mask rule | confirmed /600 | s1/s2/s3 | amp med [min-max] | margin med [min] | max id |
|---|---|---|---|---|---|---|
| A legacy | thr 100 fixed | **321** | 99/90/132 | 66.0 [40.0-138.5] | 31.3 [8.2] | 599 |
| B adaptive | min(100, max(45, 1.12·histmed)) | **321** | 99/90/132 | 66.0 [40.0-138.5] | 31.3 [8.2] | 599 |

Union over this burst (single burst, 24 planes) = the confirmed set: **A = B = 321/600
(53.5%)**. Pass B's outputs are byte-identical to pass A (ledpos + shifts diff-clean): the
adaptive rule **saturated** — `adaptive mask: histMed 99.5 -> eff thr 100.0` — because
1.12 × 99.5 = 111.4 > 100. **A→B delta = 0**; the handoff-predicted ~+22 dark-view rescue
(the r4-class result, histmed 74.5 -> thr 83.4, 492 -> 514, coded in the CLI's own evidence
block) does NOT show on this burst: the 09:44 view is mid-bright, not dark, so the first 0002
burst does not exercise the adaptive rule at all. Mask-class exposure among the missing is
tiny here anyway: only 3/279 missing ids have master blur peak < 100 at their anchors.

## Per-plane table (direct per-plane registration vs master; console fullres-refined rad 4)

Per-plane "confirm counts" don't exist in this decoder by construction — scoring integrates all
24 planes jointly (score[i] = Σ_p sign · stacksig); the per-plane quantities are registration,
gain, and ON-membership of confirmed codewords. Phone chain dx/dy quoted for cross-check
(dev = console-vs-phone |Δshift|; med 1.0, max 2.0 px — same-protocol registration).

| p | console dx,dy | conf | ref x/y | k ours | k phone | phone dx,dy | dev px | ON among confirmed |
|---|---|---|---|---|---|---|---|---|
| 00 | 3.11,-10.52 | 0.94 | no/no | 0.618 | 0.618 | 3.17,-9.52 | 1.00 | 160 |
| 01 | 3.12,-10.52 | 0.94 | no/no | 0.618 | 0.618 | 3.17,-9.52 | 1.00 | 163 |
| 02 | 3.10,-9.52 | 0.95 | no/no | 0.618 | 0.628 | 3.11,-9.71 | 0.19 | 156 |
| 03 | 4.17,-8.52 | 0.94 | no/no | 0.628 | 0.628 | 3.17,-9.52 | 1.42 | 158 |
| 04 | 3.17,-8.52 | 0.94 | no/no | 0.638 | 0.628 | 3.17,-9.52 | 1.00 | 162 |
| 05 | 1.00,-9.52 | 0.95 | no/no | 0.638 | 0.638 | 0.00,-9.52 | 1.00 | 161 |
| 06 | 1.00,-8.52 | 0.94 | no/no | 0.638 | 0.638 | 0.00,-9.52 | 1.42 | 168 |
| 07 | 1.19,-8.52 | 0.95 | no/no | 0.628 | 0.628 | 0.19,-9.55 | 1.44 | 160 |
| 08 | 1.00,-8.52 | 0.94 | no/no | 0.628 | 0.628 | 0.00,-9.52 | 1.42 | 158 |
| 09 | 1.00,-9.52 | 0.92 | no/no | 0.618 | 0.628 | 0.00,-9.52 | 1.00 | 160 |
| 10 | 1.00,-8.52 | 0.94 | no/no | 0.618 | 0.628 | 0.00,-9.52 | 1.42 | 164 |
| 11 | 1.00,-7.34 | 0.92 | no/no | 0.638 | 0.648 | 0.00,-6.34 | 1.42 | 162 |
| 12 | 0.07,-7.34 | 0.95 | no/no | 0.648 | 0.658 | 0.00,-6.34 | 1.01 | 155 |
| 13 | 0.01,-8.34 | 0.95 | no/no | 0.648 | 0.648 | 0.00,-6.34 | 2.00 | 163 |
| 14 | 1.16,-6.43 | 0.96 | no/no | 0.638 | 0.638 | 0.14,-6.41 | 1.02 | 169 |
| 15 | 1.00,-6.34 | 0.93 | no/no | 0.638 | 0.648 | 0.00,-6.34 | 1.00 | 161 |
| 16 | 0.07,-3.14 | 0.97 | yes/no | 0.658 | 0.668 | 0.06,-3.14 | 0.01 | 153 |
| 17 | -1.00,-3.10 | 0.95 | no/no | 0.648 | 0.658 | 0.00,-3.17 | 1.00 | 158 |
| 18 | -0.00,-4.17 | 0.95 | no/no | 0.638 | 0.648 | 0.00,-3.17 | 1.00 | 159 |
| 19 | 0.18,-6.34 | 0.95 | no/no | 0.628 | 0.638 | 0.00,-6.34 | 0.18 | 158 |
| 20 | 1.00,-5.34 | 0.91 | no/no | 0.638 | 0.638 | 0.00,-6.34 | 1.41 | 162 |
| 21 | 0.15,-3.06 | 0.96 | yes/yes | 0.658 | 0.658 | 0.15,-3.05 | 0.01 | 165 |
| 22 | 0.00,-1.17 | 0.93 | no/no | 0.678 | 0.678 | 0.00,0.00 | 1.17 | 166 |
| 23 | 1.12,-0.18 | 0.97 | no/no | 0.678 | 0.678 | 0.11,-0.16 | 1.01 | 166 |

(refined x 12/24, y 5/24; |shift| med 8.34 max 10.97 px — the camera was ~9 px off tripod, no
plane dropouts, all conf 0.91-0.97. k ours = page-parity histmed ratio on the pulled jpgs;
phone k = its `decode.k[]` — 13/24 exact, the rest within one histmed quantization step.)

## Never-seen 279 (47.5%) with failure anatomy

Classification via the instrument at interpolated anchors (id-chain quadratic for 265,
string-shape model for the 14 unanchored tail ids 196-199/304-310/393-399; android taxonomy):

| bucket | n | ids |
|---|---|---|
| rival-eats / suppressed (pass both gates at own site, lost 7px-window allocation) | 19 | 58, 106, 151, 246, 293, 332, 336, 418, 419, 420, 429, 494, 496, 513, 524, 534, 535, 550, 551 |
| bright-wall (ring med >= 200) | 142 | 14, 22, 23, 28, 30, 59-64, 66, 68, 73, 80-84, 101, 107-114, 116-118, 123-126, 128-134, 140, 141, 148, 160, 163-166, 170-178, 180, 182-184, 214, 217, 219, 229, 230, 234, 235, 258, 259, 261-265, 272, 274-283, 297, 325, 337, 338, 359-365, 367-374, 504-508, 510, 543, 555, 564, 566, 567, 570-578, 580-587, 590-593, 595-598 |
| low-contrast | 111 | 69-71, 74, 76, 77, 79, 120, 122, 145, 152, 153, 155-159, 161-162, 185-187, 189-199, 209, 210, 266-269, 291, 298-317, 319-324, 326-331, 340, 357, 358, 381-384, 386-399, 432, 461, 509, 511, 512, 540-542, 553, 554, 556-560, 568, 569, 588 |
| view-dark (most ON planes dark at site) | 4 | 78, 211, 213, 462 |
| on-clip (>= 75% of ON planes clipped) | 3 | 100, 244, 538 |

Sub-gate amp bands (instrument amp at anchor, ± 3 px local best): 0-8: 53, 8-25: 124, 25-40: 79.
The view-dominant failure is the bright wall (142) — this S14R-0002 rig sits the camera with a
saturated ring background swallowing half the lamp field; consistent with the android campaign's
on-clip/bright-wall tail classes, at much larger n because this is a single burst (android
4-burst union never-seen was 17).

## Comparison vs baselines

| burst | confirmed /600 | note |
|---|---|---|
| **s14r2-and-0944 A (legacy)** | **321** | this report |
| **s14r2-and-0944 B (adaptive)** | **321** (delta 0, rule saturated) | this report |
| android exp500 four-burst union | 583 | 4-burst union, prior report |
| android r5 (worst single) | 387 | dark-ish view, histmed 83.5 |
| android r3 (best single) | 549 | |
| ios r4 (worst-ish single) | 257 | prior ios report |

Single-burst vs single-burst: 321 sits **66 below android's worst (r5 387)** and **64 above
ios's worst (r4 257)** — inside the campaign-to-campaign single-burst swing (ios spanned
257-487 at constant photometry). Not directly comparable to the 583 4-burst union (this corpus
has one burst; more bursts will union upward).

## Phone-count note (373): same protocol, gap decomposed

The phone 373 IS a same-protocol number — the page and console share the protocol end to end:
identical gates (amp 40 / margin 6 / suppress), per-LED ids on the same 0..599 code space, amps
in the same units (median ratio 0.994 across shared ids), per-plane k arrays matching (13/24
exact, rest within one histmed step), and `sitesMasked` parity: phone 143,157 vs our master's
thr-100 masked count 144,458 (−0.9%) — pinning the phone's effective mask threshold at ≈100 on
this view as well (its mask did not relax either; the CWCSTATS histMed 47 is probe-frame, not
the decode master). The 373 vs 321 gap (52) decomposes as:

- **+29 suppressed**: phone-only ids that PASS console gates at the phone's own site with the
  codeword itself winning the local argmax (self-amp 40.8-84.0, margins 6.6-39.8) — every one
  sits 2.0-6.1 px from a confirmed site (26/29 within 3.5 px), so the CLI's 7 px one-site-per-
  window rule ate them (android report: "CLI one-site-per-window silently drops one member of
  some colocated pairs; the page's conflict-tolerant flag-both semantics is healthy"). The
  wire's 44 conflict pairs corroborate: 24 both-confirmed, 18 one-member on console, 2 neither.
- **+26 sub-gate at the phone site**: phone amp runs ~+10% over console at hairline sites
  (median amp ratio 1.096 at the same pixel) — 22 amp<40-only (best: id 14 phone 47.8 vs
  console 39.9), 4 margin<6-only, 1 both. k[] quantization + blur/rounding differences.
- **+3 console-gained** (ids 143, 273, 552; amps 41.6/40.7/42.8, margins 18.8/18.3/13.6) —
  the phone's dedup dropped these hairline ones; near-symmetric noise at the margin.

So the phone ran the same decode at the same mask/gates; the entire 52 is site-tolerance
semantics (29) plus hairline amp rounding (23), not a protocol mismatch.

## Probe verdict

The first S14R-0002 burst validates three of the four probe items and fails to exercise the
fourth. (1) Knee-band core: probeSteps P90 237.4 vs the 255 wall with clipPct 0 — the core sits
squarely in the band below saturation; pulled-master checks agree (no >=250 planes anywhere near
the confirmed cloud's ON-planes except the 3-id on-clip set). (2) One probe iteration:
probeIters 1 with a single accepted step {L 120, P90 237.4} — the probe accepted its first
brightness step, no iteration churn. (3) Non-{} telemetry: full CWCSTATS (chain 24 planes conf
0.909-0.972, decode block, 44 conflicts) + 373-entry per-LED CWCDEC — the build/telemetry path
is healthy. (4) Adaptive mask effect: **not validated by this burst** — its decode-master
histmed (99.5) keeps the rule saturated at thr 100, A ≡ B byte-identical, delta 0. The +22
dark-view rescue remains an r4-class prediction; validating it needs a burst whose master
histmed lands below ~89 (the 100/1.12 crossover). Recommended next: an aimed darker-view burst
(r4-style) on this build, and a page-side conflict-tolerant port to the CLI if suppression
losses (29 here, 19 console-class) keep recurring.

Artifacts (scratch, not repo): `/home/nellie/.hermes/cache/scratch/s14r2b/` — passA/passB logs +
ledpos.json + direct_shifts.json, phone_cwcdec.json, wire_cwcstats.json, wire_cwcdecs.json,
miss_anatomy.json, phone_only_anatomy.json, parity.json, classification.json, scripts
01-07. Repo writes: this report only. No image files, no git, daemon untouched, /dev/ttyACM0
never opened.