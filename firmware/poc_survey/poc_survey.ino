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
;
static const unsigned PAGE_GZ_LEN = 47408;

// ---- S14R-0003: SECOND served page — the autonomous CAL battery -------------
// Embedded by pack_cal_page.py (same gz+base64 as the survey page) and served
// at /cal with the same TLS+route table (wss /ws is shared). The battery is
// started by the CAL serial directive; page build stamp CAL (see CAL_BUILD).
static const char CAL_GZ_B64[] =
  "H4sIALOxwGoC/9W9+3rbSJIn+r+fIss9PSTbJE2AuFKWa2RbVeVe2fJarnb38fFXC5GQRIsk2AB1"
  "G7fmm4fYZ9jzHudR5klORGYCyMgMyFXdM7vf8UyXSBAZyIyMjPjFJRPPvlsU893dNhcXu/Xq+aNn"
  "+Eesss35/uN88xgv5NkC/qzzXSbmF1lZ5bv9x1e7s1HyuL68ydb5/uPrZX6zLcrdYzEvNrt8A7fd"
  "LBe7i/1Ffr2c5yP5ZbjcLHfLbDWq5tkq3/eQxm65W+XPjw5fiZfZSrzIdru8vBPvjl8+e6p+evSs"
  "2t3hXyF7OTwtFndf11l5vtzMJnun2fzyvCyuNovZ7zzP25sXq6Kc/W6xWOydQUdmXrC9FdVdtcvX"
  "o6vlsMo21ajKy+XZPdD73U2ZbYHWrereLI4m29u9mrbIrnbF3jZbLJab81myvZVNLhbl18Wy2q6y"
  "u9nZKr/dO8+2Mw/bZavl+Wa0hCdVs9OsylfLTb6Ht4zwMTP8j6Rwulp8xb6NbvLl+cVuFk8mdbfP"
  "5hHeMq526o5q+a/5zPOBeN0ND4YDXdk7LcpFXo7KbLG8qmbyisGJ6XSq6YyLy6+ER/H0zMvr5yVn"
  "SX3fabYgNwaZF3ph07FE3ni9XORFM/xNscnx6jzbXGfV7+bZ+qviozeZ/H6vvut0VcwvSe8mMGDa"
  "/0gz9zTbbPLyAe7sGWyJ2rmCj2Kyt8tvdyM5C7M5SGBe7gHN+p96oORfVayWC/G7KJq63WgYHVks"
  "9X0vm0xkL7dlcV7mVfW1lbB1sSmqbTbPzb6Szt1cgGSM5D2zbZmPWnHYbSpXovDxVHYIOWx5erXb"
  "FRsyaX7mZ9OsXQT5njvsIAiYYbecMgRe4NhMlgdqntSTZ9Dp7HSVL74WMKrl7m42nobtz2Orb95i"
  "moVn9aN1F6dZLJmwKs6/XqgJ9wNcTMV1Xp6tipvR3UwuQyI/Gf4fMzQQ+2Yg7hDVbMHtT71xEBpT"
  "Zk45P03Pnmod9Oyp1oioheDPYnktlgvQdXDbY1RRzRXQE/ICXAJqG3kNVv5jpeoOjsSLgw8fDt//"
  "RfzHv/9Pcov/+Pl//Pv/gifCpef6j0Fnvsqqav9xBZpWPriCT88/nsxQ727y+Q4G8s1GsFKrXQbt"
  "XmbrmYCPJW327CmMgoxGLUxoAB1/c/zqUPYaJ2ZTrIurSpwqvb2niFWi2IjdRS7HuViW2K3rvKUq"
  "1YikC58eC5T8arlBfSnWV7t8ISnjVRi/vFe2UmqmHsBjoezL4yiYPBZKdvYfT6PJY2ikbiUjqBft"
  "4+fLxSqX/dedfqjPoj9fLypxtlzlA4YrsHTrSVZCry6fIMHHol4fz+X3+nHPnqpbmXYHpwVpJ78/"
  "cP9JvlmYj4Gv4qwEe1yJfrZaDR5oelScGy1fFTebVZEtBKxDs5E7ZLgBGSwv13+qebnc7p4/6l1V"
  "OTCzXM53vb1HN8vNorgZ/1IoyvviVbbLx5vipj/YU0v06VPobV5djCRkuN0J0HCXeSn61VV5nd+N"
  "ttl5LlAq4O/gEdy9/5/4D+nhVL87+NGRZkAny9My2y2LzVOw42ejXV418yf6J17wfgRGbDoCAoMx"
  "UjrJoccgtzvAKLttNXv61Ev9sRcl42DsgTiuoNXhy+O3r0CUyvVNVuYClNkuhwUD0yU+HJ2IJwLW"
  "sqJ2IICJ6zUQBNHcLLIVGFpRLU+BF+eiOBPImKeKSWPERDO4H4AN4q8h/Ljc7AQ0Q0rzbLu7goet"
  "s/kFMBJ6j49+efzu9eEJUkKBv0Y4tMSHSYri7Gozx6GDEC0KsSl2SAjWJUyfMB7aAz4Vixyet7uo"
  "BvDcFXAuE5Jf5dVmg32tWYYTXBarVV7K8X2E5SVgQe4KMANiWYmf37786eDtj6AZ+388OX4r5KJ7"
  "IvwgEi+URI9Olzu8BPI3ByR6tbmsBntIC0egOy7lBQYu8FZ4JFiFXCxy7GVZyYFfbXbF1fwiX2A3"
  "sHWNN/uL/Cy7Wu2EtBriMBweesNDf3g4HR4GQ6UhgEOnd66KGMyQkBCHnjgarcCUQHtYfCMYhtCT"
  "US7PEWHm9XUQk6OhuChWC2W53lRD8W9eouiw/6pNtq0uChgXtP23cDIRa2izBQMHyHYLDFjD2rmT"
  "i0X3xheLqx1yfr3FkSsqKGuqi0MRTn4/kreoPu4D50R+m813cloXD3QGNPMmh+mHkYA/ALwDKi+y"
  "zSXQwKY3wEOQ7puhqJk6GehOTbEBgHpR3eTQbdUpEBS4ciS+XK23Ihw99/yJnEX4O3oeDuGvqMS/"
  "CD/EQaOkz+sxBjChIKh6SIocdmY0GYdicZOvVgLbKnbDxG2gy4fi/SFM38uPLwGnlNXugXH2G3HR"
  "PNrCqlzinAPv1xlwvoRHV7DmVtlpvqoeIDW/mc/Kt7PxeDyAKZdCZAmoALZdQl+r0VITOgyFNFUH"
  "h2rQktDbY9WXoYgmta7/F+HBKPtlUaxF7XjAUoF2oF13kvfSkszkcw9evP9gGriilJdx8cCcyhs1"
  "goOughmW/MM7NqCikZaUuFOEYhlafBjH2fIcZHwBXYGV8fKHH/effanAgBjGcwjWFWAVSDDI6CWs"
  "ILhJ6br/VJ3+CJm6Ey9+fn30CqTxMdHUj/f0z/8EP/WXi4HYfy7AAb5ag7MwPs93h6scP764e73A"
  "n/ceYf9G1j/sOlgo0Jngx+6uNmhA98TJwZtDcQk6aCNnBGCKoSkHmgiSO96slA6Bm0GYL5YNdoLJ"
  "yhZKTQHwrKAjeyLbbld3L8/OxTovz5HuBtDK1SksorEeC/ZmX3wFE51fv1hm1UyMvCERPXhofrst"
  "oD/5S1AG+aaShk2cwt2if7BZlAVgZ7DSMJfLYwCS6NYNHkmZfTsTXjSZOAQBw1awArZbWBq7oln1"
  "QA+Q5lpfBa2HogOgQFM7kWsfSJoU0bZUSqZHgPHF6Lk4L7PTltLz/XgilWX+Y7YV4BMUpab3rlgC"
  "Mdo5pCfX9UjqbPHu+DUwdgTwN4dvfRigWOVg8VoSf54JUBdDSuIMNDW6HMCMcg1o4F+hI0iqD/we"
  "oQ5T0z4Zj72W0l8UJfSA3pXFKbC7zN8sN0dX6wz6iaPAzq2uyhKoreAqmMvt8jZfgXWFqZyv8qyE"
  "ycRxA0vLvCH0Kgcs/Abm1p9o1uGSxuWN0OkaZWMD+gG9FrS6/S02epNnOOctgBLiJlvu1CQQYnLA"
  "0I+FMkmn+Rk8HPom+VijMQF3gzLN19lmt5xXg0f3e/zyAOVqmH0EMGC1+ujPgYpAtonrZUYVhVog"
  "SO6VshgVsGFXFKvqKawwGNIvlReU2IEx3o/4rDbcVS6xSrMc4OlqOSjnUHzqHYa9oegdevK/vvzv"
  "VP436H0mYti4A/lfryQ/a1CArMu9I2lkgCKaJGCcD/8LJqiHhyKZDJVUe3jVC/E/cSrJA2FABtrw"
  "K0OF5Px/gJzfGrmGYG2DQQSH1nI49GetXX5//FEtNhQU6NE+rmWwM5W4uSiqXN74SHT+Q0gJWA4I"
  "aOUlsYDoA5hQeMNAEGjr9x6mpVpPaiR6qkDEdNJcAbQMswoqSKocORFBwzmHTZ+H3EMAJmiAAERB"
  "ya5a419Jgq8a3pm6U7VsERL+Cp17efzm3dHhm8O3H5Bcw1c9koeHC04ZGH/JIrACwLM9hYwBQ1Ry"
  "uHmgF51CFzheRH4vwdjC871kSMi1oHArkYgco7TO/UPv6aFfNwe9ies9tNRu3bzRG6CH+/+WjFLh"
  "EJSUFFKd4fRY2hJ1e/2raqVgqalOms6isCI0oyrIYrmB4HR7KR8SsAnZpXZQLQVorzutmmrg8vbw"
  "zx80guuDIdyNaq/oPJMjy6d/BOiplyKKUvi5VrKAWCUsXeXnwJyzEhAWGCgwbvBftA+q+QeEZ9gb"
  "zxyQaq6g2yrfnO8uFFvzc9lID8APbW42jRp9foaATl7D5yEo1BIR2dwLNSbUt+lneBaT4DZNW8q/"
  "cdfEln9gEczADuD6BiD6dba6ksSri+UWVPD8AgYQ0D4sN/NS4ihYaHgb+rLn5+jWa7h6kYNULFWw"
  "BQBmmddy+sdtfv7fLWtsyukf3x3+KP56BeYY1mS/cfpK+IokTlfF6Ul+/uECdENAB1JmN6O5lML8"
  "XEEI0d9dYOwBJfSJ9JIA9YClqgmB3X53C3RsSa9vBFiiFaA04HOcD/Vfif4ySQRtZO1No9E60HCu"
  "/2UgvjbgDr7tiXtFH8YGrS/z7U6caWAuMTTA1OqRAt/L+eWdtHMIQ2AK4WHzsqgqUG0YaKlq1Skh"
  "6enVEgbY8got3AY81h3YObSSyzPRXxXQtROYCEAJCINf7/J1vwfXoG8vsH1vgChdoeqBbCaUaQeH"
  "EqMe+wKd9vEW00MPUgNCf/ub6H297w2UYcAx9hUpBM/Q8+PTL2DRx4iO+5L6oH6i6izeBrKDsPef"
  "/1lg7goayRs/wU+fZUd7m6v1aV728I5l9QMmnfJ+ew9QhOb6dqNtbavAk8vJs4DXQMl4yHfwkM0V"
  "KBi2C/hrr552GDK073oYxr7v1QPVIAn3Km4uhnoe9tz7YdkBzKIMx9vuH92D9MFiFf0cuHnfhOaw"
  "O+hdgDsknaGvQsrFb+zFQ3fDjVI2MCq4OV+e3fX1BAMgX55v+l/vhzgZQ2TSYCDXgdlT0Q0ypRum"
  "QIOEbTIcJT3R+QUGq1dDuXJG6H2KRQmu7gZhpg77GU6qclzbFVPcijnwpFqBxkG4vw8qJwetJ5a7"
  "wbB23pW3jMQW5fX3YluAMCzA8b7GSNNyhyqgHAMZ14VTsgk3bcCK4CAw1IEeyR2oE1DI6K5JAZS/"
  "g+j1s7LM7qqnioWA0/IVaKF8MBYfpAKF9mgk0NuSPmV/c7Irn27e5SX8lc4/jrAdoETQGPDEoKIc"
  "n4wXwMRVksiTmqPjR6t8J9Q8HoFmkSsdHKMqN7RaZqo0rVC+g89lDhZ2I8UDiBR7WrIKqiy+2DOO"
  "8b1+7xBnQN4BAlyTkotF6Yo6KrMvPsmFJMeohj9rpkesl2VZlGpqcVZJWLlxZIYqKia9Tan9cQR6"
  "URdj5GW3TlG/D6hOvIZuvcl2F+N1dtsHPKk+Lzd9QG+a4N/EZKAXMD7tWqqMelFKksAJ8zuQvN6r"
  "hz3eXlUX/R5e3+/BdF0PFG/unc4rGXiw/1pMfuUQfAQILeGHx6FJm0OpO8SMRv3kDMi0EKiMi7qn"
  "UkG7pqD4dPmAEcBfcazKAKBGxitWXy6hCz3ZEXm76otpFrRRkHRQtz9IB9ZPz54gfZcChQMl83Af"
  "qh18bP37lwJY3hv2kMf3j/hoFLQVpXL6QaxHFSz1PQm7JAzBaPnAaaT/aZf56PjHX94c/PmXo9dv"
  "D09gJOBs1YEyaP4eaat1ptfexEnlQL9Ql9ygkzvHbD8438tzYBJOnlRtYrcE5LfDwIbspyivNoNW"
  "iSADKjWx6iELjEf326eIETx3IJ5KjLrX3CYVF0z1Drm22I13xQ8Yxuh7A9AvC5l160cDnAbJ10pZ"
  "KzkmNUVIQAqwwkHqF43Vn1PODJqWwN2zXX/QdgPw3774p34PblAGN1+NMZ/1UlXCYEhHN1Uz+n9v"
  "mtuqOaZFPhRbuKn5+pNMZuKcNxxa6ATdEXDKZBSiTGi6yW/EC/jY/+Q+CXyZr3J1zIBR0Kun4Kou"
  "Nz1xb4wgAxpNJBTgOzBeB0P7vUx1NhsDXj6D+35+f6RvUaYcvvexG/quuqc4LyDTv0CHfkHmq5gs"
  "TIX8hh3G6e0PYNJenxyfSOMG36rVcp730Z1PBwBooK/w9emn2YfPT8+HoteTszne3WKGER83h/sv"
  "+xqHnMs5aJgFFqLfUzkjc2JRHHDiq0FPLixuWWFCHROCI51VH4I7NVqjncIs3/yyajD1nw7fvzj4"
  "8PpNs8yk1bzBCBZCxCF8PN6C86RNp0Qlr5E7zSqDXxdqlSFX3mRbHA4Sgee8z4ExeaVur9UoOFCr"
  "4mCnLrb+herpxxMiIFclCmfvpsKcpGLGXMZ/xxfgCSMvnt5UvYZ9N1VNR64YaC1Zq4x3/Xw1OOjr"
  "x/z0BJZ7vuvLG3lTflMVOH4kB2tNVMjCq1X+PtcP6rMmXj6jfSB24qYaF5tC8bLGqw1zd+UVKL5/"
  "whFUu97AWn89nE9s2jPvkVURb9H6wx3wzOKyp4xZBTPycr3of8XJgmUjGQ5QVnpTMy3K9zjgpl+g"
  "96qc65gCTA/3TDZePNi302zRc3ymT8vFUGw/y5SwEiLku4wjfwCFW1zt+tsxqt4S+rqF5YTrtY8z"
  "d4jQSE23evagsXOa0liS6XfNWDvyHEkZI3/6B1Gz4wyUWQHP+MNT434wBBWaAGiRX6s2ypUBiV/X"
  "OHFNcWJ+PV5ku8wRMVNulBJfj7e3EoZcbRY5xk0WCADW47MbhQq2xfwXtXJ71LH8rlk/eoG9wjRI"
  "c4sQ3O+14DU3uUvUMJXOXRvo677ALjO/KaDUhSTXDZJEl9obcNQbtNWN5dYmlkNKcNWkJdcwgmc5"
  "IHF2I3GRZCeq0e2t/g4Dwe8NJiWjkL8Y+M5GiO3jwAl5IVOPVcsu7R/Lnqhe9EGzX20HMwn6kWDf"
  "5gY+8rb+xR2geOLGacEm5GfSceP6f9vdb1P2lgtX9ur1dJFV8g4joKH1P8xQfdM56FJ5015zCdzK"
  "HAylvsovbk3PNhitdK/HxeVAqoCqWF0DNSAl2cpohfUYlrQM1GyAYG9gjPMe7SUu1LLWBY4ZYrSF"
  "YY4wA9UIJAihCt9iPcMf1MUtrBR/aNJ/8mQwMO0TrPlSxmXklCA9mJ91pVAKCFDNnFofycWt1PHA"
  "NJKgNYay/YAgrVr3F6dfwNXZ7lTvlaKRRu9dWayXoJP6mpfYWeSgocqMRyIfvzMMJ3xtv40x5Xt3"
  "IitrUG48pdMYLY2QRupoqvEUU5cIJ548kcBCzRV0fiyvLvUFdSN63zAk7MTXe/OHWmaKsfqE68T8"
  "HQMC660E5bhKjKE2FFAQMcLlTECz1CyBlhn2+jeJv3H9PQeXssYOmH/Vs73AyZYLoJXxJ0/26o6p"
  "tiNgYM2ZxmNz2Ylkd6qPUoaQWcC7Vu3do49b34GsAPlsfqxHAQPto/nFCSNyMNS8uG+aGBOO0tW3"
  "ImLw/GaNqT/1qOQwVQjvARdwDiihzEAbFqeYRpawVSa/h3VtTfmnH16KLeYxbBcQ1zL0JM/WDV6V"
  "BZeY0bhsLsET3qOktiBWhvJfL27V4odewaqYg2FHHLuA4W8qWUCGgSohsyYYhPnw/uDlf+tVKgeX"
  "VVtEmX0sWitxFW8vClW8pMaDt+FvIvYnt56fTEYSEgksRK4GY3FUwKPQ3cUat62qvYNbJTx6+e5n"
  "2anqAgnIm+SOgwozNxLFy0DeppAhNyxAQy9VVH+9yipZHIZs+Yi+MOYRfsKUZKSG+fLg7Z8OTsTx"
  "x7eH709+ev1O9E+vzlV5LaKwEy94N/LSyeQp/Gcqo2yXWOgkMwUlaEUM62XznS4Z2wLTXt4KWNuV"
  "+PH9wQuAgspdrhNUWFun60LUvUNZqCErJGB5LcrsBsONY0UOGFdTe/X65N3RwV9mqoJoW+a4PwNl"
  "Hchjta0kUyfNsd21cmHhY28w1JT21S9olF6qCs1+z8fYL7oCqxWKxA+lTJbvVncziYSkoGqzhh2+"
  "fsCxVEW6PdoAn6pa/ubHtiocROSHovwTCnL/+gZE+sIITl7fSJ2M19ogpU5noJCY5gnwEsrU0xZB"
  "aXJyUX40oZWvoZWsUYfbwKApck+FP4Avvmzy0wNNLvgmmhuy2hlaf9yrr6iaZ7j0U2Me8Rep0z42"
  "oOUnCb/6quRaRtdumt+upTOs3WCUeZSr90qdlZUK+1gpbFQlIywyW8ib0Q9WOkaqFyl7eMvgkTEd"
  "qIteykWtsYASNM4bkgXpcnIrzG312rlBlLTJrpfn2a4ox+t8scxeya1FFQrKz1VevsFrfWVv5HBn"
  "IDFnMs35plhg+CPfXC9L9EA2OxAntVcFEcICFBImb5MJ6n69D6H9AUXgHn5RuvlqsQTKUg9qHa8R"
  "wRgTtP1P2yGFCb9IwyBtoWMc4QfTMp1fraEXVW2dwOAPVUp5MPg8kE8fYxVjH4NlhmltFHhVWxyi"
  "wyWD/tRcAWj9adIku9qAL0wKNOs1Vut6XJVzFeIxSX9j7rBqv544/KeB0/UYf6hB/T0ZDIMmrrkO"
  "6Z9uMHx8PZZD/Ch3laEct9fq6JnhwECHBeZmePmXmfVL6Zn0DcYBdG+/jWWFpwTF3/dapMBpmqa7"
  "Uncay1ZdMFatWlF664/We9qGKusIklRoq9QSrS2xinhQQ/yrpgg5oWIN9V2dgRCh8juHsrQQIfMv"
  "+fVRVu0wkY0V4/sN1qqzDi9/+HGsKhG7o/DtPeALfW82wRqbBmiBQrEFRvr+ffT9DZGpU0YwyXIO"
  "c3wY+Nw4HpiwvJ2ub+odwJpSeB9iThuJabAvxb2GvsNSLgM76jzGUi7L2j8hHh04I629VPncw2v4"
  "fLSsoKt5iUuiWp4usQQC85znOSgyJDh4qNJKix5LTyYbZJcbSlRj79WoVLQ9yxaL39gt1QO3Hff4"
  "1thI8I9Zxs2dyoqI/jlYhitVM1LDYrRctmBK5GrnKWs5lqJTl0DU0wKYC02eseYRgGTbTA4LvaLv"
  "H/gRiM4apwpxhiQHgoh/x1wBrh0GAB4ATRMfdDZFbIKiPZoa8Zx2GQ3FAy2zW2w5bReFMSaVzMXe"
  "lFgtVfW/imxxnW3m+WImPn1l64hndcfvP9fL1NbtcoHm16riWCa4ZIvBgK5ncttZtlypsOQjOwiU"
  "X2ON8k71BmP8WNQLX0Bh5ouBLnlwI9FAWdbFNqTRVZTRf2xfXgFyeXf8eiTxdR/0rlZJ84Yd0EDW"
  "44DzonJYdVmxSjED/6UjdPwaXRGyt6xJFpiVyUYGGqbvShGRlTwX2bIcjB9l1d1mLqgEvyuWfRPL"
  "NnNXg9k6Qf9fLt/fSXqIpKWwbQsUmOOz17jvFncdOP2p+3F2a8r4ZEjQNgqxLstG2pNxWEuAbnz3"
  "qxr/xWqcyTjRb5B0eziIBm8B9YEDBi4H9OJeyXtdhYPYu1hK4T67bfKQvgxEDuXVO+OqbqY5VEeQ"
  "GZlFmkpcZ8S21TF0bd4or6VYN2IDC+Nouck7lJ4rESd1Vdn3HT8QSdDYXO6frKsxlHy0+gdtm/hO"
  "aeSBulcn/eEOCbvovQOOCCJ42jjL11ZjeQ/XmOx74HtybdEimtqkeQY2sHq1xLqbecewzhYKTD6x"
  "7h50ioBqrXK2gtNg+r5er1FasjoawZ8ESjPtjVVqHyJW2KMbtqf8f6wZVAWwSsvU8YDjt0d/QVo3"
  "uDUKVSq4n2rjXr/WcrN2e4re04d+jL5tX2FQSRI6oQqiVOygzDE4oTY2vD3+gHGPvASkAyD8ZomV"
  "sMrZV73GOEkdOpnKOAmov0aCFRA0gsi2c6BFGSuOG1hl6ikFmU13XzS4+cmTdnKJ99tUZ1ArLd1i"
  "cuMe5zXrNkauTlWfg+NW9qlNwyc3KP73IpK4eYLr/LuazwM5jT/gTf1GcKSPLD0P+cNLvWPluMGS"
  "10PJk6FMeew90tHF397QUCa/rvHZBkxa1ZopRJ4d7QbigR81fDjb9Ae6XqLKTaCNz5lOaQ91ZOwb"
  "MXtDRroc4G92vJYLKjVSA9TQXgn5GFfC6zUsJmTORP7/x6H4SeptlY5BLEKlovHt3YTGb6Iqd0iY"
  "GRyXXTV9wjUa01Bc/fsDGzIAjbDnWmK6Ie4hkEEN0hlDyGGYZtTJm4if/pXGMvsSqDmaq67gY5e9"
  "Yf1QSRksrOeectJRw0//0DwfN+6BZIzyszMMMmOKG8aC5a+o6a6zFUlANX5797Ju5wrjzLhDBXfD"
  "9EHyECVixBzrQDGCLbd/N3vuxl+qQV0aAs1+eXn86vDkF88//sEP9j89ngy94XQYDMNhNIyHydCD"
  "C97Q84de8Hj42B+mQ2869MKhFw29eOjBDenQnwx9b+j7Q38K98Ano31aN8dW8OsEqCPN4AEKPtxj"
  "tJ/U7bGNJOHBA5Bs2EVBjSI0KHhNv+UDAiAvB/UQBV9zQtJQN2sC2LeWaheFmg+JcW9LA3sYa8rf"
  "6oPNiRh+RcpINKino5NCIDnR8EE1QBJT1TlFk6cwlayy+diSUE8IJZGokw+tRLVTqQl4JumHRqHb"
  "TwyBiuphBIqIotopk5H5KL+RQS2VyCMtI2wf1BhS2oOWBD5CL5nuPtR8UFTahSQfINs2PeuUajUO"
  "Y1U1JHxjnoMHJapd24FJQA+wIc2Ool6bqdFZcxi+nA09vg55sDUDIdEqICTyICcdqUzkA2pBa9RU"
  "12xG1roKFQVPdlGvlS4KlpZqFpYiEBhUuygoqa5nwuRE0qy8tF4X8UOr21yhUd3cZm/80NqMiUxF"
  "9TCiVpzCzj40fLSWRaJ1eaDXLU/BazhpWpuwoTCR7dOaLkMhsDW9KZQJEXutrjmpNmze1GnvE+Lx"
  "w/YiankRKwJxuyIawl02K2rWcDPgRNushHKH52RkzWVLYmJosO4+RA4vQ6MTaoTtHPO2O7b1ZGh0"
  "wqtVWNDRB/WUxJnNuJbKhknTzlEY+n5qtdeqviHOcrJVQO4qSppeak51jiKyNFRkDEJJbTs6jpMB"
  "tXqBSQFHWSOTKUthMiTW27cIaMNtgiOOk4GJA6cWjXrpprWgdKHBuO2pNYqpufLDbm3f6tmQEqgn"
  "qqHeZTfrafctEr6hv7ooBMTqTS0Kqo+NHg+42QwMq+u7JOrl35gSTqJa7EDsbt2Jmk26i106KrTA"
  "aEMhsEE3pycdTG4No7VI+JTuPsS2xUh1DxJDQUlgzK8s6p1o+W1WVkpcjK6V5VqMVPextgNdffBq"
  "zOlqiJS4MA+OIqC6VivERsdR5dmho2IbA0X1KLRM+l0UWj0Zt25AS2Bqwt2HKEzJjIcNAUdS2FHY"
  "HoZvklBC346tg5Omh2OurVQ/oUbMXX0wdLW1MtJm9Te6o4MPU1cRtSR8osA6+xDa1jdue2CT7Z6L"
  "iMqDphEadoCn4FFU7FsEFHBv5SHqlqgWzTXCpyQqJiyOutdFaM5GS8FrzKHfTYF6/iZ8SA1fsDXJ"
  "SQemNVQpoaB62FCP+NmMLIkkJCbNIBu3l9MwIfWUItKFiELKpNPHMdBnRNhguXC8nowsTR0Zg/DU"
  "ECbdfTCwvUchSK2rY+INJx1eUkIwcWSIVNBoHv8BClMb0bYkHOBvUwidyMHUImFGajgKlpcWMM2n"
  "lHzCIBAa/7CJ1BqkxrxJBwqKCSY2Rcr2wZLO+ENsrs6kbe8ik+4YSGysjaTmA3WlGQqtK0fXd9LO"
  "ZWxDtK5okhUZTFqJqu1h0DmKgCKQwKQwqYGcsWD41R01uJgQaMGk103BRLQe0QONOLSKnKFA8Ifv"
  "Eqi1YBuai1lNG1neXkS6EBB/muNk7K6tiLBSu2I8hdbi2HG9ZjobcZ12UCB4cGq19yygyVBoYnJu"
  "ZDAx9YPGKB19CGgUJaSdaJ3W2iZ3Wf+ExhaT2vrbMbu4w/qnjpJLWvtPAodxB4aJSdDIJGDHLmNW"
  "w9BAKCHh2wFUl4KJoKYuhVYFNSEae12EpkWZcuPwhlMD/8cdaNCSiJiMQk9HTbsbV8c6VNI2j1hb"
  "xM1mbHs4MZlNrfCnHX0g9n9iplu0h5LY8u6ui5DGo0LaBa8JrPEUCAaaEGfI8HEisvB5/RC7wajW"
  "y2mNCdsH30CjJBhl6qioXZ5RRz7LigFSRgTm8owYTRsxEfPY0rVtNDnq8HkTgltpF6LWdEryXXm9"
  "xApntWyILczNWb2IoOKIroupEYruoOAbmiywCDSg2vAD7VGENA4TOMNo4L3HUpgSVD7lKDQuRu0A"
  "cSsrtjKbdDYDElUP2VhQTOJmFoEmfVA/wdUPNIsZMoyg2U8ey0VuMKom4CRgXVTsZhUdXrZuMTyI"
  "i8slbDDKVPcmRumSKDuX5E8agaIRQxnU4iJaxOMLFYGA+JDdFGjmoF2Bkk2GuPIU2rlwckmSxMSI"
  "t3X1wbdinK0+bQJ/jgrutllUp0ujRcENOwoa5/UtErXJoraI1/ZOLklPJ0F5HXwgPs7UbO/ZOJOh"
  "4ETtfYuI6mTtCLJ9mHK5arMbtbIn5Qe2zWKjk00n2lF29YGr4YjNHjggrwtXG+a1IRCzOJPH9k4+"
  "S88mwSZRl0SFXD6rkahIszHoHEUwjNh8VrMyWpDGUPC5WhraCa+2ONOOUfhmTGvqtA9Iqirq0jAx"
  "n8/SSsaYqahbRxFvLzKaO2q4S0cZytwgMGEsgcvJgKnpiSxGRpY/zVUvcPmsRiCCxikPu7Vc5Oaz"
  "tL637TJPgVp3g0CbLvfbeBS3skI2l6RlMqAgiVvdgQ21CAHfwmnuXJj+he+SaAsDjDARH0VxzX+j"
  "oghg5fvg5qsTswdOqIuPsqZu+K0JsyYPUvCNyL9HZq2ZC93B7j74dV7NVDBJK1G1NQk6KdBcUmAS"
  "0EbTc8pRuLUZuyFh7VUbPhhDwbeyUVOHgoYQJJLNRTgTR0UlrVC2OanOUVh52sjoQkBgf9jFSTtP"
  "GxmsDMwUbdg9m4GZc2oJMGmquMNm0UyvOQzfzEyEvEwGTO2kwckpTdjFnbY7dRM+TRomsXJEtuce"
  "MdWXZBhTswQi7rT+kQ0TtKK0c6ccBbt+IqTT2YRxfKM0yM3jWBWgxiCmZuxxyq2LqVs/YdHw2vn2"
  "uVFMnfoJazJ8GoCMWTRo17rF1ihiyqKUzzUbAfrYmksn+dqVa26lOjZm0874RR0ySVOIscEGO+sY"
  "scjcqbeLyVT4jRPW0QcDg5DMeuNUO8l4Pt+duun9xsdJrIoALltt1/MSPReayzNitVzYFP/aBHwa"
  "OWQphHxFsKWsg6YUM2K0vZ1vilwCNFnV4TUnLXQ2B8Gly1ir59QUG6x0snYpV5NjJA8jWyAC2/cg"
  "FCIXewROJ8yCV4eCXTUQss0Dq9aGr6ix06MGhrFTsMzqjp26ZmN5B04aOLXjcjFX12wpmcAsSU25"
  "+KRd10zUnJ3MZtBgyiVYDYNhZdRTruLOzj1Qsbay+ilfL0fzm6k5Bjdi11E3mFg1bXqQqVsGx9cN"
  "pm5hnRERtwKPbL1cQhBEaq5tRty5qr/ULVFsVmZih3A7qv4Sq0pS64fULazkI3tWnjetkTnR4ywF"
  "n6mnMRnRunveA6OgWigyBhGwdbZ8VbCdIU1Ne0EypHYfQrfajpDwzLUZ8jJpSX1Iu+BZ64YbhbXy"
  "LApTa+1ynLSXf0iZOaUKhJeHwCqKbwlMHR3mjsJVg9YwfCONwlCYMqrYYaVvKnNOJqe2PQjpbNK1"
  "20WBllHHRnu78jrq4KRZyB0bfEzYSn7Xv4iZHGtK/Yv0oT4wNaCxJQ+xmSF1pZqrAY0tudY+SEcf"
  "ptS3DmkXbJ88Ylc3dfBD2gUrMOBSoBHrwCHgW7GJiJGHYBgz+c2U+pspCVE6OzjSri0rVt1PvcvF"
  "7oNVRRrZXQjsymdW28d2fjM14zBWvCtxo4sxk99MzehiRIJuCR+HSd3tR0Z1A92xxFk9N7+ZmtEg"
  "WsieuHEYLr+Z0jiMWUyfcLEgJr+ZmtEgUtGfcJE9N7+ZmrE96hQnfPVj6m4FMyqP6O4xLjbIZEhT"
  "MzpIogOJi+XoHg1rFFMnQGFRsGMcISNSUxolSfhdZk7NYIvM7UCNZXHsWA83GT6NFiVd+zeTNkhr"
  "9iBlIDMTZSWVIIkxmQEpAmEp0KpissnSiPRY/gtbs2fquYRIQ2zjm5iLeKd2oJjKQ2ztgOjIodQh"
  "ZbN5zHuSbB6H+KQJ0Q8k0WRTML0sGq6mq7stcOJmM2aym4mjrSNzF0bHPlY7X1Tr6sSuSow5q2fo"
  "gIiOwbdyPGFHH9p0ESHAb/yOudxiTCMtiWU3afAxtu1mRGs4I3sm/La0mKFAI4MeTWJOSHKBpD5t"
  "iXL3oCQWimm1SMhKdWjvQUkIhiGBEq4PobuDJLGwHE02xVy+O7GD1aaCsBNesW2zImYPSuKgwdjc"
  "jcLstU/Zff2kGN4M41qoOOb2oCQOLo6M3SgMMqd7UBLi4zj7+COu/iFlT0gwgkUJLWzgajASq3ay"
  "9U9i1yo6FRS0Pjt2ZzO0TDN7BkXqHhRh7ACz0IHjscbOXpzE8lkjuivHmovY3YuTOB5rRGASew5G"
  "6h6Y0RSiJjZSc9ZmbO/mSazoQWTBRcJJa79AyLCyTaP4LoWJs18gZKazSeV4HIUGk3pW7WSrae1D"
  "NiKqaSN2R5Gra40NCJFrcRImYG4uTauKLOqqNGtToKbVY89VcKr+nB1FieWh0AKu0I1opUzAnNpu"
  "64QJJzYYE4eUmUz7kAsLgcTMjiJbpKyTNphK1Pq4johZWEz9FBNljdnzcJpwk13E5Wg5Zz8QRbVu"
  "IZkV+ef3AyUONjfq2Zxq2JQtnqT+BYm0dOjq1GJWo6qttITeFMzUDVqYUpKwIp8MBc/ci0/5ISnU"
  "KsqRVkfDcBVzehzN5oqH+mDFpLQK0A9IWc3BVoGSqLIk4Nsx6Ac4GTqhbUkisALhnRSYU1E0CbNG"
  "JOikEDCnojTDmLZ6ftrJSWuHGCExMWpleArt2nT282oKnmn12FGYMu+7JIwtZN4DfAgd1zpu+RC7"
  "O1zcuQjtCEPczoRbWWFT8IlDSZBv4zanLmBm/Sxqv7VEhXagyZ3NqV1xT0gog5O6rkeHz0vdGO30"
  "pnaxDr+yItd+68kIraifK1HW3omp0wvPSKF2jiJ0I6lxq6NoBJbjJM0MBpSAbweBuzVtbNtOSSJ0"
  "ItGcRAVOlVdkSNTUrNZlKDg7gqbOOMztup2jaPwoEqzR4SrLDLicDNgTaigzG38w4PsQ8lkKY4EH"
  "RnaD0zAhlyeJWwUTENPPabmIr/A2eWmIDK8frLMTdAiySeNwsc+OPA4NpOpEjptrduaCPekvoTPh"
  "BIHZ/EVK6m4bAgkbh2Zr9owYRVKvipQ7/YZZF8xpg4m5LiI3IM9kgtwqaC1Q5jE6LB/oYTw2gQlF"
  "cZ19CNzIWdLqSRpxi1k9GbH7eQ09GbcltRwnmWr5iLBSe+7TDgoED06t9vbBTiEvUSEfS01Mq9XG"
  "YOMO2x27+3kb603jyHGH7XZi0klruxM7300oTLkTOO25aLdOB9xs0uRAwBAIyT6OmMVRIROcT1o1"
  "R4L6cQeO4vbzNoqWGJSYxZMJv5+3QZSm/og7ECmVypi0d7woFpGau4piwkfGkjCcdHYVxYSToeVO"
  "WvIQcruKYkceYqs0wpJqNt+dmP4B8as7PJTE2lOs16az8Tpi1kXonoAZW/qBhhcsCvRUlMAhoOGH"
  "R+snJ85ZXtyOYEPLheTYFRbTxsyO4MZftAM1jK42sVRktvfNA2qm9Ig5py7I2RFseM1WvMrxmt2g"
  "mcPKgITdGLsZ0ridNZlO5I+x/hG7G7dR1aEVwGQxTMTspdVCHTphWAfDhHYc1+rC1I4EW2uT5NSm"
  "HA1j57PvUjC2LfssAc/ehhYwGibmc82JiUlDMy7vaLnQCexbgwis1IAzF0x2gQi2s6OvYzYpwk/b"
  "uUzY2kymStzSdCmdiW4Kbb29rSnT1mAEzWEgDAWzQju1SoebAKh9HFLCrSznLI3UXFnWjp6EW1nu"
  "SRipubISsh8n4bU93cCctto+ZevH2frq1CpGb7rohuQZbZ/Yu2lSU0XFdm6Cjcslbl1/k99I7fwI"
  "o+3d/Tipqe3jtqSWo2BJfUgJTK11445i6iy90GYEWbw2BRKF8V0CKluV2hk3B4E4pyZF1tIKyH6c"
  "xNVy9qlJ5iBsPcrLZMztx0kphknMs9nJXITcqUkRWRekxIIfRWhli2MyCmdnZsLj6pjsxzHmMmES"
  "0Qy2pydhxGQqQns/DiOT9kkYMZHJ0DwEgxmFR09WtEnQYxk7KFjZiZASCN2dugkfXYzJjp5mENxm"
  "4YTzea2zNCgrp9YRvI6Osqo4Qns6jVwPS8F3qjgsElPjwOEpxweS8Zq6JCbWWaFRh7ZPuBJOzwj9"
  "0dP+O+Iw9T70tnnI719P+FiQvavIGIS9jZ6xmwm3JyilsZzYfOMAkYfQrW6KXIEIzMOXEj6SEzsx"
  "J6/ZyJraFVxOfNI+HYEuLMcndvpgn9wU2cvbdswdDWOf3OSwwQ4OJG6MNGYPXTQRKYlQJHyE094T"
  "lLYxTjtI4qDBhN8TRIwWidQkboQz4fYEpTTGaXiUHaMwlFFijIHbHBrzcVpDxySGyeM2ycauRNnV"
  "qIklUZF7HIEVr3ZOK04ceUho/eSEPZPEOR2hkYbU9uOstemeNZw4K9M5GoI9i4MeHWnmcVK7+tqx"
  "Wc428ISg+9DaPh7zejK1Dp9s8zjODvaY05OJXc1uiqS9jT528zjOicmJtSyMY67cUZBDshgSnrVr"
  "iBtF6JxMkFgWK3YPXWHym87hk63Nso9ViPncYkw2WJgIxInBxi5+oLsnIlcoI+v0JAs/xNz7NxwE"
  "YRyuHnN4khxUQZtHTNjPwZOx+wYPC1GSjFfs4sk2Y+a2n7o5t5jPmCfuuUTN0SqpHUElfYg63iLi"
  "oPs2/Ri7upqYlJiyssUnU3r8I3MimbkJjPonqXswlHPGXeoePknHQLfKsjUYKdkMZ84Fs1uXqQNJ"
  "2MOynBeFsBTMXNTUpWCYXXLOl+OxusFqUyadvdNO9IB5F4q1sqwd4B1VPc7Zac3hbg5atLSctQc9"
  "drVcaCNWJ4oSO+9CsbwDezO+QYHb/RkyvCQn6UbUXli7gaOO5qF1eh17Qp1zEp71bgqPpWD4im7p"
  "oue8YsPjKJCjmQOOhFPBFXKRvZQrXfScF4V4HIWQOWSCEcqAHFNB1oW9I5mbDGuHZ+hGeiPnfSwW"
  "lrMLqKzV7ZzWEbtozqrisjiZcO9jsTCpVUnmxGm597FYXg4tZ3Mi/zETMKeq1qqo66h+bB4ij6iU"
  "pY8dL1qR5Jl1kdBn1Msi7eigc15x6kyXKZLMXDsn3KaO2JriwMg8c+6ivfi85rjhlFu5zGnDqXUE"
  "bcNo51VMJgXPzvJOHRrm0W8dnDQPkLMJWKfPsX0ImRPNIqsLAaeELQSSchbBo5vQeQp0I7x9tDLd"
  "Sj9lKZiZotQ63LmBD7xRtfCDW4PRSuWUIFZeqgOKes3mDmTmRjGlO/ZCOgwbt7t9CDj8b86FeThD"
  "wM+m64JEZHkHxH9xRzHlzmWjIkUO2eSlemoBhLghEDjoImL4EHO1B5QPBkiKOmaTIK3YmM2QecEH"
  "I5MW2ouJTIYdLhTz1gbnbQDWeQBdFGi93dSiUFdfOy8yYN7awHiUnnOsAdsH33jRmOUXe87RClOe"
  "D77rxcSGmgucF9dYcxGxbwCNHT1XI5CoQ9OGVvVC2wXm/T2sxQnJu4BMNjDvEHIsTsTVP1CbQwGC"
  "M5tWrbxDILAxiiNRoftSJCpSgQWUHE6G7ouZqFgHFlhzVlbs7iKzFldAESOjaUP79VImgcABrYx+"
  "iJ0aDBPDOLjZouDCbpeVU2PbYODMpoP83ekkr7a0KQSM+8GJ1NT0Yhg0GLk1GKbBCK0wrGMvAssN"
  "s/vgnJjO2ovArDBImuYh+4KPmOsDrfNKSA+ct4zEnESROq+EyFPakVhwzgJNnQSFZ+2D5yk4J31N"
  "nV6Ym/HZPkydusPQpGAcKWAliph3maXWS7aatc2nqqxMkLvf317ddJdWyr7TzXlfWZOESbmcnaFp"
  "0+7sn+ceMsH3IaSvREqIzYs7kp/Om8is3fqm1bNLE2IXgcTuXnuKQSJ7l1bKvhXOeQOetQfdYynQ"
  "UxmnXB88p9DEohDY5SqRK9Qpl9O38t0pVxvg0QMBvA4KnrGPZepS8JmiH0fDhO7rxhILFdPiw5jT"
  "cvb51wnBxVbtkiOTEfPOycRC96R+ilAIuTc+uNNBD06NXf+CrL/Y7UHC1boYnIydXW4OJ0P7RSEp"
  "WydmF/541t5rr5MC2UEaWCR8psAi4ny9xNrvb/p63PtSUqsqOGH2+9OVQSpNItdbjNz9/nRdOK99"
  "SZ0qr8Ta728SCJk3z6RMlRdTz+bRQwk8loKBy92aOs85nMF3KZhnK/gsCfuc74CVqNjaL2jp6tB9"
  "FVFqVXmlHcWFnn1KhedSmDq7ojhWWid1R5zdbN/pZM9myL4VKnVqWVP3iF1azGqVJ6TMyc9MpahH"
  "t7FbZaakpjdx9/tTDOIUeqROPa27358iELvaxI5Pph0ls559IoBZb2usrKTrkF67PNoouzH7kPDv"
  "IHXxICn9SZn3FDvn45rJB6f4yPQvYm7HIWMuaA1Vyu47sIu4a1TsnJaunuDsOzDUcWqNgS9CtyqT"
  "qU1IiUiG5MAimwLNVdFXtut0V9pRjG9F3VOuqt8jaTt7sq3VTWtRUmt1x9y+BqfWPbEOyDU3LqTM"
  "m87s1Z1yOyw8ks62t2c457KmziYPz8jiMjtErJp/qxYldfRDaofu6c4FpxYlZVamvdHF2rnAb5jx"
  "SD6bvkWL2+HVFMTT5gmfP3V3eCXOph/PeqdTFwXP1oKRzUhnL54zm7YqjuzpjJx6GEcmE+sdYkQo"
  "A2dbI7MuIrseJrXyF7H7NjFqedOOjWQe854wrg/ONtHIXt6WNmU1TEiqWVKCaWO3msXScgm3Rz1l"
  "kHVsvk3M3WWWOJsCW0ybcjsKnb1yzN5Gz0iIp87GSMtbpPUwqWXyYm53p1OhnTibRL0mbpdyO0wt"
  "7yDldqp6ziu6OiiYWbXAIhBwexIjezbtwFfszmVkl8smxNeL3XqYlPH07C3H1t5q7rBi6m8m1lvV"
  "uF3JCd35XK/MtGPbNNmVnHZswPbst74xFNoy0dTZRG5uCGZ2oBMt576NzdJzNGwWudo+sitqUiuK"
  "Etuvt0isyJ5TD5M6kb3YCXiRuJzzJjTH9to78hInNpi4hxWbltfZFJg4pzek3MkGnvOqMvNYBMND"
  "ibvPV/DcF6b5zih8I7gRcpycDplASWKdPp10HFTh2W9981wKzZvnmEN66fENifVWNTNWnHAvQk2Z"
  "aHFsvhOt7UPaUS6SMhmMFmckTsw85U4d8egruqzMgJFDSdx3qbE9iDsokCxt4DSfuseEUQruoWmB"
  "MwzDbDEUPDNLO+Xa+1Yu3B2FRw7SCy0KUy7/afUhcN6XkHArK3IO4rFOE02483zMdZU6hwFZucWU"
  "O5XII1vA7CONSH4zYY/5tY+ooXtRTZlM3HfKMVrOsGzOXIRM4sDRc6Z5dSiY2cuIac6dFRY7tQfm"
  "LtjEtTexux+WWpzEeq+dY3GcQ9Nip/aAPyrLs9925nEUzPMtA45AwB0eZ8pk7O7ETXjrH9HzxozK"
  "gdjeicssDFIZENqru60iDXkCgVtKGhtILGbfa8discjcUUtPwE7Yg4IpEousHbUmMk+Y99o5qtZK"
  "SccWMk87DqHzmLchehYF8o4v6xg8z3pLmHWGnnUWaMKd5uc5LzvzXQrWocxTrhceOdvZ7YNv7YSJ"
  "2bmwz8CLrAor951yjKcW092stOrP2sPKSnVs1e3auwZS95BeenSjs5uVrm47c5i4nrdVYhkRvztx"
  "32vHrG5a5Rk55y7yB2V6zLsMm1M2rVOXjf2jjLXgzk2jpy67b6VjIlLW8W326dMpd+Ko57yqzDyu"
  "1KplTbhTTz3nhWvmkakWAmEKvEktauxEpSkKSjsOb/Xst9+ZJ78aNf/s+9wYRUlrHOnZ0e773BhU"
  "bJVZclVeRGv7vhxj0nV2rjzklkGkE3O2WkTqsXPtnrtIMb6kQAKwPAXPPaePkJgYdYMP9yGyC/Nk"
  "+xoXTNllZ1dg2jWOkoRvhg3DjlH49nlghMTErs1k++CeMBcanZjS+lCHgs+ccDc1SRg4KuiUB/t8"
  "uMCkYKCg6QNzYb77irSfWhvQWU5Gw449OVErEQ0K6uSDVZ+kEUYTlLciUe5sBvY72UwKU/fwO242"
  "I64Qu5nNkLwMpUsmDU0WGs3ZSBQvk5FbkK6HETrnCNp9CLj347UkDAwTdI7CqbmPmvaRu7+b11HW"
  "mwYjowdTe3+3S8GxBgEloUOL7umyHj2FmzstU7PSPD4m7JrN0NrfHRkTSk6w6ehD6OzvjohQBq1n"
  "EfDrwnp3pkXBM0/yCbhRTJ13Z4Y2K9t6+6BLw7T7DgKHgP0WY5sC3ffguyQM/OCxFDxiUqccBZ8e"
  "zsZLdcDHpLSio/VRXRSck7SStn1sn5/PyEPkpsQTUx4S7oRbz8ktTpwyTp/mFrv7MLVj96HRBWOL"
  "atDRB/rCdt8iYbqr3RRoZU9gEoi5Y6k5BBK5hR5JKw6hXSXC6qiUK+L0acZ88uAoQvfAb41iIrfm"
  "p8NexNaZ45qREVN2xNqL2D34vLEXkV37ZGmYwK4KtCh45iH57CgC540ohIRHanJCXqqtDGRoEwjt"
  "OjQWP0RWNZsxCLcUjsUPMRNJaRZnZBXkMfiBKetLTPwQ0ZpARqK42sKkxWLkhUexo2ntdx0FDCt8"
  "Y99xzGr7iKmxTFo8GVoFmiwijaw6UaN9yp125iLS2HmzQsOFyDElLDJ3Sm6NuYjsel1GJsk7uAI6"
  "DN+tGWZkMmJqjxMT29PCZWdtWtGF0CYwtWunWQ2TMpGUxk+KrALuDi/JeVmH7kPk1pA7ujp0z9mL"
  "ibIOzELTqEPTxu5LSxp/MbIr4R2ZjPiSekPdmycS2RScN9sFzGSQ/QsRa3G4nQWJ6btHJJ/VYTfr"
  "GgjSPuReOcPNZiNTgUVhYh2aMuX7QE/6t4mQ0CJDwXff1+j2gwQ5Q2Zthu7bu2MLBUWkkMMZBXl3"
  "ZugMYkqPsQkYTkb8ph2C5kKzkMPBtBG7bygxMW1IwlkOro75Fy5QqTSVCMsHOxOcmlxgSh9dVGxW"
  "1QQNgal9WjJLwbfPf5qaNMz3pPMUPPc8srbk1tyfxZTrsnvEfIuArb46KEwdXRgarCQ6lB2Fb50o"
  "FtBBBEx5RWKvrJg7daSl4RkHo7EUAvtU1LYM3NxNw5SQuydYGsUqaWstmEqXhLNZbs1Matos+wAa"
  "x+Iw59iYAtEq0ZCXyYgvHkqpvTAqjxIeBdHqpbRFQTF3br2LghKu5M6nO7wmnRQsDRRSRk4tHcbN"
  "ZsDWs6WtkiOK1JWoqaOKQ1ukQueAKsY7cMr6UtM7iNkdR84ekIlTdOfTPSA8BWMHx8QpmfPpDo5p"
  "BwXid0+t9p51aBJDYcq8fo8SITs4OvpgncUV0k60wV57Lx6zN2pibekz90Z57oZAZ3+W5+4rpPuz"
  "Jh0UaNQ8cAjYsYmoQ8OEXMmdT/dnTTooBPR9joHDSp8eShYxmjbii7BT6nkbFdwJH4dJnZI733wv"
  "JT0xgIkFufXwqRkNiq2DHZk4jFOSn5pxGOdsSSYOE3Mldz59SejE2nFs7SKZcO9V8u1dJJ5LYWq7"
  "tCFLILIO+mQwjLPBIm1jpJF71qgT4YyYfSKpiQbtA0+Z6CKzWSU1EWlMT121JCpi98sQPEiDJI6u"
  "TrjoA10ZNFSTuBHvtKNw0Lf3RtX7/ZmYuX2MLm3v5HGZeHVMT/Kl0sCU/HEx89gp+/PN86Mm3RTo"
  "/uypRWFKa7wZCp79xoWpMw6zmqVjFAYeJKd1tGd5OY4gWZsR+267hFmbCUth4ry7InC6YThrU56T"
  "U5o3I+19J9/mctKoRfFZIhMqMjwFki+KaHM7zxSyozBTVhEdRMwel+7YLLtSxWFkZBW8OPghdg6P"
  "p/ghsmNNlr2InKohSyZJ4tGlQLM0IUNgaud5mJVlxKttElMm2cSs7th9HwFFc5FVxmVhuZh9KYKN"
  "5SLzjQpMxDvhSu58eirahJ4uS3B1ypwYQJF1RCvimIy5G2o2DYaVAY35CoqY7Pc32zPvtnCwvRmb"
  "iykXjAptnoLzRumpQ8MzNsIzFCbWOxtsAh6t0GYpWLXugUMioEeNu3yIuCJRZmWEZq0pWwfCnUFH"
  "z9EyYBJb1ZM4hYO+eTLbxD1Cj1bDOu+vofrBLrm1OMmU/DpaqvURXAoBU3Xs6MmQFFlGbhQlZnbr"
  "U2wf0QpqR9vHzuuMaDQpcooG3WhSzBUO+vQMqwlLgeaqpxwFj764waYwcarxIpaVrW/uUjAz7gFH"
  "wHpVwdSiENDdlwE/DLNmfupwsq1Un3ZQ8O1KtpCPkabukaXm6XAUYVhYLuF369PlTcu4LEwbOXtM"
  "HDbYhWRO1D2ydspYk+kWs1kyGbMvPKMeilVRx0p14jzEN993wBx/y7ytwBqob76tYNpFIWDeXDk1"
  "SRjvGujqg++eKBY27T3uhdbcKALbi6gpTC3Ho4OCW2/XkjBfGjXtoDBl6u3MYfjmLleGgm+faGaT"
  "MN7BMensg7OTLSJdiJiMn8WHwEY5EWFD2qHIPeZNAZZB8M03BXgP9cF3AGNkTGfgvPGek6jQqeJo"
  "GRFYiLdrXUR2/UNNIrChv8uHqV3zF1I+kHeIB3wfzPPAAouC7/gvLoWAdYSoSJjHmrkyGXC+WERk"
  "khxrxskDrU5q38rQuHo86GZ8PRu8++Yp/d5DFHxSpRWYBAIXIkXMbLpV5rE1myHnAXnOOf+uL+bT"
  "c/69TgpTB3LGRKopXqUUPu89OrvazHfLYiNWV+vs+Ky/XJ+/ynbZQHwV7b+nT8U6u+2Xw/Ph6UBs"
  "81Jsl7f5Soyeix9WRbab+gdlmd2JfnVVXud3Yl5s7waPBPzdVDuxEPtCkx0v4D9DsYErH8UfxE9D"
  "UVzt4MsmvyGU+pvBHrQ/K0rRX+U7sYR7JkPxV/yzB9+eiQ38efIELz3ZFwF09xF2VD2whNsWn/76"
  "eSjO1SfxRHjw7bT55n/ek/fD0z8tP8PlUjyHm78XffxwCh9KMRMw1pnon+sr5/IKtruH/5X57qrc"
  "IIG9R/ctFy+W1e5Nvlhmm77FQ8VF+ZO4XmbCD6PR6VK1KM7LbA282ORim1VVBx/xTs2rn5ebhlVA"
  "iGGW4lI2XuWb892FZNZAUviU4Yj/JiafnzzBZtgim89VG/2gbHUG3+vG4vlz4ZEnXKu7r+EJ8HT4"
  "gMS/SjIwG/Ip15/hmWeij9eeS4qDmmfXMAGTcbhn8tEPQ+QjsGhk/ROnxS1wBQYsLvIVyF7Lnj8d"
  "vn9x8OH1m4HTSP97lFV3m7loZkeS6c/Xi+PTL7LDN9lyJ6p8s3i5XujrQ7i+K5d5NRP+UOyW6xzm"
  "GD6Hk4m4H2CnoZMvP75UxCpRLhe5OH57KGAys/JOfDyBWa6q7DxXC2WVwaw6PZ6J/Dab71Z3SM0P"
  "IvFC/Me//0/xafJ5v/eiJz55n/ev83LfE5/8z/un4tP08/7ZKjuv9ifiUzAeh5/3r7xIHB2KfFvM"
  "L8SnaDz2A7iK5E6Xu5F87FB4qT/B79UQ/yu+wLydr4rTbAVNX4nlQsBwj05ejM6WJcw79vf0bpeP"
  "W3k+A8HMXwCBd5J1SOq4fHu1WgHBoXq4Wnt6HlE435XFelnl/X6ZV8XqGrpR5l/y+W4g9p/rdYqS"
  "8d1NdbzNN+JvfxPf3Sw3i+Jm/MtNhV/bb+MyzxZ3J7tsl4vv9veFh7OmqPXxUYdlWZT9HjSDBpve"
  "AOZHdUQJV71y1tW5sXASvW4CtW4E/gyMR5m+Dfw90fkPeAuT0zTxsIn3wP2qCcxj08THJqfin6W8"
  "dzc5LZfnF7sNiFHTcvpZrblvPEwKSdMowEZKQvQj8WqIV/vqMiztZND2ByiYctXMVTvvAyQxrnIq"
  "C8BJIQV5AoK8ze5AlS8M/i8XdT8eGHQ2vwQrs5tfgBRe5ndGc1yEqNPhqR/Ueuz3lTCByG4Wy835"
  "eJGDXsr7y4WUAFs+kLReyUqiR9h5KS73Q7m0tSTU5HB8y8VQChuR4aHuzL1uYIgqqpE+MEeZiEGX"
  "Ont5cKT6IEDvlznownm2BZFtlcRpvplfvNQXpQnQVB4pdsjf32S3wBFg+B7HyxIGgXTF831xGKDx"
  "FmAnnp5ewTIfwCOrFbDEJHci+7IvPn3e46QKJnkEnBB/fHf4YwXtwV6dIifwMf3qYrmFa4uy2A4e"
  "oXmQFN9fbd4WXTKLMp7tdjnoy/JqI3YZkFllp/mqEvOb+ax8C7YQOm6QewddwKfti7NsVeV7NjnZ"
  "CTCn2xKMaY429ByUxkKslpfAacXYLShlg+TPWxTTDqK4EuTvYoUy2WpEc3ZUn5UCxD6Asbkoi01x"
  "VYlqk22ri2InDn74cPhegNGXxgZ6hLzrg+TBsqjETQacna8KlP1qrMic1E1hLPOXMHO7i1xxfJ5t"
  "rrNqIE3F2+MP8H398noobi6WsJTxruViBWanzK+X+Y0iVuaLMgP9eJrvbnLQtkgHmHPiBe9GXjqZ"
  "il1e7UbrAozY6dX5ALuwg1kx4RRig0Uxv1rnm914Dhp5lx+ucvzW76ke9fRqmI9vlgsADIDuAEuM"
  "L3JUY/Dtp/rn83z3stjs8lto6i96gzF27vUa5qUvBwuDmcj//zgUP9VEZSe+bHGa5uNdgTjy5/dH"
  "/d4S2z39ss3Pe0NcVmPk+R/h638fjKvtagnPGPYGoKYVnVbOx9ur6qIPZh7QXR/I5eNNcQMaZSR2"
  "k4F4KrwJKAR40g8Achf96WAo5DyrXg0f8Rosv92iUd8eLTd5H5pgh2stIVVo+/gaVjUruYav9WBX"
  "hUR6bRMwT2PZhz19n0ENJP9s1x/Uv6yK837vUCuXH34+OpLSIpcHjFL0i9UCZlzclNl2C7IPSEQ+"
  "rQcrGD80dLDF67NXV3l/V17l+jIa1XsQO1gRYEHQGOvHyf6gwkE6/Vz88z+LfFzjIDDpudS1jxrw"
  "dJptLqVuE6BnQZKWc8Aku0Iu0adzkMabolxU4y/Vnjj0xeJqB4qiuEHVA+pMqrFq0K7JapXnW9C9"
  "Ch0wQERaC8N8lENYbqpLDRFQPC+gVwh2+psjNSU4cbWK17/j0KxLY/BnAJxgI/10+wY0N3sNkMdv"
  "Lh7ZHIFH5AedWH5zpFG8khV5i6J3LYqz+pHA3F9eHr86PPnF849/8AMA+8ZiGMhnf1rKJ+FMPbke"
  "KBSj3Rp7sPvA0c0Mnj1Uvb7fa7GeGhTl4DvEnQorbsHPO8oXCD1bDUlh8EzD420NpiUqBa6fqU89"
  "MAdaFpB1O7ipElutJjdgEWqsgRpc2qjluUCNCp3pX+bbneRSdbde56DU9mAB7MRVBTr49E6qS22D"
  "pNZ7aGrAzMppQXHYqrmWUAjvHp8tVyswrqG8AyxYTpTnqeIiFS7gil5QzUR/URP9RU40/A4f27nW"
  "GgQIfPpST932s57MLwjiAB7+DUCdJ549E/0vAOjigblk3UkzFk++e3G120Fv++p5/9TvnQLkLneo"
  "n5dVdrrKkcEmXgfD8x6ROX6BlSsB+hgs+QY0yp4mcXBaOCS67j0B7ERvJUYfnmJZbOa5cO1RFzJ2"
  "lS9O4qR+/FFxTp8OWu094kB6syHqNczoV5aXL5HNZgMQ8Qn6UyOwCHm5RGPZQhO0f+rJ9SV4Ol6U"
  "tnGDql9qC6lb8RapVSsJKXkXWWOpSjpK62x+AUaoyym2XGRt4zUz5YLXDJ0pVDQU24usymeih+gC"
  "TK20dBuJ+6tdDp8nrE3E3yr8UWQoCT+AZ6IpohaBYSB78CbQCus1us4r7HUfNNjJh4MPJ/Db3SoH"
  "+3RzUazykcLMOr4i7cRom5VLMAw3y90F0sMVXTcGcZ8vt0CtDciIAjwxHeYC+7xdAUoD9DGao6F8"
  "l07k70inyldnoyo/x0lDXSGdMXEKfnOFWAzmCzq0gBkJoO0G1F6x3habHPUXSAb2ogZ/AySXbRZK"
  "2QABuPcKQJQ4AeNaiawS/wNwi+rw10tQvbMeMgR4PB6P7/8HCsBccsXwyOVM5VucraqO1mmAopbv"
  "N4NurSECZnQH3/6OoBNw1Y3QbTsjdNsmQretI3Sr5msdotM9dWJ0qzpIt2qjdKs6TLca7NlhS0kM"
  "Bqzifas9FaZa6SjYPY2EDcXF+iF/GyjXkrjvyJwSy38oXtbfyKDbAO+SPbneAznMs0tALLKvD4sp"
  "rDOErqIR1mEtq2erolgINFnNDMNNOAjAz9j2JD//ABf+JiaoY4NJKwng7L27Ne58I7/XN7b3Vehm"
  "uOjGkClQU/NLfcvrjSV20j2Ta00raK2gYJX+CZSH8lFN5lYTxV34K4WumrSGEzmK/flUTT5jN3H+"
  "8eMzHPVAYorl5ipXkiaJaQGWXfxUbZ88QWmp8EpNRqMldf9camsUfJicuscCPbIVOvTggIuJje6X"
  "C2SjesJoVG0/15AbiClpbO9FyZFCu7htbsNBXQNhfOSgfvA1bYdPwOf8Hn2WO4z34Len4uMAZ8yk"
  "dItdRFD7nRwh3jaCtYhX9IPV9+f7imdfBb1PhsAou/RvdQyufs4zUD9wmT7rifWsJx3PevLAs57Y"
  "z7rjxvTRGtPHjjF9fGBMH+3nPBM/cWP6aI3pY8eYPj4wpuZZ940oo7QBHbkSkZJcJyAyzepQnq0U"
  "jL0G+TU/VmCG+/1MAnLwhzLo+amxLDG70s9K8I226veyrAHQ9/jl05tsdzGGh/eNX2DwQwTFfzBu"
  "lzIGfsVMSZpGnl9rL1przhlotqHsHBjfmfhrv+7nUEzG6WQwVMMDw/USfgCd1oxDP/qeIjL0IF5m"
  "276ddiFaW4W9AYJBJ8CPXIIdX4OtRtcTPY2qYUaJdgBUqqRojKKcS9n6XjTMqG8aipc//DgGoP+2"
  "Vor+ZCJtE3ed9L0m0bqvjde5OdlJbTwADrcX3+VlfV1nJNBFHk3GYe0RzSTmOPzzwcsPcmSLOhMB"
  "+OEV3IueCGIrwE6yrfT2QLYm2A4JHr9Fz0DiQeWVzR7yySTths49SAReGU3QaxcyKzGdTNBNjXDs"
  "1mOfwVP74Ncv8rPsarUbyK43z5ew7vjNu6PDN4dvPwANpNZ0QT15orwjjKiBHJQ5GLyqAE8Q/qPz"
  "LCKc/H5AYVTNhr70bqG5iZ8ecARJZBQNbOOF9jcFLmVQ/di8zdyd3jguoLHutktpDdUIcAoGIF7y"
  "y8y0gMsN4NNdc+MzMaEIjHiQxH1U7Qu0yt99d3pj+JDw5M9tiAruwCSLetCv8S4ZvxLnRgVHnzQh"
  "UJySmcyRqbjxRa5hshEvxciyQO3VhMGx1cDO4CH6BbJm4FUl8ZByHd2RfWPitLp/MhaGuVbid4By"
  "Lq7OVRgVvQsE32J+gb7cChgMSByWC2J06dj8DXwku29IV0b/dtn5UBQyxUjCqTTd+FXM17BMe0ga"
  "ED+u2OwcJqUnvb0/nhy/HVc7DOQvz+76kto952fx2cqJylZ2heskC/Ax+pGzb8Ttmr7LQNt0UrMS"
  "nNPT/E2eVXWqQgWrG0SsVf1T6WNpeNwwOFvJ7J7sTCVdJYxUguyBO7tcw9Iq1jpgNL7YrVewkuf5"
  "FgYHEBn+gvuEgn22PBewfK+qvEJk+hR0LWaPqrF2bGUP0WGqkaOdEjZG0G8DVSrhXBWr5UIGlEAb"
  "ZTtMeeJKwxEcHMJ4EFNDf89leihTbp70UxuGqYxzPdVAqYdlCGpmTJYqawLOSjiZKENy+g579ipf"
  "ZXdvKsNyTHBGsIv/5os2rC+d7e7F0PqEKqeAgXgZdkf/sG+F3NXNRo3GFtHkQrqR/zscw+1t4xni"
  "R9M11N//S3xDxx+EW8zQg67d6O9yTH7gqn4iJ3ydVZeivFrlYBnh8y4vRyXM2m55nUupGHS4lv/l"
  "DuIqW2+b6MZMhLfhSAEI0AIYbofxL9dZHbPAwIi2pvsSaskQlA7TSCrX2QpcJTFfLbf1LWiu/NDf"
  "UwnH03O1aJHcvMjKCusJVlfl+NcFHP63iEwTAfhtEtOKxEq62fFtPFqCscLhyXKe3U0hpqNdthUX"
  "Rbn8VwRGK1nMk2PSAg2qTHGoKy0wgObvsOIHrGtVzo0KCe2b89wC69LNRoORd4qD0k2BPyhUzW+3"
  "yvNo3DL4CL9rA7P7dAdL/yP0/Paz7lt7Rfl9TwS56FyQc4DZtCnTKU93Sj34N3SssDq2+wRO2Ajr"
  "Qox+7Gi35D1PzHuMbtV1XHKGjRAFymk9O333A4jRYKCgICKK4mwHSnhTLDGJrFYG4MA7Er1oQxd1"
  "FCRHCZ8+XNPRIE25BCtY0oAuQbuAOOWPLK4GLVflxyf78iEkY1RzOGg5LD9a95rxh2pt8JMGIZ4p"
  "50bZKnTT3iw3R6hItL3yYjBXbZhFjQSzMJnUTnJQdd4SF36lyhow41g/qOn1Aoc4Al0DH56B2kGs"
  "IhvglVZCzBa3dYtb2QI/mEmVphIJfkcHfnFnh4Tae4AJUorwHsWLPor44hb83OfiGvVw3XtdS1Cr"
  "44aQGTmQ9w6UXCin/drK16if/g+57Io6BsX3tWNee+V1iA6twFtix+piAXDyZIOBClloEzFQLZR1"
  "1bfChXfzXW1L2o57AF7/oJ/wlP5ohxQAkUEvhzUtGlww0bIZRKipDa2kvYoo8CmWQw+6cujPQG4X"
  "C7Br0oOVhq4u1AG9n2Mh5/bijjbXuQ6dLFHIsq9BJTCu9dzR+A4QZx5hZa1MMkgECvgPvqM79RLT"
  "B9KH0b5Vhbfjlx+zrbrNyarAtcVNvlrJ56vbTvMz1Cba0397+OcPdbdwRJv8FpYi4JgMEXKvqsEw"
  "pgQrOWZJD7xp1zk7ktxBvN0Htn5ARwjQ+bJCX1uli14vbkmeAudf/Q4zb3rlbUBHVXfU4QI00Jh9"
  "MtIY6N29yCpELuqp0qc5Qp/maK/VoNlqlj+7fD47enb0fE/MLkW2xYKvSs0OcPNRvTixEwMNpq0S"
  "yMVODunJE4D0rxdNNEDHJOqABE5inQ5+wBOYwVTfDzpKuM4Bri1y7X4o8CFlYn4zP9Fi0dchE427"
  "rjBtNzCmV6jCTi2jeule5KsFDa4rP2dfNF5IPNEVNfo5Wp1PdaGcDm9LQs9Ua6kAz2R4SWVlm6Tf"
  "oNaEpr/jYxxFtgfD40/2tMqTnanlXOca2u+1UUn2yM0g08atSsL1reBNEWR5qbDFJZqDli7qfrff"
  "cJsdQtlN0Ii3FUN7hltPIhNSGFEGUQQv9QRLSA4/osIzRXF2+Uj/vNzMS1lYBUhRuuiXOTquMlYi"
  "63mqNUiDrN46Au7BuFfgqhebwUNVRmAhJG+A3gsZCah9yWBQi3hb6yNtFym8qrYqI23VSVVt2Ejd"
  "8ayeCxtCyCfsNzM1UhT3DLN/4yucqsUKvj5TrfiJMSq3W1mCRoYktWa0Sya1BVG2unV0iOZsnJmj"
  "g5MPbUVfX9fl4WKsQFEOWl++jgD16rQuLPLjUywcHQNqXJ5v+o9UzEYme0Wd7a3NEGiuulhnJivX"
  "GpUpdQ5qwFmrLNFmzIQ3btLvsoezVrCHrWWYNfzXQSSSRv5GUGDYatjBUA8AeosVIKfZ/JJY0IEZ"
  "LFc47t62E3D7oaeLTZq5gYsgBb1Dr4cEmvoKuCCORtroKlWog1Uo1LmnDA7K86fPg/GXAqCOLHSq"
  "cYrmH4NUjhCpcFTM5OGvER0NIvuNHjCMYA9XOoxJzp8up6it4IAjhaF8y1offHgqTXRjm6Upmakl"
  "8uLwh+P3h+oS+uDNTUdNjvLG3NtxflPryoZ+qytp9vIGliHc/lsWIV2DeqBtREUPl1O2vJT4XVLi"
  "21LitzgK6zEyrKeWcXKUFRM+oFoemCLk/6eIkP9fLEK+EiFcUd+QoP8/TTmLtKczzPzsoD/VDTwH"
  "DAwINnw7El+u1ltwRM5kvL7M5rm184cToWmXCE17LYz4ssZrMooyXlYqmiKndfpHeOQAGdJ+NSyr"
  "D2q4/QG08adwKDwfFGdoePqyq28qPQv59IP+XuMZX0+Dni45Q/W9ZL78cGKJ/ZSwSok1DEZL8eh5"
  "D6v9QXRkrF8/Fi6sK/EveEk+qzON9KUda53j5mCSk2mSdnMfm3/6AiZsV9RfZEzIBE0doBhaGKhY"
  "JiMP9fQL5ReMnrd+F3wfmCDtcLOgeKUZertA3FoN4/5nikbnKiDwhseENipUi3gq8aBkD0zCrpCz"
  "UkiguMPPJzLf078cjLfZQpZg9qcwd5PeoCF72VaI/GfjvV+H+CjmUwJE1IKSXw3zjKCG1nEMRwnH"
  "fgWGYuETLuihFJFZGwnSnIZ1IDk9rAVhqDo+rCHTpZlb+23Q6KGZHbREv4WYdEmXXMt4i+nESXsu"
  "hX+Vn9s63tDcMuv39+rubyvrjsBIMBPgd+IOYmUB++DtrVSRv6zT1+ixEj+//XD888ufDl81WpsW"
  "LmAMURV9agiECS50pIcqZYxK4P3hwRFIstrf2D5EXKGNqItAD//8+uTD67c/6t+25XK9lOmYvh/U"
  "2zTbbWDKY0e1qLI30F0MfC4zmcteL5Rewo4g7ZODN6CKBm38R2ercRuT8u6qLYiY3sY0+wPWIeg4"
  "C66uAgeXY6QAQcTN/JdtUf2iLoxxo9dNUV5iYehoWReTgiMJtKuxeHusWISkLnLENzJto5h+JF6f"
  "qMwq3tKr0EbiLk+p13cX0HkgMpC1qJtC9wDcTRmawlwsZl4lSyrleJbZjW6a1/0ec9Y16LKugQ3Q"
  "AiolrXyYWCx4CIvpMsTs9kUTqoCf/RBMrvyGtY1lP/Qm4qloE6reUFhlMxjAnYwnXuAPWpCXBx0Q"
  "b9JivOAfxHg1aDQCLQgVmqHg0Ib4xBYY1MqBbKmrv+gog+w6qUpRMjlS8qg32M20SD4Sv/Ef0OuT"
  "J+439eZyS+fs76EIQljmensddnJerFbLCuSjMk35hvDKM3lFooNWfVU0mRhKtVEwSqn0DT0zmDlB"
  "Ozn38k40D9DNNnbWFriiln4pExhgbHHb0f5zGjtbN5AtnpAokRVMNOOdpsQ2w8TSJE+ORwXk8uBV"
  "HQYddvPdjE8+ajaF5YEa2Eztp2rHvC8DpbL0BEffLEfNBlJ8oNBjveGPhBPriG7tbjDNCeLS4cPF"
  "b/I37Jih+EbUsZYCaasaC9Jon1lrLBozAfMOMrIrQCZBZ+2Kq/kFiIi0Vw0vQdxqAmBTXGMyeq6t"
  "CSCsajdQABzE+UkzaT1g3qISWD4of2xqANE/BRSsJoWE4rCs453O0KsN0KTQY4xWUu+M1qd5fAO9"
  "DAh53fU6S2n4BDoTv0WMEXT6AVszt8aLu7tBDPTdwA6oa8CvLfW6On8qu0ZBqoJK+0Ltx5qotNFE"
  "zMzlWVdenshLFszdnbC43XUHRnjrs/qJXbJqSqo3acgppqoUX1ucI7nQb1E9KVbrSUWN029offQR"
  "toaPsDV8BJ/4CA9j7WaCgMlvDk4+HL7HTTnADr1tTlboGdjz4FCsc0RFZT7KFl+uwGAbFXq40YHC"
  "94fSHBTnS07FE2tWYOxvZGGRw6zfxiu1+npNK7Xv1NhYRKv/FNYB0IXWsl8jNSzfUeF3VUBIJRD9"
  "A1kfK+uIZ+LFz6+PXg1xu6SedO2RPcF8KwalunZK4WkNYIZm0pcA5CTXAxb+zoRZBgzXFICZ2Yim"
  "i2qbDG2PpVEMHgwVUnwNTKoe6JfexPXps/SyDnYvlM4jKdMO701zuzdUnCLSycnKKZ5J0Gvk5Cnu"
  "wwYHABy1psrtSNUgqQMuJOypveHuoFce6BDK36SC+AfiXWHjM4W/2mdiPaZwpjbqw9qS/qisG5bM"
  "GHxj0x4HwcMuCB7aEDykjzWAN14nObZIWfraJ8BgkSkhTSMSnvJsgPDwNLuoMN/IQlJccGgWYTm+"
  "Onj/3+y0pYUffqvXayMIkhycyOTgA0z5T80VytgBPmbWEf2hmv3/dILvgVlv7YqZ0CMJwIdkxkoK"
  "/lclAjuDT/9o6CnsWTm4DvnRcaf2d4cXbSDq7whDyQ26jWPwK5N0vynaI4MDTZmzzkqTg2twz7Ys"
  "BZcOi479oF6zdVdzd789a8Hc9V170E0xf3sETF0f1pbWK1Cu0+ZKsbkrpVVoPbNSUget0c9v29hh"
  "XgBBGTzZKsuXZ4icjXc6xoWf66NC8DPODf79SQvKmdTNDhLq3BDQ++E9hptwNPL598NvlvvbAGva"
  "AizuUImz8ZfteXtIHC6ddDIxS+S+3Ue5dUOGPyWxarWc5/3lEKkpYvdd6OLXDEY4S/wRLaf7dgcP"
  "377q/YO8k5XX9fIgpw/wHuf9wxs+8LiDX9cjIthiUWzyXve2Driv2dnx0GYO9kQlhVacEhSyw2BW"
  "A4JVnl1jAFGcvDk4OsKAEDjJlQ524pF9SGuT4457VSaRyQM66shRr1Kqo9K1LKqMRe1Z+3gykqUt"
  "1V+v8vxfcxm0lPvoxbzMqguxXi5GdQBK1cfgY+4AvW/OyZ6Sbb4Yc3t0tCEsynnOax73vAlTF0lD"
  "4igX5w5JH6fA1UPPHjLYLRmYzXcNeGvm9dtqzTnciqhLLZSG9t3r3iO0PFtc5VqcuiXJPUiL7T0K"
  "76yJHdaFtrLT92Rj3jvzcA0T2cqDKOrTMVpgi3ZG9pFAYOhm73uV/1QGQWY+RR89PMZjHPQ6j2wD"
  "5+wKHUZ5QAZ1fsUG+d4N2+VpY63icA8a0/vRRqui2CoFIs/wc3xsWYgwz9RJJ/x5ZWj9R/rwLxwu"
  "dPeqGuGmzXO34/SkNh3XnezxZ7SpswL7ctsIih4evoGgIl/s4XlMJcaXK0qR6yc99U3esVJypwPW"
  "Bk8H7Ey8OXj9Vrw4+PDh8P1fxPuf3749fI8b/Fdno5pgdobBCpAIe06YYtUXqjMG/mDOlCEjclY5"
  "cyYO1cczWe4ulVLPOp3SeVi7Ui0u1pddo2OuOPaiITOSCntoZImK1jlAbk9u+F1k5aXc4bIor78X"
  "20KeHGGeGUTOdSh3+eJg5zg+xh7I7XZ1965Y9gd7XYdfYohhJGmJd8evZeCpqnDray3BVb7O8Mgw"
  "nSuwBXWGlXzVspJH6cgDV0AiwFSZ4osBWpBcfdggiq84L4ubmt66gFkrNrhlZ3UnsnlZVJVOWFT6"
  "pMJRk7JAb3oLSO/g7avjH34Q/+//Ew5IzkYvri1uiHoNnrDZD4nod/la2ngtj28L0FqovSYAYDzp"
  "UdcnTpgtK7blsHYjzad/M4tkYA3NypneHY+KUga2pPJUioBoT5WEMsLauAOT2cEKlwcD7jm19rbu"
  "t/yue+lOtBFrYl1QVoaiKDE518Q01FeSScR6GBf5w/qtU3xmG3r41jfwXlP6spERwUNvpqoch3LP"
  "gCxlG8rKJlmSNJRZc5k/HcpwkIzjiPtP+YZssPnubGOesVdz7GpzCStLRn/qs6Vw1PlmsGfstaH4"
  "+Gxjl6bYB/xZZ2kaoKqGgO3zql8dy3OK2mVIR26mrdfsxjwqdo3HCezb5zI2mgV3bkUTcj6jR0Ir"
  "rA/feO01/BzSmG29uQxnX/nmhiAMNXBh/OSh7O5MPME/NRUYI43b4sq5xQmyo7aoft7OaPrynhFw"
  "Ru1/L3oHL47ffwAFnanZl4zD2Nty0xMYSi5wq9gud35kFqGFnshj5Jd8QWgqVIU61SQuGur8EZHm"
  "sx5wThiz2FgzFl1wCFGdVWYZwNYoEvN1/60z1cqd2uKJO9tBWS2WJWgmWU+C5tABf/KMS7gPTwav"
  "9FkWh6OTn44/iOpVeQ3OJSCC+lgKeT7ti5/fn3x4+u798YvDPWyKKrRpLU8IH4FXtsDI4dm5at9f"
  "5CvoQomnPBXKqr6E33Q5sNyk03RTHgCK4Rop1XWVTJ3kqF0mMG/gimLVCepGVTpT76Z39/rgyGEw"
  "x5t53jf385RN6sZ2exE/UKfXM5xeL2ycXo2pWtgDLd/B437Ilqu6QgO1YzkGbgyasevve9xOGjkQ"
  "WQcMt8xqLjUcqsgW+XIMD5QGuFc/aiGTjKhSemzZR1nDQy3tOP1gtsBJW7QeT8NodXNvUOtodWhk"
  "27Lp14xKIbYwYSs6bnU1heT5TE5L61rJpSipG4M4ePH+QzOK5pl4FW6pnzzQz3LhpkKRzhV1fDZv"
  "BMZSG9RndtfHUtj9evH+4M27pmPEP324lgTA4FW2Qpy4xRCD5UyoIQ3sR+JBJz3KBpx1VE4L5SfK"
  "2o/VSJZHXWQb0CflQKs3UBlyf6ktl/wa6fPngJAV5GhNeaPpsNPb6+PtdQO/BSvN6tGulw3Va9l9"
  "8sTs/u9FovLog4YX3wOFJR6HKW2XebNkj1LjPlHh/qDxpNvQ+/0jRKoIwq+zVV8PYygXPNzH617Z"
  "4Zrrapsb2PRFnZg+LYquVNkj8/TSYjNf6cN19Hnx8kBfxSEwQK3LZi2rvUfmCaYumd+yMhRD5WWQ"
  "0b/CskRz2j5Fn33K9/U7EpfCNIgdmPraHe8x1tBY1gOu7pqD87tOQCfiol54UXdUnZLa9hNfuYDt"
  "4foeO4032SUeozPHPRc4ZUYIAlm9eSBsYq0kzbePQNBZTcimHj7qqEB1A6Bkk10vzzNwk4xVUZ/T"
  "r+9rrFRz77j+aawfBn4FGN1806NHa7eDyuZ/vcKzptob7KeMs8Xi8Brw8pH0RvOy3wMrmwNEAQRa"
  "zwTTNbm91X6cbqlEp15hSqVZd2LEobrabiVwU9YftNXB1a4Y4QP23yK2fwCpISVJqDPwd/+oORDe"
  "HeI1eMWnyxUoXzw+6dwYa21N67btnfrwWVTK8iKAN/Og7ZY3zdnLRCQAxMH/OjlJ4UA+qmcOwY7d"
  "W5TkF8eA1cAqHp3URxJJNwXdRJCJ5RYk+cOr/0se+rInXnhe0uzDxHWy8p3jfKWbsYdHEm3AGH08"
  "wR5Lk/4SYFmZ4VdrPM+eqic9h0+nxeIO/+IpSM8f/X8wyQsG138BAA=="

