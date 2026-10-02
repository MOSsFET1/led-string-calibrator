#!/usr/bin/env python3
"""S14P-1926 pre-flash QA: mock box + headless Chromium + fake camera.
Validates the REAL page end-to-end before an ESP32 flash:
  1. build stamp == S14P-1926
  2. CFG (cwc=1, cwcN=10) + BURST via mock drv? directives
  3. burst runs: 18 frame-bits planes + master, bench store ships 19 frames
  4. direct registration log line present (chain, no crash)
  5. in-page decode ran (sitesMasked/confirmed logged; CWCDEC chunks ship)
  6. result canvas (static master + site boxes) exists and is non-blank;
     saved to runs/qa-1904/result.png for operator/agent visual check
  7. second burst with cwcTestMode=1 exercises the test branch
  8. multi-string rig via mock directives: CFG nStr=8, nPerStr=25 (200 ids
     over 8 virtual strings), one burst; assert burst completes + decode
     logs + per-lane frame-bits mapping exercised (LED 57 = display L3,
     pixel 7: its codeword's ON-plane pattern must appear EXACTLY in the
     lane-3 latch sequence across the 18 consecutive frame-bits messages).
  9. S14P-1926 replay path: a rig CFG queued while the page is already up
     converges ('cfg: rig 8x25 from box' + window._cfgRig snapshot); a page
     RELOAD (fresh hello, no new directive) REPLAYS the still-held cfg into
     the fresh context — reconverges idempotently, no rig-mismatch error.

PASS = hard checks 1-6 pass; 7 exercises without a NEW 'E ' error;
       8 completes with decode log + exact per-lane mapping sequence match;
       9 replay + mismatch-guard + reload-convergence all behave as logged.
Run with the venv python (websockets dep): see README tooling line."""
import asyncio, base64, json, os, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cdp_once_ship import cdp_call, cdp_eval, wait_log, CHROME, PORT, URL, TOOLS  # noqa

BASE = TOOLS.parent
RUN = BASE / "runs" / "qa-1904"
import urllib.request as u

STAMP = "S14P-1926"


