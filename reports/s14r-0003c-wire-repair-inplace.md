# s14r-0003c — in-place wire repair of truncated run jpgs (run0, run8–run11)

Date: 2026-10-04 · Author: Hermes repair agent · Capture fingerprint: sha256 `0fb02fed76f5c071…` (56,455,442 B, read-only, untouched)

## What was broken

33 of the 528 jpgs in `runs/daemon/runs/{run0,run8,run9,run10,run11}` were
byte-divergent from the wire: their disk bytes are a strict prefix of the
frame group that the wire capture holds complete, i.e. they were written
partially and never rewritten:

| run  | broken files | files |
|------|--------------|-------|
| run0 | 13 × cal_E1 (L10 ×4, L20 ×2, L5 ×2, L60, L80 ×2, L150, L179), 11 × cal_E2 (L10, L20, L40, L5, L60, L80, L100 ×2, L120, L179 ×2), 1 × cal_idle_49 | 25 |
| run8 | cwc_r8_p14, cwc_r8_p22 | 2 |
| run9 | cwc_r9_p22 | 1 |
| run10| cwc_r10_p05, cwc_r10_p13, cwc_r10_p21 | 3 |
| run11| cwc_r11_p04, cwc_r11_p21 | 2 |
| **total** | | **33** (24 run0 E1/E2 + 1 run0 idle + 8 run8–11 cwc) |

Newly found relative to the e4_union handoff: `cal_idle_49` was also
truncated (24,975 → 26,619 B) — the known-issue list covered only the
13 E1 / 11 E2 / 8 cwc frames.

## Root cause (one paragraph)

`bench_daemon.py` drains the phone's frame stream on a 30 s tick. A frame
group (`[PHONE] FRAME` json → several `[PHONE] FJPEG <b64>` lines →
`[PHONE] FEND`) that was still mid-flight when the tick fired was written to
disk partially: the truncated on-disk sizes are clean multiples of ~1350 B —
the b64 decoded size of one FJPEG line — plus one partial line, i.e. "whole
chunks plus whatever of the current line had arrived". The daemon's
skip-if-exists dedup then treated the partial file as already-delivered on
every later tick, so the missing tail (1.6–17.4 KB per file, 315,929 B total)
was never rewritten — the damage was locked in. The wire log holds the
complete groups because the phone↔daemon wire bytes were logged intact.

## Method

1. Parsed **all** FRAME/FJPEG/FEND groups in `capture.txt` (67,077 lines,
   1472 FRAME / 1469 FEND markers): **888 complete groups for 528 labels**
   were used (3 incomplete FEND-less groups dropped: `capture.txt:2024`
   `cwc:r2:p01` 08:43:49 — non-target run2; `capture.txt:40592`
   `cal:idle:10` 08:49:33 and `capture.txt:45865` `cal:idle:08` 09:38:21 —
   cut mid-b64 by `[PHONE-LOG] end` during morning/pre-leg re-pushes; complete
   groups for those labels exist and the disk idle_08/10 files are
   byte-identical to them, so no repair was needed from the cut groups).
2. For every one of the 528 jpgs: sha256-compared disk bytes against every
   complete wire group with the meta's label. **healthy ⇔ exact byte
   equality with some complete group** (or trivially, disk == candidate).
3. On mismatch: repaired **in place** only when the disk bytes were a
   strict prefix of **exactly one** wire group for the label
   (prefix-provenance; every one of the 33 files had exactly one such
   candidate). All 33 ambiguously-labelled idle files were checked byte-wise —
   none needed guessing.
4. Meta hygiene: disk `meta.json` compared field-by-field (label, t, W, H,
   exp) against the wire group's FRAME json — all 528 match.
5. Final EOI sweep over all 528 files: every file ends `\xff\xd9`, opens with
   a **plain PIL decode (no `LOAD_TRUNCATED_IMAGES`)**, and W/H equal meta.

