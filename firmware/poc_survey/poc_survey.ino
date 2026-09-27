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

CRGB lane1[N_PX];                    // lane 1 (the bench string)
CRGB lane2[N_PX], lane3[N_PX], lane4[N_PX];   // lanes 2-8 mirror lane 1 (one
CRGB lane5[N_PX], lane6[N_PX], lane7[N_PX], lane8[N_PX];  // logical string per lane)
volatile int nPx = 150;             // DEFAULT 150 (operator), max N_PX=200; runtime via npx cmd / NPX=
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
CRGB pxColour[N_PX];                 // the CURRENT paint, applied every frame

// --- embedded assets (replaced by pack_page.py) ----------------------------
static const char PAGE_GZ_B64[] =
  "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"
;
static const unsigned PAGE_GZ_LEN = 23757;

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
static char sCfg[128] = "";      // CFG=<json> for the page (CFG channel)
static char sNpx[8] = "";        // NPX= override, echoed in hello+drv?
static bool sLogArm = false;     // LOGP arms a 15 s window: logc prints to serial
static uint32_t sLogArmAt = 0;   // (a serial reader must be attached — LOGP is only
                                 //  ever sent while one is, so the CDC ring never fills)
static char sPageBuild[16] = "";
// status-LED state machine (operator request): breathing OFF during
// experiments (scanning flag from drv? traffic), solid RED until a page
// with the CURRENT build stamps hello (new code ready to load on phone)
static const char PAGE_BUILD[] = "S14L-1900";   // keep in sync with the page BUILD
static bool sBuildMatched = false;             // page hello matched PAGE_BUILD
static volatile bool sScanning = false;        // page sets via drv? flag
// millis() of the last PAINT command (frame/all/black/npx): the operator's
// rule — the status LED must be DARK while a survey runs because its
// breathing is visible in the camera and creates false detections. The
// page's drv? poll is IDLE-ONLY (skipped while scanning), so sScanning
// alone cannot darken the LED during a survey; recent paint activity can.
static uint32_t sLastPaintMs = 0;


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
// showing the commanded paint. Echoes the page's command id.
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
    char b[80];
    snprintf(b, sizeof(b), "{\"ok\":true,\"id\":%d,\"fw\":\"poc_survey\",\"px\":%d}", id, nPx);
    wsSendJson(req, b);
  } else if (!strcmp(cmd, "frame")) {
    // Arbitrary paint: {"cmd":"frame","p":["W",null,"FF0000",...],"b":160}
    // "W" = white at b, null = black, "RRGGBB" = colour at b (b scales all).
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
    cmdEpoch++;
    sLastPaintMs = millis();                          // status LED dark while surveying
    ackAfterLatch(req, id);
  } else if (!strcmp(cmd, "all")) {
    int b = fuseClampB(doc["b"] | allBright);
    for (int i = 0; i < nPx; i++) pxColour[i] = CRGB((uint8_t)b, (uint8_t)b, (uint8_t)b);
    cmdEpoch++;
    sLastPaintMs = millis();
    ackAfterLatch(req, id);
    } else if (!strcmp(cmd, "npx")) {
    int n = doc["n"] | 0;
    if (n >= 1 && n <= N_PX) { nPx = n; for (int i = 0; i < N_PX; i++) pxColour[i] = CRGB::Black; }
    cmdEpoch++;
    sLastPaintMs = millis();
    ackAfterLatch(req, id);
  } else if (!strcmp(cmd, "black")) {
    for (int i = 0; i < nPx; i++) pxColour[i] = CRGB::Black;
    cmdEpoch++;
    sLastPaintMs = millis();
    ackAfterLatch(req, id);
  } else if (!strcmp(cmd, "logc")) {
    // On-demand log pull, page -> box -> SERIAL ONLY (no NVS, no flood): the
    // bench sends LOGP when a reader is attached, the page then chunks its
    // ring through here; prints are gated on the arm window.
    if (sLogArm && millis() - sLogArmAt < 15000) {
      sLogArmAt = millis();          // sliding window: idle timeout, not total cap
      const char* d = doc["d"] | "";
      Serial.printf("[PHONE] %s\n", d);
    }
    char b[40];
    snprintf(b, sizeof(b), "{\"ok\":true,\"id\":%d}", id);
    wsSendJson(req, b);
  } else if (!strcmp(cmd, "logend")) {
    sLogArm = false;
    Serial.println("[PHONE-LOG] end");
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
    char b3[256];
    snprintf(b3, sizeof(b3), "{\"ok\":true,\"id\":%d,\"drv\":\"%s\",\"cfg\":\"%s\",\"evid\":%u,\"px\":%d}",
             id, sDrv, cfgEsc, (unsigned)strlen(sEvid), nPx);
    sDrv[0] = 0; sCfg[0] = 0;
    wsSendJson(req, b3);
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
  Serial.println("\n=== poc_survey S10: drv? escapes cfg JSON (quotes) ===");

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
  CRGB* lanes[N_LANES] = { lane1, lane2, lane3, lane4, lane5, lane6, lane7, lane8 };
  for (int l = 0; l < N_LANES; l++) for (int i = 0; i < N_PX; i++) lanes[l][i] = CRGB::Black;
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
  // 25 fps: latch pxColour into lane1 and show. Content only changes on
  // commands, but steady pacing keeps the cadence constant for the camera.
  static uint32_t lastShow = 0;
  static uint32_t lastLatch = 0;
  uint32_t now = millis();
  if (now - lastShow >= FRAME_MS) {
    lastShow = now;
    static CRGB* lanes[N_LANES] = { lane1, lane2, lane3, lane4, lane5, lane6, lane7, lane8 };
    bool changed = false;
    for (int i = 0; i < nPx; i++) if (lane1[i] != pxColour[i]) { changed = true; break; }
    if (changed || (now - lastLatch > 500)) {   // latch on change + 2 Hz refresh
      for (int l = 0; l < N_LANES; l++)         // every lane mirrors the same paint
        for (int i = 0; i < nPx; i++) lanes[l][i] = pxColour[i];
      FastLED.show();
      appliedEpoch = cmdEpoch;                  // this frame carries the latest command
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
        else if (!strncmp(sLine, "CFG=", 4)) { strncpy(sCfg, sLine + 4, sizeof(sCfg) - 1); sCfg[sizeof(sCfg) - 1] = 0; Serial.printf("[CFG] queued: %s\n", sCfg); }
        else if (!strncmp(sLine, "STAT", 4)) {
          uint32_t p0 = drvPolls;
          delay(2000);
          uint32_t d = drvPolls - p0;
          Serial.printf("[STAT] polls in 2s: %u, page build '%s', evid %u B -> %s\n",
                        (unsigned)d, sPageBuild, (unsigned)strlen(sEvid),
                        d >= 1 ? "ALIVE" : "GONE (screen asleep / tab closed / WS down)");
        }
        else if (!strncmp(sLine, "EV", 2)) {
          Serial.printf("[EVID] %s\n", sEvid[0] ? sEvid : "(none yet)");
        }
        sLineN = 0;
      }
    } else if (sLineN < 159) sLine[sLineN++] = c;
  }
}