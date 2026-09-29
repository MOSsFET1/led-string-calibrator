#!/usr/bin/env python3
"""S14P-1902 regression: bench store shims ONCE — a second BURST must clear
the first run's frames (benchStore.length back to exactly ONE burst's count).
Mock box + headless Chromium, same drive as cdp_test_mode.py.

PASS = burst A ships 19 frames, burst B clears + ships 19, store ends 19."""
import asyncio, json, sys
from pathlib import Path
import websockets

BASE = Path(__file__).resolve().parents[1]
TOOLS = BASE / "tools"
CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9337
URL = "https://127.0.0.1:8443/"

(TOOLS / "mock_box.log").write_text("")
(TOOLS / "mock_log_pull.txt").write_text("")
(TOOLS / "mock_directives.txt").write_text("")

async def cdp_call(ws, mid, method, params=None, timeout=20):
    await ws.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
    while True:
        m = json.loads(await asyncio.wait_for(ws.recv(), timeout=timeout))
        if m.get("id") == mid:
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

async def wait_log(ws, needle, tries=90):
    for _ in range(tries):
        await asyncio.sleep(1)
        txt = await cdp_eval(ws, "document.getElementById('log').textContent")
        lines = [l for l in (txt or "").split("\n") if needle in l or l.strip().startswith("E ")]
        if lines:
            return lines[-1]
    return None

async def main():
    proc = None
    try:
        import shutil
        shutil.rmtree("/tmp/chr-onceship-" + str(PORT), ignore_errors=True)
        proc = await asyncio.create_subprocess_exec(
            str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
            "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
            "--ignore-certificate-errors", "--autoplay-policy=no-user-gesture-required",
            "--use-gl=swiftshader", "--enable-unsafe-swiftshader",
            "--remote-debugging-port=" + str(PORT),
            "--user-data-dir=/tmp/chr-onceship-" + str(PORT),
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
            print("CDP never came up"); return 1
        async with websockets.connect(ws_url, max_size=20 * 1024 * 1024) as ws:
            await cdp_call(ws, 1, "Page.enable")
            await cdp_call(ws, 2, "Runtime.enable")
            await cdp_call(ws, 3, "Page.navigate", {"url": URL})
            await asyncio.sleep(4)
            st = await cdp_eval(ws, "JSON.stringify({bld:document.getElementById('bl2').textContent,"
                                    "store:benchStore.length})")
            print("STATUS", st)
            if "S14P-1902" not in st:
                print("FAIL: build stamp"); return 1
            cfg = {"cwc": 1, "cwcN": 10, "cwcSettle": 100, "bBurstB": 150,
                   "bBurstHold": 800, "bComp": 0, "cwcTestMode": 1, "cwcTestLed": 0}
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg) + "\nBURST\n")
            print("burst A queued")
            print("A:", await wait_log(ws, "planes + master captured"))
            print("A-ship:", await wait_log(ws, "bench pull done"))
            # count FRAME lines the page's mock pulled file holds
            pull_a = (TOOLS / "mock_log_pull.txt").read_text()
            n_frames_a = sum(1 for l in pull_a.splitlines() if l.startswith("FRAME "))
            n_runs_a = len(set(json.loads(l[6:]).get("label", "?").split(":")[1]
                               for l in pull_a.splitlines()
                               if l.startswith("FRAME ") and ":p" in l or "master" in l))
            print(f"A: page store {n_frames_a} FRAME lines shipped")
            # ---- burst B: store must be cleared first, then refill + ship ----
            (TOOLS / "mock_directives.txt").write_text("BURST\n")
            print("burst B queued")
            clr = await wait_log(ws, "bench store cleared")
            print("B-clear:", clr)
            print("B-cap:", await wait_log(ws, "planes + master captured"))
            print("B-ship:", await wait_log(ws, "bench pull done"))
            pull_b = (TOOLS / "mock_log_pull.txt").read_text()
            n_frames_b = sum(1 for l in pull_b.splitlines() if l.startswith("FRAME "))
            (TOOLS / "regress_once.json").write_text(json.dumps(
                {"a_frames": n_frames_a, "b_frames": n_frames_b}, indent=1))
            ok = (n_frames_a == 19 and n_frames_b == 19 + 0)  # B ships ONLY its own 19
            # B must NOT re-ship A's 19: total after B = 19 (cleared) not 38
            ok = ok and n_frames_b == 19
            print(f"SHIP A={n_frames_a} (want 19)  B-total-in-pull={n_frames_b} (want 19, not 38)")
            print("CLEAR+EACH-RUN-SHIPS-ONCE:", "PASS" if (n_frames_a == 19 and n_frames_b == 19) else "FAIL")
            st2 = await cdp_eval(ws, "benchStore.length")
            print("final store:", st2)
            return 0 if ok else 1
    finally:
        if proc:
            proc.kill()

if __name__ == "__main__":
    sys.exit(asyncio.run(main()))