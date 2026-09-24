#!/usr/bin/env python3
"""Unit-test the page's detectBlobs() on synthetic scenes with known ground
truth, executed INSIDE the live page via CDP (validates the exact browser
implementation). Cases: colour accents on bright white field (the F1
scenario), colour accents on dark background, noise-only rejection, and
adjacent-blob merge behaviour.

Usage: venv/bin/python3 cdp_detect_test.py"""
import asyncio, json
from pathlib import Path
import websockets

CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9366
URL = "https://127.0.0.1:8443/"

# JS test body: builds synthetic ImageData scenes, runs buildChannels +
# detectBlobs, returns compact results. Runs in the PAGE context.
TEST_JS = r"""
(async () => {
  const W = 640, H = 360;
  function mkScene(bg, blobs, noise) {
    // bg: [r,g,b]; blobs: [{x,y,rad,peak:[r,g,b]}]; noise: amplitude
    const px = new Uint8ClampedArray(W * H * 4);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const i = (y * W + x) * 4;
      px[i] = bg[0]; px[i+1] = bg[1]; px[i+2] = bg[2]; px[i+3] = 255;
    }
    for (const b of blobs) {
      const R2 = (b.rad + 2) * (b.rad + 2);
      for (let dy = -b.rad - 2; dy <= b.rad + 2; dy++) for (let dx = -b.rad - 2; dx <= b.rad + 2; dx++) {
        const x = b.x + dx, y = b.y + dy;
        if (x < 0 || y < 0 || x >= W || y >= H) continue;
        const d2 = dx*dx + dy*dy;
        if (d2 > (b.rad + 2) * (b.rad + 2)) continue;
        const f = Math.exp(-d2 / (2 * (b.rad/2) * (b.rad/2)));
        const i = (y * W + x) * 4;
        px[i]   = Math.min(255, bg[0] + f * (b.peak[0] - bg[0]));
        px[i+1] = Math.min(255, bg[1] + f * (b.peak[1] - bg[1]));
        px[i+2] = Math.min(255, bg[2] + f * (b.peak[2] - bg[2]));
      }
    }
    // deterministic pseudo-noise
    let s = 12345;
    for (let i = 0; i < px.length; i += 4) {
      s = (s * 1103515245 + 12345) & 0x7fffffff;
      const nz = ((s >> 16) % (2 * noise + 1)) - noise;
      px[i] = Math.max(0, Math.min(255, px[i] + nz));
      px[i+1] = Math.max(0, Math.min(255, px[i+1] + nz));
      px[i+2] = Math.max(0, Math.min(255, px[i+2] + nz));
    }
    return new ImageData(px, W, H);
  }
  const out = {};
  function run(name, imgData, chan, opt) {
    buildChannels(imgData);
    const arr = chan === 'sat' ? satArr : lumaArr;
    const det = detectBlobs(arr, imgData.data, opt);
    out[name] = {
      n: det.accepted.length,
      all: det.blobs.length,
      thr: Math.round(det.thr), d10: Math.round(det.d10out * 10) / 10,
      blobs: det.accepted.slice(0, 5).map(b => ({
        h: hueClass(b), x: Math.round(b.cx), y: Math.round(b.cy),
        pk: Math.round(b.peak), n: b.cnt }))
    };
  }
  const base = { thrK: 5, domK: 32, minPx: 2 };
  // 1. THE F1 ACCENT SCENARIO: bright white field, 3 colour pops
  run('accBright', mkScene([230,230,230], [
    {x:128,y:180,rad:5,peak:[255,40,40]},
    {x:320,y:180,rad:5,peak:[40,255,40]},
    {x:512,y:180,rad:5,peak:[40,40,255]} ], 3),
    'sat', Object.assign({}, base, { thrFloor: 60 }));
  // 2. colour pops on DARK background
  run('accDark', mkScene([8,8,8], [
    {x:128,y:180,rad:5,peak:[255,30,30]},
    {x:320,y:180,rad:5,peak:[30,255,30]},
    {x:512,y:180,rad:5,peak:[30,30,255]} ], 3),
    'luma', Object.assign({}, base));
  // 3. noise-only scene -> must accept NOTHING
  run('noiseOnly', mkScene([20,20,20], [], 40), 'luma', Object.assign({}, base));
  // 4. all-on string: one elongated bright band on dark bg
  run('band', mkScene([8,8,8], [
    {x:120,y:180,rad:6,peak:[240,240,240]},
    {x:200,y:180,rad:6,peak:[240,240,240]},
    {x:280,y:180,rad:6,peak:[240,240,240]},
    {x:360,y:180,rad:6,peak:[240,240,240]},
    {x:440,y:180,rad:6,peak:[240,240,240]} ], 3),
    'luma', Object.assign({}, base));
  // 5. two ADJACENT blobs (30 px apart) on dark bg -> merge expected
  run('adjacent', mkScene([8,8,8], [
    {x:300,y:180,rad:6,peak:[240,240,240]},
    {x:330,y:180,rad:6,peak:[240,240,240]} ], 3),
    'luma', Object.assign({}, base));
  return out;
})()
"""

async def main():
    proc = await asyncio.create_subprocess_exec(
        str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
        "--ignore-certificate-errors",
        "--remote-debugging-port=" + str(PORT),
        "--user-data-dir=/tmp/chr-dt-" + str(PORT),
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
            await ws.send(json.dumps({"id": 1, "method": "Page.enable"}))
            await ws.send(json.dumps({"id": 2, "method": "Page.navigate", "params": {"url": URL}}))
            await asyncio.sleep(3)
            await ws.send(json.dumps({"id": 3, "method": "Runtime.evaluate",
                                      "params": {"expression": TEST_JS,
                                                 "returnByValue": True, "awaitPromise": True}}))
            while True:
                m = json.loads(await asyncio.wait_for(ws.recv(), timeout=30))
                if m.get("id") == 3:
                    res = m.get("result", {}).get("result", {})
                    val = res.get("value")
                    if val is None:
                        print("TEST FAILED to run:", json.dumps(m.get("result", {}))[:500])
                    else:
                        print(json.dumps(val, indent=1))
                    break
        return 0
    finally:
        proc.kill()

if __name__ == "__main__":
    asyncio.run(main())