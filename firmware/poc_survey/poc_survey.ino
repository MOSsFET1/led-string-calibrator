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
static const unsigned PAGE_GZ_LEN = 30585;

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
static const char PAGE_BUILD[] = "S14P-1909";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1909: handheld motion guard ===");

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