#!/usr/bin/env python3
"""CDP drive for the S14P single-LED toggle TEST MODE (no phone): mock box +
headless Chromium fake camera. Validates the page's test-mode end to end:
19 frames captured (18 planes + master), run-tagged labels, auto-ship
(benchPull) emits CWCSTATS + FRAME/FJPEG/FEND into mock_log_pull.txt.

Usage: /home/nellie/.hermes/hermes-agent/venv/bin/python3 cdp_test_mode.py
"""
import asyncio, json, sys
from pathlib import Path
import websockets

BASE = Path(__file__).resolve().parents[1]
TOOLS = BASE / "tools"
CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9336
URL = "https://127.0.0.1:8443/"

def tlog(s):
    print(s, flush=True)

# each run must see ONLY its own traffic
(TOOLS / "mock_box.log").write_text("")
(TOOLS / "mock_log_pull.txt").write_text("")
(TOOLS / "mock_directives.txt").write_text("")

async def cdp_call(ws, msg_id, method, params=None, timeout=20):
    await ws.send(json.dumps({"id": msg_id, "method": method, "params": params or {}}))
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=timeout)
        m = json.loads(raw)
        if m.get("id") == msg_id:
            if "error" in m:
                raise RuntimeError(method + ": " + json.dumps(m["error"]))
            return m.get("result", {})

async def cdp_eval(ws, expr, mid=100):
    r = await cdp_call(ws, mid, "Runtime.evaluate",
                       {"expression": expr, "returnByValue": True, "awaitPromise": False})
    res = r.get("result", {})
    if res.get("subtype") == "error":
        raise RuntimeError("eval: " + res.get("description", "?"))
    return res.get("value")

async def main():
    proc = None
    try:
        import shutil
        shutil.rmtree("/tmp/chr-testmode-" + str(PORT), ignore_errors=True)
        proc = await asyncio.create_subprocess_exec(
            str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
            "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
            "--ignore-certificate-errors", "--autoplay-policy=no-user-gesture-required",
            "--use-gl=swiftshader", "--enable-unsafe-swiftshader",
            "--remote-debugging-port=" + str(PORT),
            "--user-data-dir=/tmp/chr-testmode-" + str(PORT),
            "--window-size=800,1000", "about:blank",
            stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        ws_url = None
        for _ in range(40):
            await asyncio.sleep(0.25)
            try:
                import urllib.request as u
                data = json.loads((await asyncio.to_thread(
                    u.urlopen, f"http://127.0.0.1:{PORT}/json/list", timeout=2)).read())
                pages = [t for t in data if t.get("type") == "page"]
                if pages:
                    ws_url = pages[0]["webSocketDebuggerUrl"]
                    break
            except Exception:
                continue
        if not ws_url:
            tlog("CDP never came up")
            return 1
        async with websockets.connect(ws_url, max_size=20 * 1024 * 1024) as ws:
            await cdp_call(ws, 1, "Page.enable")
            await cdp_call(ws, 2, "Runtime.enable")
            await cdp_call(ws, 4, "Page.addScriptToEvaluateOnNewDocument",
                           {"source": "window.__errs=[]; window.onerror=function(m,s,l,c){window.__errs.push(m+\' @\'+l+\':\'+c)};"})
            await cdp_call(ws, 3, "Page.navigate", {"url": URL})
            await asyncio.sleep(4)
            st = await cdp_eval(ws, "JSON.stringify({ws:document.getElementById('wsst').textContent,"
                                    "cam:document.getElementById('camst').textContent,"
                                    "bld:document.getElementById('bl2').textContent,"
                                    "errs:(window.__errs||[]).slice(0,4)})")
            tlog("STATUS " + json.dumps(st))
            stj = json.loads(st or "{}")
            if "S14P" not in (stj.get("bld") or ""):
                tlog("FAIL: unexpected build stamp: " + str(stj.get("bld")))
                return 1
            # queue test-mode CFG + BURST as ONE drv? directive set
            cfg = {"cwc": 1, "cwcN": 10, "cwcSettle": 100, "bBurstB": 150,
                   "bBurstHold": 800, "bComp": 0, "cwcTestMode": 1, "cwcTestLed": 0}
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg) + "\nBURST\n")
            tlog("queued CFG=" + json.dumps(cfg) + " + BURST")
            # wait for the test-mode capture line
            done = False
            for i in range(90):
                await asyncio.sleep(1)
                txt = await cdp_eval(ws, "document.getElementById('log').textContent")
                lines = [l for l in (txt or "").split("\n")
                         if "planes + master captured" in l or "E " in l]
                if lines:
                    tlog("TESTMARK " + lines[-1][:160])
                    if "planes + master captured" in lines[-1]:
                        done = True
                    break
            if not done:
                full = await cdp_eval(ws, "document.getElementById('log').textContent")
                tlog("PAGELOG-BEGIN")
                for l in (full or "").split("\n"):
                    tlog("  " + l)
                tlog("PAGELOG-END")
                return 1
            # wait for the auto-ship to finish (19 frames x ~40 chunks)
            for i in range(120):
                await asyncio.sleep(1)
                txt = await cdp_eval(ws, "document.getElementById('log').textContent")
                if "bench pull done" in txt:
                    break
            pagelog = await cdp_eval(ws, "document.getElementById('log').textContent")
            tail = (pagelog or "").split("\n")[-6:]
            tlog("PAGELOG-TAIL: " + " | ".join(tail))
            pull = (TOOLS / "mock_log_pull.txt").read_text()
            n_cwcstats = sum(1 for l in pull.splitlines() if l.startswith("CWCSTATS "))
            n_frames = sum(1 for l in pull.splitlines() if l.startswith("FRAME "))
            n_fend = sum(1 for l in pull.splitlines() if l.strip() == "FEND")
            labels = []
            for l in pull.splitlines():
                if l.startswith("FRAME "):
                    try:
                        labels.append(json.loads(l[6:]).get("label", "?"))
                    except Exception:
                        labels.append("PARSE-ERR")
            masters = [l for l in labels if "master" in l]
            planes = [l for l in labels if ":p" in l and "master" not in l]
            tlog(f"SHIP: CWCSTATS={n_cwcstats} FRAME={n_frames} FEND={n_fend} "
                 f"master={len(masters)} planes={len(planes)}")
            tlog("labels sample: " + ", ".join(labels[:4] + labels[-2:]))
            st2 = await cdp_eval(ws, "JSON.stringify({cwc: !!cwcStats, tm: cwcStats && cwcStats.testMode,"
                                     "led: cwcStats && cwcStats.testLed, n: cwcStats && cwcStats.n,"
                                     "store: benchStore.length})")
            tlog("STATS " + st2)
            ok = (n_cwcstats == 1 and n_frames == 19 and n_fend == 19
                  and len(masters) == 1 and len(planes) == 18)
            tlog("TEST MODE SHIP RUNTIME: " + ("PASS" if ok else "FAIL"))
            return 0 if ok else 1
    finally:
        if proc:
            proc.kill()

if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