Idle-label ambiguity (morning ghost attempts 08:46–09:08, the interrupted
leg-1 09:53:58–09:54:55 and leg-2 09:54:55–09:58:43 all used `cal:idle:NN`):
resolved strictly by byte comparison, never by label. All 60 idle disk files
matched some complete wire group exactly, so no prefix-disambiguation was
needed; for idle_00–28 the file matched multiple wire groups byte-for-byte
(identical t-stamps across replays prove the morning/ghost pushes were
regenerated replays, not distinct captures — see per-file provenance in
`analysis/run_repair/idle_provenance.json`).

## Repair table (33 rows; +bytes = bytes restored from wire)

| file | label | old→new B | +B | wire ref | stamp | t |
|---|---|---|---|---|---|---|
| cal_E1_L10_4 | cal:E1:L10:4 | 21600→26795 | +5195 | capture.txt:49407 | 10:00:27 | 415.983 |
| cal_E1_L10_5 | cal:E1:L10:5 | 18225→26827 | +8602 | capture.txt:49449 | 10:00:32 | 416.486 |
| cal_E1_L10_11 | cal:E1:L10:11 | 10800→26757 | +15957 | capture.txt:49703 | 10:00:58 | 450.365 |
| cal_E1_L10_18 | cal:E1:L10:18 | 19575→26764 | +7189 | capture.txt:49998 | 10:01:27 | 469.958 |
| cal_E1_L20_6 | cal:E1:L20:6 | 16200→22976 | +6776 | capture.txt:50227 | 10:01:57 | 506.984 |
| cal_E1_L20_14 | cal:E1:L20:14 | 13500→22388 | +8888 | capture.txt:50522 | 10:02:28 | 537.299 |
| cal_E1_L5_3 | cal:E1:L5:3 | 19575→27205 | +7630 | capture.txt:48582 | 09:58:57 | 322.435 |
| cal_E1_L5_10 | cal:E1:L5:10 | 10125→26966 | +16841 | capture.txt:48883 | 09:59:28 | 357.377 |
| cal_E1_L60_14 | cal:E1:L60:14 | 15525→24230 | +8705 | capture.txt:51880 | 10:04:57 | 686.772 |
| cal_E1_L80_10 | cal:E1:L80:10 | 21600→24727 | +3127 | capture.txt:52425 | 10:05:57 | 740.120 |
| cal_E1_L80_18 | cal:E1:L80:18 | 18225→24752 | +6527 | capture.txt:52739 | 10:06:27 | 770.804 |
| cal_E1_L150_10 | cal:E1:L150:10 | 8100→25456 | +17356 | capture.txt:54593 | 10:09:58 | 987.788 |
| cal_E1_L179_13 | cal:E1:L179:13 | 9450→25490 | +16040 | capture.txt:55439 | 10:11:28 | 1074.595 |
| cal_E2_L5_14 | cal:E2:L5:14 | 14850→27239 | +12389 | capture.txt:56240 | 10:12:58 | 1165.953 |
| cal_E2_L10_8 | cal:E2:L10:8 | 18225→27603 | +9378 | capture.txt:56761 | 10:13:57 | 1226.264 |
| cal_E2_L20_2 | cal:E2:L20:2 | 16875→27538 | +10663 | capture.txt:57282 | 10:14:56 | 1285.428 |
| cal_E2_L40_11 | cal:E2:L40:11 | 15525→22962 | +7437 | capture.txt:58388 | 10:16:57 | 1408.964 |
| cal_E2_L60_8 | cal:E2:L60:8 | 12150→23105 | +10955 | capture.txt:58946 | 10:17:58 | 1461.984 |
| cal_E2_L80_4 | cal:E2:L80:4 | 8100→23464 | +15364 | capture.txt:59469 | 10:18:59 | 1528.105 |
| cal_E2_L100_8 | cal:E2:L100:8 | 17550→24017 | +6467 | capture.txt:60296 | 10:20:27 | 1611.297 |
| cal_E2_L100_16 | cal:E2:L100:16 | 9450→23975 | +14525 | capture.txt:60602 | 10:20:58 | 1642.341 |
| cal_E2_L120_11 | cal:E2:L120:11 | 14850→24109 | +9259 | capture.txt:61101 | 10:21:58 | 1709.829 |
| cal_E2_L179_10 | cal:E2:L179:10 | 16200→25068 | +8868 | capture.txt:62477 | 10:24:28 | 1850.953 |
| cal_E2_L179_17 | cal:E2:L179:17 | 18225→25023 | +6798 | capture.txt:62759 | 10:24:57 | 1883.557 |
| cal_idle_49 | cal:idle:49 | 24975→26619 | +1644 | capture.txt:48027 | 09:57:56 | 260.951 |
| cwc_r8_p14 | cwc:r8:p14 | 11475→23890 | +12415 | capture.txt:63710 | 10:27:58 | 1949.486 |
| cwc_r8_p22 | cwc:r8:p22 | 22275→23990 | +1715 | capture.txt:64014 | 10:28:27 | 1951.826 |
| cwc_r9_p22 | cwc:r9:p22 | 9450→24693 | +15243 | capture.txt:64991 | 10:29:58 | 1972.341 |
| cwc_r10_p05 | cwc:r10:p05 | 12150→25459 | +13309 | capture.txt:65308 | 10:30:28 | 1987.634 |
| cwc_r10_p13 | cwc:r10:p13 | 18900→24858 | +5958 | capture.txt:65621 | 10:30:57 | 1989.709 |
| cwc_r10_p21 | cwc:r10:p21 | 18900→25568 | +6668 | capture.txt:65933 | 10:31:27 | 1992.038 |
| cwc_r11_p04 | cwc:r11:p04 | 22950→24863 | +1913 | capture.txt:66251 | 10:31:57 | 2007.478 |
| cwc_r11_p21 | cwc:r11:p21 | 8775→24903 | +16128 | capture.txt:66915 | 10:32:58 | 2012.117 |