;
static const unsigned CAL_GZ_LEN = 25804;

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
static char sCalCfg[768] = "";   // CALCFG=<json> for the page (cal battery config;
                                 // S14R-0003). FULL array values (the ladder lists)
                                 // need more room than sCfg's 640: 10 L + 2 key
                                 // names + battery keys ~ 500 worst case, 768
                                 // holds + '\0' + slack. NOT parsed box-side
                                 // (page-only keys); replays exactly like CFG
                                 // (2-credit window armed by hello).
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
// S14R-0003: the cal page's config slot replays with the SAME 2-credit window
// (armed by hello + a fresh CALCFG=), cleared after the last replayed delivery.
static uint8_t sCalCfgReplay = 2;
// status-LED state machine (operator request): breathing OFF during
// experiments (scanning flag from drv? traffic), solid RED until a page
// with the CURRENT build stamps hello (new code ready to load on phone)
static const char PAGE_BUILD[] = "S14R-0002";   // keep in sync with the page BUILD
static const char CAL_BUILD[] = "S14R-0003-CAL"; // the /cal battery page's stamp
                                                 // (S14R-0003; pack_cal_page.py keeps
                                                 // it in sync with page/cal.html's BUILD)
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

// frame-bits: unfold one 246 B message's bit-plane onto the lanes (S14P-1923,
// plan §6/§11.4; S14R-0000: 12-of-24 Golay bank -> 24 planes => 1920-bit
// payload, 240 B, message 246 B). j = global LED id: lane = j/nPerStr, pixel =
// j%nPerStr; ids
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
  if (ws_pkt.len == 0 || ws_pkt.len > 4096) return ESP_OK;  // 1600-px paints ~2050 B; frame-bits = 230 B
  uint8_t* buf = (uint8_t*)malloc(ws_pkt.len + 1);
  if (!buf) return ESP_OK;
  ws_pkt.payload = buf;
  if (httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len) != ESP_OK) { free(buf); return ESP_OK; }
  buf[ws_pkt.len] = 0;

  // ---- S14R-0000 binary message class: 'frame-bits' ---------------------
  // Exactly 246 B (12-of-24 Golay era, 24 planes; S14P-1928's 18-plane
  // message was 206 B): [0]='B' [1]=ver(1) [2]=b [3]=flags [4..5]=u16 LE
  // epoch [6..245]=bit-plane, 1920 bits. Bit j = LED id j, LSB-first per
  // byte: bit = (data[6+(j>>3)] >> (j&7)) & 1. Set bit = white at b, clear =
  // black. FULL-RIG repaint: lane = j/nPerStr, pixel = j%nPerStr; ids
  // >= nStr*nPerStr are ignored. Per-lane write (NO mirroring). The ack
  // rides the normal latch path with the message's u16 epoch.
  if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
    if (ws_pkt.len != 246 || buf[0] != 'B' || buf[1] != 1) {
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
    if (!strcmp(bld, PAGE_BUILD) || !strcmp(bld, CAL_BUILD)) { sBuildMatched = true; }
    // S14P-1926: (re)connect = hello = arm the replay. The NEXT TWO drv?
    // responses re-deliver whatever sCfg holds, so a page that reconnects
    // after an outage ALWAYS converges to the queued rig config (tonight's
    // 10:02 reconnect would have re-received the 08:58 nStr=8 CFG).
    sCfgReplay = 2;
    sCalCfgReplay = 2;             // S14R-0003: arm the cal-battery config replay too
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
    // S14R-0003: a QUEUED CALCFG takes the cfg slot first (the cal page polls
    // receive the battery config; the survey page simply never triggers it —
    // it applies only its known numeric keys and ignores the rest). Otherwise
    // the standing sCfg rides exactly as before (byte-compatible replies).
    const bool calCfgPending = (sCalCfg[0] != 0) && (sCalCfgReplay > 0);
    const char* cfgSrc = calCfgPending ? sCalCfg : sCfg;
    static char cfgEsc[sizeof(sCalCfg) * 2 + 4];
    const char* r2 = cfgSrc;
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
      // S14R-0003: whichever slot rode this reply consumes ONE replay credit;
      // the slot clears after its LAST delivery (survey 1926 semantics).
      if (calCfgPending) {
        if (sCalCfgReplay > 0) sCalCfgReplay--;
        else sCalCfg[0] = 0;
      } else {
        if (sCfgReplay > 0) sCfgReplay--;
        else sCfg[0] = 0;
      }
    }
  } else {
    char b[48];
    snprintf(b, sizeof(b), "{\"err\":\"unknown\",\"id\":%d}", id);
    wsSendJson(req, b);
  }
  return ESP_OK;
}

