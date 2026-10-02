# S14P-1917 Handheld Run: Phone vs Console LED Detection Gap Analysis

## 1. Phone decode summary
- Run: `runs/s14p-1917-handheld/cwc_frames.txt`
- Total phone-confirmed LEDs: 190 (from 190 CWCDEC records)
- ampGate: 60, marginGate: 10
- sitesMasked: 44,293
- Phone amp stats: min=61.9, median=108.3, max=190.1
- Phone margin stats: min=10.4, median=48.2, max=118.5
- Sorted phone LED IDs (first 30): [0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 23, 24, 26, 27, 28, 29, 30, 31, 32, 33]
- Sorted phone LED IDs (last 30): [170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199]

## 2. Console decode summary
- Output: `runs/s14p-1917-handheld/ledpos.json`
- Total console-confirmed LEDs: 195
- Console amp stats: min=60.2, median=115.1, max=201.2
- Console margin stats: min=10.8, median=62.1, max=126.7
- Sorted console LED IDs (first 30): [0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28, 29, 30, 31, 32]
- Sorted console LED IDs (last 30): [170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199]
- Notes field present: False

## 3. Diff table
| Set | Count | IDs |
|---|---|---|
| Phone ∩ Console | 187 | (see full sorted lists above) |
| Phone only | 3 | [16, 27, 91] |
| Console only | 8 | [4, 22, 25, 46, 99, 100, 122, 143] |

## 4. Per-anomaly notes
**Console-only LEDs (phone missed them):**
- LED 4 (console pos 164,586, amp=94.4, margin=46.6): phone has no record. Closest phone record is LED 17 at 8.2 px; likely the phone decoder assigned/masked the blob to a different ID or the chain step failed here.
- LED 22 (console pos 171,507, amp=109.6, margin=55.3): phone has no record. Closest phone record is LED 16 at 8.1 px; likely the phone decoder assigned/masked the blob to a different ID or the chain step failed here.
- LED 25 (console pos 166,462, amp=118.8, margin=37.3): phone has no record. Closest phone record is LED 91 at 5.4 px; likely the phone decoder assigned/masked the blob to a different ID or the chain step failed here.
- LED 46 (console pos 118,577, amp=60.2, margin=10.8): phone has no record. Closest phone record is LED 9 at 8.5 px; likely the phone decoder assigned/masked the blob to a different ID or the chain step failed here.
- LED 99 (console pos 69,356, amp=113.0, margin=57.7): phone has no record. Closest phone LED is 81 at 12.1 px; registration differences or a locally masked site are the more likely cause than the amp/margin gate.
- LED 100 (console pos 71,369, amp=96.1, margin=39.3): phone has no record. Closest phone record is LED 179 at 1.4 px; likely the phone decoder assigned/masked the blob to a different ID or the chain step failed here.
- LED 122 (console pos 187,337, amp=92.6, margin=25.7): phone has no record. Closest phone record is LED 27 at 2.2 px; likely the phone decoder assigned/masked the blob to a different ID or the chain step failed here.
- LED 143 (console pos 218,379, amp=157.9, margin=55.4): phone has no record. Closest phone record is LED 133 at 2.2 px; likely the phone decoder assigned/masked the blob to a different ID or the chain step failed here.

**Phone-only LEDs (console missed them):**
- LED 16 (phone pos 178,503, amp=114.8, margin=14.6): console has no record. Likely console excluded it due to low margin (14.6). Nearest console LED is 22 at 8.1 px.
- LED 27 (phone pos 185,338, amp=85.4, margin=15.6): console has no record. Likely console excluded it due to close to a different console ID. Nearest console LED is 122 at 2.2 px.
- LED 91 (phone pos 164,457, amp=61.9, margin=10.4): console has no record. Likely console excluded it due to low margin (10.4); low amp (61.9). Nearest console LED is 25 at 5.4 px.

**Quality / order checks:**
- Duplicate LED IDs in phone records: 0 []
- Duplicate LED IDs in console records: 0 []
- Out-of-range IDs in phone: none
- Out-of-range IDs in console: none
- Phone IDs sorted ascending: True
- Console IDs sorted ascending: True

**Cross-ID nearest-neighbour mismatches (<10 px):**
- phone LED 179 is spatially closest to console LED 100 (1.4 px) rather than its own ID.
- phone LED 27 is spatially closest to console LED 122 (2.2 px) rather than its own ID.
- phone LED 133 is spatially closest to console LED 143 (2.2 px) rather than its own ID.
- phone LED 91 is spatially closest to console LED 25 (5.4 px) rather than its own ID.
- phone LED 10 is spatially closest to console LED 11 (6.4 px) rather than its own ID.
- phone LED 16 is spatially closest to console LED 22 (8.1 px) rather than its own ID.
- phone LED 112 is spatially closest to console LED 111 (8.6 px) rather than its own ID.

## 5. Root-cause verdict
The gap is **primarily a decoder/registration problem**, not a simple amp/margin strictness issue.
- Both decoders use the same gates (ampGate=60, marginGate=10). Console medians are slightly higher (amp 115.1 vs 108.3; margin 49.0 vs 48.2). Every console-only LED has amp/margin comfortably above the gates, and the 8 console-only LEDs are not weak detections (median amp 102.8, margin 43.0); they are simply absent from the phone CWCDEC list, all falling in phone ID gaps.
- The 3 phone-only LEDs have low margins (10.4–15.6), with LED 91 also borderline on amp (61.9). This matches console excluding marginal detections.
- Position matching reveals clear ID swaps at very close distances: phone 179 ↔ console 100 (1.4 px), phone 27 ↔ console 122 (2.2 px), phone 133 ↔ console 143 (2.2 px), phone 91 ↔ console 25 (5.4 px), phone 16 ↔ console 22 (8.1 px). These are registration/chain-step mismatches, not blob-detection failures.
- Some large outliers (e.g. LED 179/100, 112/111, 133/143) show the phone and console coordinate grids diverging locally, consistent with registration differences or row-ordering errors rather than image-quality degradation.
- **Verdict:** Console decoder is more robust at accepting marginal blobs and maintains better ID-to-position registration. Phone decoder is stricter and suffers local ID swaps/gaps, most likely from parabolic-guard / chain-step / mask-threshold behaviour.