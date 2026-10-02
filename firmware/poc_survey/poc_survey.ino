// poc_survey.ino — LED survey POC: box-served HTTPS + WSS, arbitrary LED
// frames, Nellie for Oliver, Sep 2026. New paradigm (STRATEGY-RESEARCH.md).
//
// New connectivity stack for the survey POC: AP LED-SURVEY/survey, freshly
// generated self-signed cert (CN led-survey, valid to Sep 2027). Nothing is
// shared with the old cal_poc2 build — that tree is reference-only.
//
// DELETED from the old build (never reliably worked; made unnecessary):
// phone-log shipping (shipLogLine/NVS/commit task/attach dumps), ref/backlight/
// light/off/sweep/test commands, BR= override, the whole per-pixel era.
// KEPT: hello (page build stamp + string length), ackAfterLatch (latch-proof
// acks), drv? poll carrying one serial directive + CFG, PING/SCAN/ABRT/STAT
// serial directives, EV (pull the last evidence summary).
//
// The page is embedded as gz+base64 (pack_page.py of this folder). The page
// keeps its own log in memory and offers a download button; the box receives
// only ONE bounded evidence summary per survey run (cmd "evid").

#include <Arduino.h>
#include <WiFi.h>
#include <FastLED.h>
#include <esp_https_server.h>
#include <esp_http_server.h>
#include <ArduinoJson.h>

// 8 lanes, one string each, on separate GPIOs (operator pin map, 27 Sep;
// PCB plan doc to be corrected in the same change: IO0 is the 12 V SENSE):
#define LANE1_PIN   1
#define LANE2_PIN   2
#define LANE3_PIN   3
#define LANE4_PIN   4
#define LANE5_PIN   5
#define LANE6_PIN   22
#define LANE7_PIN   21
#define LANE8_PIN   20
#define N_LANES     8
#define N_PX        200           // max string length per lane (runtime npx / NPX=)
#define STATUS_PIN  8
#define FRAME_MS    40            // 25 fps pacing

#define SERIAL_DIAG 0              // 1 ONLY with a serial reader attached
                                  // (unread CDC TX ring blocks ~2 s per print)

const char* AP_SSID = "LED-SURVEY";
const char* AP_PASS = "survey2026";   // WPA2 needs >= 8 chars; the old 6-char
                                      // "survey" made softAP come up OPEN

CRGB lane1[N_PX];                    // lane 1 / string 1 (also the JSON paint target)
CRGB lane2[N_PX], lane3[N_PX], lane4[N_PX];   // S14P-1923: per-lane paint era —
CRGB lane5[N_PX], lane6[N_PX], lane7[N_PX], lane8[N_PX];  // frame-bits writes each
                                     // lane directly (plan §11.4); JSON cmds
                                     // mirror lane1 to lanes 2-8 ONLY while
                                     // nStr<=1 (single-string bench compat)
volatile int nPx = 200;             // DEFAULT 200 (operator, 30 Sep), max N_PX=200; runtime via npx cmd / NPX=
volatile int nStr = 8;              // S14P-1927 (plan §6): DEFAULT = the full 8-string rig
                                    // (8 x 200 = 1600 global LED ids); CFG key nStr narrows
                                    // it (1-8) for bench/partial rigs. The frame-bits rig =
volatile int nPerStr = 200;         // string length for the bit-plane rig (CFG key nPerStr), 1-N_PX
volatile bool frameDirty = false;   // a paint command touched buffers/lanes since the last latch
volatile bool sBitsMode = false;    // S14P-1924 latch guard: TRUE while frame-bits owns the lanes
                                    // (applyBits sets it BEFORE the lane writes); the JSON paints
                                    // (frame/all/black/npx) clear it. loop() folds pxColour into
                                    // lane1 ONLY when nStr<=1 AND !sBitsMode — bits mode owns lane
                                    // content at ALL nStr values (1923 bug: the fold repainted
                                    // lane1 from the blacked pxColour every latch)
CRGB* lanesW[N_LANES] = { lane1, lane2, lane3, lane4,
                          lane5, lane6, lane7, lane8 };   // per-lane write view
volatile uint32_t cmdEpoch = 0;      // bumped by every state-changing command
volatile uint32_t appliedEpoch = 0;  // stamped by loop() once a frame latches it
volatile uint32_t drvPolls = 0;      // page liveness (STAT)
volatile int allBright = 160;        // default all-on brightness (CFG.allB)
// Per-string brightness clamp: the string polyfuse HOLDS 2 A (trips 4 A).
// Measured white draw 14.2 mA/px at full scale; cap the whole-string white
// paint so hold-current <= 2 A (30% headroom below the 4 A trip, in the
// fuse's 100% hold regime at 25 C). Returns the clamped brightness.
static int fuseClampB(int b) {
  const float MA_PER_PX_FULL = 14.2f;         // measured, white, full scale
  const float HOLD_A = 2.0f;
  int maxB = (int)(255.0f * HOLD_A / (nPx * MA_PER_PX_FULL / 1000.0f));
  if (maxB < 20) maxB = 20;
  if (b > maxB) { Serial.printf("[PWR] b %d -> %d (string fuse %.1f A hold, %d px)\n", b, maxB, HOLD_A, nPx); return maxB; }
  return b;
}
CRGB pxColour[N_PX];                 // the CURRENT paint, applied every frame (JSON cmds;
                                     // out of the show path when frame-bits owns the rig —
                                     // pxBlack() clears it so the nStr<=1 latch gate can't
                                     // resurrect stale JSON content over per-lane planes)

