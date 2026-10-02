#!/usr/bin/env python3
"""Mock survey box: HTTPS-serves the exact survey.html (the box's own cert)
and speaks the exact WS protocol (hello/frame-bits/frame/all/black/npx/evid/
drv?) on ONE port (:8443), like the real box. Lets the bench drive the REAL
page in Chromium with a fake camera before any phone test.

Usage: /home/nellie/.hermes/hermes-agent/venv/bin/python3 mock_box.py
(evidence in tools/mock_box.log; serial-directive equivalent: put
SCAN/ABRT/CFG=<json> lines in tools/mock_directives.txt, served in drv?)

S14P-1923: speaks frame-bits (the binary per-lane paint class) + hello
carries nStr/nPerStr. Per-lane LATCH log lines ('L1/L2/...') prove the
per-lane bit mapping (lane = j/nPerStr, pixel = j%nPerStr, LSB-first).
S14R-0000: frame-bits messages are 230 B (24-plane Golay bank).

S14P-1926: the mock mirrors the firmware's idempotent-replayable CFG
channel — hello arms a 2-credit replay window, a fresh CFG= re-arms it,
each drv? reply carrying a cfg consumes one credit, and sCfg clears only
after the LAST replayed delivery (a reconnecting page ALWAYS converges).
"""
import asyncio, json, ssl, base64, hashlib, struct
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
HTML = (BASE / "page" / "survey.html").read_bytes()
CERT = str(BASE / "firmware/poc_survey/cert.pem")
KEY = str(BASE / "firmware/poc_survey/key.pem")
DIRV = BASE / "tools/mock_directives.txt"
LOGF = BASE / "tools/mock_box.log"

N_PX = 200                 # per-lane capacity (matches the firmware N_PX)
state = {"npx": N_PX, "nStr": 8, "nPerStr": 200, "build": ""}   # S14P-1927: mirror
                                            # the firmware defaults (full 8x200 rig)
s_cfg = ""                 # CFG=<json> slot (mirror of the firmware sCfg)
s_cfg_replay = 2           # S14P-1926 replay credits (boot arms 2, hello re-arms)
lanes: list[list[str | None]] = [[None] * N_PX for _ in range(8)]   # last latched content per lane
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
DIRV.write_text("")
LOGF.write_text("")

def logp(s):
    with open(LOGF, "a") as f:
        f.write(s + "\n")

def lane_tag(v):
    if v is None:
        return "off"
    if v == "W":
        return "white"
    return str(v)

def _atoi(s, i):
    """C atoi() on s[i:]: skip ws/sign, take a decimal run, 0 when none."""
    n = len(s)
    while i < n and s[i] in " \t\r\n":
        i += 1
    j, sign = i, 1
    if j < n and s[j] in "+-":
        if s[j] == "-":
            sign = -1
        j += 1
    v = 0
    while j < n and s[j].isdigit():
        v = v * 10 + (ord(s[j]) - 48)
        j += 1
    return sign * v

async def send(ws_writer, obj):
    raw = json.dumps(obj).encode()
    n = len(raw)
    if n < 126:
        hdr = struct.pack("!BB", 0x81, n)
    elif n < 65536:
        hdr = struct.pack("!BBH", 0x81, 126, n)
    else:
        hdr = struct.pack("!BBHQ", 0x81, 127, n)
    ws_writer.write(hdr + raw)
    await ws_writer.drain()

def send_binary(ws_writer, data: bytes):
    n = len(data)
    if n < 126:
        hdr = struct.pack("!BB", 0x82, n)
    elif n < 65536:
        hdr = struct.pack("!BBH", 0x82, 126, n)
    else:
        hdr = struct.pack("!BBHQ", 0x82, 127, n)
    ws_writer.write(hdr + data)            # sync write; drained on next send

