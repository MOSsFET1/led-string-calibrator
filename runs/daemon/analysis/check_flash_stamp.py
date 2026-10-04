#!/usr/bin/env python3
"""Prove which CAL page the build artifact + the live box actually carry."""
import gzip, re, sys
from pathlib import Path

REPO = Path('/home/nellie/projects/led-display/poc_survey')
hdr = (REPO / 'firmware/poc_survey/cal_page_gz.h').read_text()
blob = re.search(r'CAL_GZ\[\]\s*PROGMEM\s*=\s*\{(.*?)\};', hdr, re.S).group(1)
gz = bytes(int(b, 16) for b in re.findall(r'0x([0-9a-f]{2})', blob))
html = gzip.decompress(gz).decode('utf-8', errors='replace')
m = re.search(r'const BUILD = "([^"]*)"', html)
print('packaged header (cal_page_gz.h) serves page BUILD =', m.group(1) if m else 'NOT FOUND')
ino = (REPO / 'firmware/poc_survey/poc_survey.ino').read_text()
print('.ino CAL_BUILD =', re.search(r'CAL_BUILD\[\]\s*=\s*"([^"]*)"', ino).group(1))
# compile artifacts: the .bin that upload actually flashed
for binpath in sorted(Path.home().glob('.cache/arduino/sketches/*/poc_survey.ino.bin')):
    data = binpath.read_bytes()
    stamp = b'S14R-0003E-CAL' in data, b'S14R-0003D-CAL' in data
    print(binpath, 'size', len(data), 'contains E-stamp:', stamp[0], 'D-stamp:', stamp[1])