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

CRGB lane1[N_PX];                    // lane 1 / string 1 (also the JSON paint target)
CRGB lane2[N_PX], lane3[N_PX], lane4[N_PX];   // S14P-1923: per-lane paint era —
CRGB lane5[N_PX], lane6[N_PX], lane7[N_PX], lane8[N_PX];  // frame-bits writes each
                                     // lane directly (plan §11.4); JSON cmds
                                     // mirror lane1 to lanes 2-8 ONLY while
                                     // nStr<=1 (single-string bench compat)
volatile int nPx = 200;             // DEFAULT 200 (operator, 30 Sep), max N_PX=200; runtime via npx cmd / NPX=
volatile int nStr = 8;              // S14P-1927 (plan §6): DEFAULT = the full 8-string rig
                                    // (8 x 200 = 1600 global LED ids); CFG key nStr narrows
                                    // it (1-8) for bench/partial rigs. The frame-bits rig =
volatile int nPerStr = 200;         // string length for the bit-plane rig (CFG key nPerStr), 1-N_PX
volatile bool frameDirty = false;   // a paint command touched buffers/lanes since the last latch
volatile bool sBitsMode = false;    // S14P-1924 latch guard: TRUE while frame-bits owns the lanes
                                    // (applyBits sets it BEFORE the lane writes); the JSON paints
                                    // (frame/all/black/npx) clear it. loop() folds pxColour into
                                    // lane1 ONLY when nStr<=1 AND !sBitsMode — bits mode owns lane
                                    // content at ALL nStr values (1923 bug: the fold repainted
                                    // lane1 from the blacked pxColour every latch)
CRGB* lanesW[N_LANES] = { lane1, lane2, lane3, lane4,
                          lane5, lane6, lane7, lane8 };   // per-lane write view
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
CRGB pxColour[N_PX];                 // the CURRENT paint, applied every frame (JSON cmds;
                                     // out of the show path when frame-bits owns the rig —
                                     // pxBlack() clears it so the nStr<=1 latch gate can't
                                     // resurrect stale JSON content over per-lane planes)

// JSON-paint buffer -> black (applyBits: pxColour must not fight the lanes)
inline void pxBlack() { for (int i = 0; i < N_PX; i++) pxColour[i] = CRGB::Black; }

