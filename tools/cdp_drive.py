#!/usr/bin/env python3
"""Drive survey.html (served by mock_box on :8443) in headless Chromium with
a fake camera, over raw CDP. Validates: page load, WS connect, camera
auto-start, drv? SCAN directive, survey run paints (read from mock_box.log),
evid ship, page log content. Screenshots at start/end.

Usage: venv/bin/python3 cdp_drive.py [--no-scan]
"""
import asyncio, base64, json, sys, time, subprocess, urllib.request
from pathlib import Path
import websockets

BASE = Path(__file__).resolve().parents[1]
TOOLS = BASE / "tools"
CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9333
URL = "https://127.0.0.1:8443/"
SHOTS = BASE / "runs"
SHOTS.mkdir(exist_ok=True)

def tlog(s):
    print(s, flush=True)

async def cdp_call(ws, msg_id, method, params=None, timeout=15):
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
    return r.get("result", {}).get("value")

async def shot(ws, mid, name):
    r = await cdp_call(ws, mid, "Page.captureScreenshot", {"format": "png"})
    p = SHOTS / name
    p.write_bytes(base64.b64decode(r["data"]))
    print("shot:", p)

async def main():
    do_scan = "--no-scan" not in sys.argv
    proc = None
    cdp = None
    try:
        proc = await asyncio.create_subprocess_exec(
            str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
            "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
            "--ignore-certificate-errors", "--autoplay-policy=no-user-gesture-required",
            "--use-gl=swiftshader", "--enable-unsafe-swiftshader",
            "--remote-debugging-port=" + str(PORT),
            "--user-data-dir=/tmp/chr-survey-" + str(PORT),
            "--window-size=800,1000", "about:blank",
            stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        # wait for CDP
        ws_url = None
        for _ in range(40):
            await asyncio.sleep(0.25)
            try:
                import urllib.request as u
                data = json.loads((await asyncio.to_thread(
                    urllib.request.urlopen, f"http://127.0.0.1:{PORT}/json/list", timeout=2)).read())
                pages = [t for t in data if t.get("type") == "page"]
                if pages:
                    ws_url = pages[0]["webSocketDebuggerUrl"]
                    break
            except Exception:
                continue
        if not ws_url:
            print("CDP never came up")
            return 1
        async with websockets.connect(ws_url, max_size=20 * 1024 * 1024) as ws:
            await cdp_call(ws, 1, "Page.enable")
            await cdp_call(ws, 2, "Runtime.enable")
            await cdp_call(ws, 3, "Page.navigate", {"url": URL})
            await asyncio.sleep(4)
            st = await cdp_eval(ws, "JSON.stringify({ws:document.getElementById('wsst').textContent,"
                                 "cam:document.getElementById('camst').textContent,"
                                 "bld:document.getElementById('bl2').textContent,"
                                 "npx:window._npx||null,"
                                 "strip:document.getElementById('strip').textContent,"
                                 "log:document.getElementById('log').textContent.split('\\n').slice(-6).join(' | ')})")
            tlog("STATUS " + json.dumps(st))
            await shot(ws, 10, "s1_loaded.png")
            if do_scan:
                (TOOLS / "mock_directives.txt").write_text("SCAN\n")
                tlog("queued SCAN directive")
                # wait for the survey to finish (page log gets 'evid' line)
                done = False
                for i in range(60):          # up to 60 s
                    await asyncio.sleep(1)
                    txt = await cdp_eval(ws, "document.getElementById('log').textContent")
                    if txt and ('evid' in txt or 'E survey' in txt or 'E ack' in txt):
                        if 'evid ' in txt or 'E survey' in txt or 'E ack' in txt:
                            lines = [l for l in txt.split('\n') if 'evid' in l or 'E ' in l]
                            if lines:
                                tlog("DONE-MARK " + lines[-1][:160])
                                done = True
                                break
                full_log = await cdp_eval(ws, "document.getElementById('log').textContent")
                tlog("PAGELOG-BEGIN")
                for l in (full_log or "").split("\n"):
                    tlog("  " + l)
                tlog("PAGELOG-END")
                await shot(ws, 11, "s2_after_survey.png")
                st2 = await cdp_eval(ws, "JSON.stringify({strip:document.getElementById('strip').textContent})")
                tlog("STRIP2 " + json.dumps(st2))
            await asyncio.to_thread((TOOLS / "cdp_drive.done").write_text, "done")
            return 0
    finally:
        if proc:
            proc.kill()

if __name__ == "__main__":
    asyncio.run(main())