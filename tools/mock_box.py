#!/usr/bin/env python3
"""Mock survey box: HTTPS-serves the exact survey.html (the box's own cert)
and speaks the exact WS protocol (hello/frame/all/black/evid/drv?) on ONE
port (:8443), like the real box. Lets the bench drive the REAL page in
Chromium with a fake camera before any phone test.

Usage: /home/nellie/.hermes/hermes-agent/venv/bin/python3 mock_box.py
(evidence in tools/mock_box.log; serial-directive equivalent: put
SCAN/ABRT/CFG=<json> lines in tools/mock_directives.txt, served in drv?)
"""
import asyncio, json, ssl, base64, hashlib, struct
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
HTML = (BASE / "page" / "survey.html").read_bytes()
CERT = str(BASE / "firmware/poc_survey/cert.pem")
KEY = str(BASE / "firmware/poc_survey/key.pem")
DIRV = BASE / "tools/mock_directives.txt"
LOGF = BASE / "tools/mock_box.log"

N_PX = 10
state = {"npx": N_PX, "build": ""}
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
DIRV.write_text("")
LOGF.write_text("")

def logp(s):
    with open(LOGF, "a") as f:
        f.write(s + "\n")

def apply_paint(p, b):
    paint = []
    for i in range(N_PX):
        s = p[i] if i < len(p) else None
        if s is None or s == "0":
            paint.append("black")
        elif s == "W":
            paint.append("white@" + str(b))
        else:
            r, g, bl = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
            paint.append(str(r * int(b) // 255) + "," + str(g * int(b) // 255) + "," + str(bl * int(b) // 255))
    return paint

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

async def ws_session(reader, writer):
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
            if opcode != 1:
                continue
            m = json.loads(payload.decode())
            cmd = m.get("cmd")
            i = m.get("id", 0)
            if cmd == "hello":
                logp("HELLO build=" + str(m.get("build", "")))
                await send(writer, {"ok": True, "id": i, "fw": "poc_survey", "px": state["npx"]})
            elif cmd == "npx":
                n = int(m.get("n", 0))
                if 1 <= n <= 200:
                    state["npx"] = n
                    logp("NPX " + str(n))
                    await send(writer, {"ok": True, "id": i})
                else:
                    await send(writer, {"err": "range", "id": i})
            elif cmd == "frame":
                paint = apply_paint(m.get("p", []), m.get("b", 160))
                logp("LATCH " + " ".join(paint))
                await send(writer, {"ok": True, "id": i})
            elif cmd == "all":
                paint = apply_paint(["W"] * N_PX, m.get("b", 160))
                logp("LATCH " + " ".join(paint))
                await send(writer, {"ok": True, "id": i})
            elif cmd == "black":
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
                    cfg = d[4:]
                    d = ""
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