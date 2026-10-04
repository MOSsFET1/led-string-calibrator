#!/usr/bin/env python3
"""Final report inputs: triple unions, per-string counts, gate/cap hygiene."""
import json
from pathlib import Path

OUTD = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/'
            'analysis/e4_union')
raw = json.loads((OUTD / 'e4_union_raw.json').read_text())
TAGS = ['r8', 'r9', 'r10', 'r11']
LS = {'r8': 80, 'r9': 100, 'r10': 120, 'r11': 150}
sets = {t: {q['led'] for q in raw[t]['leds']} for t in TAGS}
import itertools as it
print('triples:')
for tri in it.combinations(TAGS, 3):
    u = len(set.union(*[sets[t] for t in tri]))
    print('  ' + '+'.join(f'{t}(L{LS[t]})' for t in tri), '=', u)
print('best triples:', max(
    (len(set.union(*[sets[t] for t in tri])), tri)
    for tri in it.combinations(TAGS, 3)))
for t in TAGS:
    leds = raw[t]['leds']
    s = {'s1': 0, 's2': 0, 's3': 0}
    for q in leds:
        s[f"s{q['led'] // 200 + 1}"] += 1
    print(f"{t}: L{LS[t]} strings {s} maxid {max(q['led'] for q in leds)} "
          f"minamp {min(q['amp'] for q in leds)} "
          f"minmargin {min(q['margin'] for q in leds)}")
# amp-headroom on union-never ids at their best-L anchor: predicted amp by law
import numpy as np
U4 = set.union(*sets.values())
print('union4', len(U4), 'never-any-L', 600 - len(U4))
# per-id best amp across L, distribution on U4 \ best-single
only_r9 = sets['r9'] - set.union(*[sets[t] for t in TAGS if t != 'r9'])
only_union = U4 - sets['r9']
print('ids rescued BEYOND best-single L100:', len(only_union),
      'of which L120-only:', len(only_union - sets['r11'] - sets['r8']),
      'L150-only:', len(only_union - sets['r10'] - sets['r8']),
      'L80-only:', len(only_union - sets['r10'] - sets['r11']))