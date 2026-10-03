#!/usr/bin/env python3
"""Pack the CAL battery page (page/cal.html) as a compile-time flash asset.

gzip -9 -> C byte array in firmware/poc_survey/cal_page_gz.h (CAL_GZ, served
at /cal from flash, Content-Encoding: gzip). Additive S14R-0003 route; the
survey page packing (pack_page.py) is untouched. Replaces the OLD pipeline
(gzip -> base64 -> CAL_GZ_B64 in the .ino -> lazy decodeB64() per /cal
request + free), which lost the fragmentation lottery on the SECOND /cal
request (no contiguous 25.8 KB block -> 500 "page not loaded").
Byte-identity with the previously served blob is preserved: MTIME carried.

Usage: python3 pack_cal_page.py [html]   (default ../page/cal.html relative
to this script's location)
"""
import re, sys
from pathlib import Path

FW = Path(__file__).resolve().parents[1] / "poc_survey" / "poc_survey.ino"
HDR = Path(__file__).resolve().parents[1] / "poc_survey" / "cal_page_gz.h"
HTML = Path(__file__).resolve().parents[2] / "page" / "cal.html"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import page_assets

html_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HTML
ino_txt = FW.read_text() if FW.exists() else ""

text, n_html, n_gz, carried = page_assets.pack_flash_header(
    html_path, HDR, "CAL_GZ", ino_text=ino_txt, legacy_marker="CAL_GZ_B64")
HDR.write_text(text)

# keep the sketch's CAL_BUILD in sync with the cal page's BUILD
html_txt = Path(html_path).read_text(errors="replace")
mb = re.search(r'const BUILD = "([^"]*)"', html_txt)
if mb is None:
    sys.exit("FAILED: cal page BUILD constant not found in html")
new = FW.read_text()
new, n3 = re.subn(r'(CAL_BUILD\[\]\s*=\s*")[^"]*(")',
                  lambda m: m.group(1) + mb.group(1) + m.group(2), new)
if n3 != 1:
    sys.exit(f"FAILED: CAL_BUILD replaced {n3} times, want 1")
FW.write_text(new)

print(f"packed {html_path.name} -> cal_page_gz.h: {n_html} B html -> {n_gz} B gz "
      f"flash array (CAL_GZ_LEN={n_gz}, MTIME {'carried' if carried else 'now'}, "
      f"CAL_BUILD={mb.group(1)})")
