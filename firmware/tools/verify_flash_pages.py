#!/usr/bin/env python3
"""Standalone freshness checker for the flash page-asset headers.

Regenerates firmware/poc_survey/page_gz.h + cal_page_gz.h from the live
page/survey.html + page/cal.html in a temp dir (deterministic gzip -9, MTIME
carried from the committed headers) and file-diffs them against the
committed headers. rc 0 = in-repo headers are byte-current for the current
html pages; rc 1 = a header is stale (re-run pack_page.py / pack_cal_page.py
or commit the regenerated headers).

Also asserts the .ino still serves the arrays directly (no leftover
base64/decode plumbing).

Usage: python3 firmware/tools/verify_flash_pages.py
Plain python3, no deps (uses only the shared page_assets.py next to it).
"""
import subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent           # firmware/tools
sys.path.insert(0, str(HERE))
import page_assets

ROOT = HERE.parents[1]
FW = ROOT / "firmware" / "poc_survey"

TARGETS = [
    ("PAGE_GZ", "page/survey.html", FW / "page_gz.h", "pack_page.py"),
    ("CAL_GZ",  "page/cal.html",    FW / "cal_page_gz.h", "pack_cal_page.py"),
]


def main() -> int:
    ok = True
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for name, html_rel, hdr_path, packer in TARGETS:
            html = ROOT / html_rel
            if not html.exists() or not hdr_path.exists():
                print(f"FAIL {name}: missing {html} or {hdr_path}")
                ok = False
                continue
            text, n_html, n_gz, carried = page_assets.pack_flash_header(
                html, hdr_path, name, ino_text="", legacy_marker=None)
            regen = tmp / hdr_path.name
            regen.write_text(text)
            r = subprocess.run(["git", "-C", str(ROOT), "diff", "--no-index", "--stat",
                                "--", str(regen), str(hdr_path)],
                               capture_output=True, text=True)
            byte_equal = regen.read_bytes() == hdr_path.read_bytes()
            status = "CURRENT" if byte_equal else "STALE"
            print(f"{name:8s} {hdr_path.relative_to(ROOT)}: {status} "
                  f"({n_html} B html -> {n_gz} B gz, MTIME "
                  f"{'carried from committed header' if carried is not None else 'now (no prior asset decompresses equal)'})")
            if not byte_equal:
                ok = False
                d = subprocess.run(["git", "-C", str(ROOT), "diff", "--no-index", "--",
                                    str(regen), str(hdr_path)], capture_output=True, text=True)
                head = "\n".join(d.stdout.splitlines()[:14])
                print("   diff head (regen vs committed):")
                for ln in head.splitlines():
                    print("    ", ln)
    # .ino wiring: headers served directly, no base64/decode remnants
    ino = (FW / "poc_survey.ino").read_text()
    wiring_ok = (
        '#include "page_gz.h"' in ino and '#include "cal_page_gz.h"' in ino
        and "gz_page_send(req, PAGE_GZ, PAGE_GZ_LEN)" in ino
        and "gz_page_send(req, CAL_GZ, CAL_GZ_LEN)" in ino
        and "PAGE_GZ_B64" not in ino and "CAL_GZ_B64" not in ino
        and "uint8_t* pageGz" not in ino and "uint8_t* calGz" not in ino
        and "= decodeB64(" not in ino
    )
    print("ino wiring (flash arrays served directly, no decode path):",
          "OK" if wiring_ok else "BROKEN")
    ok = ok and wiring_ok
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
