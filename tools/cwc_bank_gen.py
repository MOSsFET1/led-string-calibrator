#!/usr/bin/env python3
"""Generate the S14R-0000 codeword bank: 12-of-24 constant weight via the
extended Golay [24,12,8] weight-12 subcode (S14-CWC-PLAN.md §13 recipe,
computed + verified 02 Oct).

    cyclic [23,12,7] Golay generator exponent set {0,1,5,6,7,9,11},
    each of the 12 shifts x^b·g(x) (bits 0..22, no wrap possible since
    the top generator bit is 11), + the appended overall-parity bit
    (extends the [23,12,7] code to the [24,12,8] code; 4096 words,
    weight enumerator A12 = 2576).

Ordering (S14R-0000): the R-line bank needs the CWC protocol invariants,
not just d_min — every plane exactly N/2 ON at the prefix sizes the rig
uses (per-plane duty/AE stability, per-plane ON-count sanity gate, and
the per-200-block "exactly 100 per plane" per-string property). The
naive first-1600-lexicographic subset has d_min 8 but is COLUMN-
UNBALANCED (measured 312-839 per plane) — it would break all three.
Fix, structural: the extended Golay contains the all-ones word, so the
complement of every weight-12 word is another weight-12 word -> the
2576 codewords form 1288 complementary pairs {c, c^1...1}. Emit PAIRS
in deterministic order (pair key = min(c, complement), ascending): after
every pair every plane count is exactly the pair count k, i.e. exactly
N/2 at every EVEN prefix N (odd N: (N±1)/2). Per-200-block (even offset
+ 200 codes = 100 complete pairs): every plane exactly 100. d_min >= 8
holds for ANY subset of the subcode (pairwise distances are weights of
nonzero [24,12,8] codewords: 8, 12, 16 or 24).

Outputs tools/codewords_12of24.json (1600 codes, d_min 8, every plane
exactly 800 ON) and regenerates page/codewords.js (the page embed
source).

Checks on the produced bank (exit 1 if any fails):
  - Golay facts: 4096 [24] words, 2576 weight-12
  - 1600 distinct codes, all weight 12, planes within 0..23
  - pairwise d_min == 8 across the full 1600 (exhaustive 1.28M pairs)
  - every plane count exactly 800 (50% duty at the full rig)
  - every even prefix N in {200,400,...,1600}: per-plane ON exactly N/2
  - every 200-block: per-plane ON exactly 100
"""
import json
from itertools import combinations
from pathlib import Path

BASE = Path(__file__).resolve().parent
OUT_JSON = BASE / "codewords_12of24.json"
OUT_JS = BASE.parent / "page" / "codewords.js"

# --- §13 recipe: cyclic [23,12,7] generator exp(0,1,5,6,7,9,11) + parity --
EXPS = (0, 1, 5, 6, 7, 9, 11)
gen_mask = sum(1 << e for e in EXPS)          # odd weight (7 ones)
assert bin(gen_mask).count("1") == 7 and gen_mask & 1, \
    "generator must be odd-weight with bit0 present"
assert max(EXPS) + max(EXPS) <= 22, "shifts must stay inside 23 bits"

code24 = set()
for a in range(1 << 12):
    v = 0
    for b in range(12):
        if (a >> b) & 1:
            v ^= gen_mask << b                # GF(2) span of the 12 shifts
    w23 = bin(v).count("1")
    code24.add(v | ((w23 & 1) << 23))         # append overall-parity bit
print(f"Golay [24,12,8]: {len(code24)} codewords generated (want 4096)")
assert len(code24) == 4096, "Golay [24,12,8] must have 4096 words"

w12 = sorted(c for c in code24 if bin(c).count("1") == 12)
print(f"weight-12 subcode: {len(w12)} codewords (want 2576)")
assert len(w12) == 2576, "weight enumerator A12 = 2576 must hold"

# --- bank = 800 complementary pairs emitted in deterministic order --------
ONES = (1 << 24) - 1
pairs = {}
for c in w12:
    comp = c ^ ONES
    assert bin(comp).count("1") == 12 and (comp in set(w12)), \
        "complement of every weight-12 word must be weight-12 in the subcode"
    lo, hi = (c, comp) if c < comp else (comp, c)
    pairs.setdefault(lo, hi)                  # 1288 distinct pairs
