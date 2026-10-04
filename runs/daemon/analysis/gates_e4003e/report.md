# Gate-sensitivity sweep — S14R-0003E E4 battery (tonight, 04 Oct, exp 699.97)

Corpus `runs/daemon/runs/s14r0003e-r{2,3,4,5}` (r2=L80, r3=L100, r4=L120,
r5=L150; 24 planes + master each; 100/100 disk frames EOI-verified complete —
no truncation repair needed tonight, unlike the 03 Oct battery). All 4 metas
`exp=699.97 aem=continuous ev=-1`; CWCSTATS histMed 40.5/50.5/55.5/54.5
matches the CLI master histMedian exactly (4/4).

**Method & parity (independent of agent 1's run):** gates/suppress constants
read from `tools/cwc_pos_decode.py`; the candidate-selection loop is a
verbatim copy of the CLI's scoring block; my baseline reproduces agent 1's
`decode_e4003e` ledpos **EXACTLY** — 4/4 epochs, every id/site/amp/margin
identical (423/383/385/398). Score arrays bit-equal to the staged `_best`
npys. Position guard discipline (HANDOFF-S14R §0) applied to every
relaxed-gate recovery: anchor (≤6 px of the id's baseline union position),
interpolated id-kNN anchor (never-confirmed ids, 1/dist, ≤12 same-string
neighbours), or ≤5 px of a confirmed claim. Impostor proxy = id claims a
site <6 px from a STRONGER confirmed claim of a different id.

## 1. Per-epoch gate sweep (baseline = promoted set: mask-auto / amp 40 / margin 6 / sup 7 px)

All counts raw; `guard` = new ids passing the §0 audit; `imp` = impostor
proxy; `orph` = orphans (>6 px from anchor+interp, >5 px claim).
New ids = not in THIS epoch's baseline.

### r2 (L=80) — baseline 423/600, amp med 68.5
| combo | confirmed | new raw | guard | imp | orph | lost |
|---|---|---|---|---|---|---|
| amp 30 | 460 | +37 | 24 | 22 | 13 | 0 |
| amp 35 | 442 | +19 | 14 | 9 | 5 | 0 |
| amp 40 (base) | 423 | — | — | — | — | — |
| amp 45 | 399 | −24 | | | | |
| amp 50 | 357 | −66 | | | | |
| margin 4 / 5 / 8 | 423 / 423 / 423 | 0 | | | | |
| supp 3 px | 484 | +62 | 61 | 59 | 1 | 1 |
| supp 5 px | 473 | +51 | 50 | 48 | 1 | 1 |
| mask legacy 100 | 422 | −1 | | | | |

### r3 (L=100) — baseline 383/600, amp med 64.3
| combo | confirmed | new raw | guard | imp | orph | lost |
|---|---|---|---|---|---|---|
| amp 30 | 435 | +52 | 41 | 27 | 11 | 0 |
| amp 35 | 414 | +31 | 24 | 14 | 7 | 0 |
| amp 40 (base) | 383 | — | — | — | — | — |
| amp 45 | 339 | −44 | | | | |
| amp 50 | 293 | −90 | | | | |
| margin 4 / 5 / 8 | 383 / 383 / 383 | 0 | | | | |
| supp 3 px | 439 | +57 | 57 | 55 | 0 | 1 |
| supp 5 px | 429 | +48 | 48 | 47 | 0 | 2 |
| mask legacy 100 | 382 | −1 | | | | |

### r4 (L=120) — baseline 385/600, amp med 65.8
| combo | confirmed | new raw | guard | imp | orph | lost |
|---|---|---|---|---|---|---|
| amp 30 | 439 | +54 | 45 | 36 | 9 | 0 |
| amp 35 | 414 | +29 | 24 | 21 | 5 | 0 |
| amp 40 (base) | 385 | — | — | — | — | — |
| amp 45 | 344 | −41 | | | | |
| amp 50 | 294 | −91 | | | | |
| margin 4 / 5 / 8 | 385 / 385 / 384 | 0 / 0 / −1 | | | | |
| supp 3 px | 434 | +50 | 50 | 47 | 0 | 1 |
| supp 5 px | 427 | +43 | 43 | 42 | 0 | 1 |
| mask legacy 100 | 383 | −2 | | | | |

### r5 (L=150) — baseline 398/600, amp med 68.2
| combo | confirmed | new raw | guard | imp | orph | lost |
|---|---|---|---|---|---|---|
| amp 30 | 443 | +45 | 34 | 23 | 11 | 0 |
| amp 35 | 420 | +22 | 16 | 12 | 6 | 0 |
| amp 40 (base) | 398 | — | — | — | — | — |
| amp 45 | 351 | −47 | | | | |
| amp 50 | 312 | −86 | | | | |
| margin 4 / 5 / 8 | 398 / 398 / 398 | 0 | | | | |
| supp 3 px | 448 | +51 | 51 | 50 | 0 | 1 |
| supp 5 px | 442 | +45 | 45 | 45 | 0 | 1 |
| mask legacy 100 | 397 | −1 | | | | |

Read: amp relaxation keeps every baseline id (lost 0 in all amp drops) and
its GUARDED net is +24/+41/+45/+34 at amp30 — but 22/27/36/23 of those are
impostor-proxy (site <6 px from a stronger claim), reproducing the 0000
audit's relaxation-created phantoms. amp35 halves the impostor load
(+14/+24/+24/+16 guarded, 9/14/21/12 imp). Margin is INERT (no id tonight
sits in margin [4,8) — zero headroom). supp 7→3 px is the huge RAW lever
(+61/+57/+50/+51 guarded) but ~90% are impostor-proxy: they are rival
claims of codewords already confirmed at their own site re-entering under a
removed window — a relabel storm, not new lamps. Mask legacy 100 vs auto:
−1/−1/−2/−1 (auto correct at these histMeds; the adaptive rule only
relaxes, and no epoch saturates).

## 2. Conflict audit — rival claims the CLI eats (page-parity redesign population)

Gate-passing rival claims sitting inside a final claim's 7 px window
(baseline, per epoch) + per-id own-best refinement:

| epoch | eaten gate-passing claims | unique eaten ids | own codeword already confirmed (2nd-site) | TRUE recoveries (own-best passes both gates) | of those guarded (§0) | orphans |
|---|---|---|---|---|---|---|
| r2 (L80) | 868 | 243 | 57/73 | **17** | 14 | 3 |
| r3 (L100) | 674 | 197 | 55/69 | **19** | 17 | 2 |
| r4 (L120) | 710 | 208 | 57/69 | **18** | 13 | 5 |
| r5 (L150) | 797 | 227 | 55/66 | **20** | 13 | 7 |

Page-rule banded (baseline): cross-string (page would confirm BOTH)
455/319/342/371 claims — the dominant class; page-conflict keep-both
(<6 px same-string, |dId|>5) 399/346/351/412; page-suppresses-too (≤1.5 px
same-str) 14/9/17/14. KEY: every true-recovery id's own-best site sits at a
FREE pixel (0 inside a rival's window) — the conflict redesign (keep both,
flag conflict) recovers them exactly; they are NOT windowed, they are
argmax/eaten codewords whose best evidence is free of any window. The CLI
eats them at candidate stage; the page's per-pixel conflict list would
confirm them.

## 3. Never-confirmed loss census (157 ids missed in ALL 4 epochs)

First-gate classification at the id's own-best in-mask pixel (my re-derivation;
agent-1's census classed the same ids by their argmax pixel — consistent):

| epoch | photometric (own amp 25–40) | contest-structure (≥40 or rival-owned) | never own-pass (guarded) |
|---|---|---|---|
| r2 (L80) 85+? see bands: **85** in 25-40 / 72 ≥40 | | 69 contest + 83 amp-gate classed → 5 pass | 5 (4) |
| r3 (L100) **141** in 25–40 / 15 ≥40 | 13 contest | | 3 (2) |
| r4 (L120) **127** in 25–40 / 30 ≥40 | 28 contest | | 2 (1) |
| r5 (L150) 48 in 25–40 / 109 ≥40 (110 contest) | | | 1 (1) |

The never-confirmed amp-band signature per epoch: L100 (r3) = 141/157 in the
25–40 amp band (photometric, exposure-recoverable class); L150 (r5) = 110
contest-dominant; L120 mixed; L80 splits 85/72. Best PHOTOMETRIC loss profile
= **L100** (nearly all its never-missing are the amp 25–40 exposure band,
minimal contest-structure). Best single-burst identity count stays L80 (423).

Never ids that pass own-best gates in an epoch (real single-epoch recov-
erable, all guarded): r2 ids 149 182 418 578 579; r3 182 418 579; r4 418 579;
r5 418. Union across epochs: 5 ids (149 182 418 578 579).

## 4. Guard-verified unions (baseline sets)

pairs: r2+r5 437 (best); triples r2+r3+r5 442; union4 443. Never-seen 157.
(Compare agent-1 identical numbers 437/442/443 — union sets are anchor-native.)

## 5. Recommended gate set (single-burst product target)

| knob | verdict |
|---|---|
| mask | **auto** (S14R-0002 adaptive rule) — legacy 100 loses 1–2/epoch at these histMeds; auto correct everywhere tonight |
| amp | **40** (do NOT relax) — amp 30's guarded net (+24..+45) is overwhelmed by 22–36 impostor-proxies + 9–13 orphans; amp 35 only if a second-pass confirmation layer is added (+14..+24 net, 9–21 imp) |
| margin | **6** (inert) — zero ids in [4,8); relaxation buys nothing |
| suppress | **7 px CLI window + page-parity conflict LIST** — window relaxation alone = relabel storm (90% impostor-proxy); the real population is eaten rival codewords, worth **+14..+17 guarded/epoch** via the conflict redesign |

## 6. Ranked opportunities (measured populations)

1. **CLI conflict/ownership redesign** (page-parity: keep ≤7 px rival claims,
   flag conflicts, confirm both; own-best gate-pass at a FREE pixel):
   674–868 claims, 197–243 unique ids, **17/19/18/20 guarded true recoveries
   per epoch** (2–7 orphans). Low risk — no relaxation involved.
2. **Photometric headroom, L-follower** (the never-confirmed amp 25–40 band,
   141/127 ids at L100/L120): recoverable only with more headroom
   (exposure/brightness), not by gate movement.
3. **amp 35** (+14/+24/+24/+16 guarded, 9/14/21/12 impostors) — moderate.
4. **amp 30** (+24/+41/+45/+34 guarded, 22/27/36/23 impostors) — high.
5. **supp window 3 px** (raw counts big) — 90%+ impostor-proxies; do not pay
   standalone; its real value is inside the conflict-list redesign (rank 1).
6. **margin / mask** — inert knobs (0 and ≤2 deltas), keep as-is.

For the 0004 probe revision: the probe should stay at **L=100** for
photometric headroom (amp 25–40 never-band minimal) but the OPERATING POINT
for a single burst tonight, count-wise, is L=80 (423) and the count-wise
lever is the conflict redesign, not a gate change.

## Executive summary (for the operator)

1. Gate sweep on tonight's 4 E4 epochs (L80/100/120/150, exp 699.97) with the
   §0 position-guard discipline on every recovery.
2. My baseline reproduces agent-1's decode EXACTLY (423/383/385/398) — no
   parity gap; masks/amp/margin/suppress constants read from the CLI.
3. **margin gate is dead** (no id in [4,8) — 0 gain either direction).
4. **mask auto is correct** (legacy 100 loses 1–2/epoch at these histMeds).
5. **amp 40→30/35**: guarded net +24..+45 / +14..+24, but 22–36 / 9–21 of each
   are impostor-proxies — do NOT relax for identity-grade output.
6. **supp 7→3 px**: raw +50..+62/epoch but ~90% are impostor-proxies (relabel
   storm of codewords already confirmed) — only pay inside a conflict LIST
   redesign.
7. **Conflict redesign (page-parity) is the real lever**: 674–868 gate-passing
   rival claims eaten per epoch, 197–243 unique ids; **17–20 TRUE recoveries
   per epoch** (own-best amp+margin pass at a free pixel, guard-verified 13–17).
8. Never-confirmed (157 ids): best photometric profile is L100 (141/157 sit in
   the amp 25–40 band = exposure-recoverable); L80/L150 have contest-structure
   (69/110) instead.
9. Recommended single-burst set: mask auto / amp 40 / margin 6 / sup 7 + the
   conflict-list redesign (expected 383→400 at L100, 423→437 at L80).
10. Guard discipline holds: unguarded relaxation would inflate counts 22–62
    per epoch (~30–60% phantoms, tracking the 17h43 audit).

Files: `sweep_phase1.py` (build+baseline+parity), `sweep_phase2.py` (gate
sweeps + conflict audit), `sweep_phase3.py` (own-best conflict refinement +
guard-verified unions), `sweep_phase3b.py` (never-confirmed census), 
`sweep_phase4_summary.py` (assembly), `baseline.json`, `sweep_raw.json`,
`phase3_eaten_refinement.json`, `phase3b_never_census.json`,
`_never_amp_bands.json`, `summary.json`. runs/ untouched elsewhere; no
images to git; no commits; no serial/daemon interaction.