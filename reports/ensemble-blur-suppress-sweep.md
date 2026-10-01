# Ensemble blur + suppression sweep

Handheld CWC runs decoded with `tools/cwc_pos_decode.py` via a read-only wrapper in `~/.hermes/cache/scratch/led_sweep2/sweep.py`.

## Runs
Included: s14p-1908-handheld, s14p-1911-handheld-r4, s14p-1913-handheld, s14p-1917-handheld, s14p-1919-handheld-r2.
Excluded AE-lock dark runs: s14p-1914-handheld, s14p-1915-handheld, s14p-1916-handheld (empty/no usable frames or AE-lock dark exposure).
`s14p-1908-handheld` and `s14p-1911-handheld-r4` frame files were recovered from git because their working directories no longer contained `cwc_frames.txt`.

Visible count = raw confirmed LEDs minus the operator-confirmed hidden IDs [46, 90, 91] if they were recovered.

## Results

| blur | R | 1908 | 1911r4 | 1913 | 1917 | 1919r2 | mean | min | worst | conflicts |
|------|---|----|----|----|----|----|------|-----|-------|----------|
| box3x2 | 1 | 191 | 197 | 192 | 197 | 190 | 193.40 | 190 | s14p-1919-handheld-r2 | 41 |
| box3x2 | 2 | 190 | 197 | 185 | 196 | 190 | 191.60 | 185 | s14p-1913-handheld | 32 |
| box3x2 | 3 | 186 | 196 | 185 | 194 | 188 | 189.80 | 185 | s14p-1913-handheld | 25 |
| box3x2 | 4 | 185 | 196 | 185 | 192 | 188 | 189.20 | 185 | s14p-1908-handheld | 20 |
| box3x2 | 5 | 182 | 193 | 182 | 191 | 187 | 187.00 | 182 | s14p-1908-handheld | 0 |
| box3x2 | 7 | 176 | 187 | 176 | 185 | 180 | 180.80 | 176 | s14p-1908-handheld | 0 |
| gauss5_s1.2 | 1 | 191 | 197 | 192 | 197 | 191 | 193.60 | 191 | s14p-1908-handheld | 42 |
| gauss5_s1.2 | 2 | 191 | 197 | 185 | 196 | 191 | 192.00 | 185 | s14p-1913-handheld | 32 |
| gauss5_s1.2 | 3 | 186 | 196 | 185 | 194 | 189 | 190.00 | 185 | s14p-1913-handheld | 25 |
| gauss5_s1.2 | 4 | 185 | 196 | 185 | 192 | 189 | 189.40 | 185 | s14p-1908-handheld | 20 |
| gauss5_s1.2 | 5 | 183 | 193 | 182 | 191 | 188 | 187.40 | 182 | s14p-1913-handheld | 0 |
| gauss5_s1.2 | 7 | 176 | 187 | 176 | 185 | 181 | 181.00 | 176 | s14p-1908-handheld | 0 |
| gauss5_s1.5 | 1 | 191 | 197 | 192 | 197 | 190 | 193.40 | 190 | s14p-1919-handheld-r2 | 41 |
| gauss5_s1.5 | 2 | 189 | 197 | 186 | 196 | 191 | 191.80 | 186 | s14p-1913-handheld | 32 |
| gauss5_s1.5 | 3 | 186 | 196 | 186 | 194 | 189 | 190.20 | 186 | s14p-1908-handheld | 25 |
| gauss5_s1.5 | 4 | 185 | 196 | 186 | 192 | 188 | 189.40 | 185 | s14p-1908-handheld | 20 |
| gauss5_s1.5 | 5 | 182 | 192 | 183 | 191 | 188 | 187.20 | 182 | s14p-1908-handheld | 0 |
| gauss5_s1.5 | 7 | 177 | 188 | 177 | 185 | 180 | 181.40 | 177 | s14p-1908-handheld | 0 |
| gauss3_s0.8 | 1 | 191 | 197 | 192 | 197 | 192 | 193.80 | 191 | s14p-1908-handheld | 46 |
| gauss3_s0.8 | 2 | 189 | 197 | 185 | 196 | 192 | 191.80 | 185 | s14p-1913-handheld | 32 |
| gauss3_s0.8 | 3 | 186 | 196 | 183 | 194 | 190 | 189.80 | 183 | s14p-1913-handheld | 24 |
| gauss3_s0.8 | 4 | 185 | 196 | 182 | 192 | 190 | 189.00 | 182 | s14p-1913-handheld | 19 |
| gauss3_s0.8 | 5 | 184 | 194 | 181 | 191 | 188 | 187.60 | 181 | s14p-1913-handheld | 0 |
| gauss3_s0.8 | 7 | 177 | 187 | 173 | 185 | 182 | 180.80 | 173 | s14p-1913-handheld | 0 |

Duplicate LED IDs across all configs: **none** (the decoder’s strongest-site-per-codeword dedup keeps 0 duplicates).
Conflict pairs = accepted LEDs < 6 px apart with IDs differing by > 5.

## Recommendation

**Recommended phone-portable config: `gauss5_s1.2` + suppression radius **2** (window 5 px).**

- Mean visible count: **192.00/197** across 5 runs.
- Minimum visible count: **185/197** (worst run: s14p-1913-handheld).
- Total spatial conflict pairs: **32**.
- Duplicate LED IDs: **0**.

Close alternatives:
- `gauss3_s0.8` R=1: mean 193.80, min 191, conflicts 46 (highest count, highest conflicts).
- `gauss5_s1.2` R=1: mean 193.60, min 191, conflicts 42.
- `gauss3_s0.8` R=2: mean 191.80, min 185, conflicts 32.
- `box3x2` R=2: mean 191.60, min 185, conflicts 32.

Rationale: radius 1 gives the highest raw counts but also the most spatial conflicts (~40–46 across the ensemble), matching the real-phone observation that a 3 px suppression window reintroduces bloom-skirt impostors. Radius 2 with a 5 px window keeps counts within ~1 LED of the radius-1 peak while cutting conflicts by roughly one quarter, and it uses the same Gaussian mask blur already aligned between the console decoder and the S14P-1918+ page.
