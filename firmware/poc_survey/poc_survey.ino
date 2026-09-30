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
static const unsigned PAGE_GZ_LEN = 30017;

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
static const char PAGE_BUILD[] = "S14P-1908";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1908: smaller site labels, translucent boxes ===");

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