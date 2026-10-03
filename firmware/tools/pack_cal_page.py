#!/usr/bin/env python3
"""Pack the CAL battery page into poc_survey.ino (S14R-0003, additive).

gzip -9 -> base64 (76-char quoted lines) -> replace the CAL_GZ_B64 body and
CAL_GZ_LEN in the sketch, and sync CAL_BUILD with page/cal.html's BUILD const.
Mirror of pack_page.py for the SECOND served route (/cal); the survey page
packing (pack_page.py) is untouched.

Usage: python3 pack_cal_page.py [html]   (default ../page/cal.html relative to
this script's location)
"""
import base64, gzip, re, sys
from pathlib import Path

FW = Path(__file__).resolve().parents[1] / "poc_survey" / "poc_survey.ino"
HTML = Path(__file__).resolve().parents[2] / "page" / "cal.html"

html_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HTML
html = html_path.read_bytes()
gz = gzip.compress(html, 9)
b64 = base64.b64encode(gz).decode()

lines = [b64[i:i+76] for i in range(0, len(b64), 76)]
block = "\n".join(f'  "{ln}"' for ln in lines)

src = FW.read_text()
new, n1 = re.subn(r'(CAL_GZ_B64\[\]\s*=).*?\n;',
                  lambda m: f'{m.group(1)}\n{block}\n\n;',
                  src, flags=re.S)
if n1 != 1:
    sys.exit(f"FAILED: CAL_GZ_B64 replaced {n1} times, want 1")
new, n2 = re.subn(r'(CAL_GZ_LEN\s*=\s*)\d+', lambda m: f'{m.group(1)}{len(gz)}', new)
if n2 != 1:
    sys.exit(f"FAILED: CAL_GZ_LEN replaced {n2} times, want 1")
# keep the sketch's CAL_BUILD in sync with the cal page's BUILD
mb = re.search(r'const BUILD = "([^"]*)"', html.decode(errors="replace"))
if mb is None:
    sys.exit("FAILED: cal page BUILD constant not found in html")
new, n3 = re.subn(r'(CAL_BUILD\[\]\s*=\s*")[^"]*(")',
                  lambda m: f'{m.group(1)}{mb.group(1)}{m.group(2)}', new)
if n3 != 1:
    sys.exit(f"FAILED: CAL_BUILD replaced {n3} times, want 1")
FW.write_text(new)

print(f"packed {html_path.name}: {len(html)} B -> {len(gz)} B gz -> {len(b64)} B b64 "
      f"({len(lines)} lines, delta {len(new)-len(src):+d} ino bytes, CAL_BUILD={mb.group(1)})")