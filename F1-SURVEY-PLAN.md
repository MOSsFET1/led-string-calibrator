# F1 — survey frame + exposure study + blob detector (plan, Sep 21 2026)

Scope of THIS build (one feature, made robust — no point cloud yet).

## What it does

1. **All-on survey frame:** box paints every LED white at a settable
   brightness (`all` command). The page grabs frames, measures the camera's
   behaviour: peak/channel histograms, % clipped pixels (luma ≥250,
   white-clipped = min(r,g,b) ≥250, single-channel clip), AE exposure/gain
   readback where the platform exposes it. Purpose: SEE how the camera
   behaves on a bright field, find clipping, find the brightness that pins
   exposure without saturating colour.
2. **Accent study:** 3 LEDs (default positions spread along the string,
   never px0/pxN-1) lit RED, GREEN, BLUE at a settable brightness while the
   rest stay white. Purpose: see how saturated colour pops against the
   white field, measure hue separation and channel bleed — the data that
   decides whether colour can differentiate pixels (F3).
3. **Reusable blob detector** (page-side, one function, every later feature
   builds on it): relative threshold (median + k×(d90−median), the
   B101-era statistical form — no absolute floors), flood-fill connected
   components, per-blob centroid/peak/size/mean colour/hue/saturation,
   small-fragment merge, and the dominance acceptance bar (peak ≥ domK ×
   the outside-blob top-decile) carried over from the proven d10out work.
   Two measurement channels: `luma` (max(r,g,b)) for dark-scene blobs,
   `sat` (max−min) for coloured pops on a bright field.
4. **Live metrics overlay:** while idle, the page runs detect on the live
   stream (~10 Hz), draws blob boxes, and shows a one-line metric strip
   (peak, blob count, clip %, exposure). Manual All-on / Accents / Off
   buttons so the camera behaviour can be studied by eye with numbers.
5. **Survey run (the automatable "scan"):** dark baseline frame → all-on
   (N grabs) → accents (N grabs) → black; logs per-frame compact metrics,
   ships ONE evidence summary to the box (serial-visible `[EVID]`).
   Abort kills it between any two grabs.
6. **Machinery:** WS auto-connect + hello (box gives string length), camera
   auto-start with the B117 robustness (12 s timeout race ×3, retry on
   visibilitychange/gesture), CFG runtime params from serial (`CFG=<json>`
   carried by the drv? poll), serial directives SCAN/ABRT/PING, page build
   stamp in the header, in-memory log ring + Download-log button.

## Explicitly NOT in this build (anti-ghost list)

- No phone→box log shipping, no NVS commit, no flood/drain logic.
- No pause/continue, no session persistence, no resume.
- No anchor tracker, per-pixel ladder, shift compensation, march.
- No point cloud, no ID decoding (F2+).

## Firmware delta (poc_survey.ino, new sketch)

- Derived from cal_poc2: same AP (`LED-CAL-POC`/`calibrate`), same
  self-signed HTTPS+WSS on :443, same cert, same FastLED WS2815 RGB lane
  (GPIO1), N_PX=10 bench string, 15→30 fps pacing, ackAfterLatch kept.
- Commands: `hello`, `frame {p:["W",null,...],b}` (arbitrary paint),
  `all {b}`, `accents {b,ab,i:[3 idx]}`, `black`, `evid {e}`, `drv?`.
  Deleted: light/off/ref/backlight/sweep/test/log/logend + ALL
  NVS/commit/dump machinery.
- Serial directives: SCAN, ABRT, PING, CFG=, STAT, EV (prints last stored
  evidence).

## Automation (console, no operator)

STAT (liveness+build+evidence size), SCAN trigger, CFG= retune, EV evidence
pull — the optimisation loop: CFG → SCAN → EV, no rebuilds, no reboots.
The phone page must be open once (reload after each flash is the only
manual step); everything else runs from the bench console.

## Acceptance criteria (bench)

- A1 Page boots alone: STAT shows page build + polls; WS + camera self-start
  (or visibly retrying with status).
- A2 All-on: overlay shows the string as one lit band; metric strip shows
  peak/clip%/exposure; changing `CFG.allB` moves the numbers (servo data).
- A3 Accents: sat-mode detection finds exactly 3 coloured blobs at the 3
  positions; hue classification correct R/G/B; clip + bleed numbers logged.
- A4 Survey run end-to-end: black→all→accents→black, one [EVID] line on
  serial, abort works mid-run (no wedge, LEDs left black).
- A5 Download-log produces a file with the session's decisions.
- A6 Back-to-back survey runs: second run works identically (no session
  state to corrupt — by construction).