// --- embedded assets (replaced by pack_page.py) ----------------------------
static const char PAGE_GZ_B64[] =
  "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"
  "W+DNAdUWWmR464BqBy/W8HZ2GegHqdTw3pb7i2XXuLkfubeNgQeaJc7vRz623jfdrFup83tkWQfx"
  "2uaRAjK8EahSLj1U/d8j20KRDi/JzyPbfZTK/c59rf2Wg4M9bduuMj6WhVJP2xZ8oMG7W+hB26jG"
  "4W0nK3vqvmaJ3qwXv3natukDE96z9/dIt8eMiz2OLPCSv289aFu/L1tpv+3kPE/dh27cf30o7XJ3"
  "zc2nAxNeue/YuITUDu/JF3if2PxBlTPq61st9y0e6vDyddNH2877VW7T7FXNMQ5bs9wnrtwHbbn/"
  "yICXr4fVDx+z4fekCTg0ydCAl6/PPdx2X5Uu6Jvrze378T1t//VhA9/7Un9rD0yBN9d31Ns9PbmC"
  "L9yfPtD8Gtc4PFPtr3SnbH09u33/u3qbZn3b7mDbfnbV0qw7PNCdcQt+V6LPwbb7Q3XDt9/cF/8F"
  "+jxU1ZRrnEe9Bf6guV/euOLNgJfpWT9cyIbfk64jHphXEhjw8u3IerKlMf4n6nXQA1MNqvDq+ovk"
  "lWnrLzK8fH/0wNi/o+F3VQYebMlX0R3dCvu28yd1R7qvwG+Tn/Jg1AW+IJ+1NDTXSIa0tEcaNtd2"
  "f5AKf6LT26E930mFV/Af2/YLy6EeT2FI+3mVMnxfYUj9/MmBCq9dx60nT6rtqb4o6I9s8VU5dNnX"
  "OnxkOQ9Tuw1Ug9fXGXvG7Z5GgRPbfBkT47aNKzIbeOVuHgI2zu9Vxl8xiw7rAie29XdP359YwRv7"
  "Ew14WT739c0sFniZffvmkTIGvDya/W33X0grVzK59S1H0EjwkvkiwA+35Fsq8F7F7n39MDELvJSP"
  "oS3CboF3JX3dN68wNuDV/m7Zj+/p+wcb+ONt+lfdaiEXMM9DqOG1++77W86Hl1NN+pIC62/ZH+0Z"
  "+6EGEvzxFvo/0fIxXCX4Y46/MXCucoiKFd5TNaq6v0kbTyNw6Zr7oQ5qeOViJwE82HLelAIvzdZg"
  "y303amaWxO8D+/5fLZXRk2tQz2GT8evzrmZ56vJZO3pChXfN+T009IirnuFkwBsHtbhyMGGwDf5A"
  "IdCBKs8VeH3jhwLf0/jx0DT0dXhXHk9jG71rBivk9pjn8Lv6+YEDFf5Eixu7SjBhYODX4y1GlvZA"
  "bf+JJrdd/Xw/sz0GuxhXZkjwhqHsmvs1+jW8eY6Kq57ZpvXXvPfH1YMVSn+PjbwdVwk+DGz4Nfun"
  "b15pocIfmOSmeYUSvOH4ueb+jgq/+oHP1oHlPqAjE16kK7ryjYGGvNJaelQXOLHb5/pIyPCq/XM/"
  "2pmukqCM04QFT8HLuHQWUdhm2cJPohb7tMNYHpWrPGFPcRKmT92z27MPZ1fnF+MPJ1ev3OM7gL7v"
  "FtkCCu62d1vdIl1GziM7fc724O/T0wrT75jLhqw32vmsVPgev77346R0sjZL3kRh0WYTXvH+Phu7"
  "/fcd98Q7GDKomOMqWB6HESvnEZvEiZ9v2DT3l1FnEpcFW0ZF4c8iFiz8omBOmkTM6x2ylxzd7bgG"
  "yKKco2M//vnv7Pfjq3csu7vv+Hnubwr2lK4WIUvSkhWBv4DKUuYe9nosDotWl93MI44v8zeL1A9Z"
  "XDAf6ul1JpsSW1V2CPUQf7KP7JTNFunEX7A3F+eAgn1sszfjl51pnBclNoQjw7JtBu14mkd5RLAf"
  "dwsWpGH0lOYh/EhKGKhCNDvrsvd5vIRuOBnUkKwWixY75ahevHnDaDiKqOyya5rAgkYMGsleVu0e"
  "sUmUBPPrFcxu7mcFg9Y+xeUcITkiaWTnkR9CZY+xz98CqRR84lpdAIbWQWcIEhoTPbFv4dPxCxxO"
  "ByptjRj/j5BGxZxFfjBnMLgL5iQpW0T+A07LJCqfoigRM90CxPGUOnhadRGr6E7jxcLxBoMaLZ8O"
  "Ph6nLHqMgCzyeEaj+DSPS+xPtCgiIqy6tX7yANBI9/ALO+QQBbZGBDRNYWwXEZ/B3gj+ecYpFH7u"
  "7bUEKt5CRHX3kX3F3GO2x7J73k548/w5O7hn358yx2XPnjHnI/stO2qJGj7v8D+CxbAIMsgO9KWj"
  "/Uf0Tw2mMXYyPy+ikKVJAMy1x4I5TAX8GyedDMcxjJBuOJadhpF6faJejqgo8ziZAekCtQl0V+/O"
  "Log1+DeYlmQG9OAg6aSLEDHB1w6OKv7LKZFzv2D9Fsv9hP3JOwJCE0gId0FoJyskeWB9pDBAFvgZ"
  "9ByrL+ctpNRZDIV8Lh1El0RXlnGew4RgS3D2UuDKLE/LtNxkhKpM00WxD3P5AQfgAy/1oYiX3WzD"
  "nEd/EYd+SSPG8lVS7BduP8MROehkaeEyYkagZEUaNlTBZxtnWsjBD+I7++1vmfaqm3ByhUKq7KwB"
  "+ER/mWmSN0RNRCk1JcacEmOiRPi3IUMC4fgeWTrdJq5jWVgLIo0rsnX2Hlv3UIM7EnSp9/WUfWLJ"
  "EKpu80Z/HpnEq6mTgusTGkEmM+vVO0E+UEe0ZguYeOoEimygL310tOlos3RVwuu7e2V8Mj4+GYyP"
  "ewz/4vgQe2I/oSEygwKCbrYq5k7WkroBb5VeLFZL/2rqxMvZuV/6SiewF0t/7eTtWRtUFmmUeB0t"
  "WOc5ewXytTzwaCrrroTQOoGoC8Tow7DAm1to1OuqP0gEclknsRBAm30n0QGRAb7aO2X9liLgUBaG"
  "d9/dt9mM/4Kuu/A0qZ+8ey6JoHagDXids+cA/Dvm4I8J/MhBbUPvhsyZiTczejNSZZc+bnOYz7dR"
  "GPuJo40aHzf6RNrEGxyChuEl0hmoljbRQAYKvB45/CaxSDU4UHQrf/hdLrwElyCGOx/7+D3r3e/t"
  "YTEs4QcBLyMq8hdTeK4Ko/h2lRoeOfQj1AC1ww9iQUID40+1PN6PiOjw3XPCWAuCRxjyXncwkkcO"
  "1JjON2co+pylX5RR/qZdKcPKKHJPXHfIzi+vL85uJDGcy8ITjRrSsPzbu7Ozgj2CeUQoORrkuyyC"
  "v5JysSElzCUuNHu1XC0ID+hXUKXxNEbBOZ0uYjRqCAxegOz1FwVHFoDhhEA+czte9yBbA9/6aHL4"
  "JYxsnq8ylL1oDoHQBu0N+gC+eD0AzOIymI84mjDOowCMrnk8Ba7PIwAOVwHWBWIftEmWhmwGYhzG"
  "4GgfbRmuSth04c9mUciRzP0knEdgv4Gc77LLpIxmwJowBBW06x13ntCGBCURL0krzMCm5KWdbO4X"
  "0Rm0+fdjJIpHGB8YiWLICn+ZLaIOtNsJ120WbkAKLSM/6QQAkqOU29vvuB7L1q02xyX6AYM6vvr2"
  "GlXrurGVzm89lLXecaMJwjm8eQu6sItyxWvz33m6SkIHwUFQMLBi4Y/XggePff89O/JaDYJvSJ7s"
  "I24Ja4Qk7gAVglUui4ctIofXFM51M2jDSX8DpB8CW21kG4gjLDZ1+4F+X7MOc5U+bEQPALdALqFf"
  "c/RrQI/NZ2sZf13DWq7h1qhhDTWIAWiq4OINK8eu7bE1CrrpXbEh4D1Ael+Bft5p/pYlG+O6jjdi"
  "eY7yMwoqDpUmIMNPnF9hDjMHoKSvCQkbJ4+mbRascmlCcAAKnwv3YkIjoQ6+JNmguCrbPmFRkD7w"
  "AQTcCBHAE1RAT5+lGV9iFQC8LyEBIsYKodA+lqlQS6X8MxuZNBhASpAo9X5xuyuDpWo7zOfSH0GF"
  "XBk9jhAp9OURZuqx6grVQ3NefJeXju+JicbqJhEpio4bnYCaC9d8RCfhpmmaIKRVPvWDyNYxb4C8"
  "xX0LdFfA7YDH//1veI9CpAB5FtXlURQA9h//8hf2wz9cr6X2nuoFmTDCX8+Q2/GXyTibnsz40OIO"
  "SZeNK1N7CFMFEqKDksfgHuoqr2ld12RlobVZFwgstlbqAkZpE7dAbWuJkYhOxaBK023IiY0QFBtX"
  "ExSScZI+XSMkZ8s2PiOdgZDYwx7y1yOpmCIp1kJUrF1DUjRVIGEhGxBq4HvEvCYim4wU8AKJDKiO"
  "2kQCAulNAZlIhCh9+Lxj/pqoBDrxpAEUggA76qOYhY9gJPTArCqA/5p3Q3loBbHdOSEOjsuF/wAt"
  "9nDNXyC3JIJgH6IoA6cX9NQP/+P2QvS//Em6iANkMtDcS1BX1I7VMirqOtBiSdDCAw5C1hScBEg5"
  "G4XrkeCjcDOqOyvLS6har7BYTToZuPZD8K9K9n9/p/n98T/+68e//A3++Wtr3/Hw+a974qUH//65"
  "xW1pfx2jAQFyfTav8KP2BisRlG3NgGjpxELHZ3HwwL5b+aCx0eMkowGNFIzfoGocdA9h2LJ1hS6Y"
  "r5KHgoHigMLLFPV8l3298vMQ1D0izWOgOewAmA6LTZuBZQEmDr7oPBadYo5hIrKnOLo0CWNEgl7v"
  "dJEivZ69+roLFt27IHgPpd76+SxOWpUhArT56JMDDFUtqStgyZQVvgp/wWbxYwSGTz5B336KnhZW"
  "NoWxwGgVoqqHAGrZLerRAb93FXVr8RhGi9L/A+df+v1HXS4uqYXw1tZwDoiUQtTtTwoHiKNF8sZF"
  "T1h6vRGvdVEXXBI5IYRGy5OKmEdKgSmC1xxgFBOl3HutVE8qhXVq32c/C+uBjnVjKeXqpTy9Llup"
  "g58qtcQWQjc6jbCegtk5W2vjs9yYcECqs0ZJ4IQBtuen1ezCTEGp+tnUESEpCZRFUCc0DkarQ4/T"
  "niHLwk0Diz2D3lpgFaIB9Cj13KiDQ14TZd3+DvhI7UYZ0RM2pIO9R/sRym/FvTFw//GncWObYcQQ"
  "90bCbTEHP0Htw4pYedNxJr9Bh2BYkTWvVrxHTh0qGnc73TVMcN/iJufnkez1CQfpC14f+HHkdrxA"
  "Vyvq0O82voWnxzhdCYeNy0V02zb0NY+WPnqEufDv7rJ7DGuLWDNIlKvbdxxx40d2Gyc2Oydz18lC"
  "MmrB0HWW5+C5hjiWknMLH65JCWneLYwJ+lQFio0q9o92VqeMlxGZYJ0cJDraX0C/P/yjD1Kc+WTz"
  "k/hrPLkiisIuG/tLEcJuvDbwRyvfGtsx5DXfbfZCGIL1HloG/iKeJVX37vD1fVe0pqRVBw9qwxg8"
  "+rQR95F50KcgDTHiGoqGCV1KHEJXeH2A5EwELsunOBnKEcu0EAFLDFZC2z5wTQ2ugh+yfpsiqVi7"
  "FGrHL6fSN2DBKqZhOhI2c1xMgN2VEB8ld4IPVuVQ6O6Eikx2Kaic5FRssdJp+rl9AUSgtLl6CSya"
  "46IFN6ULYh3xojEAf4E9/brNXsvWNIoRgOhg6WfMPWzRkkucrCLNp6kaXDdozRu0rhu0Nk38n2V2"
  "37bZrWpyY6PW2Ki1vVGKSc49MN00326Y47xy8jFVAJjCb4V5fkvG+XvVOL+1WP6GYW6voPYrWUNo"
  "VB1Z3uQC7thMeU6C1JTtxjw35bm/yJ1HfACn0mrAq3as1bf8gjn/Txjz/4yZrS0TcQ004TGoIUF/"
  "1iOI5yRN9BBim8t2EY7nMuk9qJJ6jZF82w5Icvbiho0vby7GfFEBhZYqqKAxQZyB3M7SHMRuayhW"
  "RGvFNEMx+/ABI/I82OvwleB98cSbBsMLNmzwUMQzBN0RQXX8BDMLxb/i2CYxxh79HLUACfQQ44co"
  "bOm7op8cWVXqzZL1JSk+LdxJI+DEIDzr1VTwk6Bt0MLkLm5n9181DXYK4GD8wK7enXZcdvXq1eme"
  "C9onLiNkycliBWoi7MhhV1xQQNNL2Ndv/eLhZp6D25GD4tpgc6KMS8gOnw1amQJaXmZI8YcdbrNx"
  "ZBgLBV/gCLVhscpAxRcF9IPUUMLGN9dX776+GN90cCo77y+uO7gWdHt1fQ4KM1xluJpVzjkqVF88"
  "8D7fFHEAxoFYvvOBqB5homGQyrhTQNdwdT1e8oXD2TwtykLSS29fjL/5cPMaXXtH7SNG3lsYsnR7"
  "ICOk1f0jmKxo6gN+/MSc4gk82E6zZNfz2FWAofo8LQo2aAK8RQRVj1iC673MPRo0iultFMriVlqN"
  "qGJ2beZKkbmHBzVuB8RKhoxUEN/sE+ZWbZJ5QGc51l6ii5nmMcwMjFyYribAMQedEjhpkq6JEIC2"
  "sF/o1T3RUjvYK8E8quwxXJVFWfC1v4I59BNaiQcW+lOv28cFsYJizeSBglFRxMgEOWUHSMvvUM2B"
  "PdZrjfTWAbw2S7/4fXsk+LWI7yg6wOUqgMKzWnimWZjayEqkvMurwKwz5b+x8D38mFaf6p+0ioWT"
  "cTCS7HOlda5oHW/BL2phelfFhXlzyjsHTQ/y8m6rlpQyED6SZpRgjOZV0WQ9ljwRcuLA4X9X9CkH"
  "lWkFVFvq/Od6WRs6y4ncA5BHFd+2sDq+MFoDyCHsMTIKQDQWHaL7VTJuNfoJhUQxhBaDhB3z32CV"
  "PAyBzWoVhF7RIg5KgpNHJHmPva3X2RNQUGB818ID171rma6lKYjHSR75D5Q88qUl5KZGZKLLxLbA"
  "qIBlaUbh+0fOXGR4icXDp3kM/I5fPoGF8Vs0M2jIg709UOLVwAQjuZ+kN2wrvr9s4b9ZlFS5wLYU"
  "XpE412XNYjiSoJIJAK9+B+Y5GC6ubLjoQC2ouU5tyVoyuYsxreL4woLBNCddk4tsHa6eQanjZKEy"
  "n+Kg/FKVDpKMVN80XXDHkMwQzIpZgMMtjBToPri34N4Rg1cWKD2SzYFWm2glN1o+ZNxBBA8CnMVW"
  "I/3ZNAZTrNjiqQonbqubKgYkmuKCQlUjd1iBjEdNnoufx+UGV04foeRBj41BjeAau7TQWUieIVUb"
  "ckf3lPMbV2oBmGuGZkBZvM0tL2CclPW1oEtei/pmUwVMFAMVQTGikXfJU9KRtLj1isUF1MaA2iAU"
  "D58EXQp48pBIS+EiQ3PbelmSdS0PDXSti1NakrVtfNkoq0VzG6dS5sbP0I2fzBgi9rms6jC9Vgoe"
  "O1P0WUsK723ILdXAXfLnUO98aTH1dstSKsVV19SO9WjrGpFoCU56SQHJNfmiRgGXXD+5LYZoqn3s"
  "HuqbHkYhydN+vWV1qMe7p7iawiFuMJBbfIuCiGIUd1QOVFYPl3AcdJzLdav+uWkZqNwGlWtH5SKq"
  "cm1F8lntWYNs436hZzRx7i/vmWvrWbn5ZX1y1T7JxZveFHPVCnq0RUSF6Yya5uEBmGb70m8xV8NM"
  "gD6WZS+t/D6g4KUvijQp5jrPo508tqrOcYunhmJkSSoAHtOlgL9MZGhJjgRbEI6MTGQK8aHI3yct"
  "AwyScV/o8rxz+e784g8X58zpdbvJmw5O9Ndvrl6+eIOpwxyTcGMw87XKwMS4LaVkgg8ORn3Ml3RE"
  "0nAc8tfoaqbYFXBoAIQjS3D5CFBtBK6O/wQt4UmeYD5Bz8AWWmLLkiiezSfpKgdtgek3ThHlGea2"
  "JCJQuozDLAXbh5zKxj5j/iqMy7bsXrZ4ahGiLsWSXGN31e7MsogWj2j6YcYzOOh1A/hgffzhHy53"
  "Om8vb15fvquQOB/3wQIkh/JnWD9FwCe81/oF5o+YcVQcuiVE7nzGP9QUvSUVeCzygIvg7uM9Rbpm"
  "SMBQ/i7D3N/7e6t7YkOBXMvR8MjQGH5W0SH8jQ3CzyMiZf4iHtWBIZHOQ8Rzyoh0u9M8XTqfhGk8"
  "RLv9c5s5H9rsI+nGj5ivm5eO41O+/WlV7wQ5kf/07yX+WBXkUetJqjILGRmZ/PVHTEflFCxmAHuL"
  "+KiTajhT8OsSp6Du/D47aSK0+O1ZFT15scy+Bmq1Y0GqEcPVJIgUngg8a/RSZQEKEgv0BO+Acnpj"
  "9EDQD3Ak+/bfa2M3gP48Y1+K0RoEZaNTCmFyYzuwWOQtTmVowip0xhtaAAkVXot3tBgZ4rpe4HXq"
  "8e1QAWWUBdSzJkyFz9vHes29QsT2G4wXo9HiiBf7qIC+r/pep91+YmhPt2FsQpARbZz0IduDv7tl"
  "+ipeR6HjYoIdVQwf+A/pm1AJVuncyCqRwyxWyn02/tdvX1xfAM2vSorBrKDx765uQKKVcpJBEq1L"
  "IZFAdFH4BVeQFEnXpUUheCoYSloh4QnFXfGV8FuB54o9t1U9YoRRNI4LPx7vQ4eAYobAKlj9+MXb"
  "izoLgCrpMocS99M1uAPAgEXVFMTDeB4+6RHuE+EcYZpHGE+nmECQNVkK9aYX0CayTBZqifeNRDQv"
  "zLNFeIC42BWrXd2WNPnX2sqGoJlx1VF03RVr+uqJCDAG0kBZT8TB7P/RfMQ0BbtVn3e+sKqC5um1"
  "ua7CEwXR2r1WmmIsFG1EeW2piJffSOUbeV4J9Fqif7Ss/tQMUnNIwyIGjwgzjpbs19yOoyWmNSVZ"
  "bGjpftMTv5+hLWma90Et/T7eq/Y3+IHSwLdIsOGUtBgXyh/5HgBWLZ9GVZgFCdYwA4VbDyBpMqOo"
  "NRByJ5NXFUSsWbizUmCXu7KtIcadhVdvDT5jRr2fbKS9UBiAxu/enmCcuV/wWLRozkOcY5YM5UKh"
  "wYRyhf1blKdgtqwiEcYGcOZ4vY432HcHvY47OCSz7AE4stU17D7s0/jm+vLd12j+ZRED9Vfmz11o"
  "v29Ex5Eraw6trLWiBFsFWQxsxiVv7qgKpgP8+eWrVxfXF+9ulK05zVsSQ03EvvlQsyfxNaUDhWhh"
  "AieDc8vZmKxHWjCBbuD6Ca7yVA2LkzkxemNHUg/DLsMuPjt1aSAVs7RCM4nmPuYV5FIYerMgY+HT"
  "Z80UIPkGCkBdrA1WaLVQoTuMK4b3jR76FX4EnVt0Ue8/pwRV+NVSwCtF91mKCvAFmKvJxygou5QF"
  "VThUpjLoFyD5yieUfwCe+WUMfSJTd0jZ5DAtaRF1QE10ajHYuTxH4QvCkjaqwAj4JfzAvIroSWSP"
  "a5rIMLxFMWidMHtrnUKJbCESQpULX0lfmRpWCeUdRCFPNaAWcy2GuxWp5Wh7t6TgpoizmiFknwsu"
  "Hx00HLDaR/O1fUW0Zo/yhPx7+KkXmFjMa8oNQCgwJEUuAn+cKPaK4xS42yJURNKvQCTBh4n+QTI+"
  "mH2QdrSUoPkmS0usAhUDIMQkE7+74Q8bMteUxDXRGPqM1AI0N9CEKx/OyojxcWNAlzY5TYaiVJuB"
  "SbP3c+qv7RmvsWfYtmVfGr4mpJ6Mt8bUzQXh8Tx9uo4KkFJFE+MLo6BNe/DIqftUCSlghqBahKX9"
  "oD7ZHWAAkd8XRiUwFQwSbgMkiefUvICcUK0Fz3LcyYmznsxA7HOxWIkqtPcm5K7IkrOS7w7f7VeF"
  "SCsGWPoPmILDuTsTy1/EBxd/eH9xdgPtCfwCkyG50ZDHM02O946G2OxOsqLag3mcoZ+BCTTUN+hn"
  "m3QeoF4gz/pc6MH7Rr4FGNQK02CFmbTdAN3g6GJBebXObuAnj36xC4Ze8Nh9isMSQ4e39DTn0uaU"
  "vZbWHtZop8PHWVSeodG2BhxeuCv5VTBqpwgnarpc+rMI95BhtshrCY5vL6OdZT+xZ8yS+mPfQPao"
  "WUe1QeQNBm0pelMFgHGzmbTTTNpmxrP5+dMBPtG2JyGzoW/Zqmw6Fi/b2OKeskazSkiG2ZUKUHKX"
  "yKIlIBvV4KgvvicBI+KUWPM0paWf3QkGc1zcurRMkxRUQhDtjlT3YjAEv2EJepGhd81c3L6ELSEy"
  "WkSzGIP45ebL7ZPHl2gfZ1dp4vMqiEqrDL3jIdjsk2hB1RSsc9D7DXOo1W7/x//8m9tr8wa4A3xy"
  "0cVAEwxZFq2yh6j2JKJl3ClzP4HOcZnJLYKae4F2cWGCXiJzi+xrmQOa+VhUvAuO6W4+m/hEFO5x"
  "rw3/6w4GrV1wVvmHXhs/HQ/Ee2GAw9jz9o1LUMw0CovmE66G3Ar2cfUS1yCAnIJk6iHwK8lT+OEe"
  "4B/TLRzII8heXowvzy+qTjEn5zYAyIA27nYr0f4EQ0js6mqwVTIJl/pFYXBJFtG0RHWDe+fp1IBg"
  "AXXQpjTcJr9LUgjwR+EskgewXCPVcQ/SoYlXHJPyyRNcv4z8YpVHNygZoFCLCxUZNngCUCywx/pN"
  "pIO3H/NqcaD2QM/tIeQ+43CGm4VjwZu6x8eJ9kICKCaXzPystskqxDW657TCDBpMrRKmBP4ImA6v"
  "sh5GGrd6VjGGV1FBRTKcjg5kckEwmvqqogp9RQMDzOYNntqYlF3PWwaj16FDAMB23WRzYEjMF/EZ"
  "btUDW9wPHgSjkA0WxrOYNh/6tFkc/Y7JbGilS28bJe/+utfrSQ1Hsf5iwRd8d7HxUQ6ypfryEnQW"
  "osWPyzgMF5HBItXst6sxph63RvYR/PV0OtWG7ScRKE3EydnSQH+RzYGXgEt2VZubc9hpZU8AeezS"
  "eO7yPfxd2WKkj/RiVxfD7qEqgXcajrAwBFUqscSXiemwJTpVExIIDfg/oAaF0G8zr9+yYvl1b9qT"
  "ijZVA6V5TbFHmC0o0Q2KAkGwJLVs6PZ6vxmFcZEt/M0QvL/gYTQBspvRSucQiWU0oRBpJ/fDeFUM"
  "YRBGPOLVKdMMH3dFDXFINPQUcGtOGiDUX5JdAgaFMEpebi5DRyrSqrI2oEQLi3XziNbHb8HYc4JH"
  "0CB0Vsa/OLt4Lshuq+tnuFP4bB4vQvoOxqVfbJKA1SZmARLjjPIOac9jZUxeX9xcXoMmqcyvwVDs"
  "ViFywkLMnwA7Mocbdbgh/Oz27PzirNqcs8d8kTxNr8eYjgcjs8F0E2p0yG098bpDUTVE3GXfYCad"
  "jzGtJO2kmHkVLRbCokyJsWGSojyhRPgFplOF0QwmANoB/wQRLuZvGKhGcI5xK3NCCvGJltof0xgD"
  "G8HW8zqe0FbFmeanZ2RzzK2rHbViQYGJOOzQkRgtubw+tHn0HXiv5S0gdPjAlvlGCqXvYlVvoKZd"
  "NHwT8MZnqM0br6VKpang0JN78kFh1bDd6lNXVObsFgGa77u1T7JIZ7wm3ik/+G4VgzJsAPRaun4Y"
  "XuDu6TcxmIlJlDu74LoC70a7beaIHBpL0/CYl5FenSiJ1dV+0mdOpRokje4q42mpPM4JyvDFqkw7"
  "WMHpO0w34a3+DNNegtHvYHYyx3NB80aIUG45Ubc6MAjsxqiFtcOE1zxmdvExLoQVCD5ZMpP6Kniu"
  "LttAjkvc0o5hOF4c5L98rEgzNvUhOApJAAnCn52tQ6loeVCF1dShdtObi6T88urqhp29ePNmTMNH"
  "2TnA/WjGAlHEwFPOzfm/UTRjxF667jHo8qIAD3IHBMZk4YG8QKVxJkLQp+zlt5dvzkc7mPh4Np1h"
  "g0FgJSB+b8f4AA5nXp6BrZT7+Kj17dk+r/Q5/Jqk4Qb/nZfLxfP/Bw4oMwgT2gEA"
