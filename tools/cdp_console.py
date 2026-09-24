#!/usr/bin/env python3
"""Load survey.html in headless Chromium (fake cam) and dump EVERY console
message + exception the page produces. Pure diagnosis.
Usage: venv/bin/python3 cdp_console.py"""
import asyncio, json
from pathlib import Path
import websockets

CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9344
URL = "https://127.0.0.1:8443/"

async def main():
    proc = await asyncio.create_subprocess_exec(
        str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
        "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
        "--ignore-certificate-errors", "--autoplay-policy=no-user-gesture-required",
        "--remote-debugging-port=" + str(PORT),
        "--user-data-dir=/tmp/chr-diag-" + str(PORT),
        "--window-size=800,1000", "about:blank",
        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
    try:
        ws_url = None
        import urllib.request
        for _ in range(40):
            await asyncio.sleep(0.25)
            try:
                data = json.loads(urllib.request.urlopen(
                    f"http://127.0.0.1:{PORT}/json/list", timeout=2).read())
                pages = [t for t in data if t.get("type") == "page"]
                if pages:
                    ws_url = pages[0]["webSocketDebuggerUrl"]
                    break
            except Exception:
                continue
        async with websockets.connect(ws_url, max_size=20 * 1024 * 1024) as ws:
            mid = 0
            await ws.send(json.dumps({"id": 1, "method": "Runtime.enable"}))
            await ws.send(json.dumps({"id": 2, "method": "Log.enable"}))
            await ws.send(json.dumps({"id": 3, "method": "Page.enable"}))
            await ws.send(json.dumps({"id": 4, "method": "Page.navigate",
                                      "params": {"url": URL}}))
            end = asyncio.get_event_loop().time() + 10
            while asyncio.get_event_loop().time() < end:
                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=10 - (end - asyncio.get_event_loop().time()))
                except asyncio.TimeoutError:
                    break
                m = json.loads(raw)
                meth = m.get("method", "")
                if meth == "Runtime.consoleAPICalled":
                    args = " ".join(str(a.get("value", a.get("description", "?"))) for a in m["params"]["args"])
                    print(f"[{m['params']['type']}] {args[:300]}")
                elif meth == "Runtime.exceptionThrown":
                    d = m["params"]["exceptionDetails"]
                    txt = d.get("exception", {}).get("description") or d.get("text", "")
                    print(f"[EXC] {txt[:500]}")
                elif meth == "Log.entryAdded":
                    e = m["params"]["entry"]
                    print(f"[{e['level']}] {e.get('text','')[:300]} ({e.get('source','')})")
            # final state probe
            await ws.send(json.dumps({"id": 50, "method": "Runtime.evaluate",
                                      "params": {"expression": "JSON.stringify({ws:(window._ws&&window._ws.readyState)||null,npx:window._npx===undefined?'undef':window._npx,log:document.getElementById('log').textContent.slice(0,400)})",
                                                  "returnByValue": True}}))
            while True:
                raw = await asyncio.wait_for(ws.recv(), timeout=5)
                m = json.loads(raw)
                if m.get("id") == 50:
                    print("STATE " + json.dumps(m.get("result", {}).get("result", {}).get("value")))
                    break
        return 0
    finally:
        proc.kill()

if __name__ == "__main__":
    asyncio.run(main())