## Risks watched

- iOS: no exposureCompensation — never load-bearing; the bright field is
  the exposure lever (Oliver's insight), brightness via box servo.
- AE transient at burst start: settle (CFG.settle, default 400 ms) before
  first grab of each frame mode.
- Blob merge limit: bench geometry check, pitch in camera px logged.
- AWB drift during mode switches: logged (mean hue of white body per frame)
  so the AWB-stabilisation claim gets measured, not assumed.
## S2/S3 increments (Sep 21 pm, bench-validated)

- **Portrait fix:** processing dims (W/H) now come from the video track's
  REAL aspect (long side capped 720, short side matched) — canvas is never
  squashed; display canvas sized to the processing aspect.
- **LED count:** firmware N_PX=100 max, runtime `npx` WS command + serial
  `NPX=` directive; page has an editable LEDs field (box hello sets the
  default, field edits push to the box immediately).
- **Hue LED detector (the F1 overlay goal):** per-colour excess channels
  (r−max(g,b) etc.) through the same detectBlobs (thrFloor=CFG.colFloor=40);
  idle accents mode runs detectColours() and draws R/G/B boxes + labels on
  the camera image. Synthetic-scene proof: blobs injected at 20/50/80%
  width detected at exactly those positions, correct hues, boxes + labels
  screenshot-verified (runs/s3_hue_overlay.png).
- **Pump/overlay ownership fix (S3):** the frame pump no longer paints the
  display canvas — only detection passes do. The pump's 30 Hz raw repaint
  was flickering the overlay away between idle ticks (caught by the
  in-browser capture, invisible to parse/audit checks).
- Note: survey-run logging still uses single-channel (sat/luma) detection
  for its per-frame numbers; switching its accent passes to detectColours
  is a small follow-up once real-camera evidence exists.

## S6/S7 — overnight detector-debug era (Sep 21 night)

Field report (operator, S6): green LED detected a few times; boxes stayed
after scan 1 but vanished after later scans. Mechanism (S7 fix, built, NOT
yet flashed — the overnight sweep holds the serial port): updateLedBoxes()
REPLACED stored boxes with the current detection every pass, so one missed
accent frame erased last-known positions. S7 rule: absence never erases —
a hue's boxes update only when detection finds something; staleness is
already shown by dimming. Reload to S7 in the morning.

Overnight instrumentation (S6): frame store (16 x 320-px JPEG + context:
mode/paint/exposure/per-channel thr-d10-candidates-accepted, FRAMP pull),
per-blob dominance margins in evidence, CFG.domKHue separate hue dominance
bar. overnight_sweep.py running (domKHue 4/8/16 x colFloor 30/60, 2 surveys
each, then LOGP + FRAMP pulls) into runs/overnight-<ts>/.

Morning procedure: wait for sweep end -> flash S7 (page reload needed) ->
analyse margins + frames -> set domKHue/colFloor INSIDE the measured gap ->
confirming burst on the green-object impostor case -> re-test persistence.
Coordinate convention: (1,1) = TOP-LEFT, x right, y down, processing-canvas
space (scale noted in every pulled frame).

## S8 — temporal-diff detection (Sep 22 morning, bench-proven)

Overnight sweep never ran (venv lacked pyserial — crashed at import; root
cause of the "no runs" morning). Salvaged directly instead: phone alive on
S6, box still held the last survey's evidence (G@118,537 pk56 n8).

Morning pulls (LOGP/FRAMP, after server-side fixes: LOGP now queues its drv
string; the 15 s print window is a sliding idle timeout) produced 15 full
survey frames, 3 per mode, AE pinned (exp=300).

Offline findings (tools/offline_detect.py, offline_diff2.py — all on REAL
pulled pixels):
1. Whole-frame hue dominance is UNUSABLE in the cluttered workshop scene:
   blue-excess tail 33-42 from scene objects swamps the die (whole margin
   0.23). LOCAL windows do NOT rescue it (hypothesis refuted: the
   neighbourhood is cluttered too).
2. TEMPORAL DIFF (accent minus all-on, same survey, 2-4 s apart, tripod):
   static scene cancels EXACTLY. Green die = single 12x12 blob pk130
   (margin 1.9 at thr40) where single-frame detection saw two fragmented
   blobs through the bloom. PROVEN -> adopted as the survey accent detector
   (S8: detectDiffBlobs on hue-excess differences, CFG.diffThr=40,
   CFG.domKHue dominance).
3. R and B dies: near-zero diff residual at their 20%/80% positions —
   consistent with occlusion/aiming on the shelf layout, NOT a detector
   failure (green at 50% detected by both methods). Needs a physical check.
4. Live single-frame hue detection remains for the Accents idle mode;
   the survey run now uses the diff path when an all-on reference exists.

S8 flashed (65% flash). Reload the page (S8-1900) and re-run Survey:
expect the green die again via diff (cleaner, unfragmented), boxes
persistent, misses no longer erase positions.

## Day experiment (Sep 22, operator away; phone stays on S8)

Constraint: no page reload available -> experiments limited to CFG-tunable
axes on S8: accB sweep (80/120/200/255) at allB=160, allB sweep (80/255) at
accB=200. Dark-pair + 2-colour-field experiments are BUILT (S9 flashed with
CFG.darkPair / CFG.field2 + grabRaw exposure logging) but need the page
reload tonight. nPx default now 150 (max 200) per operator.

matrix_s8.py running: per combo CFG -> SCAN -> EV + FRAMP pull into
runs/matrix-<ts>/; analysis via matrix_analysis.py (temporal-diff detection
per accent frame vs that combo's all-on frame; peak/size/margin comparison).

## Day experiment results (Sep 22, 09:45 — report to operator)

### Headline findings (real data, pulled frames)

1. ALL THREE dies detected today at some point: G@(118,537) p68-71,
   B@(247,241) p99-115, R@(204,523-524) p96-103. Yesterday's "occlusion"
   theory is dead — nothing is hidden; detections are MARGINAL and
   position-dependent (camera reframe changes which dies clear the bar).
2. accB sweep: only 2/6 combos verified (trigger reliability, below).
   accB 120 vs 255: BOTH detect B die; G missed in both; R found only in
   the accB=255 run. Margin data: all detections are marginal (0.5-1.4 vs
   the bar) and d10out ~8-13 — the die sits right AT the acceptance bar in
   this scene. Brightness 80-255 did NOT change detectability materially:
   AE pins the field, so die-vs-field contrast is roughly preserved. The
   "should accents be less bright?" answer: brightness is not the lever;
   CONTRAST and detection margin are.
3. Trigger failures (the day's real cost): the box's drv? single-slot
   serves-and-clears; CFG and SCAN sent 0.2 s apart get split by a poll
   (~1/3 dropped). Single-burst writes fixed the split — but failures
   persisted, because the deeper issue is survey STALLS: runs stretch from
   25 s to 120+ s mid-survey (ack retries, TLS jams on a many-hour-old
   session), so "no evid change in 150 s" misclassifies slow runs as
   never-run. The two verified runs prove the pipeline; the transport
   reliability is the open problem.
4. Working infrastructure (proven today): frame store + FRAMP pulls
   (17 frames/2.4 KB JPEGs per pull), EV-change verification concept,
   offline temporal-diff analysis pipeline (matrix_analysis.py), CFG
   channel to the live page.

### Tonight (needs operator)
1. Reload page -> S9-1900 (darkPair + field2 experiments built, exposure
   logging per grab, 150-LED default). This also resets the tired TLS
   session — the likely stall cause.
2. Then: dark-pair AE-race experiment (CFG.darkPair=1), 2-colour field
   (CFG.field2=1), accB/allB matrix re-run with the fixed single-burst
   trigger. All console-drivable.

### Detection improvement candidates (next build, evidence-based)
- Die detection margins sit at 0.5-1.9 vs domKHue=8: either domKHue down
  (risky: green-object impostor enters) or per-die LOCAL reference
  subtraction (die minus its own all-on state = what S8 already does) with
  a second pass at matched sub-frame offsets. The R die at accB=255 was
  found with margin 0.50 — marginal accepts are the "doesn't always pick
  correctly" the operator sees.
- The two-blob split of one die (G p60+n6 yesterday) suggests bloom
  fragmentation: merge blobs within ~1.5x blob radius before dominance.

## S10 + evening session (Sep 22)

- ROOT CAUSE of all CFG failures (day + evening): drv? embedded sCfg JSON
  without escaping quotes -> invalid JSON page-side -> cfg AND directive
  silently dropped per poll. S10 escapes quotes/backslashes; verified live.
- Evening session (fresh S9 page + S10): all 6 experiments verified by
  evid-change; frames pulled for each (16/pull).
- Dark-pair: on/off grabs byte-identical = capture race (both grabs same
  video frame; WS round-trip faster than LED state change through the
  15 fps latch). AE-lag observation: in the dark window dies remain
  visible as ~13 px points while the scene goes dark — under-exposed
  die-only images ARE achievable. Fix: settle between on/off grabs +
  byte-diff guard before accepting the pair.
- Brightness matrix (accB 80/255, allB 80/255, all verified): die residual
  peaks 41-44 in EVERY combo — AE pinning makes absolute brightness a
  non-lever. Contrast in the difference domain is the lever.
- field2: box flag applied, all2 frames red+green-consistent (B mean 77 <
  R 59/G 64), blue-die diff sat AT diffThr=40 — inconclusive, rerun at
  diffThr=25 queued.
- Transport: sessions degrade after ~30-60 min (ack retries, 25 s -> 2 min
  surveys). S11 candidate: page auto-recycles the WS on repeated retries.
- Full report: REPORT-20260922-evening.md

## S11 (Sep 22 night, flashed — awaiting operator reload)

Built on the operator's clipping diagnosis (R/G dies look white in camera
because the white field drives all channels near saturation; only B
survives since warm-white leaves the B channel low):

1. **Complementary field (CFG.compField, default 1):** scanning die colour
   C uses a field of ONLY the other two colours (alternating down the
   string) — channel C stays dark field-wide, so the C die has maximum
   headroom; the temporal-diff reference is that same field WITHOUT the
   die (grabbed immediately before each accent). AE luminance unchanged.
2. **Bloom merge (CFG.mergeR=2.0):** blobs whose centroids sit within
   2x the larger blob's radius are merged before dominance (the G die's
   p60n10 + p48n6 fragmentation case).
3. **Dark-pair capture fix:** CFG.dpGap=300 ms settle between the on and
   off grabs + a byte-diff guard that logs 'RACE (identical)' if the pair
   still came from the same video frame.
4. **WS auto-recycle:** 8 ack retries in a short window while idle ->
   page reloads itself (the proven session-reset; fixes the 25 s -> 2 min
   transport degradation).
5. CFG gains: compField, mergeR, dpGap. Verify stamp S11-1900 on reload.

## S12 (Sep 22 night 2, flashed)

Operator field report: S11 survey left all LEDs full white — no survey
motion. ROOT CAUSE: at npx=150 every 'frame' paint message is 1537 B and
the box silently dropped WS messages > 1024 B (limit sized for the old
per-LED protocol). Only the 792 B white 'all' baseline fit -> LEDs stuck
on the last-applied white field.

1. **WS limit 1024 -> 4096 B** (200-px paint = ~2050 B). The survey now
   works at 150/200 px. (S11's complementary-field experiment NEVER ran
   at 150 px — needs re-test under S12.)
2. **Status LED state machine (operator request):**
   solid RED = new build flashed, not yet loaded on the phone (page hello
   build != firmware PAGE_BUILD const);
   OFF during surveys/experiments (page reports its scanning state in every
   drv? poll);
   slow green breathe when idle and up to date.
   Firmware carries PAGE_BUILD "S12-1900" — must be bumped with every page
   BUILD bump (one-line sync rule, same as the old build stamp discipline).

## S12 session results (Sep 22 late night)

Complementary field VALIDATED: all three dies detect in the offline diff
(R was 0-for-everything under the white field; now 4/5 runs, e.g. n398
pk152 at allB=255). Die channel genuinely dark in each ref frame
(R-mean 28 / G 44 / B 45). Full table + analysis in
REPORT-20260922-compfield.md.

New findings -> S13 items:
1. AE pump between ref and accent grabs: page accepted a 13,373-px 'blue'
   blob = whole-frame channel shift, not the die. Fix: interleave ref/die
   grabs; reject blobs > ~1% frame area; hueClass at peak, not mean.
2. Dark-pair: guards OK (dpGap fix works); AE recovers only ~halfway in
   300 ms (201->55 mean luma) — the under-exposure window is real. S13:
   make the 'on' frame die-ONLY (field off) as originally proposed.
3. Brightness: with complementary fields, dimmer accents may be better
   (less pump, less bloom) — focused sweep queued.

## PIVOT (Sep 22/23, operator decision + evidence): colour out, white patterns in

Operator field report (S12): large blue patch over non-blue scene content.
Offline confirmation: the R+G field's on-camera blue-excess moves +-40
between grabs (AWB re-balance) — field-colour wobble is indistinguishable
from a die in a whole-frame hue diff; plus saturated dies mis-classify by
mean hue. Colour as discriminator is UNRELIABLE on phone cameras.

PIVOT: binary white ON/OFF bit-planes (Twinkly's proven scheme), identity
in the code, luma-only detection (no hue anywhere), constant total
luminance across planes (AE/AWB static). Everything built since S9 kept
(diff machinery, dark-pair timing, frame store, pulls, status LED, auto-
recycle). Full design: PIVOT-white-patterns.md. S14 = 'bits' firmware
command + bit-plane survey + per-die signature assembly.
