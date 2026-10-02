# archive — retired documentation

One line per file: what it was + what superseded it. Nothing here is
deleted from git (retirement = this move); history stays intact via
`git log --follow archive/<file>`.

- `STRATEGY-RESEARCH.md` — Sep 21 strategy survey of commercial LED-camera
  mapping (Twinkly patents, dead-end analysis). Superseded by the S14 CWC
  line; residual context lives in `S14-CWC-PLAN.md` (see
  `archive/PIVOT-white-patterns.md` for the dead-end evidence).
- `F1-SURVEY-PLAN.md` — Sep 21 F1 survey-frame + exposure study plan
  (camera behaviour measurements, blob detector). Superseded by the S14
  CWC line (`S14-CWC-PLAN.md`); its exposure study fed the AE-primer
  design in that plan's §1/§3.
- `PIVOT-white-patterns.md` — Sep 22/23 pivot decision: colour scanning →
  white-pattern detection (AWB/leakage evidence closing the colour era).
  Superseded by the S14 CWC line; retained as the recorded rationale.
- `REPORT-20260922-compfield.md` — Sep 22 S12 complementary-field
  experiment report. Superseded by the S14 CWC line (that era is closed;
  see `archive/PIVOT-white-patterns.md`).
- `REPORT-20260922-evening.md` — Sep 22 day-2 evening experiment report
  (CFG-channel silent breakage + dark-pair race). Superseded by the S14
  CWC line (era closed).
- `S13-SESSION-20260923.md` — Sep 23 S13 hole-survey session log (tripod
  rounds, E0–E5 evidence). Superseded by the S14 CWC line
  (`S14-CWC-PLAN.md`); the S13 detector lives on inside the S14 decode.
- `S13-HOLE-SURVEY.md` — S13 hole-survey design (all-on master −
  single-LED-off pairs). Superseded by the S14 CWC line; the detector +
  serpentine guard are the S13-mirror machinery inside the S14 decode.
- `S14-BENCH-SESSION.md` — Sep 27–30 bench harness + bench lessons
  (S14J harness, the LOGP/BRAMP pull recipes, directive-slot race).
  Superseded as the primary capture path by `tools/bench_daemon.py`
  auto-ship (S14L) — see `HANDOFF-S14P.md` for the current bench setup;
  the harness sections are folded into `S14-CWC-PLAN.md`.
- `IMPLEMENTATION-BRIEF-1919.md` — Oct 1 implementation brief for S14P-1919
  (suppress-window reduction + conflict audit from the sweep). Superseded
  and BUILT (S14P-1919 `cwcSuppress` 1 + spatial conflict audit); results
  in `S14-CWC-PLAN.md` §10-era entries and `HANDOFF-S14P.md`.\n\n2026-10-02 restructure: the root-eran .md files here are the LIVE versions (root copies moved in); all era image/run dirs moved from runs/ to runs/images/.\n