;
static const unsigned PAGE_GZ_LEN = 36585;

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
static char sCfg[640] = "";      // CFG=<json> for the page (CFG channel).
                                 // S14P-1923 (S12 rule, sized at max N): the
                                 // widest real CFG = the page's FULL numeric
                                 // key set with every value at its 5-digit max
                                 // (incl. the two new rig keys) = 413 chars
                                 // JSON; 32-key headroom = 490. 128 (the old
                                 // size) truncated it; 640 holds + '\0' +
                                 // slack for future keys — check:
                                 // tools/verify_s12_cfg.py.
static char sNpx[8] = "";        // NPX= override, echoed in hello+drv?
static bool sLogArm = false;     // LOGP arms a 15 s window: logc prints to serial
static uint32_t sLogArmAt = 0;   // (a serial reader must be attached — LOGP is only
                                 //  ever sent while one is, so the CDC ring never fills)
static bool sLogPerm = false;    // LOGA: persistent arm for a dedicated bench reader
                                 //  (the S14L auto-upload fires ~1 s after a burst —
                                 //  a 15 s window is closed by the previous pull's
                                 //  logend, so without LOGA every auto-ship drops)
static char sPageBuild[16] = "";
// S14P-1926: how many drv? replies must still REPLAY sCfg. A fresh firmware
// boot starts at 2, and every hello RE-ARMS it to 2. The ONE-SHOT sCfg drain
// (cleared right after the reply left) was the S14P-1925 failure mode: the
// 08:58 bench CFG landed while the phone was wedged (09:45-10:01 WS outage),
// consumed on that first unseen poll, and the 10:02 reconnecting page hello'd
// into nStr=1 forever — page painted string 1 only. Page applyCfg is
// idempotent, so replaying an already-applied cfg is harmless (apply logged
// only when values actually change).
static uint8_t sCfgReplay = 2;
// status-LED state machine (operator request): breathing OFF during
// experiments (scanning flag from drv? traffic), solid RED until a page
// with the CURRENT build stamps hello (new code ready to load on phone)
static const char PAGE_BUILD[] = "S14P-1927";   // keep in sync with the page BUILD
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
// showing the commanded paint. Echoes the page's command id (frame-bits
// uses the u16 epoch from [4..5] of its message, which the page sets from
// the same cmd id space; cmdEpoch is bumped before ackAfterLatch reads it).
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