// JSON-paint buffer -> black (applyBits: pxColour must not fight the lanes)
inline void pxBlack() { for (int i = 0; i < N_PX; i++) pxColour[i] = CRGB::Black; }

// --- embedded assets (replaced by pack_page.py) ----------------------------
static const char PAGE_GZ_B64[] =
  "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"
;
static const unsigned PAGE_GZ_LEN = 42270;

// cert + key as PEM
static const char CERT_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDHDCCAgSgAwIBAgIUfxKjzqrcllfdmgmzSy5n8bQafD4wDQYJKoZIhvcNAQEL
BQAwFTETMBEGA1UEAwwKbGVkLXN1cnZleTAeFw0yNjA5MjEwNzI0MTVaFw0yNzA5
MjEwNzI0MTVaMBUxEzARBgNVBAMMCmxlZC1zdXJ2ZXkwggEiMA0GCSqGSIb3DQEB
AQUAA4IBDwAwggEKAoIBAQC7UGLbzmiAATwnM4AQzWibGgh0a7HoZ4oEZl7NPGrL
7O3k+u7C/7b39GzD+vTrVDfV6G3PKMHIIRWzerTxocEtXfEWBJH2kAa2WaqMXNwM
p51aFExS7YOEr8qqbyhLHpvB/AlV9E7hOGklNupvn21W+Bb/OlcbkDkQ1natKlBq
M2ywNdsVeuigS6Z/0OQ+P6xh08NVYPlaSLY7OoHTfpWbjw+GuR71aWBVnYQF4yJl
8I+rN358HaN3lMSsqO4BA4SNRXNoE/rVpq4z5Un+LVSXHAJfTC+PL1QdQ9/ZyjYy
pPfhPmnblOR4CHEdoCSpX8hXmtzMn9Aqm4bWrlj8cX+lAgMBAAGjZDBiMB0GA1Ud
DgQWBBQ96T7OayXSUG1zCcMCXY3WoDn0JzAfBgNVHSMEGDAWgBQ96T7OayXSUG1z
CcMCXY3WoDn0JzAPBgNVHRMBAf8EBTADAQH/MA8GA1UdEQQIMAaHBMCoBAEwDQYJ
KoZIhvcNAQELBQADggEBAGkrEQsApOkZHRbIZMsvL4zEXB+fTbxYq5f4Vd8G2BKN
a7HuqHNzIDmmQmAqmHNu3A5rs2kfOBzVISIbyFbmNNHzhW30EG3102irpS3he8T/
vJB1jKj32ou807vLrfpw6M866+OAwA7U02VyrtJv9N6YyXlJZ0BwCn7HhR0C6b8d
1uqDzuJuu5yb/jTqZ8Uspys5i551awi/5kim2oSP3qEbOW+UDpexTZ1sTxwinpzS
iGbARAc1kyyBgQzKyZjAlbOSqQbagmodgzgL3hKCKv307jHRGnIGtOkM/skFEBQP
N5arv0g7NwdQLSD2xZ8GJEQ5LyXibz9/wuIQXB00fIk=
-----END CERTIFICATE-----
)PEM";
static const char KEY_PEM[] = R"PEM(
-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC7UGLbzmiAATwn
M4AQzWibGgh0a7HoZ4oEZl7NPGrL7O3k+u7C/7b39GzD+vTrVDfV6G3PKMHIIRWz
erTxocEtXfEWBJH2kAa2WaqMXNwMp51aFExS7YOEr8qqbyhLHpvB/AlV9E7hOGkl
Nupvn21W+Bb/OlcbkDkQ1natKlBqM2ywNdsVeuigS6Z/0OQ+P6xh08NVYPlaSLY7
OoHTfpWbjw+GuR71aWBVnYQF4yJl8I+rN358HaN3lMSsqO4BA4SNRXNoE/rVpq4z
5Un+LVSXHAJfTC+PL1QdQ9/ZyjYypPfhPmnblOR4CHEdoCSpX8hXmtzMn9Aqm4bW
rlj8cX+lAgMBAAECggEAJaW+eNc/gZq98FMVhksCn0nYMS4ED+Xfg4rfuvhNrrbs
CX21x1OF/sgNpEYoO7QtlLymdWCHsiWUKwKao4YTQX8EGZzJiXjhIH1dHeD8CT8X
DSfPP0ulh2Gdpiu5OX/pZk+1wKTdxb6Ew4oKDG1KmJQ8awfawht2nL++EofSqcVc
FtZ82erDxr3XY1eOGtoUh8IbkJ9vKV9srMMWEQ57mKOHv19isqVzNf2EvcPEkg4K
XyiAL587TtkXClRp7ZfuNzgdR+aRHywulkw7GgouDG/CgeRtu2FoeWVybNRLkYDR
eMEE8lXFCF2VJZaxhYeWY9UKcqpvzPrBc3bNxiVuAQKBgQDj1hHCOCkkcYCZ9Ytk
jWs+X7wD2njW9dJTZz08eCxqawFrptl1H+YyORcaduJO5D0DKwqzu8TYAhkR9+xF
hNIk/t0c5Ph6ghgUKYa4aAsYENq+IDJAFArq6D+GIrnbZJAwv1JSvluti2slKE9s
2UWpR99JlhBBGUeZH9rSE3oDxQKBgQDSd/tx3qYBGikS5FDSIHY4VNXWSGURWf60
ED41B0XLlfGvUt51AjXeq91Z3nG87Sp710Q0ZgYFkYoy6WDDf/dOAN/QzEOsXx10
cY3itB3hmEFxTdCiIf2LIBFCRVqqw+O+UTfG0hK+xgEWGZ+QeMcYiuKOegXBJN5q
Sg/oTPTqYQKBgQCK57SkCMFsqpaRRxbZEy9TM+LZJpWN2QmGN+cpusq5hsuy6mKh
+fTKoevoApsvJg/cop0/vzbfy0eloNW3/KZyT8BXIXIsnqw3fqnYO/ankX8Lc22v
i4isdzRjf0B49fLDBaIXOF+Eiv+kA9OItV63Ok5z+r2mMtdoD/fFJIK7UQKBgB6M
7gHMaNpWGso0PAsUTTTGE7gkEA+huZgXl4AJCzePD2L8q2/en0UwO1Q1NttOrdEG
IU9d09fxFVdoivQ12gcHl3VugRA/Sj5B0W+r5358pFs3CWbPekc8o2S0PoH1J1TT
4z3H9pKcmUHE/GVzMqs8VcCKs9UibeqNz5tPuGlhAoGARJIgx0XmtwxERY7SWWyP
iitZxKphAsWcMb3u59Y4GduGT8VaPh0/qWQ49o5li49v3xXkgthRx9TuIShxlzyM
LnbIjrG5qPXvdsXv7sohjPmrdf8iV/cK1UAiRmCdN8ElDw9NIkUUBWBiANdG8JIu
qHb0ZV2S7BpBeFnOuGgR/AQ=
-----END PRIVATE KEY-----
)PEM";

