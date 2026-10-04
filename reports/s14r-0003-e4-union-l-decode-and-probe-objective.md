# S14R-0003 E4 union(L) — four cal-battery CWC bursts decoded, union(L) objective for the probe revision

Corpus: `runs/daemon/runs/run8..run11` — first complete `CAL` battery E4, four REAL 24-plane CWC
bursts (epochs r8–r11, L ∈ {80, 100, 120, 150}), 25 jpgs each (`cwc_rN_p00..p23` + `cwc_rN_master`),
rig 3×200 = 600 lamps, `cwcN=600` honored (max claimed id 599 in all four bursts, **no cap phantoms,
no double-claim sites**; min confirmed amp 40.0/40.1/41.5/40.2, min margin 7.7/11.9/6.4/14.3 — gates
exactly honored). run10's max confirmed id is 598 (599 missing there — one lamp below the
cap in one burst). Every meta reads `exp=699.97 aem=continuous ev=-1` — the exposure is the SAME
across all four L steps; brightness walked 80→150 by the box, not by exposure. Wire confirms the
epochs: four `CWCSTATS mode:calE4` lines with `"L":80/100/120/150, "n":25, probeIters:0` (capture
10:25:59–10:26:59), no phone `decode:` blocks — console decode is the only decode, as with the
android campaign.

Plan context: HANDOFF-S14R §9 morning-analysis item (e) — union(L) at {80,100,120,150} is the
objective function the brightness-probe revision (S14R-0004) must maximize.

## Wire repair first (blocking)

8 of the 100 pulled jpgs were tail-truncated on disk (clean prefixes of their wire frames,
HANDOFF §5 species: daemon writer loses the tail of the last frame in a batch). The truncated
files decode with `LOAD_TRUNCATED_IMAGES` only down to the intact-macroblock line and corrupt
per-plane gain badly (plane k read as 2.0–4.1 vs true 0.62–0.81; "AE flare" was the first read,
wrong — it is truncation, and it tracks truncation size per FILE: run9 p22 −22 B → k 4.08,
run11 p21 −59 B → k 3.17, run8 p14 −35 B → k 3.52, while run8 p22 and run11 p04 (0 B over the
last macroblock) stayed near-true). All 8
were repaired from `runs/daemon/capture.txt` by the proven prefix-provenance re-join recipe
(marker `'] [PHONE] FJPEG '`, payload startswith disk bytes, unique group per label; +1.7 KB to
+16.1 KB restored; capture line refs in `repaired/repair_report.json`). The `runs/` originals are
untouched; repaired corpus (jpg+meta copies + 8 repaired files) lives under
`runs/daemon/analysis/e4_union/repaired/run8..run11/`. All analysis below is on the repaired
corpus. Post-repair label integrity: all 8 repaired frames carry their lab-led plane's duty
signature (D labeled 53.7–74.7, vs a 15.2–32.2 floor the same metric gave these same 8 frames
while still truncated; the initial "r8/r9 p22 mismatch" flag was an argmax TIE in the
low-resolution duty metric — D22 ≈ D23 exactly, same sign structure, not a label swap).

## Method and parity (bit-exact vs the shipped tool)

