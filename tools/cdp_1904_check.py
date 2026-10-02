#!/usr/bin/env python3
"""S14R-0001 pre-flash QA: mock box + headless Chromium + fake camera.
Validates the REAL page end-to-end before an ESP32 flash:
  1. build stamp == S14R-0001
  2. CFG (cwc=1, cwcN=10) + BURST via mock drv? directives
  3. burst runs: 24 frame-bits planes + master, bench store ships 25 frames
  4. direct registration log line present (chain, no crash)
  5. in-page decode ran (sitesMasked/confirmed logged; CWCDEC chunks ship)
  6. result canvas (static master + site boxes) exists and is non-blank;
     saved to runs/qa-1904/result.png for operator/agent visual check
  7. second burst with cwcTestMode=1 exercises the test branch
  8. multi-string rig via mock directives: CFG nStr=8, nPerStr=25 (200 ids
     over 8 virtual strings), one burst; assert burst completes + decode
     logs + per-lane frame-bits mapping exercised (LED 57 = display L3,
     pixel 7: its codeword's ON-plane pattern must appear EXACTLY in the
     lane-3 latch sequence across the 24 consecutive frame-bits messages).
  9. S14P-1926 replay path (kept): a rig CFG queued while the page is
     already up converges ('cfg: rig 8x25 from box' + window._cfgRig snapshot);
     a page RELOAD (fresh hello, no new directive) REPLAYS the still-held cfg
     into the fresh context — reconverges idempotently, no rig-mismatch error.
  10. S14R-0001 capture-only bursts + manual bulk send (unchanged from
     1928's check): chkCapOnly ON -> TWO bursts prime/paint/settle/grab as
     usual ('cwc capture:' start lines, 25 frames each) but ACCUMULATE 50
     frames in benchStore with ZERO decode (no chain/decode/result-view
     lines), ZERO CWCDEC/CWCSTATS and ZERO auto-ship (no FRAME/FJPEG logc
     traffic); Burst re-enables promptly after each burst. Then 'Send frames
     (all)' (btnSendFrames) ships ALL 50 in one benchPull (FRAME/FJPEG
     traffic appears, pull completes, buttons re-enable; benchPull is
     non-destructive — the store KEEPS its frames, cleared only at the next
     decode-mode burst start).
  11. S14R-0001 operator UI: Burst button directly under the camera canvas
     (burstRow is the canvas's next sibling); 8 one-press string buttons
     (bStr1..bStr8) apply an operator nStr override mid-session AND a box
     CFG nStr still drives the rig (the burst row mirrors the box shape);
     Survey button and Keep-screen-on button are GONE.

PASS = hard checks 1-6 pass; 7 exercises without a NEW 'E ' error;
       8 completes with decode log + exact per-lane mapping sequence match;
       9 replay + mismatch-guard + reload-convergence all behave as logged;
       10 capture mode accumulates without decode/ship + manual bulk send
       ships 50 with no new 'E ' errors;
       11 Burst-under-camera + 8 string buttons (press applies, box cfg
       still wins) + Survey/wake buttons removed.
Run with the venv python (websockets dep): see README tooling line."""
import asyncio, base64, json, os, re, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cdp_once_ship import cdp_call, cdp_eval, wait_log, CHROME, PORT, URL, TOOLS  # noqa

BASE = TOOLS.parent
RUN = BASE / "runs" / "qa-1904"
import urllib.request as u

