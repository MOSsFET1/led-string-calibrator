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

#define LANE1_PIN   1
#define N_PX        200           // max string length (runtime npx / serial NPX=)
#define STATUS_PIN  8
#define FRAME_MS    40            // 25 fps pacing

#define SERIAL_DIAG 0              // 1 ONLY with a serial reader attached
                                  // (unread CDC TX ring blocks ~2 s per print)

const char* AP_SSID = "LED-SURVEY";
const char* AP_PASS = "survey2026";   // WPA2 needs >= 8 chars; the old 6-char
                                      // "survey" made softAP come up OPEN

CRGB lane1[N_PX];
volatile int nPx = 150;             // DEFAULT 150 (operator), max N_PX=200; runtime via npx cmd / NPX=
volatile uint32_t cmdEpoch = 0;      // bumped by every state-changing command
volatile uint32_t appliedEpoch = 0;  // stamped by loop() once a frame latches it
volatile uint32_t drvPolls = 0;      // page liveness (STAT)
volatile int allBright = 160;        // default all-on brightness (CFG.allB)
CRGB pxColour[N_PX];                 // the CURRENT paint, applied every frame

// --- embedded assets (replaced by pack_page.py) ----------------------------
static const char PAGE_GZ_B64[] =
  "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"
  "BwEA"
;
static const unsigned PAGE_GZ_LEN = 21549;

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
static char sPageBuild[16] = "";
// status-LED state machine (operator request): breathing OFF during
// experiments (scanning flag from drv? traffic), solid RED until a page
// with the CURRENT build stamps hello (new code ready to load on phone)
static const char PAGE_BUILD[] = "S13L-1900";   // keep in sync with the page BUILD
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
    int b = doc["b"] | 160;
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
    int b = doc["b"] | allBright;
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
    // ring through here; prints are gated on the arm window.
    if (sLogArm && millis() - sLogArmAt < 15000) {
      sLogArmAt = millis();          // sliding window: idle timeout, not total cap
      const char* d = doc["d"] | "";
      Serial.printf("[PHONE] %s\n", d);
    }
    char b[40];
    snprintf(b, sizeof(b), "{\"ok\":true,\"id\":%d}", id);
    wsSendJson(req, b);
  } else if (!strcmp(cmd, "logend")) {
    sLogArm = false;
    Serial.println("[PHONE-LOG] end");
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
  FastLED.setMaxPowerInVoltsAndMilliamps(12, 2000);
  for (int i = 0; i < N_PX; i++) { lane1[i] = CRGB::Black; pxColour[i] = CRGB::Black; }
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
    bool changed = false;
    for (int i = 0; i < nPx; i++) if (lane1[i] != pxColour[i]) { changed = true; break; }
    if (changed || (now - lastLatch > 500)) {   // latch on change + 2 Hz refresh
      for (int i = 0; i < nPx; i++) lane1[i] = pxColour[i];
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