// --- evidence store ---------------------------------------------------------
// The page ships ONE compact summary per survey run. Kept in RAM; serial EV
// prints it. No NVS, no commit task, no floods.
static char sEvid[1280] = "";
static char sDrv[16] = "";       // serial directive for the page (SCAN/ABRT/PING)
static char sCfg[640] = "";      // CFG=<json> for the page (CFG channel).
                                 // S14P-1923 (S12 rule, sized at max N): the
                                 // widest real CFG = the page's FULL numeric
                                 // key set with every value at its 5-digit max
                                 // (incl. the two new rig keys) = 413 chars
                                 // JSON; 32-key headroom = 490. 128 (the old
                                 // size) truncated it; 640 holds + '\0' +
                                 // slack for future keys — check:
                                 // tools/verify_s12_cfg.py.
static char sNpx[8] = "";        // NPX= override, echoed in hello+drv?
static bool sLogArm = false;     // LOGP arms a 15 s window: logc prints to serial
static uint32_t sLogArmAt = 0;   // (a serial reader must be attached — LOGP is only
                                 //  ever sent while one is, so the CDC ring never fills)
static bool sLogPerm = false;    // LOGA: persistent arm for a dedicated bench reader
                                 //  (the S14L auto-upload fires ~1 s after a burst —
                                 //  a 15 s window is closed by the previous pull's
                                 //  logend, so without LOGA every auto-ship drops)
static char sPageBuild[16] = "";
// S14P-1926: how many drv? replies must still REPLAY sCfg. A fresh firmware
// boot starts at 2, and every hello RE-ARMS it to 2. The ONE-SHOT sCfg drain
// (cleared right after the reply left) was the S14P-1925 failure mode: the
// 08:58 bench CFG landed while the phone was wedged (09:45-10:01 WS outage),
// consumed on that first unseen poll, and the 10:02 reconnecting page hello'd
// into nStr=1 forever — page painted string 1 only. Page applyCfg is
// idempotent, so replaying an already-applied cfg is harmless (apply logged
// only when values actually change).
static uint8_t sCfgReplay = 2;
// status-LED state machine (operator request): breathing OFF during
// experiments (scanning flag from drv? traffic), solid RED until a page
// with the CURRENT build stamps hello (new code ready to load on phone)
static const char PAGE_BUILD[] = "S14R-0001";   // keep in sync with the page BUILD
static bool sBuildMatched = false;             // page hello matched PAGE_BUILD
static volatile bool sScanning = false;        // page sets via drv? flag
// millis() of the last PAINT command (frame/all/black/npx): the operator's
// rule — the status LED must be DARK while a survey runs because its
// breathing is visible in the camera and creates false detections. The
// page's drv? poll is IDLE-ONLY (skipped while scanning), so sScanning
// alone cannot darken the LED during a survey; recent paint activity can.
static uint32_t sLastPaintMs = 0;
// deferred STAT report (S14P-1904): the old inline delay(2000) froze loop()
// for 2 s, which stamped drv? ack waits as 'late' mid-STAT — non-blocking now
static uint32_t statBase = 0, statAt = 0;
static bool statPending = false;


void wsSendJson(httpd_req_t *req, const char* json) {
  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.payload = (uint8_t*)json;
  ws_pkt.len = strlen(json);
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  httpd_ws_send_frame(req, &ws_pkt);
}

// Ack only after loop() has latched a frame carrying this command's epoch —
// the page's timer for the NEXT step starts when the LEDs are physically
// showing the commanded paint. Echoes the page's command id (frame-bits
// uses the u16 epoch from [4..5] of its message, which the page sets from
// the same cmd id space; cmdEpoch is bumped before ackAfterLatch reads it).
void ackAfterLatch(httpd_req_t *req, int id) {
  uint32_t my = cmdEpoch;
  int waits = 0;
  while (appliedEpoch < my && waits < 150) { vTaskDelay(pdMS_TO_TICKS(2)); waits++; }
  char b[64];
  snprintf(b, sizeof(b), "{\"ok\":true,\"id\":%d%s}", id, (appliedEpoch < my) ? ",\"late\":true" : "");
  wsSendJson(req, b);
}

