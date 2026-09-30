#!/usr/bin/env python3
"""Bank feasibility check for per-STRING codeword blocks (string s uses
codes 200*s .. 200*s+199 instead of all strings sharing codes 0..N-1).

Verified here (never assume):
- 1600 distinct codewords, weight exactly 9 of 18.
- Per 200-block: min pairwise distance; per-plane ON count == 100
  (needed: 50% duty per plane per string, and the plane ON-count gate
  N/2-per-block must stay exact).
- Cross-block min distance for the block pairing we'd actually use
  (misattribution risk when a corrupted read is judged against the
  whole universe).

Usage: venv python3 cwc_block_check.py [--bank tools/codewords_9of18.json]
"""
import json, sys
from itertools import combinations
from pathlib import Path

run = Path(__file__).resolve().parent / 'runs' / 'cwc_bank_check.txt'
bank_f = Path(sys.argv[sys.argv.index('--bank') + 1]) if '--bank' in sys.argv else \
    Path(__file__).resolve().parent / 'codewords_9of18.json'
bank = json.load(open(bank_f))
codes = bank['codes'] if isinstance(bank, dict) else bank
N = len(codes)

def D(a, b):
    return len(set(a) ^ set(b))

print(f'bank: {N} codes, C(18,9)=48620')
print('distinct:', len({tuple(c) for c in codes}) == N)
print('weight all 9:', all(len(c) == 9 for c in codes))
print('planes 0..17 used:', sorted({p for c in codes for p in c}) == list(range(18)))

print('\nper-200-block:')
dmins = []
for b in range(N // 200):
    lo, hi = b * 200, b * 200 + 200
    dmin = min(D(codes[i], codes[j]) for i, j in combinations(range(lo, hi), 2))
    counts = [sum(1 for c in codes[lo:hi] if p in c) for p in range(18)]
    ok = min(counts) == max(counts) == 100
    print(f'  block {b}: d_min {dmin}, per-plane ON {min(counts)}-{max(counts)} '
          f'({"50% exact" if ok else "UNBALANCED!"})')
    dmins.append(dmin)

# first-150 prefixes (bench-string case, one 150-LED string per block)
print('\nper-150-prefix d_min (strings of 150):')
for b in range(N // 200):
    lo, hi = b * 200, b * 200 + 150
    dmin = min(D(codes[i], codes[j]) for i, j in combinations(range(lo, hi), 2))
    counts = [sum(1 for c in codes[lo:hi] if p in c) for p in range(18)]
    print(f'  block {b} codes {lo}-{hi}: d_min {dmin}, per-plane ON {min(counts)}')

# cross-block distance (a string-2 site misread toward a string-1 codeword)
print('\ncross-block min distance (blocks 0 vs 1, 150-prefixes):')
dmin = min(D(codes[i], codes[j])
           for i in range(0, 150) for j in range(200, 350))
print(f'  block0<->block2 prefix min d: {dmin}')