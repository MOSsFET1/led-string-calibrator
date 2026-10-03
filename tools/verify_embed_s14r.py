#!/usr/bin/env python3
"""S14R build-chain verifier: embedded page asset == gzipped page byte-exact.
Adapted from verify_embed_1928.py (kept for history); stamps S14R-0002.

Flash-asset era: PAGE_GZ is a compile-time C byte array in
firmware/poc_survey/page_gz.h (packed by pack_page.py), replacing the old
PAGE_GZ_B64 base64 string + malloc-RAM decode. All served-gz byte invariants
are unchanged: decompressed array == page/survey.html bytes, header LEN
matches, BUILD stamps present, served UI ids intact.
"""
import gzip, re, sys
from pathlib import Path
base = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "firmware" / "tools"))
from page_assets import array_from_header, header_len

ino = (base / 'firmware/poc_survey/poc_survey.ino').read_text()
hdr = (base / 'firmware/poc_survey/page_gz.h').read_text()
html = (base / 'page/survey.html').read_bytes()
raw = array_from_header(hdr, 'PAGE_GZ')
gz = gzip.compress(html, 9)
mlen = header_len(hdr, 'PAGE_GZ')
# NOTE: gzip.compress embeds a wall-clock MTIME in the header, so raw == fresh-gz
# is only true within the same second. The real invariants: lossless roundtrip
# (decompressed array == page bytes) + stored length == fresh gzip length.
print('embed roundtrip byte-exact (decompressed array == page):', gzip.decompress(raw) == html)
print('PAGE_GZ_LEN:', mlen if mlen is not None else '?', 'actual fresh gzip:', len(gz))
served = gzip.decompress(raw).decode()
print('served stamp S14R-0002:', 'S14R-0002' in served)
print('build stamps agree (page BUILD == sketch PAGE_BUILD):',
      'const BUILD = "S14R-0002"' in served and
      re.search(r'PAGE_BUILD\[\] = "S14R-0002"', ino) is not None)
print('no stale BUILD constant (BUILD is 0002, 0001 only in history comments):',
      'const BUILD = "S14R-0002"' in served and 'const BUILD = "S14R-0001"' not in served)
print('served ids present (S14P-1928 kept):',
      'chkCapOnly' in served and 'btnSendFrames' in served)
print('served S14R-0001 UI ids present (kept):',
      all(f'bStr{i}' in served for i in range(1, 9)) and 'burstRow' in served)
print('served S14R-0002 probe ids present:',
      all(k in served for k in ('bProbe', 'waitSettleMs', 'probeSteps', 'brightnessProbe',
                                'probeMeasure', 'coreP90')))
print('served S14R-0002 adaptive mask present:', 'cwcMaskAdaptive' in served and 'cwcMaskK' in served)
print('served BSTATS probe keys present:', served.count('bright: probeRes') == 2 and
      'probeIters: probeRes' in served and 'clipPct: probeRes' in served)
print('served iOS-no-evBias gate present:', 'isIOS' in served and
      'iOS gate: exposureCompensation was never exposed' in served)
print('served probe directive handled:', "'PROBE'" in served)
print('served label chip fill removed:', 'rgba(0,0,0,0.3)' not in served)
print('served Survey button removed:', 'id="bSurvey"' not in served)
print('served wake button removed:', 'id="bWake"' not in served)
print('served fixed close present:', 'end non-capture CWC decode branch' in served)
print('served bank is 12OF24:', 'window.CWC_CODES_12OF24 = [' in served)
print('served bank retired (no 9OF18):', 'CWC_CODES_9OF18' not in served)
# flash-asset wiring checks (replaces the old base64-string presence checks)
print('sketch serves the flash array directly (no B64 consts, no RAM bufs):',
      'gz_page_send(req, PAGE_GZ, PAGE_GZ_LEN)' in ino and
      'PAGE_GZ_B64' not in ino and 'uint8_t* pageGz' not in ino)
print('sketch includes the page_gz.h header:', '#include "page_gz.h"' in ino)
print('header array length matches header LEN:', len(raw) == (mlen if mlen else -1))