// parse "RRGGBB" -> CRGB
CRGB parseColour(const char* s) {
  if (!s || strlen(s) < 6) return CRGB::Black;
  uint8_t r = 0, g = 0, b = 0;
  auto hx = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  int r0 = hx(s[0]), r1 = hx(s[1]), g0 = hx(s[2]), g1 = hx(s[3]), b0 = hx(s[4]), b1 = hx(s[5]);
  if (r0 < 0 || g0 < 0 || b0 < 0) return CRGB::Black;
  r = r0 * 16 + r1; g = g0 * 16 + g1; b = b0 * 16 + b1;
  return CRGB(r, g, b);
}

// frame-bits: unfold one 246 B message's bit-plane onto the lanes (S14P-1923,
// plan §6/§11.4; S14R-0000: 12-of-24 Golay bank -> 24 planes => 1920-bit
// payload, 240 B, message 246 B). j = global LED id: lane = j/nPerStr, pixel =
// j%nPerStr; ids
// >= nStr*nPerStr ignored. Full-rig repaint at ONE brightness — set bit =
// white at b, clear bit = black. Per-lane write, NO mirroring (the JSON
// cmds keep their nStr<=1 lane1 mirror for bench compat).
void applyBits(const uint8_t* data, httpd_req_t *req) {
  sBitsMode = true;                     // latch guard FIRST: frame-bits owns lane content from
                                        // here (loop()'s pxColour fold must not fight it);
                                        // JSON paints hand ownership back
  const uint8_t braw = data[2];
  const uint32_t epoch = data[4] | (data[5] << 8);    // u16 LE
  const int ps = nPerStr;                             // snapshot: CFG can flip
  const int ns = nStr;                                // mid-paint (WS is a task)
  const uint32_t total = (uint32_t)ns * (uint32_t)ps;
  // per-string polyfuse budget (same 2 A-hold rule as fuseClampB, at the
  // STRING length): a plane lights at most ps px per lane/string.
  int maxB = (int)(255.0f * 2.0f / (ps * 0.0142f));
  if (maxB < 20) maxB = 20;
  const uint8_t b = (braw > maxB) ? (uint8_t)maxB : braw;
  if (b != braw) Serial.printf("[PWR] frame-bits b %d -> %d (%d-px string fuse hold)\n", braw, b, ps);
  for (uint32_t j = 0; j < total; j++) {
    const int l = (int)(j / (uint32_t)ps);
    const int px = (int)(j % (uint32_t)ps);
    const bool on = (data[6 + (j >> 3)] >> (j & 7)) & 1u;
    lanesW[l][px] = on ? CRGB(b, b, b) : CRGB::Black;
  }
  // off-rig pixels stay OFF: rig lanes' tails beyond nPerStr, and lanes
  // beyond nStr entirely (a smaller rig must not carry stale content from a
  // previous larger paint — the mock box resets ALL lanes the same way)
  for (int ln = 0; ln < N_LANES; ln++)
    for (int i = (ln < ns ? ps : 0); i < N_PX; i++) lanesW[ln][i] = CRGB::Black;
  pxBlack();                                          // JSON buffer out of the show path
  frameDirty = true;                                  // loop() latches + stamps the epoch
  cmdEpoch = epoch ? epoch : (cmdEpoch + 1);          // ack epoch = the page's u16 id
  sLastPaintMs = millis();                            // status LED dark while surveying
  ackAfterLatch(req, (int)epoch);
}

