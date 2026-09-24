#!/usr/bin/env python3
"""One-shot CDP probe: load the page, then evaluate diagnostics about which
boot symbols exist and what calling connectWS() throws."""
import asyncio, json
from pathlib import Path
import websockets

CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9355
URL = "https://127.0.0.1:8443/"

async def main():
    proc = await asyncio.create_subprocess_exec(
        str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
        "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
        "--ignore-certificate-errors",
        "--remote-debugging-port=" + str(PORT),
        "--user-data-dir=/tmp/chr-probe-" + str(PORT),
        "about:blank",
        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
    try:
        import urllib.request
        ws_url = None
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
            mid = [0]
            async def ev(expr):
                mid[0] += 1
                await ws.send(json.dumps({"id": mid[0], "method": "Runtime.evaluate",
                                          "params": {"expression": expr, "returnByValue": True}}))
                while True:
                    m = json.loads(await asyncio.wait_for(ws.recv(), timeout=10))
                    if m.get("id") == mid[0]:
                        r = m.get("result", {}).get("result", {})
                        return r.get("value", r.get("description", r.get("type")))

            await ws.send(json.dumps({"id": 1, "method": "Page.enable"}))
            await ws.send(json.dumps({"id": 2, "method": "Page.navigate", "params": {"url": URL}}))
            await asyncio.sleep(3)
            print("typeof log:", await ev("typeof log"))
            print("typeof connectWS:", await ev("typeof connectWS"))
            print("typeof CFG:", await ev("typeof CFG"))
            print("typeof startCamera:", await ev("typeof startCamera"))
            print("bl2:", await ev("document.getElementById('bl2').textContent"))
            print("cfgline:", await ev("document.getElementById('cfgline').textContent"))
            print("logtext:", await ev("document.getElementById('log').textContent"))
            print("manual connectWS():", await ev("(function(){ try { connectWS(); return 'called'; } catch(e) { return 'THREW: ' + e.message; } })()"))
            await asyncio.sleep(2)
            print("ws state after:", await ev("(window._ws && window._ws.readyState) || 'null'"))
            print("logtext2:", await ev("document.getElementById('log').textContent"))
        return 0
    finally:
        proc.kill()

if __name__ == "__main__":
    asyncio.run(main())