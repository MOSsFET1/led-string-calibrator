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
  "gHmv+I9RAQA="
;
static const unsigned PAGE_GZ_LEN = 23207;

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
static const char PAGE_BUILD[] = "S14K-1900";   // keep in sync with the page BUILD
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