// ws handler
esp_err_t ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) return ESP_OK;    // upgrade handshake
  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  if (httpd_ws_recv_frame(req, &ws_pkt, 0) != ESP_OK) return ESP_OK;
  if (ws_pkt.type == HTTPD_WS_TYPE_CONTINUE) return ESP_OK;
  if (ws_pkt.len == 0 || ws_pkt.len > 4096) return ESP_OK;  // 1600-px paints ~2050 B; frame-bits = 230 B
  uint8_t* buf = (uint8_t*)malloc(ws_pkt.len + 1);
  if (!buf) return ESP_OK;
  ws_pkt.payload = buf;
  if (httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len) != ESP_OK) { free(buf); return ESP_OK; }
  buf[ws_pkt.len] = 0;

  // ---- S14R-0000 binary message class: 'frame-bits' ---------------------
  // Exactly 246 B (12-of-24 Golay era, 24 planes; S14P-1928's 18-plane
  // message was 206 B): [0]='B' [1]=ver(1) [2]=b [3]=flags [4..5]=u16 LE
  // epoch [6..245]=bit-plane, 1920 bits. Bit j = LED id j, LSB-first per
  // byte: bit = (data[6+(j>>3)] >> (j&7)) & 1. Set bit = white at b, clear =
  // black. FULL-RIG repaint: lane = j/nPerStr, pixel = j%nPerStr; ids
  // >= nStr*nPerStr are ignored. Per-lane write (NO mirroring). The ack
  // rides the normal latch path with the message's u16 epoch.
  if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
    if (ws_pkt.len != 246 || buf[0] != 'B' || buf[1] != 1) {
      free(buf);
      wsSendJson(req, "{\"err\":\"frame-bits shape\"}");
      return ESP_OK;
    }
    applyBits(buf, req);
    free(buf);
    return ESP_OK;
  }
  if (ws_pkt.type != HTTPD_WS_TYPE_TEXT) { free(buf); return ESP_OK; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, (const char*)buf, ws_pkt.len);
  free(buf);
  if (err) { wsSendJson(req, "{\"err\":\"badjson\"}"); return ESP_OK; }

  const char* cmd = doc["cmd"] | "";
  const int id = doc["id"] | 0;

  if (!strcmp(cmd, "hello")) {
    const char* bld = doc["build"] | "";
    strncpy(sPageBuild, bld, sizeof(sPageBuild) - 1);
    sPageBuild[sizeof(sPageBuild) - 1] = 0;
    if (!strcmp(bld, PAGE_BUILD)) { sBuildMatched = true; }
    // S14P-1926: (re)connect = hello = arm the replay. The NEXT TWO drv?
    // responses re-deliver whatever sCfg holds, so a page that reconnects
    // after an outage ALWAYS converges to the queued rig config (tonight's
    // 10:02 reconnect would have re-received the 08:58 nStr=8 CFG).
    sCfgReplay = 2;
    sScanning = false;
    char b[112];
    snprintf(b, sizeof(b),
             "{\"ok\":true,\"id\":%d,\"fw\":\"poc_survey\",\"px\":%d,\"nStr\":%d,\"nPerStr\":%d}",
             id, nPx, nStr, nPerStr);
    wsSendJson(req, b);
  } else if (!strcmp(cmd, "frame")) {
    // Arbitrary paint: {"cmd":"frame","p":["W",null,"FF0000",...],"b":160}
    // "W" = white at b, null = black, "RRGGBB" = colour at b (b scales all).
    // Single-string bench path: writes lane1, then mirrors it to lanes 2-8
    // ONLY while nStr <= 1 (multi-string rigs paint per-lane via frame-bits).
    sBitsMode = false;                       // JSON paint: legacy lane semantics restored
    int b = fuseClampB(doc["b"] | 160);
    JsonArray arr = doc["p"];
    int i = 0;
    for (JsonVariant v : arr) {
      if (i >= nPx) break;
      if (v.isNull()) { pxColour[i] = CRGB::Black; }
      else {
        const char* s = v | "";
        if (!strcmp(s, "W")) pxColour[i] = CRGB((uint8_t)b, (uint8_t)b, (uint8_t)b);
        else if (!strcmp(s, "H")) {                       // half-bright white:
          uint8_t hb = (uint8_t)(b / 2);                  // the survey page's
          pxColour[i] = CRGB(hb, hb, hb);                 // dimmed neighbour
        }
        else {
          CRGB c = parseColour(s);
          pxColour[i] = CRGB((uint8_t)((int)c.r * b / 255), (uint8_t)((int)c.g * b / 255),
                             (uint8_t)((int)c.b * b / 255));
        }
      }
      i++;
    }
    for (; i < nPx; i++) pxColour[i] = CRGB::Black;   // unpainted slots = black
    // S14P-1923 per-lane era: the JSON plane is string 1's content. nStr<=1
    // keeps the legacy mirror (bench compat); nStr>1 paints ONLY lane 1 and
    // zeroes lanes 2-8 (defined fallback — the rig is otherwise per-lane
    // territory of frame-bits; NO mirroring).
    for (int l = 1; l < N_LANES; l++)
      for (int i = 0; i < nPx; i++)
        lanesW[l][i] = (nStr <= 1) ? pxColour[i] : CRGB::Black;
    frameDirty = true;
    cmdEpoch++;
    sLastPaintMs = millis();                          // status LED dark while surveying
    ackAfterLatch(req, id);
  } else if (!strcmp(cmd, "all")) {
    sBitsMode = false;                       // JSON paint: legacy lane semantics restored
    int b = fuseClampB(doc["b"] | allBright);
    // RIG-WIDE all-on (the command's intent + the fuse note): every lane,
    // every live pixel — NOT a lane1 mirror of string-1 content.
    for (int i = 0; i < nPx; i++) pxColour[i] = CRGB((uint8_t)b, (uint8_t)b, (uint8_t)b);
    for (int l = 0; l < N_LANES; l++)
      for (int i = 0; i < nPx; i++) lanesW[l][i] = CRGB((uint8_t)b, (uint8_t)b, (uint8_t)b);
    frameDirty = true;
    cmdEpoch++;
    sLastPaintMs = millis();
    ackAfterLatch(req, id);
    } else if (!strcmp(cmd, "npx")) {
    sBitsMode = false;                       // JSON paint: legacy lane semantics restored
    int n = doc["n"] | 0;
    if (n >= 1 && n <= N_PX) {
      nPx = n;
      for (int i = 0; i < N_PX; i++) pxColour[i] = CRGB::Black;
      // legacy observable behaviour kept: npx blacks the rig
      for (int l = 0; l < N_LANES; l++) for (int i = 0; i < nPx; i++) lanesW[l][i] = CRGB::Black;
    }
    frameDirty = true;
    cmdEpoch++;
    sLastPaintMs = millis();
    ackAfterLatch(req, id);
  } else if (!strcmp(cmd, "black")) {
    // RIG-WIDE black (see cmd "all")
    sBitsMode = false;                       // JSON paint: legacy lane semantics restored
    for (int i = 0; i < nPx; i++) pxColour[i] = CRGB::Black;
    for (int l = 0; l < N_LANES; l++) for (int i = 0; i < nPx; i++) lanesW[l][i] = CRGB::Black;
    frameDirty = true;
    cmdEpoch++;
    sLastPaintMs = millis();
    ackAfterLatch(req, id);
  } else if (!strcmp(cmd, "logc")) {
    // On-demand log pull, page -> box -> SERIAL ONLY (no NVS, no flood): the
    // bench sends LOGP when a reader is attached, the page then chunks its
    // ring through here; prints are gated on the arm window. LOGA (the bench
    // daemon) arms PERSISTENTLY so a pull's logend never closes the window
    // mid-pull and the S14L auto-ship (~1 s after every burst) reaches serial.
    if ((sLogArm && millis() - sLogArmAt < 15000) || sLogPerm) {
      if (!sLogPerm) sLogArmAt = millis();   // sliding window: idle timeout, not total cap
      const char* d = doc["d"] | "";
      Serial.printf("[PHONE] %s\n", d);
    }
    char b[40];
    snprintf(b, sizeof(b), "{\"ok\":true,\"id\":%d}", id);
    wsSendJson(req, b);
  } else if (!strcmp(cmd, "logend")) {
    if (sLogPerm) {
      Serial.println("[PHONE-LOG] end (persistent arm stays)");  // LOGA: stream ends, window stays
    } else {
      sLogArm = false;
      Serial.println("[PHONE-LOG] end");
    }
    char b[40];
    snprintf(b, sizeof(b), "{\"ok\":true,\"id\":%d}", id);
    wsSendJson(req, b);
  } else if (!strcmp(cmd, "evid")) {
    // ONE bounded evidence summary per survey run (serial pull via EV).
    const char* e = doc["e"] | "";
    strncpy(sEvid, e, sizeof(sEvid) - 1);
    sEvid[sizeof(sEvid) - 1] = 0;
    char b[40];
    snprintf(b, sizeof(b), "{\"ok\":true,\"id\":%d}", id);
    wsSendJson(req, b);
  } else if (!strcmp(cmd, "drv?")) {
    drvPolls++;
    sScanning = doc["scanning"] | false;   // page reports its survey state
    // sCfg carries JSON (quotes!): ESCAPE it for embedding in this response,
    // else the page's JSON.parse fails and the cfg+directive are BOTH lost
    // (bench-verified failure mode: quoted CFG values never applied, and
    // surveys triggered only when a poll split them from the CFG).
    static char cfgEsc[sizeof(sCfg) * 2 + 4];
    const char* r2 = sCfg;
    char* w2 = cfgEsc;
    while (*r2 && (w2 - cfgEsc) < (int)sizeof(cfgEsc) - 2) {
      if (*r2 == '"' || *r2 == (char)92) { *w2++ = (char)92; }
      *w2++ = *r2++;
    }
    *w2 = 0;
    // S14P-1923: sized with the sCfg widening — 640-B cfg worst-case escapes
    // to 1280 chars; the reply overhead is ~70. snprintf truncates safely,
    // but a TRUNCATED cfg silently drops tail keys on the page (S14O shape)
    static char b3[sizeof(cfgEsc) + 128];
    snprintf(b3, sizeof(b3), "{\"ok\":true,\"id\":%d,\"drv\":\"%s\",\"cfg\":\"%s\",\"evid\":%u,\"px\":%d}",
             id, sDrv, cfgEsc, (unsigned)strlen(sEvid), nPx);
    // consume the slots ONLY after the reply got on the wire (S14P-1904):
    // zeroing them BEFORE the send turned every dropped ack/WS hiccup into a
    // silently lost CFG+directive (the S14O 'burst never started' shape).
    // wsSendJson does not report the transport result — send b3 directly so
    // the send's return value can gate the consume.
    httpd_ws_frame_t resp;
    memset(&resp, 0, sizeof(httpd_ws_frame_t));
    resp.payload = (uint8_t*)b3;
    resp.len = strlen(b3);
    resp.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_send_frame(req, &resp) == ESP_OK) {
      // S14P-1926 idempotent-replayable CFG channel: sCfg is NOT one-shot any
      // more. Every reply carries the queued cfg while sCfgReplay > 0 (armed
      // by hello / boot); each delivered copy consumes one replay credit, and
      // sCfg clears only after the LAST replayed delivery. A fresh CFG=
      // always rearms the slot and can never be drained by polls that fired
      // before any hello (the single-slot race behind the S14P-1925 gap).
      sDrv[0] = 0;                       // directives stay ONE-SHOT (unchanged)
      if (sCfgReplay > 0) sCfgReplay--;
      else sCfg[0] = 0;
    }
  } else {
    char b[48];
    snprintf(b, sizeof(b), "{\"err\":\"unknown\",\"id\":%d}", id);
    wsSendJson(req, b);
  }
  return ESP_OK;
}

