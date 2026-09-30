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
static const unsigned PAGE_GZ_LEN = 29752;

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
static const char PAGE_BUILD[] = "S14P-1906";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1906: masked-gate fix + dedup ===");

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