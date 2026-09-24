#!/usr/bin/env python3
"""Extract the embedded CERT/KEY raw literals from poc_survey.ino and verify
they parse with openssl — proves the bytes the box will serve are intact."""
import re, subprocess
from pathlib import Path

d = Path(__file__).resolve().parents[1] / "firmware" / "poc_survey"
txt = (d / "poc_survey.ino").read_text()

cert_m = re.search(r'R"PEM\((.*?)\)PEM"', txt[txt.index("CERT_PEM"):], re.S)
key_m = re.search(r'R"PEM\((.*?)\)PEM"', txt[txt.index("KEY_PEM"):], re.S)
(d / "extract_cert.pem").write_text(cert_m.group(1))
(d / "extract_key.pem").write_text(key_m.group(1))

r1 = subprocess.run(["openssl", "x509", "-in", str(d / "extract_cert.pem"), "-noout", "-subject", "-dates"],
                    capture_output=True, text=True)
r2 = subprocess.run(["openssl", "pkey", "-in", str(d / "extract_key.pem"), "-noout"],
                    capture_output=True, text=True)
r3 = subprocess.run(["openssl", "x509", "-in", str(d / "extract_cert.pem"), "-noout", "-pubkey"], capture_output=True, text=True)
pub_from_cert = r3.stdout.strip() if r3.returncode == 0 else ""
r4 = subprocess.run(["openssl", "pkey", "-in", str(d / "extract_key.pem"), "-pubout"], capture_output=True, text=True)
pub_from_key = r4.stdout.strip() if r4.returncode == 0 else ""

rep = [
    f"cert parse: rc={r1.returncode} {r1.stdout.strip() or r1.stderr.strip()}",
    f"key parse: rc={r2.returncode} {r2.stdout.strip() or r2.stderr.strip()}",
    f"cert/key MATCH: {pub_from_cert != '' and pub_from_cert == pub_from_key}",
]
Path(d.parent / "extract_report.txt").write_text("\n".join(rep) + "\n")
print("done")