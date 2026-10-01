# S14P-1922 page simulation report

## Change
`page/survey.html` bumped to **S14P-1922**.

The page already had `nccRefine()` defined but `cwcDecode()` was not calling it. The console decoder (`tools/cwc_pos_decode.py`) uses a score-time full-res NCC refine (±4 px, stride 2) around the decimated seed before building the per-plane stacksig. The page now applies the same refine, keeping the decimated parabolic sub-pixel fraction and adding the integer full-res correction, exactly as the console does.

## Simulation method
A Node.js harness extracted the live `cwcBankBits`, `lumaOf`, `histMedian`, `cwcChain`, `nccRefine` and `cwcDecode` functions verbatim from `page/survey.html` and ran them on the real handheld frame sets (master + 18 planes) without the physical phone. Canvas/`node-canvas` supplied `getImageData` luma arrays matching the page runtime.

Runs tested:
- `s14p-1908-handheld` (recovered from git, working copy missing)
- `s14p-1911-handheld-r4` (recovered from git, working copy missing)
- `s14p-1913-handheld`
- `s14p-1917-handheld`
- `s14p-1919-handheld-r2`

Hidden LEDs excluded from visible counts: `[46, 90, 91]`.
Page CFG for the simulation matched the console defaults:
- `cwcMaskThr = 150`
- `cwcAmpGate = 60`
- `cwcMarginGate = 10`
- `cwcSuppress = 1` (3 px window)
- `cwcNccPeakMargin = 0.05`

## Results

| run                    | page visible | console visible | diff |
|------------------------|--------------|-----------------|------|
| s14p-1908-handheld     | 192          | 191             | +1   |
| s14p-1911-handheld-r4  | 197          | 197             | 0    |
| s14p-1913-handheld     | 192          | 192             | 0    |
| s14p-1917-handheld     | 198          | 197             | +1   |
| s14p-1919-handheld-r2  | 189          | 190             | -1   |
| **mean**               | **193.60**   | **193.40**      | **+0.20** |

Console numbers are from `reports/ensemble-blur-suppress-sweep.md` for the `box3x2` blur, suppression radius 1 configuration.

## Conclusion
The page code now reproduces the console decoder performance to within ±1 visible LED per run and +0.20 LEDs on the ensemble mean. The change can be flashed to the box when convenient.

## Note
`python3 firmware/tools/pack_page.py` succeeded and updated `PAGE_BUILD` to `S14P-1922`, but `arduino-cli compile` currently fails with a FastLED static assertion that GPIO 22 is an invalid pin on ESP32-C3. This is a hardware/library pin-mapping issue, not related to the S14P-1922 page change.
