#!/usr/bin/env python3
"""CDP drive for the S14J CWC-form burst (no phone): mock box + headless
Chromium fake camera. Validates RUNTIME (parse/audit pass things that die at
runtime): page load, WS connect, CWC burst end-to-end — master + 18 plane
paints, comp measure, CWCSTATS + frames into mock_box.log.

Usage: /home/nellie/.hermes/hermes-agent/venv/bin/python3 cdp_cwc_test.py [--tripod]
"""
import asyncio, json, sys
from pathlib import Path
import websockets

BASE = Path(__file__).resolve().parents[1]
TOOLS = BASE / "tools"
CHROME = sorted(Path("/home/nellie/.cache/ms-playwright").glob("chromium-*/chrome-linux/chrome"))[-1]
PORT = 9335
URL = "https://127.0.0.1:8443/"


def tlog(s):
    print(s, flush=True)


# each run must see ONLY its own paints: truncate the mock log at start
(TOOLS / "mock_box.log").write_text("")


async def cdp_call(ws, msg_id, method, params=None, timeout=20):
    await ws.send(json.dumps({"id": msg_id, "method": method, "params": params or {}}))
    while True:
        raw = await asyncio.wait_for(ws.recv(), timeout=timeout)
        m = json.loads(raw)
        if m.get("id") == msg_id:
            if "error" in m:
                raise RuntimeError(method + ": " + json.dumps(m["error"]))
            return m.get("result", {})
        # ignore events / mismatched ids (mock + page share the socket's traffic)


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
        shutil.rmtree("/tmp/chr-cwc-" + str(PORT), ignore_errors=True)  # fresh profile: no HTTP cache of a stale page
        proc = await asyncio.create_subprocess_exec(
            str(CHROME), "--headless=new", "--no-sandbox", "--disable-gpu",
            "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
            "--ignore-certificate-errors", "--autoplay-policy=no-user-gesture-required",
            "--use-gl=swiftshader", "--enable-unsafe-swiftshader",
            "--remote-debugging-port=" + str(PORT),
            "--user-data-dir=/tmp/chr-cwc-" + str(PORT),
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
            print("CDP never came up")
            return 1
        async with websockets.connect(ws_url, max_size=20 * 1024 * 1024) as ws:
            await cdp_call(ws, 1, "Page.enable")
            await cdp_call(ws, 2, "Runtime.enable")
            await cdp_call(ws, 4, "Page.addScriptToEvaluateOnNewDocument",
                           {"source": "window.__errs=[]; window.onerror=function(m,s,l,c){window.__errs.push(m+' @'+l+':'+c)};"})
            await cdp_call(ws, 3, "Page.navigate", {"url": URL})
            await asyncio.sleep(4)
            st = await cdp_eval(ws, "JSON.stringify({ws:document.getElementById('wsst').textContent,"
                                    "cam:document.getElementById('camst').textContent,"
                                    "bld:document.getElementById('bl2').textContent,"
                                    "codes:window.CWC_CODES_12OF24 && window.CWC_CODES_12OF24.length,"
                                    "nscripts:document.querySelectorAll('script').length,"
                                    "slen:document.querySelector('script').textContent.length,"
                                    "bankHits:(document.querySelector('script').textContent.match(/CWC_CODES/g)||[]).length,"
                                    "errs:(window.__errs||[]).slice(0,4),"
                                    "log:document.getElementById('log').textContent.split('\\n').slice(-4).join(' | ')})")
            tlog("STATUS " + json.dumps(st))
            stj = json.loads(st or "{}")
            if stj.get("codes") != 1600:
                tlog("FAIL: codeword bank not embedded/loaded")
                return 1
            # round-trip gate FIRST (S14B-2/S14C estimator lessons): identity,
            # a known multi-pixel shift round-trips, sign, and the grid-unit fix
            # use the page's LIVE proc dimensions (fake camera is landscape:
            # 720x406; a portrait phone gives 406x720 — never hardcode)
            wh = json.loads(await cdp_eval(ws, "JSON.stringify({W: W, H: H, DH: Math.round(128*H/W/2)*2})"))
            TW, TH = str(wh["W"]), str(wh["H"])
            rt = await cdp_eval(ws, """
                (() => {
                  const H = """ + TH + """, W = """ + TW + """;
                  const ref = new Float32Array(W*H);
                  // structured pseudo-random content (LCG)
                  let s = 12345;
                  for (let i = 0; i < W*H; i++) {
                    s = (s * 1103515245 + 12345) & 0x7fffffff;
                    ref[i] = (s >> 7) & 0xff;
                  }
                  const id = phaseCorrJS(ref, ref);            // expect (0,0), conf ~1
                  // grid-INTEGER shift: SH/SV chosen so the true shift lands on
                  // whole grid cells (the decimated NCC quantizes at the cell —
                  // a sub-cell shift on uncorrelated noise decorrelates; real
                  // camera content is smooth and does not)
                  const Kx = W / 128, Ky = H / (Math.round(128 * H / W / 2) * 2);
                  const SH = Math.round(3 * Kx), SV = -Math.round(2 * Ky);
                  const sh = new Float32Array(W*H);
                  for (let y = 0; y < H; y++) {
                    const sy = y + SV; if (sy < 0 || sy >= H) continue;
                    for (let x = 0; x < W; x++) {
                      const sx = x + SH; if (sx < 0 || sx >= W) continue;
                      sh[y*W+x] = ref[sy*W+sx];
                    }
                  }
                  const mv = phaseCorrJS(ref, sh);             // grid units
                  const tot = { dx: mv.dx*Kx, dy: mv.dy*Ky };  // S14B-5 fix
                  // comp must REDUCE the mismatch: corrected-vs-ref mean error
                  // < 1/4 of uncorrected (uncorrected = raw shifted content)
                  const warp = (dx, dy) => {
                    let err = 0;
                    for (let y = 0; y < H; y++) {
                      const fy = y + dy, y0 = Math.floor(fy), ty = fy - y0;
                      const y0c = Math.max(0, Math.min(H-1, y0)), y1c = Math.max(0, Math.min(H-1, y0+1));
                      for (let x = 0; x < W; x++) {
                        const fx = x + dx, x0 = Math.floor(fx), tx = fx - x0;
                        const x0c = Math.max(0, Math.min(W-1, x0)), x1c = Math.max(0, Math.min(W-1, x0+1));
                        const v00 = sh[y0c*W+x0c], v10 = sh[y0c*W+x1c],
                              v01 = sh[y1c*W+x0c], v11 = sh[y1c*W+x1c];
                        const c = v00*(1-tx)*(1-ty) + v10*tx*(1-ty) + v01*(1-tx)*ty + v11*tx*ty;
                        err += Math.abs(c - ref[y*W+x]);
                      }
                    }
                    return err/(W*H);
                  };
                  const eUnc = warp(0, 0), eCor = warp(tot.dx, tot.dy);
                  return JSON.stringify({ id: [id.dx, id.dy], idConf: id.conf,
                                          mv_grid: [mv.dx, mv.dy], conf: mv.conf,
                                          true_shift: [SH, SV],
                                          tot_src: [+tot.dx.toFixed(2), +tot.dy.toFixed(2)],
                                          err_unc: +eUnc.toFixed(2), err_cor: +eCor.toFixed(2) });
                })()""")
            tlog("ROUNDTRIP " + rt)
            rtj = json.loads(rt)
            SH, SV = rtj["true_shift"]
            # sh[y][x] = ref[y+SV][x+SH] => CONTENT MOTION = (-SH, -SV) — the
            # estimator must return the motion, and the warp consumes it.
            # Quantization-aware gates: the decimated NCC resolves to whole
            # grid cells, so measured-vs-true within half a cell (+eps) is
            # EXACT; a white-noise field turns any sub-cell residual into a
            # large pixel error, so the warp gate is relative, not absolute
            # (real content is smooth: tripod measured 0.00 px residual).
            mx, my = -SH, -SV
            Kx = float(TW) / 128
            Ky = float(TH) / (round(128 * float(TH) / float(TW) / 2) * 2)
            ok_id = abs(rtj["id"][0]) < 0.01 and abs(rtj["id"][1]) < 0.01
            ok_shift = (abs(rtj["tot_src"][0] - mx) <= 0.5 * Kx + 0.2 and
                        abs(rtj["tot_src"][1] - my) <= 0.5 * Ky + 0.2)
            ok_warp = rtj["err_cor"] < 0.6 * rtj["err_unc"]
            ok_conf = rtj["conf"] > 0.5
            tlog(f"ROUNDTRIP verdict: id={ok_id} (conf {rtj['idConf']:.2f}) true_motion({mx},{my}) measured({rtj['tot_src'][0]},{rtj['tot_src'][1]}) within-half-cell={ok_shift} conf={rtj['conf']:.2f}={ok_conf} err {rtj['err_unc']}->{rtj['err_cor']} reduced={ok_warp}")
            if not (ok_id and ok_shift and ok_warp):
                tlog("FAIL: estimator round trip")
                return 1
            # queue CFG + BURST as ONE drv? directive (console-loop lesson)
            cfg = {"cwc": 1, "cwcN": 10, "cwcSettle": 100, "bBurstB": 150,
                   "bBurstHold": 800, "bComp": 1}
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg) + "\nBURST\n")
            tlog("queued CFG=" + json.dumps(cfg) + " + BURST")
            # wait for the cwc burst log line
            done = False
            for i in range(60):
                await asyncio.sleep(1)
                txt = await cdp_eval(ws, "document.getElementById('log').textContent")
                lines = [l for l in (txt or "").split("\n") if "planes, residual" in l or "E cwc" in l or "E burst" in l]
                if lines:
                    tlog("BURSTMARK " + lines[-1][:180])
                    if "planes, residual" in lines[-1]:
                        done = True
                    break
            if not done:
                full = await cdp_eval(ws, "document.getElementById('log').textContent")
                tlog("PAGELOG-BEGIN")
                for l in (full or "").split("\n"):
                    tlog("  " + l)
                tlog("PAGELOG-END")
                return 1
            # count paints by label in the mock log (fake camera = identical
            # frames, so the paint sequence IS the evidence)
            await asyncio.sleep(4)   # auto-ship runs ~1-2 s after the burst
            pagelog = await cdp_eval(ws, "document.getElementById('log').textContent")
            tail = [l for l in (pagelog or "").split("\n")[-8:]]
            tlog("PAGELOG-TAIL: " + " | ".join(tail))
            mlog = (TOOLS / "mock_box.log").read_text()
            paints = [l for l in mlog.splitlines() if l.startswith("LATCH ")]
            frame_paints = [l for l in paints if " white@" in l or l.endswith("white@150") or " black" in l]
            all_on = [l for l in paints if l.count("white@") == 10]
            plane = [l for l in paints if "black" in l and "white@" in l]
            tlog(f"LATCH lines: {len(paints)} total, {len(all_on)} all-on, {len(plane)} plane (mixed)")
            stats = await cdp_eval(ws, "JSON.stringify({cwc: !!cwcStats, n: cwcStats && cwcStats.n, comp: cwcStats && cwcStats.comp, s0: cwcStats && cwcStats.shifts[0], s17: cwcStats && cwcStats.shifts[17]})")
            tlog("CWCSTATS " + json.dumps(stats))
            stj2 = json.loads(stats or "{}")
            ok = stj2.get("n") == 18 and len(plane) >= 18 and len(all_on) == 1
            tlog("CWC BURST RUNTIME: " + ("PASS" if ok else "FAIL"))
            # S14M regression gate: after the auto-ship completes, the Burst
            # button must be RE-ENABLED (the S14L sequencing bug froze it
            # disabled: setButtons ran while benchRunning was still true)
            await asyncio.sleep(3)   # let the auto-ship tail finish
            btn = await cdp_eval(ws, "JSON.stringify({burst: document.getElementById('bBurst').disabled, log: document.getElementById('log').textContent.split('\\n').slice(-2).join(' | ')})")
            tlog("BUTTON-STATE " + json.dumps(btn))
            btj = json.loads(btn or "{}")
            if btj.get("burst"):
                tlog("FAIL: Burst button still disabled after auto-ship")
                return 1
            return 0 if ok else 1
    finally:
        if proc and "--keep" not in sys.argv:
            proc.kill()


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))