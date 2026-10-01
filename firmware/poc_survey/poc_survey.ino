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
static const unsigned PAGE_GZ_LEN = 34998;

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
// status-LED state machine (operator request): breathing OFF during
// experiments (scanning flag from drv? traffic), solid RED until a page
// with the CURRENT build stamps hello (new code ready to load on phone)
static const char PAGE_BUILD[] = "S14P-1923";   // keep in sync with the page BUILD
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
    httpd_ws_send_frame(req, &resp);
    sDrv[0] = 0; sCfg[0] = 0;
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
  Serial.println("\n=== poc_survey S14P-1923: frame-bits per-lane rig + nStr/nPerStr CFG ===");

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
  //   nStr<=1: lane1 is master; lanes[0] copies pxColour; lanes 2-8 keep
  //            their legacy mirror (JSON cmds already mirror them).
  //   nStr>1:  every lane rides its own frame-bits content; pxColour is
  //            out of the show path (applyBits cleared it).
  static uint32_t lastShow = 0;
  static uint32_t lastLatch = 0;
  uint32_t now = millis();
  if (now - lastShow >= FRAME_MS) {
    lastShow = now;
    bool changed = false;
    if (nStr <= 1) {
      for (int i = 0; i < nPx; i++) if (lane1[i] != pxColour[i]) { changed = true; break; }
    }
    if (frameDirty || changed || (now - lastLatch > 500)) {   // latch on change + 2 Hz refresh
      if (nStr <= 1)                                          // string-1 master paint
        for (int i = 0; i < nPx; i++) lanesW[0][i] = pxColour[i];
      // nStr>1: nothing to fold in — lanes already hold their frame-bits
      // content; this latch exists to SHOW and stamp the epoch.
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
            const char* k1 = strstr(j, "\"nStr\"");
            if (k1) { int v = atoi(k1 + 6); if (v >= 1 && v <= N_LANES) nStr = v; }
            const char* k2 = strstr(j, "\"nPerStr\"");
            if (k2) { int v = atoi(k2 + 9); if (v >= 1 && v <= N_PX) nPerStr = v; }
          }
          Serial.printf("[CFG] queued: %s\n", sCfg);
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