Driver `runs/daemon/analysis/e4_union/e4_union_decode.py` imports the CLI's own functions
(`register_direct`, `ncc_refine`, `hist_median`, `round_half_up`, gates/suppress constants;
12-of-24 bank default) and re-implements only the scoring loop; a subset-aware completion row
(missing planes imputed as background: per-codeword `(|miss| − 2·(12−on))·(M − mmed)`, active
only when a plane is dropped; in these decodes no plane was dropped, so scores are exactly
the CLI's). Gates: the promoted set mask-thr auto (S14R-0002 adaptive) / amp 40 / margin 6 /
`--fullres-rad 4` / n 600; SUPPRESS=7 code constant. Parity: the real
`tools/cwc_pos_decode.py` (subprocess, synthesized `<tag>_frames.txt`, saved json) reproduces
every run's id set, sites, amp, margin EXACTLY — 4/4 runs PASS with max Δ amp 0.0, Δ margin
0.0, Δ site 0 (`e4_union/parity_all.py`; single-run gate `parity_check.py`). The bank is the
frozen `tools/codewords_12of24.json` — regenerated nowhere.

Exact CLI line per run (equivalent decode proven identical):

```
PY=/home/nellie/.hermes/hermes-agent/venv/bin/python3
$PY tools/cwc_pos_decode.py <repaired_run_dir> --tag rN --n 600 --amp-gate 40 --margin-gate 6 \
   --mask-thr 100 --mask-adaptive 1 --mask-k 1.12 --mask-floor 45 --fullres-rad 4 --save-json
# mask-thr auto = the S14R-0002 adaptive rule min(100, max(45, 1.12·histMed)); on these dark
# views it relaxes to eff thr 45.0-55.4 (histMed 39.5-49.5) — mask-stage deaths ≈ 0 (below).
```

## union(L) — the objective table

Per-L single-burst confirmed /600 (amp med over confirmed; eff mask thr from the auto rule;
histMed = decode-master page-parity median; lamp-core P90 measured on that same master at
confirmed sites):

| run | L | confirmed /600 | % | amp med [range] | core P90 (clip frac) | histMed → eff thr |
|---|---|---|---|---|---|---|
| run8 | 80 | 412 | 68.7% | 70.0 [40.0–145.7] | 253 (3.6%) | 45.5 → 51.0 |
| run9 | **100** | **454** | **75.7%** | 82.7 [40.1–185.3] | 253 (4.6%) | 39.5 → 45.0 |
| run10 | 120 | 390 | 65.0% | 92.4 [41.5–187.5] | 253 (3.1%) | 46.5 → 52.1 |
| run11 | 150 | 411 | 68.5% | 70.9 [40.2–163.4] | 253 (5.8%) | 49.5 → 55.4 |

Unions by led id (the objective function values):

| pair | ∪ | triple | ∪ | |
|---|---|---|---|---|
| r8∪r9 | 458 | r8+r9+r10 | 477 | |
| r8∪r10 | 456 | r8+r9+r11 | 464 | |
| r8∪r11 | 443 | r9+r10+r11 | **480** | |
| r9∪r10 | **475** | **4-burst union** | **481** | |
| r9∪r11 | 462 | never-seen in ANY L | 119 | |
| r10∪r11 | 456 | | | |

Growth in running order (the order the battery actually ran): **412 → 458 (+46) → 477 (+19) →
481 (+4)**. The in-order cumulative IS the optimum order (best of 4! = same sequence); the best
single L is **L=100** (454, +42 over the next best), and L100's burst contributes 46 extra ids
over L80 — the largest single marginal step. The best triple (r9+r10+r11 = 480) excludes L80:
after L100, going BRIGHTER (120, 150) adds more unique ids (23) than going dimmer (L80 adds 1
new id beyond L100∪L120∪L150). Of the 27 ids rescued beyond the best single burst: 23 were
suppressed at L100 (argmax/site-window losses on colocated or interlaced neighbours, re-won
when the amp landscape shifts), 3 were gate-contest losses, 1 was wall-class; 17 of the 27
are L120-only (a suppression-dynamics window: same physical layout at every L, but the
per-L amp ordering changes which codeword takes the shared 7 px window).

## Failure axes per L (miss side, 119 no-anchor ids excluded; anchors = union-confirmed site coords, tripod-fixed rig, cross-run site identity measured med L1 0.0)

| L | misses | mask-stage | wall/amp-gate | suppressed | contest/other | no-anchor |
|---|---|---|---|---|---|---|
| 80 | 188 | 1 | 8 | 34 | 26 | 119 |
| 100 | 146 | 0 | 1 | 23 | 3 | 119 |
| 120 | 210 | 1 | 10 | 45 | 35 | 119 |
| 150 | 189 | 0 | 24 | 29 | 17 | 119 |

Read: at L=100 the miss side is dominated by the **no-anchor dark tail (119)** — ids no burst
confirms (hidden/colocated/interior wall spots, the exp500 campaign's analogous hard floor) —
plus 23 suppression losses and only 3 contest/other. Mask-stage ≈ 0 everywhere (dark-view mask
law not binding at exp 699.97: eff thr barely relaxes). Wall/amp-gate deaths RISE AT THE TOP of
the ladder (8 → 1 → 10 → 24): at L=150 the 255-wall headroom law k(255−wall) starts to fail
high-wall lamps (clip floor), the inverse of the low-L regime; contest/suppression losses peak
at L=120 (80/210 = 38%). The wall/headroom law's fitted per-plane k medians sit ~1.7 here
(r8 1.66, r9 1.77, r10 1.77, r11 1.67 on confirmed ids — much higher than the 0.54–0.79 at
exp 500 because exp 700 brightens everything).

## Knee-band hypothesis at LOW L: NOT SUPPORTED at 80–150

The knee-band rule (S14R-0002) chose L by driving lamp-core P90 into 235–250 (below clip)
with ≤5% clip. At THIS exposure (exp 699.97, ev −1, Android) the core P90 is **253 at every
L in 80–150** (master measured at confirmed sites; clip frac 3.1–5.8%) — the core saturates
before the ladder even starts, so the band cannot be entered and L does not move it.
Confirmed counts instead track **amp/suppression dynamics**, not core brightness: L100
(P90 253) out-decodes L120/L150 (also P90 253) by +64/+43, and gate-honoring min amps sit
at exactly 40 in every burst. The knee signal the 0002 probe saw at exp 500 (probeSteps P90
237.4, probeIters 1) is an exp-500-regime signature; at exp 700 the E4 CWCSTATS lines show
`probeIters:0, steps:[]` at every L — the probe (as built) rejects all steps because
nothing lands in the band. The knee-band objective, as specified, cannot select among
80–150 on this camera: the discriminator is off.

## Probe objective recommendation (S14R-0004)

Drop the knee-band selection for the Android path at exp≈700 — it cannot discriminate below
its own clip ceiling, and the E4 telemetry agrees: CWCSTATS shows `probeIters:0, steps:[]`
at every L (probe iterations all rejected; nothing entered the 235–250 band), while the
on-master core P90 sits at 253 with clip 3.1–5.8%. Two candidate replacements, in order of
preference: (a) count-based: fix L=100 (the E4 argmax, 454); if the schedule allows a second
epoch, L=120 is the best complement (17 L120-only ids; +27 total beyond L100 for three more
bursts). (b) brightness-only surrogate if the probe must stay open-loop: maximize median
predicted amp k·(255−wall) at masked sites subject to clip ≤ 5% — here that selects
L≈100–120 (amp med peaks 92.4 at L120 while wall/amp-gate deaths stay ≤ 10). In both cases
the knee/threshold constants must be re-derived per exposure regime — ev −1 + exp 699.97
saturates the lamp core at every L in 80–150, so the exp-500 knee numbers do not transfer;
the probe should lock exposure first and sweep L second. Union(L) itself is maximized by the
full 4-L ladder (481), so keep E4's ladder for battery diagnostics; the PROBE (single-burst,
real-time) operating point should be L=100.

## Files (all under runs/daemon/analysis/e4_union/, no images, no git)

- `e4_union_decode.py` — driver (imports CLI internals; subset-capable scorer; parity-exact)
- `parity_check.py` / `parity_all.py` — one-run and 4-run bit-exact parity gates vs the CLI
- `wire_repair.py` + `repaired/run8..run11/` + `repaired/repair_report.json` — wire repair
- `run8..run11_ledpos.json`, `e4_union_raw.json` — per-L confirmed site lists (ledpos format)
- `union_analysis.py`, `union_summary.json` — union table, growth, failure axes
- `knee_stats.py` + `knee_stats.json`; `label_audit.json`, `post_repair_label_audit.json`,
  `divergence_anatomy.py`, `final_stats.py`, `decode*.log` — support evidence

Hygiene: runs/ read-only (repairs went to analysis/e4_union/repaired/), no images to git, no
git commands, bench_daemon untouched, /dev/ttyACM0 never opened, frozen bank used as-is.
Caveat: 8/100 frames shipped truncated and were wire-repaired; if the daemon's writer loses
tails this systematically, E4 re-runs should ship the capture-side repair or raise the ship
batch pacing (§9's shipBatch note) — the decode conclusions above are robust to it (parity was
run on the repaired corpus) but raw-corpus parity would fail blindly.