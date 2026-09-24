#!/usr/bin/env python3
"""Minimal CDP eval diagnostic: run small probes against the live page and
print the RAW responses, to find why the hue-test eval returns null."""
import asyncio, json
from pathlib import Path
import websockets

CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9388
URL = "https://127.0.0.1:8443/"

async def main():
    proc = await asyncio.create_subprocess_exec(
        str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
        "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
        "--ignore-certificate-errors",
        "--remote-debugging-port=" + str(PORT),
        "--user-data-dir=/tmp/chr-diag2-" + str(PORT), "about:blank",
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
        async with websockets.connect(ws_url, max_size=50 * 1024 * 1024) as ws:
            await ws.send(json.dumps({"id": 1, "method": "Page.enable"}))
            await ws.send(json.dumps({"id": 2, "method": "Page.navigate", "params": {"url": URL}}))
            await asyncio.sleep(4)
            tests = [
                "1+1",
                "typeof manualMode",
                "typeof idleFrame",
                "typeof detectColours",
                "(async () => { return {a: 1}; })()",
                "(async () => { const inp = document.getElementById('npxin'); return inp ? 'inp ok' : 'inp MISSING'; })()",
                "(async () => { try { manualMode = 'x'; return 'assign ok'; } catch(e) { return 'assign THREW ' + e.message; } })()",
            ]
            mid = 10
            for t in tests:
                mid += 1
                await ws.send(json.dumps({"id": mid, "method": "Runtime.evaluate",
                                          "params": {"expression": t, "returnByValue": True,
                                                     "awaitPromise": True}}))
                got = False
                while not got:
                    m = json.loads(await asyncio.wait_for(ws.recv(), timeout=15))
                    if m.get("id") == mid:
                        print(f"EXPR {t[:50]!r} ->", json.dumps(m.get("result", {}))[:300])
                        got = True
        return 0
    finally:
        proc.kill()

if __name__ == "__main__":
    asyncio.run(main())