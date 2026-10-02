#!/usr/bin/env python3
"""S14R build-chain verifier: embedded page blob == gzipped page byte-exact.
Adapted from verify_embed_1928.py (kept for history); stamps S14R-0001."""
import gzip, base64, re
from pathlib import Path
base = Path(__file__).resolve().parents[1]
ino = (base / 'firmware/poc_survey/poc_survey.ino').read_text()
html = (base / 'page/survey.html').read_bytes()
block = ino.split('PAGE_GZ_B64[] =', 1)[1].split(';\n', 1)[0]
raw = base64.b64decode(''.join(re.findall(r'"([^"]+)"', block)))
gz = gzip.compress(html, 9)
mlen = re.search(r'PAGE_GZ_LEN = (\d+)', ino)
# NOTE: gzip.compress embeds a wall-clock MTIME in the header, so raw == fresh-gz
# is only true within the same second. The real invariants: lossless roundtrip
# (decompressed blob == page bytes) + stored length == fresh gzip length.
print('embed roundtrip byte-exact (decompressed blob == page):', gzip.decompress(raw) == html)
print('PAGE_GZ_LEN:', mlen.group(1) if mlen else '?', 'actual fresh gzip:', len(gz))
served = gzip.decompress(raw).decode()
print('served stamp S14R-0001:', 'S14R-0001' in served)
print('served ids present (S14P-1928 kept):',
      'chkCapOnly' in served and 'btnSendFrames' in served)
print('served S14R-0001 UI ids present:',
      all(f'bStr{i}' in served for i in range(1, 9)) and 'burstRow' in served)
print('served Survey button removed:', "id=\"bSurvey\"" not in served)
print('served wake button removed:', "id=\"bWake\"" not in served)
print('served fixed close present:', 'end non-capture CWC decode branch' in served)
print('served bank is 12OF24:', 'window.CWC_CODES_12OF24 = [' in served)
print('served bank retired (no 9OF18):', 'CWC_CODES_9OF18' not in served)