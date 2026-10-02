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
volatile int nStr = 1;              // S14P-1923 (plan §6): active strings 1-8 (CFG key nStr);
                                    // the frame-bits rig = nStr*nPerStr global LED ids
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
  "vNLn8GuShlv8d14uF8//H0Xdqq9I2AEA"
;
static const unsigned PAGE_GZ_LEN = 36447;

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
static const char PAGE_BUILD[] = "S14P-1926";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1926: CFG replay on hello + rig-mismatch guard ===");

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