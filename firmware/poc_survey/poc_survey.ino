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
  "3p4+FqLP6WmaBiX+LopV/Pz/APJi0J8ooAEA"
;
static const unsigned PAGE_GZ_LEN = 31263;

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
static const char PAGE_BUILD[] = "S14P-1910";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1910: parabolic sub-peak registration ===");

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