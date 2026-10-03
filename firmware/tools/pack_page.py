#!/usr/bin/env python3
"""Pack the survey page (page/survey.html) as a compile-time flash asset.

gzip -9 -> C byte array in firmware/poc_survey/page_gz.h (PAGE_GZ, served
from flash with Content-Encoding: gzip). Replaces the OLD pipeline (gzip ->
base64 -> PAGE_GZ_B64 in the .ino -> decodeB64() into malloc'd RAM at boot),
which held ~47 KB of heap and contributed to the mbedtls -0x7F00 wedge.
Byte-identity with the previously served blob is preserved: MTIME carried.

Usage: python3 pack_page.py [html]   (default: ../page/survey.html relative
to this script's location)
"""
import re, sys
from pathlib import Path

FW = Path(__file__).resolve().parents[1] / "poc_survey" / "poc_survey.ino"
HDR = Path(__file__).resolve().parents[1] / "poc_survey" / "page_gz.h"
HTML = Path(__file__).resolve().parents[2] / "page" / "survey.html"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import page_assets

html_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HTML
ino_txt = FW.read_text() if FW.exists() else ""

text, n_html, n_gz, carried = page_assets.pack_flash_header(
    html_path, HDR, "PAGE_GZ", ino_text=ino_txt, legacy_marker="PAGE_GZ_B64")
HDR.write_text(text)

# keep the sketch's PAGE_BUILD in sync with the page's BUILD so the status
# LED (red = page stale, green/breathe = up to date) tracks automatically.
html_txt = Path(html_path).read_text(errors="replace")
mb = re.search(r'const BUILD = "([^"]*)"', html_txt)
if mb is None:
    sys.exit("FAILED: page BUILD constant not found in html")
new = FW.read_text()
new, n3 = re.subn(r'(PAGE_BUILD\[\]\s*=\s*")[^"]*(")',
                  lambda m: m.group(1) + mb.group(1) + m.group(2), new)
if n3 != 1:
    sys.exit(f"FAILED: PAGE_BUILD replaced {n3} times, want 1")
FW.write_text(new)

print(f"packed {html_path.name} -> page_gz.h: {n_html} B html -> {n_gz} B gz "
      f"flash array (PAGE_GZ_LEN={n_gz}, MTIME {'carried' if carried else 'now'}, "
      f"PAGE_BUILD={mb.group(1)})")