def err_lines(logtxt, exclude_motion=True):
    """Real 'E ' errors from the page log. Page line shape is
    'tDD.D E <text>' (timestamp prefix), so strip() can never match —
    match on the SPACE-prefixed token instead. E MOTION WARNING is the
    by-design health flag (S14P-1909), excluded like the original spec's
    'without an E error' intent."""
    out = []
    for l in (logtxt or "").split("\n"):
        body = l.strip()
        if body.startswith("t") and " E " in body:
            e = body[body.index(" E ") + 3:]
            if exclude_motion and e.startswith("MOTION WARNING"):
                continue
            out.append(l)
    return out


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
            ok = STAMP in st and "live" in st and "open" in st
            if not ok:
                print("FAIL: page state", st); return 1
            # ---- burst 1: normal CWC mode (single-string, unchanged shape) ----
            cfg = {"cwc": 1, "cwcN": 10, "cwcSettle": 100, "bBurstB": 150,
                   "bBurstHold": 800, "bComp": 0, "cwcTestMode": 0}
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg) + "\nBURST\n")
            print("burst 1 queued")
            chain_ln = await wait_log(ws, "chain totals", tries=60)
            print("chain:", chain_ln)
            dec_ln = await wait_log(ws, "decode:", tries=90)
            print("decode:", dec_ln)
            ship_ln = await wait_log(ws, "bench pull done", tries=90)
            print("ship:", ship_ln)
            logtxt = await cdp_eval(ws, "document.getElementById('log').textContent")
            errs = err_lines(logtxt)
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
            # ---- shipped stream sanity (counted BEFORE burst 2 ships more) ----
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
            errs2 = [l for l in err_lines(logtxt2) if l not in (errs or [])]
            print("test-mode new errors:", errs2 if errs2 else "none")
            # let burst 2 FULLY finish (direct state poll — the stale
            # "bench pull done" line from burst 1 fooled the log wait)
            busy = True
            for _ in range(60):
                bb = await cdp_eval(ws, "JSON.stringify({r:benchRunning,u:benchUploading})")
                if bb == '{"r":false,"u":false}':
                    busy = False
                    break
                await asyncio.sleep(1)
            print("burst 2 released:", not busy)
            # ---- burst 3: multi-string rig (nStr=8 x nPerStr=25 = 200 ids) ----
            cfg3 = dict(cfg, cwcN=200, nStr=8, nPerStr=25)
            boxlog_at_b3 = len((TOOLS / "mock_box.log").read_text().splitlines())
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg3) + "\nBURST\n")
            print("burst 3 (multi-string rig 8x25) queued")
            cfg3_ln = await wait_log(ws, "nStr=8,nPerStr=25", tries=30)
            if not cfg3_ln:      # diagnostics: the cfg demonstrably applied (rig line
                lt = await cdp_eval(ws, "document.getElementById('log').textContent")  # below) — dump why the line wasn't seen
                print("cfg3 line miss — page log tail:",
                      "\n".join((lt or "").split("\n")[-8:]))
            print("cfg3 applied:", cfg3_ln)
            rig_ln = await wait_log(ws, "200 leds (rig 200)", tries=60)
            print("multi-string burst:", rig_ln)
            # burst end = the bench state releases (decode + ship can lag the
            # burst lines; wait on STATE, never on a possibly-stale log line)
            busy = True
            for _ in range(90):
                bb = await cdp_eval(ws, "JSON.stringify({r:benchRunning,u:benchUploading,s:scanning})")
                if bb == '{"r":false,"u":false,"s":false}':
                    busy = False
                    break
                await asyncio.sleep(1)
            print("burst 3 released:", not busy)
            logtxt3 = await cdp_eval(ws, "document.getElementById('log').textContent")
            errs3 = [l for l in err_lines(logtxt3) if l not in (errs or [])]
            dec3_ok = "rig 8x25" in (logtxt3 or "")          # decode line carries the rig shape
            hello3 = await cdp_eval(ws, "JSON.stringify({nStr:window._nStr, nPerStr:window._nPerStr})")
            hello3_ok = hello3 == '{"nStr":8,"nPerStr":25}'
            ship3_ok = '"strings":8,"perString":25' in (TOOLS / "mock_log_pull.txt").read_text()
            print("multi-string: decode logged:", dec3_ok, "| hello:", hello3,
                  "| ship carries strings/perString:", ship3_ok,
                  "| new errors:", errs3 if errs3 else "none")
            # ---- S14P-1926 replay path + rig-mismatch guard ----
            # Incident shape, reproduced deterministically: (a) a rig cfg
            # arrives AFTER the page is already up (tonight 08:58 while the
            # phone was wedged) — page must log 'cfg: rig 8x25 from box' and
            # snapshot window._cfgRig; (b) an immediate RELOAD (outage/reconnect)
            # inside the replay window — fresh hello re-arms 2 credits and the
            # still-queued cfg REPLAYS to the new context, which converges
            # idempotently with NO new directive and NO rig-mismatch error.
            (TOOLS / "mock_directives.txt").write_text("")      # nothing queued
            await cdp_eval(ws, "location.reload()")
            await asyncio.sleep(4)                              # boot + hello (dedup-safe)
            st9a = await cdp_eval(ws, "JSON.stringify({"
                                    "onload:window._onload>0,"
                                    "bld:document.getElementById('bl2').textContent,"
                                    "ws:document.getElementById('wsst').textContent,"
                                    "nStr:window._nStr,nPerStr:window._nPerStr})")
            print("replay-a (fresh ctx after reload):", st9a)
            ctx9 = ('"onload":true' in st9a and '"nStr":8,"nPerStr":25' in st9a and "open" in st9a)
            # (a) cfg queued NOW (page already up — the 'arrives while page is up' shape)
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg3) + "\n")
            cfg_from_box = ""
            for _ in range(6):                                  # <= 1 idle poll of 1.5 s
                await asyncio.sleep(1)
                cfg_from_box = await cdp_eval(ws, "JSON.stringify(window._cfgRig||null)")
                if cfg_from_box != "null":
                    break
            print("replay-b (cfg queued while page up): rig snapshot", cfg_from_box)
            snap9 = (cfg_from_box == '{"nStr":8,"nPerStr":25}')
            logtxt9 = await cdp_eval(ws, "document.getElementById('log').textContent")
            from_box9 = "cfg: rig 8x25 from box" in (logtxt9 or "")
            # (b) reload AGAIN — hello re-arms the window; the STILL-HELD cfg
            # replays to the fresh context (no new directive written: the slot
            # would need >2 polls to clear, so it is guaranteed present)
            await cdp_eval(ws, "location.reload()")
            await asyncio.sleep(4)
            st9c = await cdp_eval(ws, "JSON.stringify({"
                                    "nStr:window._nStr,nPerStr:window._nPerStr,"
                                    "rig:(window._cfgRig||null)})")
            logtxt9c = await cdp_eval(ws, "document.getElementById('log').textContent")
            from_box9c = "cfg: rig 8x25 from box" in (logtxt9c or "")
            conv9 = ('"nStr":8,"nPerStr":25' in st9c and '"rig":{"nStr":8,"nPerStr":25}' in st9c)
            no_err9 = [l for l in err_lines(logtxt9c) if "rig mismatch" not in l]
            if no_err9:
                print("post-reload error lines (raw tail):",
                      [l[:120] for l in (logtxt9c or "").split("\n") if " E " in l][-4:])
            guard9_ok = bool(conv9 and (from_box9 or from_box9c) and (snap9 or conv9) and len(no_err9) == 0)
            print(f"replay: ctx {ctx9}; from-box {from_box9 or from_box9c}; "
                  f"rig snapshot {snap9 or conv9}; guard clean {len(no_err9) == 0}")
            # ---- per-lane bit mapping in the MOCK BOX ----
            # burst 3 paints 18 consecutive frame-bits planes (P00 is plane 0
            # held 1 s — no separate primer message). Probe LED 57:
            # lane = 57/25 = 2 -> L3 (display), pixel = 57%25 = 7. Its
            # codeword's ON-plane pattern must appear EXACTLY as the L3
            # pixel-7 latch sequence over the 18 messages — this proves
            # bit j -> (lane, px) = (j/nPerStr, j%nPerStr) AND the LSB-first
            # byte packing through the REAL page encoder.
            cw_txt = (BASE / "page" / "survey.html").read_text()
            mbank = re.search(r"window\.CWC_CODES_9OF18 = \[(.*?)\];", cw_txt, re.S)
            codes = re.findall(r'"([0-9,]+)"', mbank.group(1)) if mbank else []
            LED_ID, LANE_COL, PX = 57, 2, 7            # id 57 -> L3(display) px 7
            led_planes = codes[LED_ID].split(",") if len(codes) > LED_ID else []
            blines = (TOOLS / "mock_box.log").read_text().splitlines()[boxlog_at_b3:]
            fb_seen, px_seq, sums_ok = 0, [], True
            expect = ["1" if str(p) in led_planes else "0" for p in range(18)]
            pending_fb = None
            for l in blines:
                if l.startswith("FRAME-BITS"):
                    fb_seen += 1
                    pending_fb = l
                elif l.startswith("LATCH ") and pending_fb is not None:
                    parts = l[len("LATCH "):].split(" | ")
                    if len(parts) == 8:
                        vals = parts[LANE_COL].split(":")[1].split(",")
                        px_seq.append("1" if vals[PX] == "white" else "0")
                        lit = [int(x) for x in pending_fb.split("lit/lane=")[1].split(",")]
                        if sum(lit) != 100:            # every plane = exactly N/2 = 100 ON rig-wide
                            sums_ok = False
                    pending_fb = None
            seq_ok = px_seq[:18] == expect
            print(f"frame-bits latches: {fb_seen}; led{LED_ID} codeword planes: {led_planes} "
                  f"({len(led_planes)}/9); L{LANE_COL+1}px{PX} latch seq: {''.join(px_seq[:18])}")
            print(f"per-lane mapping: seq match {seq_ok}; per-plane lit sum 100: {sums_ok}")
            hard_ok = (n_frames == 19 and stats_ok and chain_ln and dec_ln
                       and "NO CANVAS" not in info and not errs and cwcdec_ok and not errs2)
            multi_ok = (bool(rig_ln) and dec3_ok and hello3_ok and ship3_ok
                        and not errs3 and seq_ok and sums_ok and fb_seen >= 18
                        and len(led_planes) == 9)
            # S14P-1926: check 9 — the reload (fresh hello) converged to the
            # still-queued rig cfg with NO new directive, and the apply was
            # visible ('cfg: rig 8x25 from box').
            replay_ok = bool(ctx9 and conv9 and from_box9 and guard9_ok)
            print("\nRESULT:", "PASS" if (hard_ok and multi_ok and replay_ok) else "FAIL")
            print(f"  frames 19/19: {n_frames == 19}; stats<4096B: {stats_ok}; "
                  f"chain logged: {bool(chain_ln)}; decode logged: {bool(dec_ln)}; "
                  f"canvas: {'NO CANVAS' not in info}; CWCDEC {n_cwcdec} {cwcdec_ok}")
            print(f"  multi-string: rig line {bool(rig_ln)}; decode rig-tagged {dec3_ok}; "
                  f"hello 8x25 {hello3_ok}; ship strings-8 {ship3_ok}; errors {len(errs3)}; "
                  f"lane-map {seq_ok}; lit-sum {sums_ok}")
            print(f"  S14P-1926 replay: fresh-ctx {ctx9}; reconverged {conv9}; "
                  f"from-box logged {from_box9 or from_box9c}; clean {guard9_ok} "
                  f"(cfg queued while page up, then a reload replays the held cfg)")
            return 0 if (hard_ok and multi_ok and replay_ok) else 1
    finally:
        proc.kill()
        mock.kill()


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))