# REVIEW — runs/s14p-1911-handheld-r4 (30 Sep, console recovery + decode)

## Frame-ship recovery (FACT, pull log + cwc_frames.txt)

- Operator handheld r4 burst t=36.7→42.1 s (5.47 s span; exp pinned).
- 19:53 pull #1 (BRAMP only): 0 bytes — arm dead post-power-cycle.
- 20:07 pull #2: `[DRV] BRAMP queued` + bare `[PHONE-LOG] end` = empty ring.
- 20:14 pull #3 with LOGA restored: ack `[LOGA] persistent arm ON`,
  ship completed in 90 s — 19 FRAME headers / 19 FJPEG groups / 19 FENDs,
  all labels unique (`cwc:r1:p00..p17` + `cwc:r1:master`). 0-byte cwc_frames.txt
  is pull #1's artifact; pull #2's was overwritten by the full ship (no
  data lost — pull #2 had none).
- CWCSTATS line: build **S14P-1911**, n 18, comp 0, testMode 0.

## Counts (FACT)

| decode | amp/margin gates | count | missing |
|---|---|---|---|
| Page (in-phone, CWCSTATS `decode`) | 60 / **25** (page CFG sticky — 1911 shipped CFG says 10, page kept 25) | **187**/200 | n/a (page doesn't ship list) |
| Console `cwc_pos_decode.py` run #1 | 60 / **10** | **191**/200 | [5,16,22,46,67,90,91,109,114] |
| Console probe run #2 | 60 / **25** | **188**/200 | [5,16,22,25,27,28,46,67,90,91,109,114] |

- The 187 on-phone count matches CWCSTATS `confirmed: 187` exactly —
  Oliver's view was the page's own verdict.
- Gate deltas @25→10 add LED27 (margin 10.9) + LED25 (10.9) + LED28
  (margin 15.0) — all three @25 absent / @10 present (ledpos.json
  verified). CORRECTED 20:2x: an earlier version of this file said
  LED28 'present @25 then dropped at 10' — wrong; @25 loses 25/27/28
  all three. The @25/@10 gap is therefore +3 LEDs, and 188→187 (not
  188) is the console-vs-page residual at matched gates 60/25.
- LED16 = previously-verified real LED; LED5/109/114 = near-gate
  candidates; 46/90/91 = the standing hidden trio (operator-confirmed
  physically not visible); 67/22 = to check.

## Dedup anatomy (FACT, probe print)

- @60/10: 669 pre-dedup sites over 191 codewords; 129 codewords claimed
  >1 site (max 14 sites for codeword 1) → strongest-site-per-codeword
  dedup keeps one each. Same anatomy @60/25 (519/188/103), so the
  bloom-skirt ghost field is large but the dedup handles it; post-dedup
  zero duplicate claims in ledpos.json (verified).
- amp: med 155.4, min 82.5, max 260.1; margin med 71.2, min 10.9.

## Motion / drift (FACT from CWCSTATS + direct_shifts.json; interpretation INFERENCE)

- Page chain (CWCSTATS): Σdx −146.02, Σdy −29.99; max|dx| 12.77,
  max|dy| 3.09; mean|dx| 8.11, mean|dy| 1.67; conf 0.767–0.885
  (med 0.820). Console direct: Σdx −145.51, Σdy −29.47; max|dx| 12.76,
  max|dy| 3.17; refined x 16/18, y 11/18 (page-vs-console dev ≤~0.5 px).
- Median plane offset ≈ (−8.4, −1.9) → net drift vs median ≈ (+5.8,
  +1.7), magnitude ≈ **6.1 px** — well inside the §4 ~15 px budget,
  ~5× tighter than 1908's ~30 px pan. INFERENCE: operator discipline
  held; the drift-trail shape is a smooth x-travel (plane dx slides
  −12.4→−0.02 across the burst, dy ≈ −3→−1).
- Guard config shipped as cwcGuardConf 0.90 / cwcGuardRem 5
  (survey.html:118,121 — VERIFIED live). r4 therefore WOULD flag: with
  conf checked FIRST (else-if at :786) and every plane's direct-conf
  0.767–0.885 below 0.90, badConf = 18 → 'MOTION FLAGGED, unreliable'
  caption — CORRECTED 20:2x: an earlier version of this file said
  'would NOT flag' after re-deriving thresholds from the r3-era rem>3 /
  conf<0.75 read; the shipped thresholds are 0.90/5.0, not that read.
  (No caption screenshot exists to prove it fired on-screen — the
  inference is from code + CWCSTATS conf data. If your run showed a
  green caption, this prediction was wrong and worth a page-log LOGP
  check.)

## Serpentine sanity (FACT from ledpos.json gap stats)

- Consecutive-id pitch: median 18.4 px, min 5.0, max 262.1.
- Gaps >5× median: (111→112, 144.0), (113→115, 223.2), (148→149,
  262.1), (149→150, 227.1), (173→174, 101.0).
- Gaps >10× median: 113→115, 148→149, 149→150.
- Longest consecutive run 103 ids; 150 runs ≥10. **Serpentine holds
  except the flagged pairs** — reads as route fold-backs/occlusions
  (148–150 gap is the largest); not a global identity failure. The
  113→115 gap pairs with LED114 itself missing — worth a crop check.

## Anomalies (FACT unless noted)

| what | reading | verdict |
|---|---|---|
| pull #1 19:53 0 bytes | arm dead post-reboot (LOGA-on top = no ack) | fixed by pull #3 |
| pull #2 20:07 empty ring | frame-store empty OR a race consumer | resolved by pull #3 full ship |
| page marginGate 25 sticky | page CFG kept 25 vs 1911-built default 10 | count difference is REAL and gate-borne |
| LED28 | recovered at margin 15.0 @10 (absent @25) | @25→10 gains it; watch next run |
| page 187 vs console 188 @matched 60/25 | one-claim decode residual (argmax/owner class) | known residual class, not a defect |

## Files

- runs/s14p-1911-handheld-r4/: pull_handheld.py (LOGA+BRAMP, logend-after-
  frames), cwc_frames.txt (1118 lines, 19 frames), ledpos.json (60/10),
  ledpos_m25.json (60/25 probe), direct_shifts.json, this file.
- No tracked-file edits; no push.