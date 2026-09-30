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
;
static const unsigned PAGE_GZ_LEN = 31562;

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
static const char PAGE_BUILD[] = "S14P-1911";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1911: direct per-plane registration ===");

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