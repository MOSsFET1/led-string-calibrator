#!/usr/bin/env python3
"""Inventory: counts, labels, plain-decode health, byte sizes."""
import json
import sys
from pathlib import Path

from PIL import Image

RUNS = Path('/home/nellie/projects/led-display/poc_survey/runs/daemon/runs')
DIRS = ['run0', 'run8', 'run9', 'run10', 'run11']

import collections
for d in DIRS:
    p = RUNS / d
    jpgs = sorted(p.glob('*.jpg'))
    pats = collections.Counter()
    for j in jpgs:
        n = j.name
        if n.startswith('cal_idle'):
            pats['cal_idle'] += 1
        elif n.startswith('cal_E'):
            pats[n.rsplit('_', 1)[0]] += 1
        elif n.startswith('cwc'):
            pats[n.rsplit('_p', 1)[0]] += 1
    print(d, len(jpgs), 'jpg')
    for k in sorted(pats):
        print('   ', k, pats[k])
    # meta label sample
    labs = collections.Counter(json.loads(
        (j.with_name(j.stem + '.meta.json')).read_text())['label'].split(':')[0]
        for j in jpgs)
    print('   label prefixes:', dict(labs))