// page handler: serve a gzipped page with Content-Encoding: gzip
extern uint8_t* pageGz;
static esp_err_t gz_page_send(httpd_req_t *req, const uint8_t* body, size_t len) {
  if (!body) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page not loaded"); return ESP_OK; }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, (const char*)body, (ptrdiff_t)len);
  return ESP_OK;
}
esp_err_t page_handler(httpd_req_t *req) {
  return gz_page_send(req, pageGz, PAGE_GZ_LEN);
}
// S14R-0003: /cal — the autonomous CAL battery page (survey page untouched)
uint8_t* calGz = nullptr;
esp_err_t cal_page_handler(httpd_req_t *req) {
  return gz_page_send(req, calGz, CAL_GZ_LEN);
}

// base64 -> bytes
uint8_t* pageGz = nullptr;
static uint8_t* decodeB64(const char* b64, size_t wantLen) {
  static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t n = strlen(b64);
  uint8_t* out = (uint8_t*)malloc(wantLen + 8);
  if (!out) return nullptr;
  int acc = 0, bits = 0; size_t o = 0;
  for (size_t i = 0; i < n; i++) {
    const char* f = strchr(B64, b64[i]);
    if (!f) continue;
    acc = (acc << 6) | (f - B64); bits += 6;
    if (bits >= 8) { bits -= 8; if (o < wantLen) out[o++] = (acc >> bits) & 0xFF; }
  }
  Serial.printf("asset decoded: %u/%u bytes\n", (unsigned)o, (unsigned)wantLen);
  return out;
}
static void decodePage() {
  pageGz = decodeB64(PAGE_GZ_B64, PAGE_GZ_LEN);
  calGz = decodeB64(CAL_GZ_B64, CAL_GZ_LEN);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== poc_survey S14R-0002 (+S14R-0003 CAL battery, /cal): pre-burst brightness calibration + adaptive thresholds ===");

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
  httpd_uri_t uriCal = { .uri = "/cal", .method = HTTP_GET, .handler = cal_page_handler, .user_ctx = nullptr };
  httpd_register_uri_handler(srv, &uriCal);
  Serial.println("HTTPS+wss on :443 (pages: https://192.168.4.1/ survey, /cal battery)");
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
        else if (!strncmp(sLine, "PROBE", 5)) {
          // S14R-0002: operator/console trigger for ONE calibration probe —
          // the page paints solid all-ON frames at increasing brightness,
          // measures ON-peak percentile + clip fraction per step (~2 s apart
          // so the AE re-converges), picks the level whose ON-peak p95 lands
          // in [bProbeMin, bProbeMax] (225-245, corpora-calibrated), then
          // reports. Probe + chosen level ride BSTATS {bright, histMed,
          // clipPct, probeIters} at the next burst / BRAMP pull.
          strncpy(sDrv, "PROBE", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0; Serial.println("[DRV] PROBE queued");
        }
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
        else if (!strncmp(sLine, "CALCFG=", 7)) {
          // S14R-0003: the cal battery's config channel — EXACTLY the CFG=
          // pattern (queues a JSON the page applies on its drv? poll), minus
          // the box-side nStr/nPerStr extraction (the operator's rig CFG= line
          // already set it; CALCFG carries battery keys only). Replays with
          // the same 2-credit window, armed here and by every hello.
          strncpy(sCalCfg, sLine + 7, sizeof(sCalCfg) - 1); sCalCfg[sizeof(sCalCfg) - 1] = 0;
          sCalCfgReplay = 2;
          Serial.printf("[CALCFG] queued (%u B): %s\n", (unsigned)strlen(sCalCfg), sCalCfg);
        }
        else if (!strncmp(sLine, "CAL", 3) && (sLine[3] == 0 || sLine[3] == ' ')) {
          // S14R-0003: battery start directive (one-shot sDrv slot, exactly
          // like BURST/PROBE). The CAL PAGE answers it; the survey page just
          // logs an unknown-directive line (its pollDrvOnce falls through).
          strncpy(sDrv, "CAL", sizeof(sDrv) - 1); sDrv[sizeof(sDrv) - 1] = 0;
          Serial.println("[DRV] CAL queued (cal page battery start)");
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