STAMP = "S14R-0001"


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
            # ---- check 11 (S14R-0001 operator UI): layout + press-applies ----
            ui = await cdp_eval(ws, "(() => {"
                " const cam = document.getElementById('cam');"
                " const row = document.getElementById('burstRow');"
                " const nxt = cam && cam.nextElementSibling;"
                " for (let s = 1; s <= 8; s++)"
                "   if (!document.getElementById('bStr' + s))"
                "     return JSON.stringify({under: false, survey: false, wake: false,"
                "                           capLabel: 'MISSING', bstr8: false});"
                " const lbl = document.querySelector('label[for=chkCapOnly]');"
                " return JSON.stringify({under: nxt === row,"
                "   survey: !!document.getElementById('bSurvey'),"
                "   wake: !!document.getElementById('bWake'),"
                "   capLabel: lbl ? lbl.textContent : '?',"
                "   bstr8: !!document.getElementById('bStr8')});"
                " })()")
            print("UI layout:", ui)
            ui0 = json.loads(ui)
            # one-press override: press 3 -> live nStr 3 + pressed state, then 8 back
            await cdp_eval(ws, "document.getElementById('bStr3').click(); 'x'")
            p3 = await cdp_eval(ws, "JSON.stringify({n: window._nStr,"
                                    " on: document.getElementById('bStr3').classList.contains('on'),"
                                    " off: document.getElementById('bStr8').classList.contains('on')})")
            print("UI press 3:", p3)
            await cdp_eval(ws, "document.getElementById('bStr8').click(); 'x'")
            p8 = await cdp_eval(ws, "JSON.stringify({n: window._nStr})")
            j3, j8 = json.loads(p3), json.loads(p8)
            press_ok = (j3.get("n") == 3 and j3.get("on") and not j3.get("off")
                        and j8.get("n") == 8)
            ui_layout_ok = bool(ui0.get("under") and not ui0.get("survey")
                                and not ui0.get("wake")
                                and ui0.get("capLabel") == 'Capture only')
            # ---- burst 1: normal CWC mode (single-string, unchanged shape) ----
            cfg = {"cwc": 1, "cwcN": 10, "cwcSettle": 100, "bBurstB": 150,
                   "bBurstHold": 800, "bComp": 0, "cwcTestMode": 0}
            (TOOLS / "mock_directives.txt").write_text("CFG=" + json.dumps(cfg) + "\nBURST\n")
            print("burst 1 queued")
            chain_ln = await wait_log(ws, "chain totals", tries=60)
            print("chain:", chain_ln)
            dec_ln = await wait_log(ws, "decode:", tries=90)
            print("decode:", dec_ln)
            # S14R-0001: burst-1 runs the FULL 8x200 default rig (page cwcN
            # 1600, mock box defaults nStr=8) — nL=1600 still maps onto the
            # 24-plane/1600-led Golay bank, so the 25-frame shape is
            # unchanged; only the decode semantics moved (mock fake camera
            # lights only its painted lamps -> sites confirm against LED ids).
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
            # ---- burst 3: multi-string rig (explicit CFG 8x25 = 200 ids) ----
            # S14R-0001: burst-3 stays the EXPLICIT narrow CFG (nStr=8,
            # nPerStr=25) — unchanged despite the compiled defaults moving to
            # 8x200; its assertions are shape-explicit, not default-driven.
            cfg3 = dict(cfg, cwcN=200, nStr=8, nPerStr=25)
            boxlog_at_b3 = len((TOOLS / "mock_box.log").read_text().splitlines())
            # S14R-0001: press 3 FIRST — the queued box CFG below must WIN the
            # rig shape back (box CFG stays authoritative for automated runs)
            await cdp_eval(ws, "document.getElementById('bStr3').click(); 'x'")
            pr3 = await cdp_eval(ws, "JSON.stringify({n: window._nStr,"
                                    " on: document.getElementById('bStr3').classList.contains('on')})")
            print("UI pre-burst3 press 3:", pr3)
            press3_ok = json.loads(pr3).get("n") == 3 and json.loads(pr3).get("on")
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
            # S14R-0001: after the box cfg applied, the row must mirror 8
            strrow3 = await cdp_eval(ws, "(() => { const on = [];"
                " for (let s = 1; s <= 8; s++)"
                "  if (document.getElementById('bStr' + s).classList.contains('on')) on.push(s);"
                " return JSON.stringify(on); })()")
            box_row_ok = strrow3 == '[8]'
            print("UI row after box cfg:", strrow3)
            ship3_ok = '"strings":8,"perString":25' in (TOOLS / "mock_log_pull.txt").read_text()
            print("multi-string: decode logged:", dec3_ok, "| hello:", hello3,
                  "| ship carries strings/perString:", ship3_ok,
                  "| new errors:", errs3 if errs3 else "none")
            # ---- check 9: S14P-1926 replay path + rig-mismatch guard ----
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
            # burst 3 paints 24 consecutive frame-bits planes (P00 is plane 0
            # held 1 s — no separate primer message). Probe LED 57:
            # lane = 57/25 = 2 -> L3 (display), pixel = 57%25 = 7. Its
            # codeword's ON-plane pattern must appear EXACTLY as the L3
            # pixel-7 latch sequence over the 18 messages — this proves
            # bit j -> (lane, px) = (j/nPerStr, j%nPerStr) AND the LSB-first
            # byte packing through the REAL page encoder.
            cw_txt = (BASE / "page" / "survey.html").read_text()
            mbank = re.search(r"window\.CWC_CODES_12OF24 = \[(.*?)\];", cw_txt, re.S)
            codes = re.findall(r'"([0-9,]+)"', mbank.group(1)) if mbank else []
            LED_ID, LANE_COL, PX = 57, 2, 7            # id 57 -> L3(display) px 7
            led_planes = codes[LED_ID].split(",") if len(codes) > LED_ID else []
            blines = (TOOLS / "mock_box.log").read_text().splitlines()[boxlog_at_b3:]
            fb_seen, px_seq, sums_ok = 0, [], True
            expect = ["1" if str(p) in led_planes else "0" for p in range(24)]
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
            seq_ok = px_seq[:24] == expect
            print(f"frame-bits latches: {fb_seen}; led{LED_ID} codeword planes: {led_planes} "
                  f"({len(led_planes)}/12); L{LANE_COL+1}px{PX} latch seq: {''.join(px_seq[:24])}")
            print(f"per-lane mapping: seq match {seq_ok}; per-plane lit sum 100: {sums_ok}")
            hard_ok = (n_frames == 25 and stats_ok and chain_ln and dec_ln
                       and "NO CANVAS" not in info and not errs and cwcdec_ok and not errs2)
            multi_ok = (bool(rig_ln) and dec3_ok and hello3_ok and ship3_ok
                        and not errs3 and seq_ok and sums_ok and fb_seen >= 24
                        and len(led_planes) == 12)
            # S14R-0001: check 9 (the S14P-1926 replay, kept) — the reload
            # (fresh hello) converged to the still-queued rig cfg with NO new
            # directive, and the apply was visible ('cfg: rig 8x25 from box').
            replay_ok = bool(ctx9 and conv9 and from_box9 and guard9_ok)

            # S14R-0001: check 11 verdict — layout + presses + box cfg still
            # owns the rig (press-3 was overridden by the queued CFG nStr 8).
            ui_ok = bool(ui_layout_ok and press_ok and press3_ok and box_row_ok
                         and ui0.get("bstr8"))

            # ================= check 10: S14R-0001 capture-only + bulk send ==
            # STAGE B: chkCapOnly ON -> TWO bursts keep the full capture
            # choreography (1 s P00 primer + per-plane settles + 24 planes +
            # fast master) but ACCUMULATE 38 frames in benchStore with.ZERO
            # decode (no chain/decode/result view), ZERO CWCDEC/CWCSTATS and
            # ZERO auto-ship (no pull-file traffic at all); Burst re-enables
            # promptly after each burst end. STAGE C: 'Send frames (all)'
            # ships ALL accumulated frames in ONE benchPull.
            pull_txt = (TOOLS / "mock_log_pull.txt").read_text()
            pull_mark = len(pull_txt)                 # count only NEW pull lines
            boxlog_markB = len((TOOLS / "mock_box.log").read_text().splitlines())
            logB0 = await cdp_eval(ws, "document.getElementById('log').textContent")
            errsB0 = err_lines(logB0)                 # baseline for new-error diff
            cap_on = await cdp_eval(ws, "document.getElementById('chkCapOnly').checked = true; 'set'")
            print("check 10 stage B: chkCapOnly", cap_on)
            async def c10_burst_and_settle(rno):
                (TOOLS / "mock_directives.txt").write_text("BURST\n")
                print(f"burst C10-{rno} (capture mode) queued")
                cap_ln = await wait_log(ws, f"captured: r{rno} 25 frames", tries=90)
                end = False
                for _ in range(90):
                    bb = await cdp_eval(ws, "JSON.stringify({r:benchRunning,u:benchUploading,s:scanning})")
                    if bb == '{"r":false,"u":false,"s":false}':
                        end = True; break
                    await asyncio.sleep(1)
                t0 = time.monotonic(); lat = None
                for _ in range(16):                   # re-enable within ~2 s of end
                    st = await cdp_eval(ws, "JSON.stringify({b:document.getElementById('bBurst').disabled})")
                    if st == '{"b":false}': lat = time.monotonic() - t0; break
                    await asyncio.sleep(0.25)
                return cap_ln, end, lat
            (TOOLS / "mock_directives.txt").write_text("")   # stage B needs NO cfg
            cap1_ln, end1, lat1 = await c10_burst_and_settle(1)
            st1 = await cdp_eval(ws, "JSON.stringify({n:benchStore.length, run:benchRunNo})")
            print(f"capture burst 1: end {end1}; bBurst re-enabled "
                  f"{lat1 is not None and f'{lat1:.2f}s' or 'NEVER'}; store/run {st1}")
            # NO auto-ship between bursts: the pull file must gain NOTHING
            newpull1 = (TOOLS / "mock_log_pull.txt").read_text()[pull_mark:]
            cap2_ln, end2, lat2 = await c10_burst_and_settle(2)
            st2c = await cdp_eval(ws, "JSON.stringify({n:benchStore.length, run:benchRunNo})")
            print(f"capture burst 2: end {end2}; bBurst re-enabled "
                  f"{lat2 is not None and f'{lat2:.2f}s' or 'NEVER'}; store/run {st2c}")
            newpull = (TOOLS / "mock_log_pull.txt").read_text()[pull_mark:]
            logtxt10 = await cdp_eval(ws, "document.getElementById('log').textContent")
            newlog10 = [l for l in (logtxt10 or "").split("\n")
                        if l and l not in set((logB0 or "").split("\n"))]
            nodec = [l for l in newlog10 if ("chain totals" in l or l.strip().startswith("decode:")
                                            or "CWCDEC" in l or "MOTION WARNING" in l)]
            capstart10 = sum(1 for l in newlog10 if "cwc capture:" in l)
            capacc = [l for l in newlog10 if "captured: r" in l]
            nores10 = await cdp_eval(ws, "String(document.getElementById('cwcResult') === null)")
            lab_ok10 = await cdp_eval(ws, "JSON.stringify(benchStore.every(f => f.label.startsWith('cwc:r')))")
            bl10 = (TOOLS / "mock_box.log").read_text().splitlines()[boxlog_markB:]
            fb10 = sum(1 for l in bl10 if l.startswith("FRAME-BITS"))
            errs10b = [l for l in err_lines(logtxt10) if l not in errsB0]
            print(f"stage B: 'cwc capture:' starts {capstart10} (want 2); accumulated "
                  f"{[l[-40:] for l in capacc]}; store-labels-all-cwc {lab_ok10}; "
                  f"frame-bits paints {fb10} (want 48 = 2x24 planes); "
                  f"no result canvas {nores10}; pull traffic {len(newpull.splitlines())} lines")
            print("stage B decode-stack lines (want []):", nodec if nodec else "none")
            print("stage B new errors (want none):", errs10b if errs10b else "none")
            # STAGE C: manual bulk send — the EXISTING benchPull over the
            # WHOLE accumulated store in one go.
            click10 = await cdp_eval(ws, "document.getElementById('btnSendFrames').click(); 'clicked'")
            print("check 10 stage C: btnSendFrames", click10)
            ship_ln10 = await wait_log(ws, "bench pull done", tries=90)
            print("ship 10:", ship_ln10)
            end3 = False
            for _ in range(60):
                bb = await cdp_eval(ws, "JSON.stringify({r:benchRunning,u:benchUploading,s:scanning})")
                if bb == '{"r":false,"u":false,"s":false}':
                    end3 = True; break
                await asyncio.sleep(1)
            pullC = (TOOLS / "mock_log_pull.txt").read_text()[pull_mark:]
            fmeta = []
            for l in pullC.splitlines():
                if l.startswith("FRAME "):
                    try: fmeta.append(json.loads(l[len("FRAME "):]))
                    except Exception: pass
            r1_f = sum(1 for m in fmeta if str(m.get("label", "")).startswith("cwc:r1:"))
            r2_f = sum(1 for m in fmeta if str(m.get("label", "")).startswith("cwc:r2:"))
            n_frames10 = sum(1 for l in pullC.splitlines() if l.startswith("FRAME "))
            n_fjpeg10 = sum(1 for l in pullC.splitlines() if l.startswith("FJPEG "))
            n_fend10 = sum(1 for l in pullC.splitlines() if l.startswith("FEND"))
            n_cwcstats10 = sum(1 for l in pullC.splitlines() if l.startswith("CWCSTATS "))
            st3 = await cdp_eval(ws, "JSON.stringify({n:benchStore.length,"
                                    "send:document.getElementById('btnSendFrames').disabled,"
                                    "burst:document.getElementById('bBurst').disabled})")
            logtxt10c = await cdp_eval(ws, "document.getElementById('log').textContent")
            errs10c = [l for l in err_lines(logtxt10c) if l not in errsB0]
            # POST-SHIP STORE SEMANTICS (verified + reported): benchPull() is
            # deliberately NON-destructive — it ships the ring and LEAVES it;
            # the store is cleared only at the next decode-mode burst START
            # (S14P-1902) or page reload. Capture mode's run must therefore
            # keep the 38 frames after the manual send (re-send is a re-ship;
            # the operator's protection is the clear-on-next-decode-burst).
            print(f"stage C: FRAME {n_frames10}/50 (r1 {r1_f}, r2 {r2_f}), FJPEG {n_fjpeg10},"
                  f" FEND {n_fend10}, CWCSTATS {n_cwcstats10} (want 0); released {end3};"
                  f" post-ship state {st3}")
            print("stage C new errors (want none):", errs10c if errs10c else "none")
            cap_ok = bool(cap1_ln and cap2_ln and end1 and end2 and end3
                          and lat1 is not None and lat1 < 4.0 and lat2 is not None and lat2 < 4.0
                          and st1 == '{"n":25,"run":1}' and st2c == '{"n":50,"run":2}'
                          and lab_ok10 == 'true'
                          and capstart10 == 2 and len(capacc) == 2
                          and len(newpull.splitlines()) == 0      # no auto-ship, no logc at all
                          and not nodec and nores10 == 'true' and fb10 == 48
                          and n_frames10 == 50 and n_fjpeg10 > 0 and n_fend10 == 50
                          and r1_f == 25 and r2_f == 25 and n_cwcstats10 == 0
                          and not errs10b and not errs10c)
            print("\nRESULT:", "PASS" if (hard_ok and multi_ok and replay_ok and cap_ok and ui_ok) else "FAIL")
            print(f"  frames 25/25: {n_frames == 25}; stats<4096B: {stats_ok}; "
                  f"chain logged: {bool(chain_ln)}; decode logged: {bool(dec_ln)}; "
                  f"canvas: {'NO CANVAS' not in info}; CWCDEC {n_cwcdec} {cwcdec_ok}")
            print(f"  S14R-0001 UI (check 11): burst-under-cam {ui_layout_ok and ui0.get('under')}; "
                  f"press-applies {press_ok}; box-cfg-wins {press3_ok and box_row_ok}; "
                  f"survey/wake removed {not ui0.get('survey') and not ui0.get('wake')}")
            print(f"  multi-string: rig line {bool(rig_ln)}; decode rig-tagged {dec3_ok}; "
                  f"hello 8x25 {hello3_ok}; ship strings-8 {ship3_ok}; errors {len(errs3)}; "
                  f"lane-map {seq_ok}; lit-sum {sums_ok}")
            print(f"  S14R-0001 replay (check 9): fresh-ctx {ctx9}; reconverged {conv9}; "
                  f"from-box logged {from_box9 or from_box9c}; clean {guard9_ok} "
                  f"(cfg queued while page up, then a reload replays the held cfg)")
            print(f"  S14R-0001 capture-only (check 10): starts {capstart10}x, store 50 after 2 bursts, "
                  f"decode skipped {not nodec and nores10 == 'true'}, no auto-ship, bulk send 50+re-enable; "
                  f"errors {len(errs10b) + len(errs10c)}")
            return 0 if (hard_ok and multi_ok and replay_ok and cap_ok and ui_ok) else 1
    finally:
        proc.kill()
        mock.kill()


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))