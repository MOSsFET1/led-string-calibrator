#!/usr/bin/env python3
"""S14R-0003 build-chain verifier: the /cal battery page blob == gzipped
page/cal.html byte-exact + length + stamp agreement (mirror of
verify_embed_s14r.py for the SECOND asset, additive only)."""
import gzip, base64, re
from pathlib import Path
base = Path(__file__).resolve().parents[1]
ino = (base / 'firmware/poc_survey/poc_survey.ino').read_text()
html = (base / 'page/cal.html').read_bytes()
block = ino.split('CAL_GZ_B64[] =', 1)[1].split(';\n', 1)[0]
raw = base64.b64decode(''.join(re.findall(r'"([^"]+)"', block)))
gz = gzip.compress(html, 9)
mlen = re.search(r'CAL_GZ_LEN = (\d+)', ino)
print('cal embed roundtrip byte-exact (decompressed blob == page):', gzip.decompress(raw) == html)
print('CAL_GZ_LEN:', mlen.group(1) if mlen else '?', 'actual fresh gzip:', len(gz))
served = gzip.decompress(raw).decode()
print('served stamp S14R-0003-CAL:', 'S14R-0003-CAL' in served)
print('build stamps agree (page BUILD == sketch CAL_BUILD):',
      'const BUILD = "S14R-0003-CAL"' in served and
      re.search(r'CAL_BUILD\[\] = "S14R-0003-CAL"', ino) is not None)
print('cal banner present (CAL MODE):', 'CAL MODE' in served)
print('battery labels present (cal:e / cal:idle / cwc:r):',
      all(k in served for k in ('cal:e', 'cal:idle', "'cwc:r' + benchRunNo")))
print('transport copied not linked (frameBitsPaint body present):',
      'function frameBitsPaint(bitsOrNull, b, epoch)' in served)
print('survey page blob untouched (PAGE_GZ still 47408):', 'PAGE_GZ_LEN = 47408' in ino)
ok = (gzip.decompress(raw) == html and
      mlen and int(mlen.group(1)) == len(raw) and
      'const BUILD = "S14R-0003-CAL"' in served)
print('PASS' if ok else 'FAIL')
raise SystemExit(0 if ok else 1)