// page handler: serve the gzipped page with Content-Encoding: gzip
extern uint8_t* pageGz;
esp_err_t page_handler(httpd_req_t *req) {
  if (!pageGz) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page not loaded"); return ESP_OK; }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, (const char*)pageGz, PAGE_GZ_LEN);
  return ESP_OK;
}

// base64 -> bytes
uint8_t* pageGz = nullptr;
static void decodePage() {
  static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t n = strlen(PAGE_GZ_B64);
  pageGz = (uint8_t*)malloc(PAGE_GZ_LEN + 8);
  if (!pageGz) return;
  int acc = 0, bits = 0; size_t o = 0;
  for (size_t i = 0; i < n; i++) {
    const char* f = strchr(B64, PAGE_GZ_B64[i]);
    if (!f) continue;
    acc = (acc << 6) | (f - B64); bits += 6;
    if (bits >= 8) { bits -= 8; if (o < PAGE_GZ_LEN) pageGz[o++] = (acc >> bits) & 0xFF; }
  }
  Serial.printf("page decoded: %u/%u bytes\n", (unsigned)o, (unsigned)PAGE_GZ_LEN);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== poc_survey S14R-0001: operator UI round + exposure levers at burst start ===");

  FastLED.addLeds<WS2815, LANE1_PIN, RGB>(lane1, N_PX);   // bench chips are RGB-wired
  FastLED.addLeds<WS2815, LANE2_PIN, RGB>(lane2, N_PX);
  FastLED.addLeds<WS2815, LANE3_PIN, RGB>(lane3, N_PX);
  FastLED.addLeds<WS2815, LANE4_PIN, RGB>(lane4, N_PX);
  FastLED.addLeds<WS2815, LANE5_PIN, RGB>(lane5, N_PX);
  FastLED.addLeds<WS2815, LANE6_PIN, RGB>(lane6, N_PX);
  FastLED.addLeds<WS2815, LANE7_PIN, RGB>(lane7, N_PX);
  FastLED.addLeds<WS2815, LANE8_PIN, RGB>(lane8, N_PX);
  // 8 lanes x 200 px @ b150 white ~ 8 x 0.9 A ~ 7 A worst case: the limiter
  // scales all lanes together before the first power draw can trip anything.
  // Power budget (operator, 27 Sep): supply 15 A across 8 lanes; each
  // string polyfuse holds 2 A (trips 4 A). Measured 14.2 mA/px full white
  // (teardown x3) -> b150 all-8 = 10.0 A (67% supply, 63% fuse); the
  // limiter is a BACKSTOP (20 mA/px default overestimates ours), the
  // per-string fuse budget is enforced in the paint handlers.
  FastLED.setMaxPowerInVoltsAndMilliamps(12, 15000);
  for (int l = 0; l < N_LANES; l++) for (int i = 0; i < N_PX; i++) lanesW[l][i] = CRGB::Black;
  for (int i = 0; i < N_PX; i++) pxColour[i] = CRGB::Black;
  FastLED.show();

  decodePage();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP up, IP: ");
  Serial.println(WiFi.softAPIP());

  httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
  conf.servercert = (const uint8_t*)CERT_PEM;
  conf.servercert_len = strlen(CERT_PEM) + 1;
  conf.prvtkey_pem = (const uint8_t*)KEY_PEM;
  conf.prvtkey_len = strlen(KEY_PEM) + 1;
  conf.httpd.max_open_sockets = 4;
  httpd_handle_t srv = nullptr;
  esp_err_t e = httpd_ssl_start(&srv, &conf);
  if (e != ESP_OK) { Serial.printf("https start FAILED: %d\n", e); return; }

  httpd_uri_t uriPage = { .uri = "/", .method = HTTP_GET, .handler = page_handler, .user_ctx = nullptr };
  httpd_uri_t uriWs   = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = nullptr,
                          .is_websocket = true, .handle_ws_control_frames = true };
  httpd_register_uri_handler(srv, &uriPage);
  httpd_register_uri_handler(srv, &uriWs);
  Serial.println("HTTPS+wss on :443 (page: https://192.168.4.1/)");
}