pair_items = sorted(pairs.items())            # deterministic: by min word
assert len(pair_items) == 1288, "1288 complementary pairs expected"
bank_pairs = pair_items[:800]

flat = []
for lo, hi in bank_pairs:
    flat.append(lo)
    flat.append(hi)
assert len(flat) == 1600
codes = [{p for p in range(24) if (c >> p) & 1} for c in flat]

# --- verify --------------------------------------------------------------
def dm(a, b): return len(a ^ b)

N_ALL = len(codes)
pairs_d = min(dm(a, b) for a, b in combinations(codes, 2))
print("pairwise d_min over the full 1600 (exhaustive):", pairs_d)
assert pairs_d == 8, "d_min 8 must hold over the whole bank"
assert N_ALL == 1600 and len({frozenset(c) for c in codes}) == 1600
assert all(len(c) == 12 for c in codes)
assert all(0 <= p < 24 for c in codes for p in c)

cnts = [sum(1 for c in codes if p in c) for p in range(24)]
print("per-plane ON counts @1600:", min(cnts), "-", max(cnts))
assert min(cnts) == max(cnts) == 800, "every plane exactly 800 ON @1600"

# prefix property: every EVEN prefix keeps exact N/2 per-plane balance
for N in range(200, 1601, 200):
    pre = codes[:N]
    cnts = [sum(1 for c in pre if p in c) for p in range(24)]
    ok = min(cnts) == max(cnts) == N // 2
    print(f"prefix {N:5d}: per-plane ON {min(cnts)}-{max(cnts)} "
          f"({'exact N/2' if ok else 'UNBALANCED'})")
    assert ok, f"prefix {N} balance FAILED"

# per-200-block per-string property: exactly 100 ON per plane per block
for b in range(8):
    blk = codes[200 * b: 200 * b + 200]
    cnts = [sum(1 for c in blk if p in c) for p in range(24)]
    ok = min(cnts) == max(cnts) == 100
    print(f"block {b}: per-plane ON {min(cnts)}-{max(cnts)} "
          f"({'exact 100' if ok else 'UNBALANCED'})")
    assert ok, f"block {b} balance FAILED"

# --- write the JSON bank --------------------------------------------------
bank = {"scheme": "12-of-24", "planes": 24, "weight": 12, "n_codes": 1600,
        "dmin": 8,
        "generator": "extended Golay [24,12,8]; cyclic [23,12,7] exp(0,1,5,6,7,9,11) + parity bit; weight-12 subcode emitted as 800 complementary pairs (exact N/2 per-plane balance at every even prefix)",
        "source_recipe": "S14-CWC-PLAN.md §13 (02 Oct, computed + verified); ordering per tools/cwc_bank_gen.py docstring",
        "cols": [800] * 24,
        "codes": [sorted(c) for c in codes]}
OUT_JSON.write_text(json.dumps(bank, separators=(",", ":")) + "\n")
print("bank ->", OUT_JSON, OUT_JSON.stat().st_size, "B")

# --- regenerate page/codewords.js (the page embed source) -----------------
js_codes = ",".join('"' + ",".join(str(p) for p in c) + '"' for c in codes)
header = """// CWC codewords, 12-of-24 constant weight (S14R-0000 Golay bank). Generated by
// tools/cwc_bank_gen.py from the extended Golay [24,12,8] weight-12 subcode
// (cyclic [23,12,7] exp(0,1,5,6,7,9,11) + parity bit; S14-CWC-PLAN.md §13),
// banked at tools/codewords_12of24.json; embedded page-side so the burst loop
// needs no network fetch. Codes are ON-plane index lists:
// code[i] = [p, ...] = planes where LED i paints ON (weight exactly 12).
// Prefix-valid: first N codes keep d_min 8 AND exactly N/2 per-plane ON for
// every even N (bank order = 800 complementary pairs; verified by
// cwc_bank_gen.py). The P-line 9-of-18 bank is retired with S14P-1928
// (tools/codewords_9of18.json stays as the P-line record). S14R: plane p
// paints LED i iff LED i's codeword contains plane p. NO off frame: the
// point set comes from the pile-up of registered (master - plane) diffs.
// The STATEMENT must start its own line — appended to a // comment line it
// silently never executes (S14J lesson: grep-visible but dead).
window.CWC_CODES_12OF24 = ["""
OUT_JS.write_text(header + js_codes + "];\n")
print("page/codewords.js ->", OUT_JS, OUT_JS.stat().st_size, "B")