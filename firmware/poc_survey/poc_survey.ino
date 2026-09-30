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
volatile int nPx = 200;             // DEFAULT 200 (operator, 30 Sep), max N_PX=200; runtime via npx cmd / NPX=
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
  "UNLfRbGK3v4PqipMINaMAQA="
;
static const unsigned PAGE_GZ_LEN = 29372;

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
static const char PAGE_BUILD[] = "S14P-1905";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1905: chained reg + in-page decode ===");

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
        else if (!strncmp(sLine, "CFG=", 4)) { strncpy(sCfg, sLine + 4, sizeof(sCfg) - 1); sCfg[sizeof(sCfg) - 1] = 0; Serial.printf("[CFG] queued: %s\n", sCfg); }
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