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
static const unsigned PAGE_GZ_LEN = 32884;

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
static const char PAGE_BUILD[] = "S14P-1919";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1919: smaller CWC suppression radius + spatial conflict audit ===");

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