async def ws_session(reader, writer):
    global s_cfg, s_cfg_replay
    logp("WS OPEN")
    try:
        while True:
            hdr = await reader.readexactly(2)
            opcode = hdr[0] & 0x0F
            ln = hdr[1] & 0x7F
            if ln == 126:
                ln = struct.unpack("!H", await reader.readexactly(2))[0]
            elif ln == 127:
                ln = struct.unpack("!Q", await reader.readexactly(8))[0]
            masked = bool(hdr[1] & 0x80)
            mask = await reader.readexactly(4) if masked else None
            payload = bytearray(await reader.readexactly(ln))
            if masked:
                for i in range(ln):
                    payload[i] ^= mask[i % 4]
            if opcode == 8:
                logp("WS CLOSE")
                break
            if opcode == 2:                       # BINARY: frame-bits
                # S14R-0000: 24-plane Golay bank -> the message is exactly
                # 246 B (was 206 B in the 18-plane 9-of-18 era), header
                # unchanged: [0]='B' [1]=ver(1) [2]=b [3]=flags [4..5]=u16 LE
                # epoch [6..245] = 1920-bit plane, bit j = LED id j LSB-first.
                if ln != 246 or payload[0] != 0x42 or payload[1] != 1:
                    logp("BAD FRAME-BITS len=%d tag=%r" % (ln, payload[:2]))
                    await send(writer, {"err": "frame-bits shape", "id": 0})
                    continue
                b = payload[2]
                epoch = payload[4] | (payload[5] << 8)
                nS, nPs = state["nStr"], state["nPerStr"]
                total = nS * nPs
                for ln_ in range(8):
                    lanes[ln_] = [None] * N_PX
                for j in range(total):
                    bit = (payload[6 + (j >> 3)] >> (j & 7)) & 1
                    if bit:
                        lanes[j // nPs][j % nPs] = "W"
                lit = {l: sum(1 for v in lanes[l] if v == "W") for l in range(8)}
                logp("FRAME-BITS b=%d epoch=%d lit/lane=%s" % (b, epoch, ",".join(str(lit[l]) for l in range(8))))
                logp("LATCH " + " | ".join("L%d:%s" % (l + 1, ",".join(lane_tag(v) for v in lanes[l])) for l in range(8)))
                await send(writer, {"ok": True, "id": epoch})
                continue
            if opcode != 1:
                continue
            m = json.loads(payload.decode())
            cmd = m.get("cmd")
            i = m.get("id", 0)
            if cmd == "hello":
                logp("HELLO build=" + str(m.get("build", "")))
                # S14P-1926: every (re)connect re-arms the cfg replay window
                s_cfg_replay = 2
                await send(writer, {"ok": True, "id": i, "fw": "poc_survey",
                                    "px": state["npx"], "nStr": state["nStr"],
                                    "nPerStr": state["nPerStr"]})
            elif cmd == "cfg":
                # direct JSON cfg channel (bench convenience): nStr/nPerStr box-side
                nS = int(m.get("nStr", state["nStr"]))
                nPs = int(m.get("nPerStr", state["nPerStr"]))
                if 1 <= nS <= 8:
                    state["nStr"] = nS
                if 1 <= nPs <= N_PX:
                    state["nPerStr"] = nPs
                logp("CFG nStr=%d nPerStr=%d" % (state["nStr"], state["nPerStr"]))
                await send(writer, {"ok": True, "id": i})
            elif cmd == "npx":
                n = int(m.get("n", 0))
                if 1 <= n <= 200:
                    state["npx"] = n
                    logp("NPX " + str(n))
                    await send(writer, {"ok": True, "id": i})
                else:
                    await send(writer, {"err": "range", "id": i})
            elif cmd == "frame":
                p = m.get("p", [])
                b = m.get("b", 160)
                paint1: list[str | None] = []
                for i2 in range(N_PX):
                    s = p[i2] if i2 < len(p) else None
                    if s is None or s == "0":
                        paint1.append("black")
                    elif s == "W":
                        paint1.append("white@" + str(b))
                    else:
                        r, g, bl = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
                        paint1.append(str(r * int(b) // 255) + "," + str(g * int(b) // 255) + "," + str(bl * int(b) // 255))
                for l in range(8):                 # single-string: all lanes mirror
                    lanes[l] = paint1 if state["nStr"] <= 1 else ([None] * N_PX if l else paint1)
                logp("LATCH " + " | ".join("L%d:%s" % (l + 1, ",".join(lane_tag(v) for v in lanes[l])) for l in range(8)))
                await send(writer, {"ok": True, "id": i})
            elif cmd == "all":
                b = m.get("b", 160)
                paint1: list[str | None] = ["W"] * N_PX
                for l in range(8):
                    lanes[l] = paint1
                logp("LATCH " + " | ".join("L%d:%s" % (l + 1, ",".join(lane_tag(v) for v in lanes[l])) for l in range(8)))
                await send(writer, {"ok": True, "id": i})
            elif cmd == "black":
                for l in range(8):
                    lanes[l] = ["off"] * N_PX
                logp("LATCH all black")
                await send(writer, {"ok": True, "id": i})
            elif cmd == "logc":
                with open(str(BASE / "tools/mock_log_pull.txt"), "a") as f:
                    f.write(m.get("d", "") + "\n")
                await send(writer, {"ok": True, "id": i})
            elif cmd == "logend":
                with open(str(BASE / "tools/mock_log_pull.txt"), "a") as f:
                    f.write("[PHONE-LOG] end\n")
                await send(writer, {"ok": True, "id": i})
            elif cmd == "evid":
                logp("EVID " + m.get("e", "")[:1200])
                await send(writer, {"ok": True, "id": i})
            elif cmd == "drv?":
                d = ""
                if DIRV.exists():
                    lines = [l.strip() for l in DIRV.read_text().split("\n") if l.strip()]
                    if lines:
                        d = lines.pop(0)
                        DIRV.write_text("\n".join(lines))
                cfg = ""
                if d.startswith("CFG="):
                    s_cfg = d[4:]
                    d = ""
                    # S14P-1926: a fresh CFG= re-arms the replay window, exactly
                    # like the firmware CFG= handler
                    s_cfg_replay = 2
                    # S14P-1924: parse with the SAME offset-style algorithm as
                    # the firmware CFG= handler (strstr on the quoted key,
                    # atoi-style digits after `"key":`, i.e. +7 / +10 from the
                    # opening quote) — keeps this mock a true regression mirror
                    # of the box. Parity gate: tools/verify_cfg_parse.py asserts
                    # new offsets PASS and the old (+6/+9) offsets FAIL on the
                    # real rig CFG string.
                    try:
                        k1 = s_cfg.find('"nStr"')
                        if k1 >= 0:
                            v = _atoi(s_cfg, k1 + 7)
                            if 1 <= v <= 8:
                                state["nStr"] = v
                        k2 = s_cfg.find('"nPerStr"')
                        if k2 >= 0:
                            v = _atoi(s_cfg, k2 + 10)
                            if 1 <= v <= N_PX:
                                state["nPerStr"] = v
                        logp("CFG nStr=%d nPerStr=%d" % (state["nStr"], state["nPerStr"]))
                    except Exception:
                        pass
                if s_cfg:
                    cfg = s_cfg
                    # S14P-1926: replay window — each delivered cfg consumes a
                    # credit; sCfg clears only after the LAST replayed delivery
                    if s_cfg_replay > 0:
                        s_cfg_replay -= 1
                    else:
                        s_cfg = ""
                await send(writer, {"ok": True, "id": i, "drv": d, "cfg": cfg, "evid": 0})
            else:
                logp("UNKNOWN cmd " + str(cmd))
                await send(writer, {"err": "unknown", "id": i})
    except (asyncio.IncompleteReadError, ConnectionError):
        logp("WS ENDED")
    except Exception as e:
        logp("WS ERR " + repr(e))

async def http_handler(reader, writer):
    try:
        data = await asyncio.wait_for(reader.read(8192), timeout=5)
        req = data.decode("utf-8", "replace")
        if "GET /ws" in req and "Upgrade: websocket" in req:
            key = ""
            for line in req.split("\r\n"):
                if ":" in line:
                    k, v = line.split(":", 1)
                    if k.strip().lower() == "sec-websocket-key":
                        key = v.strip()
            accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
            writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                         b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept.encode() + b"\r\n\r\n")
            await writer.drain()
            await ws_session(reader, writer)
            return
        writer.write(b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                     b"Cache-Control: no-cache\r\nConnection: close\r\n\r\n")
        writer.write(HTML)
        await writer.drain()
    except Exception as e:
        logp("HTTP ERR " + repr(e))
    finally:
        try:
            writer.close()
        except Exception:
            pass

async def main():
    ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_ctx.load_cert_chain(CERT, KEY)
    server = await asyncio.start_server(http_handler, "0.0.0.0", 8443, ssl=ssl_ctx)
    logp("MOCK BOX UP :8443")
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    asyncio.run(main())