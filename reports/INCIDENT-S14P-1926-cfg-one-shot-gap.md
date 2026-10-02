# S14P-1925/S14P-1926 Evening Incident — One-Shot CFG Delivery Gap

Night of 1–2 Oct 2026 (capture: `runs/daemon/capture.txt`, working tree S14P-1925).
Fixed in S14P-1926 (uncommitted): **idempotent-replayable CFG channel** —
firmware (`poc_survey.ino`), page (`survey.html`), mock mirror (`tools/mock_box.py`),
QA (`tools/cdp_1904_check.py` check 9).

## Timeline (all from runs/daemon/capture.txt)

| time | line | event |
|---|---|---|
| 07:40:12 | L1112 | `=== daemon start ===` — bench daemon restart; LOGA persistent arm ON. Serial cmds/ queue mechanism now live against the running box. |
| 08:32:30 | L1114-15 | `>> CMD CFG={...nStr:8...}` + `[CFG] nStr=8 nPerStr=200 queued` — box-side rig applied; payload one-shotted into `sCfg`. |
| 08:32→08:36 | L1117-30, 2031-39 | sustained `httpd_accept_conn: session creation failed` / TLS handshake floods — the phone's WS is wedged; every drv?-carried delivery window fails. |
| 08:35:27 | L1134 | CWCSTATS S14P-1923 `nStr:8` — the OLD page session (pre-wedge) had converged; this is the rig the operator queued for. |
| 08:43:46 | L1981 | last CWCSTATS of the old session: `nStr:8 nPerStr:200`, TLS read errors trailing. |
| 08:58:05 | L2049-50 | second daemon start; the queue file `cmds/rig8.cfg.txt` re-sends `CFG=...nStr:8...`; acked. sCfg still drains on the next unseen drv? (one-shot). |
| 09:45:26 | L2051 | third daemon start; from here until ~10:01 the capture is ONLY TLS/session-creation failures — the page is down/wedged, **no drv? reaches the box and no hello arrives for ~15 min**. |
| 10:01:00 | L3189 | fourth daemon start. Boot uptime stamps reset (3.14e6 ms → 52 s): the ESP32 itself rebooted ~10:00:48. Startup CFG read: nothing queued (cmds/ empty). Box hello answer = **nStr 1 × 200 defaults**. |
| 10:02:33 | L3216-17 | phone reconnects and bursts: CWCSTATS S14P-1925 **`nStr:1 nPerStr:200`** — page painted ids 0-199 (string 1 only). String 2 lit only via the master's JSON `all`. |

## Mechanism (log-proven)

1. **sCfg is a single one-shot slot**: `drv?` copies it into the reply, then
   clears it — once — regardless of whether that reply reached a page (S14P-1904
   made the copy send-gated, but send != seen; the 08:58 copy was consumed by a
   poll that fired into a dead WS).
2. The page's rig shape comes ONLY from the hello reply (`window._nStr` default
   1). A reconnecting page re-hellos and takes whatever the box answers.
3. In tonight's window no `drv?` + hello exchange completed between the queued
   CFG (08:32/08:58) and the phone's real reconnect (10:02) — the wedge
   (08:32-08:43) and the outage (09:45-10:01) bracket every delivery chance,
   and the 10:00:48 box reboot reset the box side to 1×200 defaults.
4. Result: page nStr=1 vs the intended 8×200. No log line anywhere flags the
   divergence — the burst just paints 1/8 of the rig.

## Second, smaller gap the QA run exposed (page-side half of the incident)

After the replayable firmware landed, the reload check converged via the
replayed cfg but the fresh context initially did NOT re-apply its own hello.
Reproduced with a hand-probe (mock log: exactly one `WS OPEN`/`HELLO` per
reload, no crash): a reload keeps the pre-reload context's auto-reconnect
socket — it re-opens RIGHT AFTER the new context booted — so the fresh
context saw `wsOpen == true` and never sent its own hello, and code reading
`window._nStr/_nPerStr` kept seeing the dead context's module-global values
(exactly tonight's 10:02 shape: page alive, box answered 8x200, page stayed
1x200). Fixed in S14P-1926: the hello shape applies AT MOST ONCE PER CONTEXT
(`connectWS._helloDone`, a per-context function property reset by every
reload), with a visible `hello (dedup): rig AxB left as CxD` line when a
second hello lands in the same context. This is the page-side twin of the
one-shot slot: state owned by the wrong lifetime.

## Fix (S14P-1926)

- **Firmware**: `sCfgReplay` credit counter (boot = 2; every hello re-arms 2;
  every fresh `CFG=` re-arms 2). Each drv? reply carrying a cfg consumes one
  credit; `sCfg` clears only after the LAST replayed delivery. A reconnecting
  page therefore receives the queued rig config on its first drv? after hello,
  always. Directives (`sDrv`) stay one-shot. `applyCfg` is idempotent, so
  replay-to-an-already-converged-page is a no-op.
- **Page**: apply logs `cfg: rig AxB from box` on hello/replay convergence;
  burst start flags `E rig mismatch: page AxB vs box CxD — reconnect/CFG`
  (loud, non-refusing) by comparing live `_nStr/_nPerStr` to `window._cfgRig`
  (the rig shape the box's cfg channel last delivered).
- **Mock/QA**: mock box mirrors the same replay semantics; QA check 9 reloads
  the real page with NO new directive and asserts it converges to 8×25 via the
  replayed cfg (`cfg: rig 8x25 from box`, converged state, clean guard).

Build/flash stamp: S14P-1926 (PAGE_BUILD, page BUILD, boot banner, QA STAMP).