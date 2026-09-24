#!/usr/bin/env python3
"""Embed cert.pem/key.pem into poc_survey.ino as C++ RAW string literals
(R"PEM(...)PEM") — no escape sequences at all, so no transport can mangle
them. Run from anywhere: python3 embed_cert.py"""
import re, sys
from pathlib import Path

d = Path(__file__).resolve().parent.parent / "poc_survey"
ino = d / "poc_survey.ino"
cert = (d / "cert.pem").read_text().strip()
key = (d / "key.pem").read_text().strip()
txt = ino.read_text()

new, n1 = re.subn(r"static const char CERT_PEM\[\] =.*?;\n",
                  'static const char CERT_PEM[] = R"PEM(\n' + cert + '\n)PEM";\n',
                  txt, flags=re.S, count=1)
new, n2 = re.subn(r"static const char KEY_PEM\[\] =.*?;\n",
                  'static const char KEY_PEM[] = R"PEM(\n' + key + '\n)PEM";\n',
                  new, flags=re.S, count=1)
if n1 != 1 or n2 != 1:
    sys.exit(f"FAILED: cert replaced {n1}, key replaced {n2} (want 1/1)")
ino.write_text(new)

# verification written to a file: stdout channels have eaten backslashes before
t = ino.read_text()
raw_starts = t.count('R"PEM(')
begin_lines = sum(1 for l in t.split('\n') if l.strip().startswith('"-----BEGIN'))
real_nl_after_begin = t.count('-----BEGIN CERTIFICATE-----\n') + t.count('-----BEGIN PRIVATE KEY-----\n')
report = (
    f"raw literals: {raw_starts} (want 2)\n"
    f"BEGIN lines: {begin_lines}\n"
    f"PEM newline-terminated blocks: {real_nl_after_begin} (want 2)\n"
)
Path(d.parent / 'embed_report.txt').write_text(report)
print(report)