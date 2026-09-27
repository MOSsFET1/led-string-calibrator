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
static const unsigned PAGE_GZ_LEN = 23810;

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
static bool sLogPerm = false;    // LOGA: persistent arm for a dedicated bench reader
                                 //  (the S14L auto-upload fires ~1 s after a burst —
                                 //  a 15 s window is closed by the previous pull's
                                 //  logend, so without LOGA every auto-ship drops)
static char sPageBuild[16] = "";
// status-LED state machine (operator request): breathing OFF during
// experiments (scanning flag from drv? traffic), solid RED until a page
// with the CURRENT build stamps hello (new code ready to load on phone)
static const char PAGE_BUILD[] = "S14M-1900";   // keep in sync with the page BUILD
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
        else if (!strncmp(sLine, "LOGA", 4)) {   // persistent arm (bench daemon)
          sLogPerm = true; sLogArm = true; sLogArmAt = millis();
          Serial.println("[LOGA] persistent arm ON");
        }
        else if (!strncmp(sLine, "LOGX", 4)) {   // release the persistent arm
          sLogPerm = false; sLogArm = false;
          Serial.println("[LOGA] persistent arm OFF");
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