void loop() {
  // 25 fps: latch the per-lane rigs to the LEDs and show. Content only
  // changes on commands, but steady pacing keeps the cadence constant for
  // the camera.
  //
  // S14P-1923 per-lane era (plan §11.4): the OLD lane1-vs-pxColour compare
  // MIRRORED lane 1 to all 8 lanes every latch — that is the single-string
  // bench structure. With frame-bits the lanes carry INDEPENDENT content
  // (lane = j/nPerStr): the latch gate must see all 8 lanes. frameDirty is
  // set by every paint command (frame-bits sets it BEFORE bumping
  // cmdEpoch); the 2 Hz refresh is unchanged. Latched content:
  //   nStr<=1 AND !sBitsMode: lane1 is master; lanes[0] copies pxColour;
  //            lanes 2-8 keep their legacy mirror (JSON cmds already mirror
  //            them). The JSON paints clear sBitsMode, so legacy behavior.
  //   nStr<=1 AND sBitsMode: frame-bits OWNS lane1's content too — the
  //            pxColour fold must NOT run (1923 bug: pxBlack() left the
  //            fold painting blacked pxColour over the plane every latch).
  //   nStr>1:  every lane rides its own frame-bits content; pxColour is
  //            out of the show path (applyBits cleared it).
  static uint32_t lastShow = 0;
  static uint32_t lastLatch = 0;
  uint32_t now = millis();
  if (now - lastShow >= FRAME_MS) {
    lastShow = now;
    bool changed = false;
    if (nStr <= 1 && !sBitsMode) {                 // 1924: bits mode owns lane1 at ALL nStr
      for (int i = 0; i < nPx; i++) if (lane1[i] != pxColour[i]) { changed = true; break; }
    }
    if (frameDirty || changed || (now - lastLatch > 500)) {   // latch on change + 2 Hz refresh
      if (nStr <= 1 && !sBitsMode)                           // string-1 master paint (JSON era only)
        for (int i = 0; i < nPx; i++) lanesW[0][i] = pxColour[i];
      // sBitsMode or nStr>1: nothing to fold in — lanes already hold their
      // frame-bits content; this latch exists to SHOW and stamp the epoch.
      FastLED.show();
      appliedEpoch = cmdEpoch;                  // this frame carries the latest command
      frameDirty = false;
      lastLatch = now;
    }
  }

  // status LED state machine (operator request):
  //   solid RED   = new page build flashed, not yet loaded on the phone
  //   OFF         = survey/experiment running (page reports via drv?)
  //   slow breathe (green) = idle, page up-to-date
  static uint32_t lastBeat = 0;
  static uint8_t beat = 0;
  if (millis() - lastBeat > 10) {
    lastBeat = millis(); beat += 1;
    if (!sBuildMatched) {
      rgbLedWriteOrdered(STATUS_PIN, LED_COLOR_ORDER_RGB, 255, 0, 0);   // red: load me
    } else if (sScanning || millis() - sLastPaintMs < 3000) {
      rgbLedWriteOrdered(STATUS_PIN, LED_COLOR_ORDER_RGB, 0, 0, 0);     // dark: experimenting (or a survey painted <3 s ago)
    } else {
      rgbLedWriteOrdered(STATUS_PIN, LED_COLOR_ORDER_RGB, 0, beatsin8(12, 0, 50), 0);
    }
  }

  // deferred STAT report (non-blocking; sampled 2 s ago)
  if (statPending && (int32_t)(millis() - statAt) >= 0) {
    statPending = false;
    uint32_t d = drvPolls - statBase;
    Serial.printf("[STAT] polls in 2s: %u, page build '%s', evid %u B -> %s\n",
                  (unsigned)d, sPageBuild, (unsigned)strlen(sEvid),
                  d >= 1 ? "ALIVE" : "GONE (screen asleep / tab closed / WS down)");
  }

  // serial directives
  static char sLine[160]; static int sLineN = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (sLineN) {
        sLine[sLineN] = 0;
        if (!strncmp(sLine, "SCAN", 4)) { strncpy(sDrv, "SCAN", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0; Serial.println("[DRV] SCAN queued"); }
        else if (!strncmp(sLine, "ABRT", 4)) { strncpy(sDrv, "ABRT", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0; Serial.println("[DRV] ABRT queued"); }
        else if (!strncmp(sLine, "PING", 4)) { strncpy(sDrv, "PING", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0; Serial.println("[DRV] PING queued"); }
        else if (!strncmp(sLine, "BURST", 5)) { strncpy(sDrv, "BURST", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0; Serial.println("[DRV] BURST queued"); }
        else if (!strncmp(sLine, "BRAMP", 5)) { strncpy(sDrv, "BRAMP", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0; Serial.println("[DRV] BRAMP queued"); }
        else if (!strncmp(sLine, "NPX=", 4)) {
          int v = atoi(sLine + 4);
          if (v >= 1 && v <= N_PX) { nPx = v; Serial.printf("[NPX] %d\n", v); }
          else Serial.println("[NPX] out of range 1-N_PX");
        }
        else if (!strncmp(sLine, "FRAMP", 5)) {
          sLogArm = true; sLogArmAt = millis();
          strncpy(sDrv, "FRAMP", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0;
          Serial.println("[FRAME-PULL] begin");
        }
        else if (!strncmp(sLine, "LOGP", 4)) {
          sLogArm = true; sLogArmAt = millis();
          strncpy(sDrv, "LOGP", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0;
          Serial.println("[PHONE-LOG] begin");
        }
        else if (!strncmp(sLine, "LOGA", 4)) {   // persistent arm (bench daemon)
          sLogPerm = true; sLogArm = true; sLogArmAt = millis();
          Serial.println("[LOGA] persistent arm ON");
        }
        else if (!strncmp(sLine, "LOGX", 4)) {   // release the persistent arm
          sLogPerm = false; sLogArm = false;
          Serial.println("[LOGA] persistent arm OFF");
        }
        else if (!strncmp(sLine, "CFG=", 4)) {
          // S14P-1923 (plan §6): the rig keys ride the CFG channel. Numeric
          // nStr / nPerStr are applied box-side; the FULL json still queues
          // to the page so its CFG object stays the single source (the page
          // clamps cwcN to its product cap and reads nStr/nPerStr from hello).
          strncpy(sCfg, sLine + 4, sizeof(sCfg) - 1); sCfg[sizeof(sCfg) - 1] = 0;
          const char* j = strchr(sCfg, '{');
          if (j) {
            // S14P-1924 parse fix: k1/k2 point at the OPENING quote of the
            // key ("nStr"/"nPerStr"); the digits start after `"key":` =
            // +7 / +10 (1923's +6/+9 landed on the COLON and atoi()=0
            // failed the range check — box-side nStr never left 1;
            // parity gate: tools/verify_cfg_parse.py).
            const char* k1 = strstr(j, "\"nStr\"");
            if (k1) { int v = atoi(k1 + 7); if (v >= 1 && v <= N_LANES) nStr = v; }
            const char* k2 = strstr(j, "\"nPerStr\"");
            if (k2) { int v = atoi(k2 + 10); if (v >= 1 && v <= N_PX) nPerStr = v; }
          }
          Serial.printf("[CFG] nStr=%d nPerStr=%d queued: %s\n", nStr, nPerStr, sCfg);
          // S14P-1926: fresh payload re-arms the replay window (boot armed it too)
          sCfgReplay = 2;
        }
        else if (!strncmp(sLine, "STAT", 4)) {
          // deferred: sample drvPolls now, report in 2 s WITHOUT delay(2000)
          // (blocking loop() stamped drv? ack latches mid-sample)
          statBase = drvPolls; statAt = millis() + 2000; statPending = true;
        }
        else if (!strncmp(sLine, "EV", 2)) {
          Serial.printf("[EVID] %s\n", sEvid[0] ? sEvid : "(none yet)");
        }
        sLineN = 0;
      }
    } else if (sLineN < 159) sLine[sLineN++] = c;
  }
}