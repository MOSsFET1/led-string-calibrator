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
static const unsigned PAGE_GZ_LEN = 32923;

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
static const char PAGE_BUILD[] = "S14P-1922";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1922: box-blur + suppress R=1, conflict audit ===");

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