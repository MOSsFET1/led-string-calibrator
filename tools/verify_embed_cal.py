#!/usr/bin/env python3
"""S14R-0003 build-chain verifier: the /cal battery page asset == gzipped
page/cal.html byte-exact + length + stamp agreement (mirror of
verify_embed_s14r.py for the SECOND asset, additive only).

Flash-asset era: CAL_GZ is a compile-time C byte array in
firmware/poc_survey/cal_page_gz.h (packed by pack_cal_page.py), replacing
the old CAL_GZ_B64 base64 string + lazy malloc decode per /cal request.
"""
import gzip, re, sys
from pathlib import Path
base = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "firmware" / "tools"))
from page_assets import array_from_header, header_len

ino = (base / 'firmware/poc_survey/poc_survey.ino').read_text()
hdr = (base / 'firmware/poc_survey/cal_page_gz.h').read_text()
html = (base / 'page/cal.html').read_bytes()
raw = array_from_header(hdr, 'CAL_GZ')
gz = gzip.compress(html, 9)
mlen = header_len(hdr, 'CAL_GZ')
print('cal embed roundtrip byte-exact (decompressed array == page):', gzip.decompress(raw) == html)
print('CAL_GZ_LEN:', mlen if mlen is not None else '?', 'actual fresh gzip:', len(gz))
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
print('survey page asset untouched (PAGE_GZ still 47408):', 'PAGE_GZ_LEN = 47408' in
      (base / 'firmware/poc_survey/page_gz.h').read_text())
print('sketch serves the flash array directly (no B64 consts, no RAM bufs):',
      'gz_page_send(req, CAL_GZ, CAL_GZ_LEN)' in ino and
      'CAL_GZ_B64' not in ino and 'uint8_t* calGz' not in ino)
print('sketch includes the cal_page_gz.h header:', '#include "cal_page_gz.h"' in ino)
ok = (gzip.decompress(raw) == html and
      mlen is not None and mlen == len(raw) and
      'const BUILD = "S14R-0003-CAL"' in served)
print('PASS' if ok else 'FAIL')
raise SystemExit(0 if ok else 1)
