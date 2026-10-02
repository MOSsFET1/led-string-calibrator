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
static const unsigned PAGE_GZ_LEN = 37951;

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
static const char PAGE_BUILD[] = "S14P-1928";   // keep in sync with the page BUILD
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

// frame-bits: unfold one 206 B bit-plane onto the lanes (S14P-1923, plan
// §6/§11.4). j = global LED id: lane = j/nPerStr, pixel = j%nPerStr; ids
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
  if (ws_pkt.len == 0 || ws_pkt.len > 4096) return ESP_OK;  // 200-px paint ~= 2050 B
  uint8_t* buf = (uint8_t*)malloc(ws_pkt.len + 1);
  if (!buf) return ESP_OK;
  ws_pkt.payload = buf;
  if (httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len) != ESP_OK) { free(buf); return ESP_OK; }
  buf[ws_pkt.len] = 0;

  // ---- S14P-1923 binary message class: 'frame-bits' ---------------------
  // Exactly 206 B: [0]='B' [1]=ver(1) [2]=b [3]=flags[4..5]=u16 LE epoch
  // [6..205]=bit-plane, 1600 bits. Bit j = LED id j, LSB-first per byte:
  // bit = (data[6+(j>>3)] >> (j&7)) & 1. Set bit = white at b, clear =
  // black. FULL-RIG repaint: lane = j/nPerStr, pixel = j%nPerStr; ids
  // >= nStr*nPerStr are ignored. Per-lane write (NO mirroring). The ack
  // rides the normal latch path with the message's u16 epoch.
  if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
    if (ws_pkt.len != 206 || buf[0] != 'B' || buf[1] != 1) {
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
  Serial.println("\n=== poc_survey S14P-1928: capture-only bursts + manual bulk send ===");

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