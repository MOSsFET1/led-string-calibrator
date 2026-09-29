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
  "AA=="
;
static const unsigned PAGE_GZ_LEN = 25309;

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
static const char PAGE_BUILD[] = "S14P-1903";   // keep in sync with the page BUILD
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