## Verification numbers

- 528 / 528 files processed: **495 healthy** (byte-identical to wire) + **33
  repaired in place** + **0 unresolvable**.
- Per dir: run0 403H/25R/0U (428), run8 23H/2R/0U (25), run9 24H/1R/0U (25),
  run10 22H/3R/0U (25), run11 23H/2R/0U (25).
- EOI sweep: 528/528 end with `FFD9`; 528/528 plain-PIL decode (no
  LOAD_TRUNCATED_IMAGES); 528/528 W/H == meta; 528/528 disk meta == wire meta.
- Post-write re-read sha256 == payload sha256 for all 33 (atomic write +
  assert).
- Repair sizes: +1,644 … +17,356 B (total 315,929 B restored). All disk
  bytes were a strict prefix of the single matching wire group.
- `capture.txt` untouched (read-only pass, sha256 recorded above); the
  provenance copies in `analysis/e4_union/repaired/` untouched; no jpg/png
  staged for git (standing rule).

## Implications

- Analysis can now use **all 528 jpgs in the five run dirs**, including the
  **24 previously-excluded E1/E2 ladder frames** (13 E1 + 11 E2) which were
  dropped from photometry fits as truncated.
- The **8 cwc frames in run8–11** that `analysis/e4_union/wire_repair.py`
  had reconstructed into copies under `analysis/e4_union/repaired/` can now
  be **re-derived from the run dirs directly** — the in-sequence files are
  complete and byte-identical to what the copies hold.
- The idle ladder (60 frames) is fully byte-provenanced against the wire;
  the E1/E2/CWC labels are unique on the wire (earlier attempts died in E5),
  so no cross-session contamination is possible for them.
- Remaining caution: `bench_daemon.py` still has the 30 s tick + skip-if-
  exists pattern (daemon left running, untouched); any future capture can
  re-create the same tail-truncation until the writer defers incomplete
  groups to a later tick.

## Manifest

Per-file verdicts (`healthy|repaired|unresolvable`), sizes, shas before/after,
wire-group refs and stamps: `runs/daemon/analysis/run_repair/manifest.json`
(capture sha256 + parse stats embedded; `capture_groups.json` holds the full
wire-group index; `idle_provenance.json` the per-idle-file wire provenance).