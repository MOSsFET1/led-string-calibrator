# Console decode + gate sweep — S14P-1927 console round, 02 Oct afternoon

## Setup

CFG (CMD 12:31:58, latched): `cwc=1 cwcN=400 nStr=2 nPerStr=200 suppress=1
maskThr=100 ampGate=40 marginGate=6`. Six 19-frame rounds (master + 18 planes)
captured by the daemon 12:40:26–12:45:10 from the box-perspective phone; the
box is ~3 m off-axis with 9.5–16 px/LED projection. cwcN=400 matches the
connected strings (2 × 200) — the S14P-1927 phantom-cousin failure mode
(morning 11:06 round, cwcN=1600, 646 confirmed = 392 real + 254 relabels) does
not apply at N=400.

## Frame recovery before decode

`bench_daemon.py`'s writer lost 9 of the 95 run2–run6 image files, but every
line the phone sent is in `runs/daemon/capture.txt` (ends cleanly
`[PHONE-LOG] end` 12:45:10; daemon died 12:58 on TLS errors, never recovered):

- 7 truncated (writer killed mid-write while the phone kept streaming):
  r2 p06/p17, r4 p00/p11, r5 p03, r6 p15 (+r2 p01, which was the 08:43
  morning re-pull tail already truncated on disk)
- 2 blocked: r2 p00/p01 still held morning 08:43 bytes (same-label re-pull
  skipped by the decoder, writer raced) — morning bytes verified intact and
  moved to `runs/run2/morning_0843_backup/`
- 5 morning-afternoon collision tail frames (12:40:26–37, labels `cwc:r1:*`)
  could not go to run1 (committed S14P-1922 reference, untouched) → new dir
  `runs/run1af/` (jpg+meta only, not a decodable run)

Recovery: parsed capture.txt FRAME/FJPEG-chunks/FEND groups in the 12:40–12:45
host window, base64-joined the ~920-char chunks, byte-compared every jpg.
All 95 files re-verified: SOI/EOI correct, PIL loads (406×720), bytes exactly
the capture decode. Backups of the replaced bytes in
`runs/truncated_backup/`. Per-run `cwc_frames.txt` packs written for the
console decoder. No CWCSTATS/CWCDEC line exists for this burst anywhere in
the capture (phone decode stats never shipped before the daemon died) — the
console numbers below are the only decode ground truth for today.

## Console baseline (N=400, mask 100 / amp 40 / margin 6 / fullres rad 4 / SUPPRESS 7)

| run | confirmed /400 | note |
|-----|---------------|------|
| r2  | 354 | |
| r3  | 309 | |
| r4  | 358 | |
| r5  | 389 | |
| r6  | 300 | |
| all | 1710 /2000 | 85.5% |

Amp (per-plane mean): med 77–100, min at gate ≈40–43.5. Per-run JSON +
overlays: `runs/runN/ledpos.json`, `runs/runN/led_overlay.png`
(`--save-json --save-shifts` also wrote `direct_shifts.json`).

## Gate sweep (90 combos × 5 runs, N=400)

Registration built once per (fullres, run) via the parity-checked driver
(`/tmp/rig2_sweep/sweep_driver.py` build_score/gate_combo; exact 5-tuple
match vs the CLI on run2 at n=400: 354 sites identical). Grid: mask
{100,125,150,175,200} × amp {40,60,80} × margin {6,10,14} × fullres {off, rad4}.

Top of the table (totals /2000 across runs 2–6):

| mask | amp | margin | fullres | total | per-run 2/3/4/5/6 |
|------|-----|--------|---------|-------|--------------------|
| 100 | 40 | 6 | 4 | **1710** | 354/309/358/389/300 |
| 100 | 40 | 10 | 4 | 1708 | 354/309/359/390/296 |
| 125 | 40 | 6 | 4 | 1704 | 354/309/355/388/298 |
| 125 | 40 | 10 | 4 | 1700 | 354/308/355/389/294 |
| 150 | 40 | 6 | 4 | 1682 | 351/307/347/384/293 |
| 150 | 60 | 10 | 4 | 1458 | old default 285/245/284/373/271 |

Read-across:

- **The promoted set (mask 100 / amp 40 / margin 6 / fullres on) IS today's
  console optimum** — top total, top-or-tied min per run (min 300). Margin 10
  trades run6 (−4) for run4/5 (+1); keep 6.
- amp 40 is essential: amp 60 costs 80–240/run; amp 80 collapses the decode
  (blobs gate out).
- fullres OFF costs ~500 total (−100/run avg) — fullres rad-4 is confirmed
  mandatory off-tripod, same as the handheld result.
- Mask 150+ loses 16–114 per run at amp 40 — mask 100 keeps the lead on the
  console perspective too.
- amp 60→40 remains the single biggest gate lever; margin gate only matters
  at amp 40; mask 100 + margin 6 is the working edge.
- Sweep data: `~/.hermes/cache/scratch/sweep/sweep_results.json` (450 rows).
  Consider copying into the repo before it prunes.

## Missing-LED profile (354+309+358+389+300)

No LED is missed in all 5 runs; misses are viewpoint-scattered (61 missed
once, 58 twice, 27 thrice, 8 four times; worst IDs 394–399, 246/248, 78–82).
Not a dead-band or codeword gap — exposure/angle per round. Round spread
300–389 tracks per-round luma conditions, not identity structure.

## Provenance / housekeeping

- r1 jpgs (git-committed S14P-1922 morning round) untouched — verified still
  truncated exactly as before (p01/p07/p13); `/dev/ttyACM0` never opened.
- run6 metas' pre-existing `git status M` = daemon's afternoon re-write over
  morning-era committed content; afternoon metas match afternoon jpgs.
  Committed morning versions remain in git history.
- capture.txt untouched (source of truth); all writes were new/backup files.