#!/usr/bin/env python3
"""Validate the S2 hue-detector overlay end-to-end: LED-count field -> box,
then inject a synthetic scene (R/G/B accents on a white field) into the live
video element via CDP, switch the page to accents mode, and confirm the
detector finds the three LEDs where they were injected. Screenshot as proof.
Usage: venv/bin/python3 cdp_hue_test.py"""
import asyncio, base64, json
from pathlib import Path
import websockets

CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9377
URL = "https://127.0.0.1:8443/"
SHOTS = Path(__file__).resolve().parents[1] / "runs"

# JS: set the LED-count field to 55 and fire its handler (box npx push);
# then paint 3 Gaussian colour blobs + white field onto procCx? No — the
# pipeline reads from the VIDEO, so we paint onto a hidden source: simplest
# reliable trick is to draw our synthetic scene INTO the video element's
# stream is impossible; instead we monkey-patch: temporarily replace
# procCx.getImageData with a function returning our synthetic frame, call
# idleFrame() once, restore. That tests detectColours + overlay drawing
# against known truth with the REAL page code.
TEST_JS = r"""
(async () => {
  const out = {};
  // 1. LED-count field -> box (fires onchange handler; ws is open)
  const inp = document.getElementById('npxin');
  inp.removeAttribute('data-user');
  inp.value = '100';
  inp.dispatchEvent(new Event('change'));
  await new Promise(r => setTimeout(r, 600));
  out.npxAfter = window._npx;
  out.fieldVal = inp.value;
  // mark as user-set so hello doesn't stomp it again
  inp.dataset.user = '1';
  // 2. synthetic scene: white field + R/G/B pops at known spots
  const WW = W, HH = H;
  const px = new Uint8ClampedArray(WW * HH * 4);
  for (let i = 0; i < WW * HH; i++) { px[i*4]=232; px[i*4+1]=232; px[i*4+2]=232; px[i*4+3]=255; }
  function blob(cx, cy, rad, col) {
    for (let dy=-rad-2; dy<=rad+2; dy++) for (let dx=-rad-2; dx<=rad+2; dx++) {
      const x = cx+dx, y = cy+dy;
      if (x<0||y<0||x>=WW||y>=HH) continue;
      const d2 = dx*dx+dy*dy, R2 = (rad+2)*(rad+2);
      if (d2 > R2) continue;
      const f = Math.exp(-d2/(2*(rad/2)*(rad/2)));
      const i = (y*WW+x)*4;
      px[i]   = Math.min(255, Math.round(232 + f*(col[0]-232)));
      px[i+1] = Math.min(255, Math.round(232 + f*(col[1]-232)));
      px[i+2] = Math.min(255, Math.round(232 + f*(col[2]-232)));
    }
  }
  const y0 = Math.round(HH*0.5);
  blob(Math.round(WW*0.2), y0, 6, [255,40,40]);
  blob(Math.round(WW*0.5), y0, 6, [40,255,40]);
  blob(Math.round(WW*0.8), y0, 6, [40,40,255]);
  const img = new ImageData(px, WW, HH);
  // pause the video stream so no real frame repaints the overlay
  document.getElementById('vid').pause();
  // swap the grabber: feed our image to detectColours + overlay
  const orig = procCx.getImageData.bind(procCx);
  procCx.getImageData = () => img;
  manualMode = 'acc';
  // let the idle tick repaint the overlay a few times WITH the stub in place,
  // then capture the canvas IN THIS EVAL while the overlay is guaranteed live
  await new Promise(r => setTimeout(r, 350));
  const res = detectColours(img);
  const png = await new Promise((resolve) => {
    document.getElementById('cam').toBlob((b) => {
      const fr = new FileReader();
      fr.onload = () => resolve(fr.result.split(',')[1]);
      fr.readAsDataURL(b);
    });
  });
  procCx.getImageData = orig;
  // PERSISTENCE PROOF: back to luma mode, video resumed (real frames) — the
  // boxes must STILL be drawn from ledBoxes (dimmed, stale) by the next idle.
  manualMode = '';
  document.getElementById('vid').play();
  await new Promise(r => setTimeout(r, 400));
  const png2 = await new Promise((resolve) => {
    document.getElementById('cam').toBlob((b) => {
      const fr = new FileReader();
      fr.onload = () => resolve(fr.result.split(',')[1]);
      fr.readAsDataURL(b);
    });
  });
  return { res: {
    shot2: png2,
      npxAfter: out.npxAfter, fieldVal: out.fieldVal,
      R: res.R.accepted.map(b => ({x: Math.round(b.cx), y: Math.round(b.cy), pk: Math.round(b.peak), n: b.cnt, h: hueClass(b)})),
      G: res.G.accepted.map(b => ({x: Math.round(b.cx), y: Math.round(b.cy), pk: Math.round(b.peak), n: b.cnt, h: hueClass(b)})),
      B: res.B.accepted.map(b => ({x: Math.round(b.cx), y: Math.round(b.cy), pk: Math.round(b.peak), n: b.cnt, h: hueClass(b)})) },
    shot: png };
})()
"""

async def main():
    proc = await asyncio.create_subprocess_exec(
        str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
        "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
        "--ignore-certificate-errors",
        "--remote-debugging-port=" + str(PORT := 9377),
        "--user-data-dir=/tmp/chr-hue-" + str(PORT),
        "--window-size=800,1000", "about:blank",
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
            mid = [10]             # above the manual Page.enable/navigate ids
            async def ev(expr, await_p=False):
                mid[0] += 1
                await ws.send(json.dumps({"id": mid[0], "method": "Runtime.evaluate",
                                          "params": {"expression": expr, "returnByValue": True,
                                                     "awaitPromise": await_p}}))
                while True:
                    m = json.loads(await asyncio.wait_for(ws.recv(), timeout=20))
                    if m.get("id") == mid[0]:
                        print("RAW:", json.dumps(m)[:600], flush=True)
                        if "exceptionDetails" in m:
                            d = m["exceptionDetails"]
                            return "EXC: " + (d.get("exception", {}).get("description") or d.get("text", ""))[:400]
                        return m.get("result", {}).get("result", {}).get("value")
            await ws.send(json.dumps({"id": 1, "method": "Page.enable"}))
            await ws.send(json.dumps({"id": 2, "method": "Page.navigate", "params": {"url": URL}}))
            await asyncio.sleep(4)
            res = await ev(TEST_JS, await_p=True)
            if isinstance(res, dict) and res.get("shot"):
                (SHOTS / "s3_hue_overlay.png").write_bytes(base64.b64decode(res["shot"]))
                print("shot saved from in-eval capture: runs/s3_hue_overlay.png")
                res["res"].pop("shot", None) if isinstance(res.get("res"), dict) else res.pop("shot", None)
            if isinstance(res, dict) and isinstance(res.get("res"), dict) and res["res"].get("shot2"):
                (SHOTS / "s4_persistent_boxes.png").write_bytes(base64.b64decode(res["res"]["shot2"]))
                print("shot saved: runs/s4_persistent_boxes.png")
                res["res"].pop("shot2", None)
            print("RESULT", json.dumps(res, indent=1) if not isinstance(res, str) else res)
        return 0
    finally:
        proc.kill()

if __name__ == "__main__":
    URL = "https://127.0.0.1:8443/"
    asyncio.run(main())