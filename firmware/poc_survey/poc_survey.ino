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
static const unsigned PAGE_GZ_LEN = 40686;

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
static const char PAGE_BUILD[] = "S14R-0000";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14R-0000: 12-of-24 Golay bank + 24-plane bursts ===");

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