// frame-bits: unfold one 206 B bit-plane onto the lanes (S14P-1923, plan
// §6/§11.4). j = global LED id: lane = j/nPerStr, pixel = j%nPerStr; ids
// >= nStr*nPerStr ignored. Full-rig repaint at ONE brightness — set bit =
// white at b, clear bit = black. Per-lane write, NO mirroring (the JSON
// cmds keep their nStr<=1 lane1 mirror for bench compat).
void applyBits(const uint8_t* data, httpd_req_t *req) {
  sBitsMode = true;                     // latch guard FIRST: frame-bits owns lane content from
                                        // here (loop()'s pxColour fold must not fight it);
                                        // JSON paints hand ownership back
  const uint8_t braw = data[2];
  const uint32_t epoch = data[4] | (data[5] << 8);    // u16 LE
  const int ps = nPerStr;                             // snapshot: CFG can flip
  const int ns = nStr;                                // mid-paint (WS is a task)
  const uint32_t total = (uint32_t)ns * (uint32_t)ps;
  // per-string polyfuse budget (same 2 A-hold rule as fuseClampB, at the
  // STRING length): a plane lights at most ps px per lane/string.
  int maxB = (int)(255.0f * 2.0f / (ps * 0.0142f));
  if (maxB < 20) maxB = 20;
  const uint8_t b = (braw > maxB) ? (uint8_t)maxB : braw;
  if (b != braw) Serial.printf("[PWR] frame-bits b %d -> %d (%d-px string fuse hold)\n", braw, b, ps);
  for (uint32_t j = 0; j < total; j++) {
    const int l = (int)(j / (uint32_t)ps);
    const int px = (int)(j % (uint32_t)ps);
    const bool on = (data[6 + (j >> 3)] >> (j & 7)) & 1u;
    lanesW[l][px] = on ? CRGB(b, b, b) : CRGB::Black;
  }
  // off-rig pixels stay OFF: rig lanes' tails beyond nPerStr, and lanes
  // beyond nStr entirely (a smaller rig must not carry stale content from a
  // previous larger paint — the mock box resets ALL lanes the same way)
  for (int ln = 0; ln < N_LANES; ln++)
    for (int i = (ln < ns ? ps : 0); i < N_PX; i++) lanesW[ln][i] = CRGB::Black;
  pxBlack();                                          // JSON buffer out of the show path
  frameDirty = true;                                  // loop() latches + stamps the epoch
  cmdEpoch = epoch ? epoch : (cmdEpoch + 1);          // ack epoch = the page's u16 id
  sLastPaintMs = millis();                            // status LED dark while surveying
  ackAfterLatch(req, (int)epoch);
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

  // ---- S14P-1923 binary message class: 'frame-bits' ---------------------
  // Exactly 206 B: [0]='B' [1]=ver(1) [2]=b [3]=flags[4..5]=u16 LE epoch
  // [6..205]=bit-plane, 1600 bits. Bit j = LED id j, LSB-first per byte:
  // bit = (data[6+(j>>3)] >> (j&7)) & 1. Set bit = white at b, clear =
  // black. FULL-RIG repaint: lane = j/nPerStr, pixel = j%nPerStr; ids
  // >= nStr*nPerStr are ignored. Per-lane write (NO mirroring). The ack
  // rides the normal latch path with the message's u16 epoch.
  if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
    if (ws_pkt.len != 206 || buf[0] != 'B' || buf[1] != 1) {
      free(buf);
      wsSendJson(req, "{\"err\":\"frame-bits shape\"}");
      return ESP_OK;
    }
    applyBits(buf, req);
    free(buf);
    return ESP_OK;
  }
  if (ws_pkt.type != HTTPD_WS_TYPE_TEXT) { free(buf); return ESP_OK; }

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
    // S14P-1926: (re)connect = hello = arm the replay. The NEXT TWO drv?
    // responses re-deliver whatever sCfg holds, so a page that reconnects
    // after an outage ALWAYS converges to the queued rig config (tonight's
    // 10:02 reconnect would have re-received the 08:58 nStr=8 CFG).
    sCfgReplay = 2;
    sScanning = false;
    char b[112];
    snprintf(b, sizeof(b),
             "{\"ok\":true,\"id\":%d,\"fw\":\"poc_survey\",\"px\":%d,\"nStr\":%d,\"nPerStr\":%d}",
             id, nPx, nStr, nPerStr);
    wsSendJson(req, b);
  } else if (!strcmp(cmd, "frame")) {
    // Arbitrary paint: {"cmd":"frame","p":["W",null,"FF0000",...],"b":160}
    // "W" = white at b, null = black, "RRGGBB" = colour at b (b scales all).
    // Single-string bench path: writes lane1, then mirrors it to lanes 2-8
    // ONLY while nStr <= 1 (multi-string rigs paint per-lane via frame-bits).
    sBitsMode = false;                       // JSON paint: legacy lane semantics restored
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
    // S14P-1923 per-lane era: the JSON plane is string 1's content. nStr<=1
    // keeps the legacy mirror (bench compat); nStr>1 paints ONLY lane 1 and
    // zeroes lanes 2-8 (defined fallback — the rig is otherwise per-lane
    // territory of frame-bits; NO mirroring).
    for (int l = 1; l < N_LANES; l++)
      for (int i = 0; i < nPx; i++)
        lanesW[l][i] = (nStr <= 1) ? pxColour[i] : CRGB::Black;
    frameDirty = true;
    cmdEpoch++;
    sLastPaintMs = millis();                          // status LED dark while surveying
    ackAfterLatch(req, id);
  } else if (!strcmp(cmd, "all")) {
    sBitsMode = false;                       // JSON paint: legacy lane semantics restored
    int b = fuseClampB(doc["b"] | allBright);
    // RIG-WIDE all-on (the command's intent + the fuse note): every lane,
    // every live pixel — NOT a lane1 mirror of string-1 content.
    for (int i = 0; i < nPx; i++) pxColour[i] = CRGB((uint8_t)b, (uint8_t)b, (uint8_t)b);
    for (int l = 0; l < N_LANES; l++)
      for (int i = 0; i < nPx; i++) lanesW[l][i] = CRGB((uint8_t)b, (uint8_t)b, (uint8_t)b);
    frameDirty = true;
    cmdEpoch++;
    sLastPaintMs = millis();
    ackAfterLatch(req, id);
    } else if (!strcmp(cmd, "npx")) {
    sBitsMode = false;                       // JSON paint: legacy lane semantics restored
    int n = doc["n"] | 0;
    if (n >= 1 && n <= N_PX) {
      nPx = n;
      for (int i = 0; i < N_PX; i++) pxColour[i] = CRGB::Black;
      // legacy observable behaviour kept: npx blacks the rig
      for (int l = 0; l < N_LANES; l++) for (int i = 0; i < nPx; i++) lanesW[l][i] = CRGB::Black;
    }
    frameDirty = true;
    cmdEpoch++;
    sLastPaintMs = millis();
    ackAfterLatch(req, id);
  } else if (!strcmp(cmd, "black")) {
    // RIG-WIDE black (see cmd "all")
    sBitsMode = false;                       // JSON paint: legacy lane semantics restored
    for (int i = 0; i < nPx; i++) pxColour[i] = CRGB::Black;
    for (int l = 0; l < N_LANES; l++) for (int i = 0; i < nPx; i++) lanesW[l][i] = CRGB::Black;
    frameDirty = true;
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
    // S14P-1923: sized with the sCfg widening — 640-B cfg worst-case escapes
    // to 1280 chars; the reply overhead is ~70. snprintf truncates safely,
    // but a TRUNCATED cfg silently drops tail keys on the page (S14O shape)
    static char b3[sizeof(cfgEsc) + 128];
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
    if (httpd_ws_send_frame(req, &resp) == ESP_OK) {
      // S14P-1926 idempotent-replayable CFG channel: sCfg is NOT one-shot any
      // more. Every reply carries the queued cfg while sCfgReplay > 0 (armed
      // by hello / boot); each delivered copy consumes one replay credit, and
      // sCfg clears only after the LAST replayed delivery. A fresh CFG=
      // always rearms the slot and can never be drained by polls that fired
      // before any hello (the single-slot race behind the S14P-1925 gap).
      sDrv[0] = 0;                       // directives stay ONE-SHOT (unchanged)
      if (sCfgReplay > 0) sCfgReplay--;
      else sCfg[0] = 0;
    }
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
  Serial.println("\n=== poc_survey S14P-1927: defaults 8x200 rig ===");

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
  for (int l = 0; l < N_LANES; l++) for (int i = 0; i < N_PX; i++) lanesW[l][i] = CRGB::Black;
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
  // 25 fps: latch the per-lane rigs to the LEDs and show. Content only
  // changes on commands, but steady pacing keeps the cadence constant for
  // the camera.
  //
  // S14P-1923 per-lane era (plan §11.4): the OLD lane1-vs-pxColour compare
  // MIRRORED lane 1 to all 8 lanes every latch — that is the single-string
  // bench structure. With frame-bits the lanes carry INDEPENDENT content
  // (lane = j/nPerStr): the latch gate must see all 8 lanes. frameDirty is
  // set by every paint command (frame-bits sets it BEFORE bumping
  // cmdEpoch); the 2 Hz refresh is unchanged. Latched content:
  //   nStr<=1 AND !sBitsMode: lane1 is master; lanes[0] copies pxColour;
  //            lanes 2-8 keep their legacy mirror (JSON cmds already mirror
  //            them). The JSON paints clear sBitsMode, so legacy behavior.
  //   nStr<=1 AND sBitsMode: frame-bits OWNS lane1's content too — the
  //            pxColour fold must NOT run (1923 bug: pxBlack() left the
  //            fold painting blacked pxColour over the plane every latch).
  //   nStr>1:  every lane rides its own frame-bits content; pxColour is
  //            out of the show path (applyBits cleared it).
  static uint32_t lastShow = 0;
  static uint32_t lastLatch = 0;
  uint32_t now = millis();
  if (now - lastShow >= FRAME_MS) {
    lastShow = now;
    bool changed = false;
    if (nStr <= 1 && !sBitsMode) {                 // 1924: bits mode owns lane1 at ALL nStr
      for (int i = 0; i < nPx; i++) if (lane1[i] != pxColour[i]) { changed = true; break; }
    }
    if (frameDirty || changed || (now - lastLatch > 500)) {   // latch on change + 2 Hz refresh
      if (nStr <= 1 && !sBitsMode)                           // string-1 master paint (JSON era only)
        for (int i = 0; i < nPx; i++) lanesW[0][i] = pxColour[i];
      // sBitsMode or nStr>1: nothing to fold in — lanes already hold their
      // frame-bits content; this latch exists to SHOW and stamp the epoch.
      FastLED.show();
      appliedEpoch = cmdEpoch;                  // this frame carries the latest command
      frameDirty = false;
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
        else if (!strncmp(sLine, "CFG=", 4)) {
          // S14P-1923 (plan §6): the rig keys ride the CFG channel. Numeric
          // nStr / nPerStr are applied box-side; the FULL json still queues
          // to the page so its CFG object stays the single source (the page
          // clamps cwcN to its product cap and reads nStr/nPerStr from hello).
          strncpy(sCfg, sLine + 4, sizeof(sCfg) - 1); sCfg[sizeof(sCfg) - 1] = 0;
          const char* j = strchr(sCfg, '{');
          if (j) {
            // S14P-1924 parse fix: k1/k2 point at the OPENING quote of the
            // key ("nStr"/"nPerStr"); the digits start after `"key":` =
            // +7 / +10 (1923's +6/+9 landed on the COLON and atoi()=0
            // failed the range check — box-side nStr never left 1;
            // parity gate: tools/verify_cfg_parse.py).
            const char* k1 = strstr(j, "\"nStr\"");
            if (k1) { int v = atoi(k1 + 7); if (v >= 1 && v <= N_LANES) nStr = v; }
            const char* k2 = strstr(j, "\"nPerStr\"");
            if (k2) { int v = atoi(k2 + 10); if (v >= 1 && v <= N_PX) nPerStr = v; }
          }
          Serial.printf("[CFG] nStr=%d nPerStr=%d queued: %s\n", nStr, nPerStr, sCfg);
          // S14P-1926: fresh payload re-arms the replay window (boot armed it too)
          sCfgReplay = 2;
        }
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