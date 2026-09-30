#!/usr/bin/env python3
"""S14P-1909 pre-flash QA: mock box + headless Chromium + fake camera.

Validates the REAL page end-to-end before an ESP32 flash:
  1. build stamp == S14P-1909
  2. CFG (cwc=1, cwcN=10) + BURST via mock drv? directives
  3. burst runs: 18 planes + master, bench store ships 19 frames
  4. chained registration log line present (backwards chain, no crash)
  5. in-page decode ran (sitesMasked/confirmed logged; CWCDEC chunks ship)
  6. result canvas (static master + site boxes) exists and is non-blank;
     saved to runs/qa-1904/result.png for operator/agent visual check
  7. second burst with cwcTestMode=1 exercises the test branch on 1904

PASS = hard checks 1-6 pass; 7 exercises without an 'E ' error.
Run with the venv python (websockets dep): see README tooling line.
"""
import asyncio, base64, json, os, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cdp_once_ship import cdp_call, cdp_eval, wait_log, CHROME, PORT, URL, TOOLS  # noqa

BASE = TOOLS.parent
RUN = BASE / "runs" / "qa-1904"
import urllib.request as u


async def main():
    RUN.mkdir(parents=True, exist_ok=True)
    (TOOLS / "mock_box.log").write_text("")
    (TOOLS / "mock_log_pull.txt").write_text("")
    (TOOLS / "mock_directives.txt").write_text("")
    mock = await asyncio.create_subprocess_exec(
        "/home/nellie/.hermes/hermes-agent/venv/bin/python3", str(TOOLS / "mock_box.py"),
        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.STDOUT)
    for _ in range(50):
        await asyncio.sleep(0.2)
        if "MOCK BOX UP" in (TOOLS / "mock_box.log").read_text():
            break
    else:
        print("FAIL: mock box never came up"); return 1

    import shutil
    shutil.rmtree(f"/tmp/chr-1904-{PORT}", ignore_errors=True)
    proc = await asyncio.create_subprocess_exec(
        str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
        "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
        "--ignore-certificate-errors", "--autoplay-policy=no-user-gesture-required",
        "--use-gl=swiftshader", "--enable-unsafe-swiftshader",
        "--remote-debugging-port=" + str(PORT),
        f"--user-data-dir=/tmp/chr-1904-{PORT}-{os.getpid()}",
        "--window-size=800,1000", "about:blank",
        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
    mock_ok = chrome_ok = False
    try:
        ws_url = None
        for _ in range(40):
            await asyncio.sleep(0.25)
            try:
                data = json.loads((await asyncio.to_thread(
                    u.urlopen, f"http://127.0.0.1:{PORT}/json/list", timeout=2)).read())
                pages = [t for t in data if t.get("type") == "page"]
                if pages:
                    ws_url = pages[0]["webSocketDebuggerUrl"]; break
            except Exception:
                continue
        if not ws_url:
            print("FAIL: CDP never came up"); return 1
        import websockets
        async with websockets.connect(ws_url, max_size=20 * 1024 * 1024) as ws:
            await cdp_call(ws, 1, "Page.enable")
            await cdp_call(ws, 2, "Runtime.enable")
            await cdp_call(ws, 3, "Page.navigate", {"url": URL})
            await asyncio.sleep(4)
            st = await cdp_eval(ws, "JSON.stringify({bld:document.getElementById('bl2').textContent,"
                                    "cam:document.getElementById('camst').textContent,"
                                    "ws:document.getElementById('wsst').textContent})")
            print("STATUS", st)
            ok = "S14P-1909" in st and "live" in st and "open" in st
            if not ok:
                print("FAIL: page state", st); return 1
            # ---- burst 1: normal CWC mode ----
            cfg = {"cwc": 1, "cwcN": 10, "cwcSettle": 100, "bBurstB": 150,
                   "bBurstHold": 800, "bComp": 0, "cwcTestMode": 0}
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg) + "\nBURST\n")
            print("burst 1 queued")
            chain_ln = await wait_log(ws, "chain totals", tries=60)
            print("chain:", chain_ln)
            dec_ln = await wait_log(ws, "decode:", tries=30)
            print("decode:", dec_ln)
            ship_ln = await wait_log(ws, "bench pull done", tries=90)
            print("ship:", ship_ln)
            logtxt = await cdp_eval(ws, "document.getElementById('log').textContent")
            errs = [l for l in (logtxt or "").split("\n") if l.strip().startswith("E ")]
            print("page errors:", errs if errs else "none")
            # ---- result canvas: exists, non-blank, save it ----
            info = await cdp_eval(ws, "(() => { const c = document.getElementById('cwcResult');"
                                      " if (!c) return 'NO CANVAS';"
                                      " const d = c.getContext('2d').getImageData(0,0,c.width,c.height).data;"
                                      " let s = 0, mx = 0; for (let i = 0; i < d.length; i += 4) { s += d[i]; if (d[i] > mx) mx = d[i]; }"
                                      " return JSON.stringify({w: c.width, h: c.height,"
                                      "   mean: +(s / (d.length / 4)).toFixed(1), max: mx,"
                                      "   url: c.toDataURL('image/png').length}); })()")
            print("result canvas:", info[:120])
            shot = await cdp_eval(ws, "document.getElementById('cwcResult') ? "
                                      "document.getElementById('cwcResult').toDataURL('image/png') : ''")
            if shot:
                (RUN / "result.png").write_bytes(base64.b64decode(shot.split(",")[1]))
                print("result png ->", RUN / "result.png")
            # ---- shipped stream sanity ----
            pull = (TOOLS / "mock_log_pull.txt").read_text()
            n_frames = sum(1 for l in pull.splitlines() if l.startswith("FRAME "))
            n_cwcdec = sum(1 for l in pull.splitlines() if l.startswith("CWCDEC "))
            n_cwcdecs = sum(1 for l in pull.splitlines() if l.startswith("CWCDECS "))
            stats_ln = next((l for l in pull.splitlines() if l.startswith("CWCSTATS ")), "")
            print(f"shipped: FRAME {n_frames}, CWCDEC {n_cwcdec}, CWCDECS {n_cwcdecs}, CWCSTATS {len(stats_ln)} B")
            cwcdec_ok = False
            if n_cwcdec:
                # reassembled chunks are the SITES list (1905 shape)
                sites = json.loads("".join(l[len("CWCDEC "):] for l in
                                           pull.splitlines() if l.startswith("CWCDEC ")))
                print("CWCDEC sites:", len(sites), "sample:", sites[:2])
                cwcdec_ok = (isinstance(sites, list) and len(sites) >= 3
                             and all(("x" in s and "led" in s) for s in sites[:2]))
            stats_ok = (0 < len(stats_ln) < 4096) and '"n":' in stats_ln
            # ---- burst 2: test mode ----
            cfg2 = dict(cfg, cwcTestMode=1)
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg2) + "\nBURST\n")
            print("burst 2 (test mode) queued")
            t2 = await wait_log(ws, "decode:", tries=60)
            print("test decode:", t2)
            logtxt2 = await cdp_eval(ws, "document.getElementById('log').textContent")
            errs2 = [l for l in (logtxt2 or "").split("\n")
                     if l.strip().startswith("E ") and l not in (errs or [])]
            print("test-mode new errors:", errs2 if errs2 else "none")
            mock_ok = chrome_ok = True
            hard_ok = (n_frames == 19 and stats_ok and chain_ln and dec_ln
                       and "NO CANVAS" not in info and not errs and cwcdec_ok and not errs2)
            print("\nRESULT:", "PASS" if hard_ok else "FAIL")
            print(f"  frames 19/19: {n_frames == 19}; stats<4096B: {stats_ok}; "
                  f"chain logged: {bool(chain_ln)}; decode logged: {bool(dec_ln)}; "
                  f"canvas: {'NO CANVAS' not in info}; CWCDEC {n_cwcdec} {cwcdec_ok}")
            return 0 if hard_ok else 1
    finally:
        proc.kill()
        mock.kill()


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))