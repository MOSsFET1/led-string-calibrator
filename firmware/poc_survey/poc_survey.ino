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
  "H4sIAAAAAAAC/9S923IbSZIo+M6viFafaQJFAEQmkAAIiqqlKOrSLYkyUdWaNq1MlgQSZBYBJDoT"
  "IInRqGwe93n3/MGa7T7vL5wP2I+YL1m/RGTGLSFV99gx25pzWkSmR2SEh4e7h9/i8R+m2WS9XSXi"
  "Zr2YP9l7jP+Ieby8PnmULB/hgySewj+LZB2LyU2cF8n65NFmPWuPHqnHy3iRnDy6S5P7VZavH4lJ"
  "tlwnSwC7T6frm5NpcpdOkjb9aKXLdJ3G83YxiefJSYB9rNP1PHny+vyZuNzkd8lWvLs4e3zIT/ce"
  "F+st/itogK2rbLr9uojz63Q57h5fxZPb6zzbLKfjPwZBcDzJ5lk+/uN0Oj2ewRjGQX/1IIptsU4W"
  "7U3aKuJl0S6SPJ19g/7+eJ/HK+jrgUc2Hg66q4dj1beIN+vseBVPp+nyejxaPVCTm2n+dZoWq3m8"
  "Hc/mycPxdbwaB9gunqfXy3YKXyrGV3GRzNNlcowgbfzMGP+HeriaT7/i2Nr3SXp9sx4Pu1017NGE"
  "xtUp1gxRpP+WjIMQOlfDCGA6MJTjqyyfJnk7j6fpphjTEw0TvV5P9tPJbr8aOBr2ZkFSfm82UnBX"
  "8dQA7MdBFEQKcDYiwLt0mmTl9JfZMsGnk3h5Fxd/nMSLr4zHoNv9l2MFdTXPJrfG6LowYXP8A4nc"
  "Yp2nq6+8cDDrw6ATiUW2zIpVPCkHPZwO1Rrh4naP728A6W2CGa/ypF1her0s3MWCj1nLorobYHfY"
  "8mqzXmdLAx9hHMa9uKKvRE6BVqTI5ulU/LHf77sTO4b+5H8aLQkkzGNtkfuMAv7yGAYdX82T6dcM"
  "ZpWut+NOL6ped6yxBdNeHM3Up+UQe/GQkDDPrr/eMKWFI6TT7C7JZ/Psvr0dE4UbSxPj/3mmBhRV"
  "TsSdIq9YQCvW15dMzRiBapdpMrvGvfK17MVd89HoyFjzb3uPDyVbeHwo+RMyBvhnmt6JdAqcB7p/"
  "hFyjfAJblx7AI+h8Sc9gMz6yGI/4z//47wZE+OjJf/7H/wUfhEdP5D9aN5N5XBQnjwpge/TdAv56"
  "8vFyjExwmUzWMP/vNoK9g63O4sVYFOs4Nxs9PoQp0B+0AakF/PVIIGEX6RKxJxabdTIlnoVPYZwE"
  "S614g6oPPRLMlB8N+t1Hgknj5FFv0H0EjRjUQBttSokCNQ71Ti7doyfwxxgRZ4LQEp08crbgyGKX"
  "E5AVSW6ssFqpeXyVzMUsy08eLVcPj1SX2s7pwWNcwmL8+JCgZct0udqsaZTQMF0+Eijk4MdmcZXk"
  "j8QiXZ48CuDf+OHkUdgFVNzF803Cf1d7VqgvMmujHWTsvRj/7/fyhb7G0nG6A5yDRh7uLKE7bTM8"
  "etK4yh7ENJnFm/laxKvVPIXVz5aK6Jo+6lGrhnxRfY45Cj/mPfBIKO7zhB88PmQgT4vTKxL3ZQP6"
  "vQt+Pteh5/N2ttwBfjGbaeDwawfs001e6EOh3zvgX2fXGvSz7H45z+KpAHa5o9HH+BaI/S9JshLF"
  "JE+SpTDH7+Ia+sN9RY/VP9A0Xa2f7O1vikTg9pqs94/3Dg+FhxE9D4Aj8KNZDlqWOBDJwyqDR9h0"
  "M93CgzzZ0DQEyNorIIo1EECWd7DHt8k9iB2kuuuFaFx+eH/64fzF39rvzy/PT9+fvewsps0xjBP2"
  "H0gZYAxzUP3Su0SkSElTIKk5sgfsaRWvYY8ui5ZYZmtRJH/fYKN4LtY5bAcg5I74cJMWIKLS+XQM"
  "22pyA5whx/GBntAubrAVTQR7y2YiptWH1/EE9z9PD7q/T9c3Ii6nIZI77GUew9fFLInXOHOccVLQ"
  "DJ+BogUbGl7Pt+L06eX52w+iscRGADVPAS8wryy/TabHMKhpIja4QZKiiPMtzH11g6N7ArsJO4PV"
  "EsVNulrBfFowZfjKYZ4Um0XSgikXRZrh3oRvdcQF8Fw1nBi2oFini6SztwcbsFiLp7+8ev1MnIhH"
  "l0H/XTs4CoaPjuWr/waPG+m0KU6eCFC9oe/lunOdrM/nCf75dPtqiq+P93BAbes/cfb8hWigBgsa"
  "9HqzxGVv6ft/mt/9LFbZfN6022J3l0H7MugpgqLxxMt1Id6fv7n4KxBfIxyKy2TV7IhLeEHsSa7T"
  "Ia9SgaxYrG8S7G0RLzdAAEz+sHLJ3dM0hn8XSX6dvBcNABNnH8/EKp0n7c1qv2ACpddNGPVyqnoC"
  "LEE3sGHFbbItOuJttgbquQbpBNgFouIBg/KQTNJZOoGmW1AScsA34xSxciK+wsaD0T4di2DQbTH3"
  "hs6ZzchhiqscKXoJiym67TCKsM1kAm2A71dt5sl1PNlSv8nkJsNhiYYkVIm9PFmAKgULBRsCfgBp"
  "5dAX42As2kFL9aX261m2WCXLIl4jFV0BlGicLqd5luLCzbfHIr0ArQG16iZ0xEgci0GHhwUdVdgT"
  "Dd4ez9LZ7Ck8BaRbyJYjao51USbK+cEii7AvkdHOs2wBO2yKcku8uXh78eHi7bkIR4dRe3DYEzNA"
  "Ksq8wt8XUH942D8cwKDShZjBTgWcDGA9shXsCSQQ+koLnoTIWAAKVVli0G8R7y2tM+YCYgXbVzII"
  "xDJuDEkgjbDd7zbLDl6AOiHKHpAooTHozEA8oOQCrV4l63tk1Nd5fCUuP5y+/3ApGl0YC+B/FkOH"
  "sW9asjNEKqhMwF6AHyFjzItjYHhbJBaxzsQ6gQ4iMVsV8B62w7Qa2MsM2SAchWhwPDA5oxt4BeOC"
  "nZRIph4AKZwDh1nDibfqAgk50ucm22skLHejwg2Aw8yA+rvt1UO7iGdJ/dwQxdgUmMV2hpKICeAY"
  "v9lG5QUEE2Bxkm1gtGsQfLTjaHRIyRrWZYcBfBo28mkITCidraFlRe9j/FybJ1skCUqWt2dn9YMD"
  "vALprBNxV2jzW8B6JTmIiDhf0eMCBAR0Rf3Wd4a0KxpXKaqqcQ6cB1k8bKQYd+UUf82AsQFlPgX6"
  "+HC5R42Q2cJs/oybqg1oXigc8yBAbgUjVMCXKLPeXoBEm/E4iPHisfh+AuvXsgeDWLK7pKkQVwHO"
  "S7TpI/0Od/pWZ1WyU1R/ATurFUwHyBJFN8i8KbC4TgebtIHAgm4bCaPJvVwSrRGBtoytk4LKifhq"
  "P+Et0wBqTWCXye00B6rD5Xw87OJWv902K3Sly/YqvkaKKlJictMERyKuYSGB2SGrhr7w618A5It8"
  "mwNPXyXH3A0dkwGp4n/830FXkI5N+gscEkS8WIknJ2LQPSS0ozjH8wJ00AYELmDqvS5Jr2oBTher"
  "F/Bx4KLaLKHJis7JjCTsd56CKgVcFcQhsLkUlIAJbs5DcSTR9YY+xX0F2JfsaDpQo2hwE3i4ATmH"
  "fAHmO5OKGW0FULGyvFlPpjx66D/ojEATucc+rpPlhk53/BHAKdIKIP76OpnWd3UDrOgmgX2bb5bH"
  "uJOz5TV0B2cJ2FKw+UA+XCFuC9xB82RJL+q7u77JinWhMFHcfrjJK9YE7yeoK0xxu2I/sFRX800O"
  "W6st98p8s4hx6UCoF536zzy7gI30QeRxCtwIyDgYRmORTK+Tw+IGdkR2346X14BNpPb6XhpKqQKC"
  "CFHEk6KBTC2IBnzwbuI4UW4Foz4qZ50fWBTgrHBApFlOcbPToRvXA3T5bArs4HctCqmMQM9AzLi0"
  "QFPH5a5tB90e6KWgmGWL+t6A6ksOhj391u9ipxNE/HItCaagM0Se3KU44XS2Y4lpj07gq6hdFPd4"
  "xIHVAordEr8BLWYy3yDfRpWtthtzoXDtBG6LAhV+OGcVckUOQKCkc7Uo9d3dA+EBKeQbSVlyX87i"
  "HFR90FhpN/DQQb3bIQGAHttEhXLFNa51meJY23j2WmfXOGyS6g2g9Q/wxxtYlZNA4ynqYSkAoR/k"
  "2Utg6KDsAG8/Jib/DpAWFMhH33WDTuddMMS/SeXgbVF19zqZVuJUngPT5TR5IB0jJ/FVcnJt6C8V"
  "Rd0k8RxOTdebGPhZIzjqHh2jagDSN50UkoaRWIugv4Lpd0ekF0p+u8iQWbenCfB7XGASNgVwIED9"
  "VbrIpkD2cDjEXYSoBh4PigezBKQ4pgfZ1zLDvcuiotE7QvFNIiEAYZkh/5KiHOTcc9gvpIQQN9fG"
  "A0RIx8/lDCQj765u5yjCvtQWqgbb7UTDdrczghXCAVLbqivQ0WNEJHzkATSADdAj6Gugwt3ESp/M"
  "Qdj02kdoaYbu+p0IhnYKx4tqO2tiKcFDW4wGJNRBlqgfER84EGiOAVUJjwc32f1SAqXMr8mTIcX3"
  "C1yjM5gcrHhnEEnlcJqCGARFPLmGPZfzEYEwQKjsiHeAczoQSQTUkDrLJcUWup1hOPrP//g/AD1H"
  "I9HIe4d5H1RzXH9RIvAeucU629Uh4hhP98Dv5rQWXUECF3DA2hlhvY1aMq/Xrs7Y/kaLJVEsT/ig"
  "DuBQKi6JAkpH2vtkAayoX266+ArUiQ0IHeDGoDojfSgFcfUAJ1jYjfMfQphUV2NUJeLJjfgt7CK9"
  "4BBBDYVOWFM5pgHTWW1Xb/c3cNq4T6BZtlkXKagViCJYXjS8s5mG6FCf2uWaZEwpUBcx6N+bCZop"
  "0BpDpNdeZ22mQZjjiud4Fq8nN0mxazjFBrT6pfg1yW8Lsq3AqBjzSE8FMGuef2dXJ8BSgkBKB5IA"
  "4u+bmG1GsxzOjrxly93WkVrT28nkXRLfsvKE9N4FeqejR3zbvivQKASHC6AGIPUpqYzVuU3ZGYTY"
  "MS5UDa+yeTqBaV61sdtj0L1wTsBqb1GGgTqbXOP+Tye3u/tqqH0zVjJdydAF7aVgODhEle83/LO1"
  "u69yd5XUzH10+xGOLE/wwEWI5BMVqqrJ62xyq5+qUG6h0pbMABfrsUBHmoBjYjxDnYpJ/WAFUqYh"
  "DTHK0FCnY5IxRFocFvEWcLZCd61I12R7EJcxCNYUxlygSUN2enre2ftG1knYC5PbLYxpCXsEz6qw"
  "WCABJnkG8iBP0HxKch7J4/L0zTmbAoGbimVyzxYx7ibOS8nB1jZAhTRlS4Wlv1+wZQ4WcaWUGYGq"
  "HM0dqZG6ko1x40hrkmTC13BgYe4Ur9liie94H+edPTzCo7UoBREDaI3nl6DogJxCO9yrdbJo7IOk"
  "nMyun+IM9pvi5OSEJ9CkZoJNZ6KI7+DrJ+LPlxdvOyv0jO/sDTr6938X+1+/7TfZmYY03uCu0MAE"
  "uLu4+hXkQAetYA3qvdmkQeJrwMDZ8xdN/J9P8PszfJhA6Ad2+E0kc5C+PEJjIIVvWi05pWMXnm1b"
  "5tjpC3vfgE8CzxENOOd+/bZ3D/s9u+98wYGcza7RtEmGza9kJfn6e0axExbACMlslEhn2waiAhqZ"
  "4xFMqWjjLAlK2TbJrky0aRhBlclzXFrQWC9YJqCukqka+D29w0UBoQOIAXmvbJxeO5xlg+vswWA7"
  "sgUcYI99G5MYwtVWWB06VlTV6d5ss5yQnoDW3y0gv/FrsyTqP8DfebLe5EtctjlIx+xYLklmEuyv"
  "NhLRCN7YPxeAdYKAlVddCXL3E72usw1IHiT+T0R7OikjpWaSbiXVij/9ifxvQOLZp9vPtKH2WRXY"
  "x3dp8RzjQpIGvm2qXUaUjnSOT4/VNzurTXEDPR+I/ZN9dC9gE6ZOnryCg4Pt9fqmnBJMSCC8ev1r"
  "li4b+619JCNU2hCFiIy9bxVuy+fQyX+jLlDf22921snD+oyjXMQJfHefPKBofaYx4YLjDxyktAmX"
  "z/mnOMBWko7Kd5JI+B3Z2cpX9Iv6A2lRPoW/1bO3+sO36ikbevRX/ER+I9a+UBlMqDFzXf1tZQWR"
  "rfUzkgannqkxyDOODfIafTLf/H4O9MSQ/bGB54c26VFsJZ8qVx0b9F0/B1Ph64sXX96c/uuX16/e"
  "nl8CCfW7XeWBgb7fY9dMu5Ke8Qj3DD07y+weyYA2JUqgezTGTUj4ZnmKcmhWeqXQ5wPyDNaFhokn"
  "e21jItUVTMv8kekaGWT1FdGG7zbFIZmJj0swOlLA/lgjvqbrzjp7nj4k00bQhD0LuiLIz8agSchF"
  "iIK2OM+J9wZ2QDuC5Ru/4d0gnpiYaZYtyQzaaFbDSOYwCCB6AGDun8xNsi+b8k76X5clWAFKwXz+"
  "IVsBUPnzJYUaHOvbS63l64y2WPlp8nScsN4AfzY+uV/63EIhAyxlDIiCUaFRMF3ui2/aDGLoo3Sx"
  "TYBxrhPpZWvsxzzYuHMD2hjA/fL+tQRhGQy/GzgMCVVSHawLS44vMKYviH/29cFq0C8cM65wA3hE"
  "9ury4pIkFvwqQE1NGqCpBEdNELAwXPh5+Gn84fPhdUvs79OCdtYP6AvGL04A/pbXg9gX7gg1CmC8"
  "DfyYtbZIEbj2RRMnV7OzMDQFw0TaMlSgJVI01K2JnaP502lS7iyUI/cFLsxmPm/BnxcrOFqcoIOo"
  "SFpispi+QgSVGw3eTnmjIVbexCveWLy3QAkFVfhrnsAp7g5a58mvNBrcU/k3+haM5n0C+EsK6tUW"
  "mfiRZLJZ4xEJbSS5hIVulbMWjl3byTypKE5O+uOlQW+bHGl9/74oxoeHjNgJHcI7aOZAvB7eF/vl"
  "UgAOZD+0AaE1LRPLV6USMaJg3h+Tq0vgHsm6QYB+aXtfZIhL7C5BeYSrsZkn7xP5oYZXCtM3qg/i"
  "IO6LTrbMeF2ULlYuFFrSjnFPY5SSLcXEPpIGNt3XYchi+hYdN0j6oEjc7rPGWMDqni2mja+48LAL"
  "4Zwzz0BRkxEAvC2+4YTLcU3IqOQZGFHQd0ZGjac7x3YVT/cd1fpTOoXT8mdUryVBIt6BKuL8A9Aa"
  "nIcbqw5RHYx11WE6bODKned5lvNy87dJ46T+ZU8d6qZRt2LVzBPsSpv54U9CoWOWocm0ED8davAL"
  "jFC4Jlwld9yG1XTYFgulyi1MVS6560zjdeyQmE43LBMWndWD+APoYBs4rs+AY0xRCVt0Zvesma2y"
  "yRfmcvvYgWHFxlUWyL+2yresCHAJnZ4I7PtY/Nh/dBwkMc8cTHbI65YuVyyAKH5LnZmkfgsvabJw"
  "TuiA6pw3EbxDIVz1Y8ATdRU2pc7wtAPxOU9tdk96CiEDGerqQf5ePTSPnf5mKZ7u79FMAONKpqDD"
  "KlsTDO0ph0Y05Nj1BQD25yyAIio4gRNEqQgrlCBCFBAcLhnouHw0TYA8EvXUT+GyP4u1aqhddLLb"
  "Ju0DYsyNBXRF50rP1lh0gK7pULuEDnF7lPP8hgIIqTVXG+J0zZ/Ch7Bv16c8hC0If35Rad3uXtKY"
  "9X2cYk9v4vVNZwHqQEQ+dvhf8RM/XIFuFbb0Dx8cNJs690ZJQQdqXFnqD1Z6UTCNwboprKndSiTH"
  "zKqpixDYUy1qT+IWbZKT2zZ3ro5rJ9LywAahdhlFlF4vOXioEUaCZFZIHkXedkWTgptOydwxKiUb"
  "vI/xTJKv5aZrKQlHX/n48uL1ORncx2IG63cjPry+bPGfe+Snw1AG+QBNSWhsoQNrDqfapTTtwEvY"
  "VBTgRJqoNGKj/pE8kJZViPubbWcPz+gYuAm7TmFK6pwadT05gfEDbf+hmMTLJTPfvXLbKXSUBjNS"
  "a7TmuAM12d6kIZP6whYnxRZkR475AEHfM4pANO0H+5q5gwQ8d9NQ5o0W6eLdpk6NUs5lV7+2MKaF"
  "J8BMlQT8uzxbpMB/G5Yuo7FtjYBwu/xBUxLgZ/Wrg6f77SWGmBF7CJh/eyQSqoIkj0zuLlknqmEH"
  "B6SQ8Xxh8B16msoHDJhxmA4O4us3/YViDVmH/wKA0MP9KKCDggYKEWOs1jRZrDKU2lpfGDm4WNHZ"
  "Z57MdLSUX0PehPYsZ+uVpkyLx1GEnHpHxxzoGQ433VKnUlQD+3yKREU8saKsg4NjNTBu2wZkKyzi"
  "f8TzXNRjt2seI3EPRCzguVkOB0go6ygIRFtE5GTOAibaQLUEF9en/8pjjC5dgTiQEhuWHQy+X7Jd"
  "/kfNijcM0XX9aYBZwljk2dUGlwp3FlloW6Igx6jI//r8TKw2cMZ1DwMwjCRelAcCCjn/QP5K9Qj6"
  "f48kXZ0SiGZeTR+Y58OYYPuQowO0gCnMfYn7GBXsRcLOBWQ+H96fnv1lHxl3PBcxxgGu4cgNfDBH"
  "5k2m4orBIRgZtodh9yEIR9026YkCI6mBsYrXGXo+0KIw4ZCZmEBJZzx79wsbu4nJEhAlKhXoiqVT"
  "EiAmQ4syBSimBVkCRPH3TVygUYnQ8hGtfH0QSi/hj96Ap3l2+vavp5fi4uPb8/eXL1+9Q3/kNccj"
  "oGoqA0W73UP4nx6xOXJgYMyEyEEYoisjnqzHe+SPBI49OXsQwAQK8eL96VPQj9kkAVMisbICSUi2"
  "wkLCttiSiUE9sLemeQxMJF13uDtAnOrt2avLd69P/4ZhwXOUDAmmdSGhQ/eYb0DdyOBimvoyXlF0"
  "L8k5DvWXOUGlfKGu8gQ/agTkFWTCZzMrdw9y8wF2Hky2WMPglT+BGU2DzbeMqB756dsLtDMBJpsq"
  "FhSncseKI/y532zJyZ3wG9Sb6GDxsG7sh2h//kpxz0ilz3MOawbVloMOcONIzQtxeLfDnsAz3jcb"
  "4Fe55e/+bCV+gGqfZ/lfcW817kDi391odt67e5In+Kyy90rvBNKtrigFLSLzQ/kkflDdEZP4WILC"
  "C1Cf6G9ywgIYqFbc3aEIm/AjpCYvdzS58TeR2KBsCmj98Vg94UQUePSyVNTwDfHYj6gGPOBfL0kh"
  "aHAeDD64uy/f3ZEBRJo+cBsihb1n9poXbO2zg2iAt7UxWW8qybEhmR7xO9oOCNLc05YDmeMZ8Rmp"
  "lTKh+U6tlM9Di1ugs2y/WhvUC5fxXXodY6T+As4N8TNKkiyQUH6BA80bfNZg+UfTHQPFzCiylaNP"
  "9pPlXZrjSXG53m9xUg/CAGw8HwvkeyiLZO5X9QJJ4Bu8YVmxmabQM7FmKXOkNtPJ0T71adUyVZwv"
  "JKhINjvCGl7okvJ6s4BRFEpagrICylWIylXzM7soO7Cflw20kWqivpQphZKAhlghBP21fAJnq0/d"
  "z8eGNiF3PzSrDo13nSKfsGVP7/o7a4e8TC0cx5OQ0nfXwRflqc6YjEe7ufMNSL6CAze879AUP1J+"
  "LNJx9UwZTbWjKgyYkzS89M/xQnRkbWiIAw28+tXhrCo8t/28X2kuPk5TDpd4p7Zt+YG2a1UgCwsA"
  "5ntSrLPARj+sFJRVp0o5YMuUqRv80BIhJtgmpKBqDVaCXWXn5H9pVDoZ7HV7Lcl80kDzibaayjEG"
  "+Cf0JojVpIPJz4jLpMLkd1kCqKVEV7vGXRmzSjXZVJE1VtQSIzq1WCGCqP3KfBpSK43eblPaUOqM"
  "a5gLMIS/lHTsDT6HE+L6dQpieZnkSMxFiiHV6y2HhQALwg41rdzzHxONtz/yytGMyp5MXnus9FtR"
  "jSyeTn/nsHgEbjvf5y1Eoqt1uWX3oWhgPG0Za8EKtumRVWRG5KMCDhTuQUtDiaRtSdQP4lVMY8cD"
  "1887XkKn4/K8hmoAdQfEiP92fPkmtiEJJoqhcJr4rm2KqgOSd7vXqlSJyo/ZEjtaxg/YsldtDG1O"
  "7LbG0eR0fmx8FfH0Ll5OMALn01dv2sxYDfzbZ7VVbdZLmzS54wQbcvdSi2bT3NMG2CxO52zd5dXk"
  "s58EGVP4CI0G7RMYKAM/gJ8l06aMinAN+tAz8L+rpOya/cpxsV1OhOYgnNyeJmTkedftSlIBenta"
  "hf2glYZ8jxz5ggqwTA5Y5XRQbKxg36O1Zo6xWEnyb0kVkVlmCpKSvFYxPbi1KdKaSdmH6Bao6RNM"
  "f6s6k3kwGB0Uz8lMATCgx+cbMutU8RXTpEgx/PD8r4TdThlzdHYDWoTsDeOP4NSWbXI64RgBSIzr"
  "AhSPFCOk6MDFxyASvwXqapNkBufLbafUgytRh/qwcmdTUJWuGv9P2IjlXiAXuL0r8WEnXVIwddHY"
  "5xXZ1yy9Mdkj/4GNItVC2SNvEUNxkKTEOSIyvIvJCmho3yT+r56GnPnJQWOcbUirwtU99nXbr7sh"
  "VBe8H8aGAFW+DilDzQgMmN3rdJk0DD8Ee/8WKzgPV0SONMmpPqCBrDG/pj1N0OACjL7pWXp33S9V"
  "UNvPNS+M9ZaaPEWzqTAYpoJqtVGeij+wMaTJsOyi3wcIUtJM2KavE1xYs3GcLKzGBONrbGQd+kdy"
  "Z/VlCA69zxnI3eJZikmjk5ppzTjKo3FgQTfLKIZQsVmpTHNrduwLH0OVcPv7FmWw3maQBVPGh5fn"
  "yjK5wUS1BVr8JoX4Lei+/LeWMjwkOQbvYQTmXt3BQdLJOllVelJlzlXaq85eRKnCHhzwb7STXHw4"
  "RykiDRFkBaHMVmUesawmsKfg6MnhYCoEloJKMTKMZg6KImZUoVnlOU30kGZFDEcaUppV2CNioVOO"
  "2zgVVzFShnpAx2UD8Nh3mpZtNF9rjgabHA50eaN5XJe4ovogREg7CQaUFwk8L3QuhOMtzwT/gkml"
  "Jydo4i0n3ihpiQ7ZdHShF2cyZ/WiVGnvWjJwOpSKMsYP4zH/7flfz99jItSq2JNG1N/dnU6XP9Z4"
  "BjJ2UVSWHFSLa9o1xY6XUu2ZLRtNGX2DRQWqQwJ+p9czRyhtgF/r1kblmS3h7MImVyn8VaLlDueH"
  "tkvqTuPfna4iRpNUicGo0wqTTQdJ6NUChAeitEv/72NLvCS3CLsvUZ5You1HXdOY1/3X52djPju1"
  "JcfAQVUeXttT+LvGRc4mXW66y6T6r1ZLhlDfMUenlJ+xdB4gZmHQzPtAa++Jolos067DJPCPG3fI"
  "K0DjIMUZaEwadmo9DYoLt3//f3ucrizzh5Xglx2OKZ3gkGxoy8PpUZdyzTYLOBNP5ukKsUHohJMK"
  "xnMVFX6puzfcS2OqMIycFkiWVu9ZvI4rmx2aJaYUdQB4QyuI+ElaK+FEssKzdLdFgf70B46E/sBR"
  "fKz+POM/Qby+TJWdgz+A+XYyZugXUPR64Wmex9tGMGiWEbb4pZQ7WHFMVCoeiyX8c3CAjw5ORN+M"
  "UUe32urh0+ozCD75J+Yuw8+r6mf4WVdpKFHuBFo+gSY/iwb+cQV/5KD8XKEG1LiWT67pyXGZtNLI"
  "W9etq2a5yznxE3DTZPzgb/4SzvUTv34i+p+VtOQBLJb0+cfq84+dzz/WP29E5K/lZwRQ3LLiN9Dl"
  "kxP0xTV5PdD992NcgOtWYaOVDO0oRZPKay27Pftet7h5Od8QzRPLZE7NKhULho8Oc+iO6IPR8k0e"
  "mYjCMSQUKIs8Y5SgCTt5IRp/j7oYQwAwLfH3I/obwJqSOOPJhKmFsfR3KhOwRNQHDA7qzRKo+ahJ"
  "y+GSG9NZMCBCUwSGvQLB0VKmmvqLz2EW+BWMKsINwWKb90YDunqMZHogRm6jI/Lg8uYxIMVVTvk9"
  "3yRGJFsDlnvb4nlDI7nb5E6Tuwx367davuSvniN+kC3hMo7F83kWq/0qGh9/etnUj8NKbVNLzlFK"
  "lPqIzi7ypWEGWEHFv9YpJtpgYZ0bDNPAlLsGIu5A3P7UgCm24UeTAhGgTxCZW06YwofYEf6YtmeY"
  "ttanEE/il9kSi7e0iIvSPPFD8FumqLUpHXcadMUURMESFfY9KpyQd8R7eejG6icrPPddcaIDV6HA"
  "VNlZStUJ/HHcaKimkjktpZm2JOem0K/V7mhTFTRSuY9jLDFFuaNl1CBvKwVJ6cDSe6m+uCd+/L+q"
  "sA3FNVTlbXAWmwKTc6U8Ksh6uJb5g3g6n6MkbmO+PZ6y+Wyt3LKo+ksXaJGJlIoqYbAPIKionKSw"
  "eisZusWOaS5zAWck5QolpUwWhKgCqaHbZziMDzgKI1xqrYWITksQFR3+B4pR+MO6A5PNC/3v8mhg"
  "+xUnypf60BIPXZKDhyJsiS3+/ZL/vjw7fX2OZv0O+wCh3wH18NDBVCEZZv7QwQCjj9Kp0Dum14S8"
  "SyyAhnbw/PoqbnRbYRS1gnDU6naOmvuy7VVynS7fxesb1KXgNxqVP2SNhy4OpVnKZRwtPluhm2Hb"
  "tZJVVoRWnjEyHgAHlrbqwHnjJ57FMbbkZ9vqmRw7fG/1gH3L0JFyAuUMcSeWs/njbDarG36cT+TY"
  "W6JPGiNZW9+9Yt+p6quu48FgV8c8yJaIfqTjjF0VwUAvxag5L9FnhfjmfMkP6NJek5+jyQ4i3wDl"
  "OtL/dQblGs7IBT5Zw8xBYMO0By2BHq0RnKtC/0y7s67eWvt8i9Z5gOpNX7UFFoo1YhqmZo3b5YJ5"
  "A2Zwye2CJK1p7urQYirwAGhvtmNvcR0PdwABrzY8c4KiaSQbIT8kY9eiJaphlcYqNSDNlWm49UAj"
  "qqxlYzSYVGJyf3U7lmGst5zdk0zlAxYu+yg+5ROUvgfEM/dZntLzRkAhlosOK7TAJjtL3ayDnfzL"
  "vtHwzG149t2GJLPlSFhL5jcNtMvJlBeangq6nKazWZInILTamgwndwGGWWJ2aQkhYpbRspZXW1rT"
  "WXaWGeWl6G2xPBVIaa1KOGKuucru5mo3IOVQhpIsf4phlG0ZU9mQtUK+4BjCDm5CKr4VNqW0BzkI"
  "Q8FYJOh4jvWlyCKEdc/2ZAkqCtyBEaVTFEuNa6w8CMee26DXFcvhsKUSZIPOEYuMvN9t6tLBzCls"
  "4FBAV8FkxZxm9RdVtE0nOeOII6kwoWh9dUIZ8QFlaRjLZPQXgLyqzjA6COkPZuIgifouK5rwL51o"
  "im6laZJuDN/+VHQ/oyyRE6Cfj3EWFJW7Tpeb5LgMky/kCYmG9KlYHRxQyiw+UV2diKCCnxDbw6PZ"
  "g/x3K09aoHfKJ/f8772EuN9Wnmo4J8yxdMpKRiGasezk0+aRtNvFqopYWK7V2UfBPlCUJtq7gONs"
  "uVrgA+yaj03x75VbvCBB9XCMo4Q/tm4IhEIStP6sB3jf4YkMptRUE7tTb7H0yC9vTtsfz1+9ePnh"
  "/Jk4O3/74f3Fq2eicRn0ngPFcs1qQWUSSHlFG2W6RifACt3MmB27p5VPK7fRajOfo3bGFcdkfSis"
  "bgK/scLUPnQR57eoTq4yVLtkAFjVmVR/rpNMqo+LdEo6Hj8HqsoW0Etxm6I92cDG/TWu7B2m293k"
  "JQLvEW/w6hiXE3EJtM4/GaPyp4a5B1xaDmZGCsJlacNRGp9oyOZnVO4oR5XChEWSs0hSvquCT/lb"
  "j2H3wWPzewee7x3UfO9gx/cO7O9tfXP76Jnbx5q5fdwxt4/2tx6DouiZ20fP3D7WzO3jjrmV36sy"
  "LnB341G9yfyHrYlfcf/BYfFhjPvpUP7ajnFT8S8VhEsg94Sjn5FeDvGX3uqempUQ2xJC9cS77VuZ"
  "qczDKOA002jELbRsnDwRVx2CArFEfzRlWbSnry8u3og35+9fnONWDGAnxkKaJEhWzPL4ekH1OWHv"
  "ZPypA3ETzzNp9OISFxTgPxYvxGrQXcJp70Cs+qPlQPSo3gl6Ypod8YYqTDKX5gJMKCrRg0sx3twV"
  "Hm9B4sh85QfOEEBPe04tMVSXSh3j+RFGsxYbYM9zXKwrLoVDRhlN5sjUJnwyVfFAOl+Vb4A8GHFl"
  "OmtQ8dqyNR8M5VMQ1Ek+1n0VlllD79AwcBgNfkXq4n3zq9PoV7NRGfkeQiOC/JSixa36+evnYwc6"
  "j1VARvF3oIo47CDRHip1HRTR/MqAuLIh3D6nbFMkgJvtKuNucU9iYzgV4M+t/Lk1OsAVouaP1TL/"
  "VIWLYMhRftU0J11qDqdYw5AG1xLLpzhp+mHGBvFAQLzxHz9hswMeFv54ionRDXoGf7tNt6rpVm+6"
  "/ZGmJOjla+etFIrlTOUjXL1qT1b/yW28ouzeX1uYRGC891B02RQNWkye+osqBUH9VfEyrQADnieU"
  "EqUS5p5SgFiyrDPgoaEYZVvYVFl0T4EwDZ77mdUXUmaJR1ZvOMkVX9hcCxmWpuEZ+AvkeZOsVA1q"
  "Lvcu5r03y4P6NOhixoTUp3yjv5WbU0LCPLA3aXqUDw9PAKxKAFc2K0+ZiivUHGj1dC1TMuAnJ6QW"
  "6xQP8+BvINEDoy7Hy38clx9jtF2VySRkLmbjpW26lKY01VIp5NzzN+PIqrk8NQ8WcOLfgI+//Dd6"
  "X7m6D/jY2eZjpzSDeWIgpipA/YGK1SifR8M68ZaGeeStpteEX+sHagoKOPYX2eLxuGYveRzek8GT"
  "dBJ2gyc/IexnPgHqZ2X+oj/w5Jw/wPEm3lATykVXjvS0gC7mcyoqK3NPyEl8l8YV1AeMnOQ6Uw3y"
  "DbbkKjR9OXEyMEiGupqpcLoju/IHei2pmKHKFsmbZI72hR/1pNlRZ9RJY7KYXlxhxRgZcKSy3Pg5"
  "FVfA/KExWvNkBPeYcpk4pds/RlUae7MUv9/DpwrM8dH8EE8FZZEetFtjVURkRrBhuIRPE1fLKV1O"
  "1nRVoluarouyuI8qOruBAzQVWVYJMwwSHEknI8CSBRxBWlrxKM5uL+Q5BldjMs82U64/tc/f3edC"
  "+oC/a9SHUt1QyxN6v1k2SgrlR2NVUFy09XGbY+Y4wqqzeZKsGhRF4HfJ267cnGIO6tdP2rr/AR+t"
  "ni5ZpT2r/Ah5tQTsaHXjQhnyrcXY1q+V7OV0Pje70FIq1Z46lrAXs9kPw9K1FRa0DUPEUttjGQyE"
  "P0jLfi/DItXvX1aYZ6p1+BrLqujdWcU8yGd27AaNsnPidE5Wfrl5eUurQgzAv7AMw7iqQvTNF/16"
  "ztUmFE9sUgkuTJzk+FK9jBFRjHccgObacVzNMSn89308w2XDr9UVBUJDwj8UQ0DOAGmFKE8ITkDu"
  "9bvNXPlPkG/LFroXpOpEqXSVVC0rtSAQRznW1WgxisBZmo7ZRiptA9uoNLnZLG81wuEKM2mLjimD"
  "plWWp4ottQt3QPsJEAz8yT1+q+P+Xeb+ZmfEgwZdo8RB7Xfgyf6P9W9gEpSxZbJfL+LhHwLbHVUq"
  "3PVnPvj8/embd/yhH6wWd2xViyvTGOX9BaDHZZvrG976SFKi8RQ/0qyyJx3qri7B4Dz8xgXdOCDt"
  "xVFT7JSeLzJM7pI+GQ7a4gqKMmgmnpL5+0ArSZlxpDcc4/G6BxZ1e1wjow3q2l2yxHtAsODy819e"
  "v8ZLYy5e//Lh1cVbNUsqRzBNpumEqmJzvS5EDNYBLbHBXWC+epVJ26MSq030hBo1bw/YC895q5hQ"
  "jlMoOEv1TNaPp6EVpJFN8zvx9Jf3lx/gLEH4PaZic1ipbywvUfj6tvUiXrXwOobWs03+rSPvG6Ec"
  "0nCM0gXLXd4Lysm9eHt2Tp55WZnfcsRWvlf2ybLbACaBiI5z9KQQFclqr4W03pMMU3UUyB91rMXX"
  "wSu8XgGzcYmEqDJUB8f4mmLpr5dcr5ji8adGgUHMMZhjWVwqMYvoPz07++XNL69PP7CXmp3P+Iki"
  "xeWnxrgrZBUpqqaAPms57LYqqZlxnXJZ0JCKasp7e1I8JGAMPmgatEv4G9XsWReKRdRmTNH32PT0"
  "Jn6gGwewq9/CzkC8eSr+/O78hawCQxRFEQN/vmRHTUGBMCoekeqJK3tv9rBP6LpdYr3lj5ftmyRe"
  "YX4Z5UA0FlfJdD0vxOnr1xdnX56fvkL1UVaJKHOFy0Gd4LC8dRnxEo31lpELqqJolDWssdERXUai"
  "dcbE7uSaqnoJgJ426po4aTyl0RULwAChdxJQSnV4m7kVr6oaNnAgYZokO7xGjyBIMLOvaFadYS0J"
  "VbLr2H8NSBvpQzGIYrNYxLCeDWJpuCC4ptwhFg/c1R1fLRP0/8zXfXC1Ygw/UXdsNEqO2TSmuzSY"
  "cvnmnc2uyzelNmVFgeCmUYq9zHWYbnLleADp085mkiyp1MGG+qmUV+r8jE8EjMsyKQc1BeDqy2xT"
  "VDnvp88/nL8HemeJJ6ND8Sy3xrOJVjpwQS4JHKFqKgOfiV8SGXD0N1c4wewcSlVrof1zclOdlGVC"
  "/p5UmjmP38ypb9SnyHsSYH4op53gjfRPK2EbX1sp7k3Nl85J3o4nXQ3i1xUu5KSzztD6gKX39onN"
  "HP66SrD2axfLsqONbU0FOz8F0npabTllzwc1olFXYbHyOfeaLUGry2Opiw9KHlbjyqXeomF+04KY"
  "tc+X5mjFUpr64LTail79hUCRu3wvLQY37E1c0K0KeTJnoSnvXRnjlTqUpYjJ33inLR1a6R4dJMx0"
  "is3vQnR+r0F1b45ZKOP9J8hPUeHJQNEYPAT9fks8f/6BZOzkrk3XEYpljJw5fCaewZtsqZzTl2+A"
  "xVL3dFMXFUwHVny3Zd6RIzPvdDpAuPH1Av3dY0H33q1iugZ4millgS9QSFHGUU3ptj5HmtYkwzpn"
  "/C0S/1Q1H2noDv32lHpJYgrm/zAMtdlT1DVD4Pa6Rp8+TPZ//D+BwNCYCcqOqoZ0dW0A++DLivpn"
  "F29BBzrncqSo/Szj+RZG1OBNCjx4yzKfr4fEA8L6RvfE0+Kdwbz+fNnIk9lrDlPe5PiH7nt/hgHD"
  "GPAknpVFFriwAjxEnzz6g8vKCkgiw9Cse4mGAD0ssfEMffnPXjY58Lf2tWF8Zfe2QH/ds5fwb+Xl"
  "kN7+rV5fglx6RhWIrRwq9GufdjjHW6Cb8xnwkwfdgSI7f9A7/+h0jl4CxMGzj1V+afwJP/kMC0Y8"
  "oDtQ4vhTsSXgA+i0dLxcWbByGTyw36og3N1EivVQgKZCdKcVoDlitZiAfmB4pzR00+VAJ6IdJEew"
  "FlMZSnA13ZrR4EB0VMMR9CBpR6SgBkab79AIgKYTq8BTY0yme1UmCp4dGoDftA8i2eBXG7otPz7z"
  "UUusogPrbPWxOZT4DD0LMBr8py04Cpwik8MdEzqzOglpQtTVT/wvlUoNLbeZNvrFlZrTlR6B4p3T"
  "1ffmdGUO50rO6UrO6cpoR8vZDsJj/Osxbmb8q6LyCvChBHwoAY3tUK679CB2j21nZv0+tfYqXsc5"
  "3ZqVswps1iW72BYdH7BZrWiaH9641uZFV/z0wfYpFg/l9x7oex9939MjWQDTRblXtR1MYRySJvS9"
  "/FMV0kIbnejmznxs+tYEY1Z3rVqVOpYUQg/UBd3AS+nnx+1UPRublRSxyRPa7uQx4m0PD4/lrgfU"
  "yG0PS2I5+YDVcOA1iZM356eXv7yHA8ybCzp/wxkIuJVgxnNHqd3A6UD1gY6JlZBp/F4mN2KYOkAv"
  "NnRIESzwlYMjE3QTMx/lsBRyA5hYOsPrREHALeGosaWrrGVNN5SGZKVuHATd1kHE/j+6MEl7QEwR"
  "M+Q/bSVn/bSF15Ijw8Om1IbR5k+3+aWm7k21zhHxG7QzJA+gsVAVftQwYLQd3Yk2fRgLmvd0i39s"
  "W3Rzzrjy3OG+oUX4JgMGy+Ep4/+4ulLw4v2rF6/enr5WKW+yHhUlFl9tRbuxzqCnDWZxkK1E3irI"
  "WKXrSxCtrNzvF+V9XFU6kfesgfNtSI2UPvD1n3fW/ddp9lrYt6Pkl1muiMExu0c0BJejROS5yIIz"
  "bOPhABdvezDdNvXKhORbnTzIkVezdeZJewmQ1mEaUD+2lsKh8UibRXI/M5dB8gsKbNd82DNMrl1T"
  "STuMnNh2bfCJ7v/uthwNadttWtxlG3y/DcaeVO18/Nhlx3JmPmYsgw3tuT3g3BB+hkEiD123Qe1I"
  "pYr2oM2ubBV8v5U5P71CZUNpZA+o7vY9QmlyI/ObbgANmEhw4xdLd91umcT3qYErJTvuTqhr+HNy"
  "4wnNuQtq2gXfadcN9HbBj3+vpl3t92C/MHTG7zDgkcKuGwEegglz/OcWA65xQj/hQutPd6fJ4FyM"
  "7tZb6ihQHa23rni1BtbDcYVRdOxEtlC0/mqjsTVoyAaD//+ZCvjiC3V7B/MmK+C9Vb7ZWm/+F/UG"
  "xZj27n+C/YFsdd+zQVg+M2XD05xmux2gXADXdZ3q40ELHRfeoUoz9h00lt1Q+eG0bDHNuewzUlaV"
  "coxEMcw50HK/jvd+T9bYLM0X9+izoEDmZaFSt2TSmHJY/Py7OsWr3DGqWc2MrqBr4NNn+R1VCC1+"
  "V39cGZXq9VBcZayuQdwsi+aeW7Tc1kGWvpKPyMax3CldU8P3emOoOlW/LYUBt8eLuUxBUDXCW4b/"
  "3drulP9Y1wCdOk6LK2OEuqTBJDKt9VM1xiAqB+kxmbu5c+oVf/S7NnHTMA4EMWfPIaVgwjGUiqje"
  "yCxg3TklGtkKVFDMRpXFEprSX0UXWJOvCq1Z5KSqXEDlnZzcG155jgeGylfAdzmSIRmN2dLHRDet"
  "1fmLVG/sNmqx346cRoXtNZK+wYbmp2qWljW0s6m+1MzzeHmLt8RxaaDDRVoUyo9WGhS16xPHlRdN"
  "9UTbgNIQ5Mc52Bjh3p7/6wfdQ1JIRyyXcsPqPHwR+1514zvwmNcXL05lr1RxK1/QvRhW5d95Rhdh"
  "USg2FQdjUml26hl00yysJO/SI5+RdDVy0IDL2VGiwNIrROfwA1GUlAif5hm+qpz9bh+l6UB66X2p"
  "tF7SRQfHFVd9ko5wOkwif6bnlnO02krkytqZFI88iTyg1+Vl9vEEDi0bPNou+RIo/RRyP6EqN6AU"
  "quuvkAFQtEwgnUDkZAKqwhwVLk2OpEG3slJoi1bCh3uzV4WljxYHc0XoJ16EP+kP7caAKuTBG4ij"
  "xUzI2F1kXNaFUyrRgKPdTbWEwR/Td5tG3EVYlrI5Pac7GeeJrlExyzXqU8nIA3XpqV4dCv1Gp8DT"
  "Oe4ApU6SoJMYr2OQ99prMQfqQklOckbmgt6nlsBat2h0r+o2Q08ca4Bpc2TOOL1EtxlHJgBrUH2d"
  "Xbx/f372odrgU3n85vP56QxvypBVkdTg1rDQBRb7kmXiyp0sO5C7A+WyKlvHEQdJrgqh4d8dFV7A"
  "tn1p2Ne5An6wEL912ZfJDgnJnNJr3Xcg0/SYvauADG1gBs7/vuHbldmVMd92xGXC0WBxwctDnOWD"
  "aCwzZNnTlLQuyWMQ/3OYNskDCrxQnooGH4abFMxQFoRBEAx2OcdA4RjvrgLx8OGy7Kz0HPbH0u8P"
  "+qC8xB0YHmz1JVuV2AVPIXGFBgDSSJ8l1x2Egc/x/vlcuicBbQle3LrGJBM0q8iNFhuhFR02BtgX"
  "TIJWaj0qL2uTObcyega4g842cDQn2j16ZSadFMzENJCMxpSOhqoI30M7Bw0Y98bjYVcsCt7g7Sfk"
  "sJ3DmlOlcqAYjVIqy9EqoQs0qKOmFNV4WRyJRr7Kl7ZsS8nCW1jGVF7FqzIuEeCMmgHPW3Ct5FK/"
  "GWLFmUIpMkOlCVEqDa3Ka67loukrrCRdskO+4gvqVha9gcuq6UzTpeLy40qDYPrCaigGi5HEFAwQ"
  "uW1ErgqrkbUr33W7fDH6gX4vusZoVHd4l3oqkY97GtYlEAVu+JLtcaTKsuoTdzwu0xXH1cOrcnT0"
  "MUl1knhpQVkVkBsJn/Kd0O14+iuKuwZHCAeRJIpxpRVNbin45lyrrCjr7s7y7N+SpbfIJqk38UwG"
  "A0HH/b0ywuOaGwI1lFmS8XyGA0ZcEPGo0AC85xaPHM2OeDUr64WmRckIy4qNKUUlYD3JtCxPIVEr"
  "MUVsTKJBXVvfsRTTIBiL9+cvXl1+eH9K1uhXl+LZK2Lc787ft9+9Pn17LhogWs/wVuixTBYlcn97"
  "dlYOS8oCxTyWZa3G+bYsjIeciEyvwP6nRbtSDLiaSrrUpgnnQyw7ekzCvoQkHg8qN4b15zlNWwTt"
  "sEMZb1ieVNPWlG4BPKahbmgByv1ydvHs/PLL0cXzYKRf3mK9Uhpe06kXfT9BC/o0uc9yQC5ouwJ1"
  "XKz/bBYm08bg1OtVN2pWek95yaam/5gei9dGGo5xEurKcxTdFOoeg0q2+U8dpvTBv06mdQZAGCdZ"
  "AM2bQVV/Wm+Iz0aJiZ/FPmp/+Bt15jH/lLrbvmnN2meG0wiAfbafCFhHoke6dokJsIE8odki9Q4G"
  "RFG6yRTL3JAJBxCh92eMooWQbOiRQ6fiBBYScIO9o02gSkka5d2MEsbASC9mje+Y+u0PyBk5hRkp"
  "7pmtoyus4TSCf03jKJVfUmeBZpWq5Cq2gGGawzv6hXcDvG4BblyrLfNk9FdwHDvgCc1qmA9bCbSG"
  "cSGt08f60qsh1+jIlzA37tvUkIOu1pJxxAZABiqXpdG0LhRSIwduYFUT5pZ28eaqtRFEhlQ5zssz"
  "Hcf2AXmNV/hM3g660m52BQV8v7uvj+ZaVVetZtw8ttyXwABZqx4b0s0Wa6fnUrKVUq1Mld+ZSTC5"
  "qgn8HtIBBL5OEjJZUtSzpvnAsI/5IhtogiMz+QtwO/KKOothHGK/i0vew9VhbBfGMDyIZJiMHqoi"
  "Fa1IZBlWVZUrkPkoB3t6vczNHIs1U0CeHpmPsulEKCHY4Im2JAGaZZkZWEqpRpFt8kmCwdFsCaC3"
  "wDNXjQbW70IGsVexNYSgS6EbXMJXMzMHlZm50I3MQWVkLmwTs7xAparGq2PtJonnoNJQxUnRANF8"
  "dIjKEOaiL7dSwKuiKMZJ6GozxZrteIQrdNxp2FanJ4lieR1c0RFknsBEOxxpSiGqrbJOi14cYlZQ"
  "aM02K89hpcVIDqCFVhM4fGymUzzNYVVfFTcp+YIWb3EVT+FAOJNRF/H0fbIo/77kgsCW2w8ts9da"
  "qqnqCbVkQwMXck1hU57HmBaj1tXx2+X0US13uyAfJy6nxax4JYEFSlb1AhcJJ0CRBzwXNMPIQcpi"
  "zRX5EHqFlybcS9Hwgzi0J8bXAEPyY/DXzm9hW/wF/2pU6f8OYs8bX8JrYOEGBAE2KLFU/nYz26kD"
  "cwa4rnIK+OfOOVBzTkRNVnWzqP6SRFDeO/PNuM1UERu6SpjU+C8aUNmHVCdlCMjH0/dvX719MZa8"
  "prxAUe1Aueno0Al9P9433AQHDqHQrJS5K64e0dZoFFhxkPbYk10d4dChEajWVU/ymdYRomxnN7TB"
  "7H7Uw7Ij5o9qhVyWJaMDkgkz4mfEWixO3GL0oSJjMmVmRPwJ+LuzvEzX8jpKrIoWF7d4rFRvC3yp"
  "W2rpAfMszIvYN7VRULcWqycn+9W0TxerF2gQQY4tqzkZ79/QIwXS1K4RAvXpJrt/T4KoKGcHg2oJ"
  "XUmlQo1oLbb01FJn1tfjQDR20yQV4kLSksT4/PXpixfnz1pis8xB8iPT3be0YKRzNaAm45jGhJ4x"
  "fUzMjNRhCc5yUgzeT57Cj4YEowWTa97S17zyxnw1LqKGBmPF5nmdWmTMGANP9znQ1FDHOhYDjPBq"
  "qaHWtntKo9ba+QYPXZV3/3p7mvDp2ZT/tyQnQD+7bVHc00FhepY5COqgML3KKiLqwGLuvSYwoqb3"
  "64r+v4qYSXNskaoiU+NdRabfKblJ++MN7aKxtr9a1ZYZOxvrO13ecotbxhXfmX1wp821ideeafoM"
  "l+XnnSovRMllXHdJRsoQgYk70qJLii25PzDcu+puQwHjZWqS6HePBuKp+HgplBN9Eq+O5ffIYIsO"
  "YfzjjJjTvqYbUR4mxpt/PHt2fsb5oly3IqNiCVj/f3E15wPs43yzfHIIQ/6C8/+1KK3DStcNx0b1"
  "BRBDmwldzXWXaJXxCpVZVVbBY4ZfaJ1dbhYLlUwj32qV++EryT2avOg2uTnosTegKPTF//t/gmT+"
  "z//tfw/6g6ZZmIuLjtoa0zJZ/ysrW/DX32TlNWA7nKjlRr+mXAgKQ3R5t9SU8tHCxQjuU/qZdQb5"
  "iwp0VVFkFczWgdEiXbhYqowpwT2pQhGh2TdN7aBZHXDUJ83r4MQIxa1mWFpJ5KOWruZIu75zAPS6"
  "qb/y6Mb8T4svZByLQbWVMJBPigFDpYHPWicGHDS91gaDc+JVaurQ1UVddqXMHWfNMltc26KlX4P2"
  "n+JJvIXmaOt//gqTPxtyn9C2KUTQ6bxtSpuv8rtISyjnttEW5jaXonErrulO6wMuHYzCBtMdZSZW"
  "udm2AsVNYfbICdPyykIVppqh57x0pURilgJVkClTuW/Jf8WD0c4wXNKTiz7A0MihpW9rcRNP5diw"
  "UgNdp4eoKDcVVWyyuKa7AX5Fu4p1i3DZqukJrtNC4H+9NHPS8T5HUz3/Xor5vlwrpCTozchZ595+"
  "MP3ctkT0dEPPt73fOZ5LGpCFlX9Y+P1j8s0UYXc+AfbD2PmmyzktDR2ddzKC4oYymynNmK99xs0W"
  "V0mZIMOS5dTcjEXlm5VkHtNV6nOqmgNidHM1r8JB2ISvXLDKOcjGhDgHmVa0y2Dz8EiWb3EuoVLu"
  "pCxPAb94Z5X03MD5HqOZlVNps+Qr/6bHuIHIsSniKTmwDccwRZbwZFiiJ1in9R65BbtDMbAjqSI/"
  "8MIM9knpdyRkGZ4/ORbOvPSz3De3vG9uufDWrSmMKotXaQN2DKAv0Hrnt4GSrwAdmX/6E3zAqDiq"
  "nFLoyJMiHY1zbeXikXcpy/xTjCnAAImmWzruR8PNDcvXJvfl1lAdWQN8x3UW0Icuwu17LcoPbnJO"
  "wJFhsissXab+Pgg+490R3lchvqrejPU3zd9TKl7gZRT6F+s+gu/Mz/gLvvE+48AGFUcxlgt2ix4z"
  "tvG2FMzVVnz48vW2HXxzFkJaHKSj95P8V51RqRQpHOhYY+mywtKtBLfRj29BqzUypoK1tJbrBJ39"
  "K44Lm61Zz1UR/41ic9VePWg5ltcZ14SoEltdWsynD2YapDTzYE3E6db3auunth1ZAN5kqVxX0eoy"
  "pl76E5h+NEjfmzeVm4lTdclTH+s+TTanT2XgvErD8SVCulTokhObIvX01Sq+oIWfUlEY1boqAt6z"
  "oyCftqVORAF6Z2clJJUbAGH86g3Wr2i/eP/qGRyr0J/bePbxJAhHZleU4Qyb4qOojOZ0xQkIFXKb"
  "l4+VC4TCG/esiAZKMsKLLuL7NqUW86MVe5nLTBYqwv1b0H728RCTc3vBvwB3NfuSUU6Nbmcwih4E"
  "mq7L8JtmR7C2IF2cIDNUjnC66jj4/ou8VwEm7e7JdbYm5R73bk4Fb9GUSlcW/IW3Mm4K+XTLT9kC"
  "gE/I1GvsdMUc6BCD6UfWlvbEZdGcfGlTzKzKdCnoTf+QnfIkY+18Pp0ZPrvldChNsTOvpzTCUsYy"
  "Hktmy1UBaDAKWdZNrhArBHwq3vPERx/ySfk+y+E4gYEcmI0kSYFWOBxQVZ7rmFKeMF56zyrRAdPC"
  "0NpZjA4iijk66ZKbg78AKOx2usggkCabHcs67RHppVh+x5xdBesc10WO10Tn/FNC/neL+f8KQa9f"
  "ZVWKbb7OSv3UrrTSHoWfbZ5Y6gw/frlVLZO0XBE6b6Q1osT+ZlXh7nn4XNKfp5N/Xlxb25hBpZhU"
  "fgplmpAisny8+4YcZh0EybzDQooWNgYzNjQbd7f+w4SnB6f9k3SnuvqfTHzqs/+FFKiOAU0z3E/n"
  "DaoMF9DLq0s7qvP3abscIyqPXxQEjc7gGBQ+Ci5rfo8aFeEqaRRYpPStRlR8X0x4j/5lFAAdpKzD"
  "U33AC/JOOPKhcEtV3XJMW1kU2iXc796f//XVxS+XHGFBEavJ1D6/qUvWYSCfrq3djNam1a6Ylsfq"
  "5nLDyhFZJ3zJAlRISrZqGMkw6Z1mXfUZTKthSXNpesc4wzFTXQP6g8asMs/u7PrPeNmeeQ8f3/QG"
  "kHLKP8Pfn6qfePHc5ypdXjrQ6SZubPmkChiia+8qSCM/x+PhqabTQvx+mcyux/iHl79Bz18WxZgv"
  "j1ukS/phjblLw/Q3jx98LaqfbTlJb+sZXvemnVpw9j9R/V9OQfT7hMhjRftXeaN8YFxta1xG+X7T"
  "SiqWIdYIsDzRkk3gSWep7gYic5i8GQgIv2HB4Wgrl+Y+zqaJKDwEpAgLljGLHR3abwiDWkcNOTVO"
  "nqwiSFSvDcVU2GwYNDugi27gL6wCbYVGL6TZPL4qKDagqUzX8sEWby3tNu2LiFYPpufSzJdg5Q3l"
  "ZkL3Lah7nexErZZgDdETy6xE/O1nVUxb3BoZEjIiVvJ1tEHjxpWR15T+pRmz0uUkT/jmh1O/yqtK"
  "r+XxfZNuY8AJUJHzP3HQ0V5VCU7ED2lxzBczs42wchvF5M9e8mFITr9QlfkwAop12CphsN7J42NE"
  "psbj+G4cL4tCo/LhlL9LL45ftlGRCK3t1tO2Th1iqaW1ttQhzeiqTiRqC2I0OmKVF+euaMtVll60"
  "Y30hT1BV7Fh9BVZfcmtgRsoxqZaFeonlMcnmUmXJ7OmnAmmVOaiO6uRp4GWiG3zoC3yW4qxCLAcx"
  "Tebr2LpY5wEHy0uDJusVbsAVm2W2/ldbKzwDdir6+h6KcieXUqXcrFdYbaX8FWP0wRXs0bhKDdd6"
  "Q3/h9r+mN9gddWPD3Jkrb4u679e0AL2XnX6MKo2drfzsjB1vrMqvSi5m5fwhc2WGhVIScdwi3NBp"
  "+l/pf//WUt/+Zga+YDO+U5m5Pra2gwqFfPE360WTqqkRV6HG2jbCD3u7wbF4u0GmX/okeay2SxKY"
  "te4u1G7dk7dxsqfigDkaJ2DrHkntQj7DrVdexYkVXLSbO4tmdTdntnTKwXZ+3BdbbQp1UqsOZx3y"
  "HDe/F0KhfLnKh7t7yepWbP97ZR+s//a15fUuat2a7n/PLeytTMBU/L26zj/kUf6GK5ssp3QmHX/H"
  "naTlvLwWp798uGhTfeBK0HPWNMh5Lgg9FlxKVGUZx5idhIGthrRmWkkwbUxFlqg85+en6MgOu8pb"
  "9htmOHHiNOYIiavtnmaxXOcbecOhDDgBxk6ZrujYVH7wi7ev/ybT05QzC+j29cWLd2XNHiJUYv+U"
  "rrTMhBZkouJZZHdU2oCTjEUBpxXK1NGyN1VmNNr+CkBHxyziys51ED94mdT22C4KUZUykFl0XMSV"
  "jWuqliua4arcbolxFI+gTKj4Xac+LJeCsAsbSE1FEk450kZJUR5iRDcpeUlrbxPxDkDmVtsj8Ffi"
  "oFl/j979dXOFc11yaZMHnoU0r1eQrWpdqFsfMJp9kdhFIMp98KYy2pf+YkoSVsCg4CzlQVYf4J7/"
  "WvVY5fbj+pTbQa43J7+VlyNU525ZsNdfhcSq3a+Tn17A36ot7Fbxl4lYJEcrJfRHIgnYf+wJJai6"
  "+weq6/d+sLq+GsjT2mFo52c0J/5oPIF3MFoFg+q2A3/hAuZpxo0H8sxPlzCVbey0N8pSPnHDMmB8"
  "s85aFubBv1WFHvwbbYf478sWV+WZdeAfF7P16OMMaT4Aw/f/mQXzR9HMOr+urMsdjsw4mu+Pkcqm"
  "UwA0dWbE0Rz9rjAau56AdZj5/lDO3z7b/yexVG+R+6+9UKIi1+9dKUGQP3SphL9Uec0NO1gAiIr6"
  "YCZcngL35TQk0kEp3FvsuL8EWr+Dts9Zo+w695fIukAXy0li3AGflylWNgpxPCYCAw2BQVQiUBZs"
  "qtilMxZexbwzmcEKYpLxFrPz+fex6a6bXYun588v3p9rsx+rSkR09wMiQqu4Pt/u6Y6DvIM3P8Ai"
  "8B3XFPdG6Xr7dBvEPtfbVHWpeJnKlBIFiWVgKkhd+Nugl2enbwlSu73JD3n69D1/XZ6l7gQ/OTaK"
  "rbBG4muPyplqX43oW806N8ri9BeUvoYSDIUnLAnWnJBO7+v0Dilzs2LZXZapKstrsoJKd7TdHeLa"
  "FPNMmXZSrL7JcVWkJsalCoge3vtsM9eUQYyb2isVuHJpKRbyQibNN7HUDw91kqGPVKmNFICFCopZ"
  "pV4q9jphO3vWUedMcEuhCytGWxJ1jYqmWNPBgU7t/yJGnBvaLJf5Z1m9SDxQSogGjOKvwSwkNNhH"
  "mW2n15j+ZlwgJ6fRon1Yey8ca0x4rcscrxxpPL24+NA+w6Lsl6fPzzGkkJIMFDlcZVhflFiKcTNX"
  "tpzMU7q7m8PZdFrf02/JsgG/emibL5Ki0kDSIkqVifAIWF3kVXVU3mnFr/nuLvs1POXXfHNW9Vru"
  "lWO6e51jAWlLw6EMiF1G8pVVVxATBFRe8QNHtiWp6sh0qCXV2Y9BaQF0yoJmh9fxCu+kmx4+RTUH"
  "HfpAypd0tw5meWBJDPkhOkCBZky8iC+oVztB3l2D2i4lzKrjZFteFkfJIZkSDeQZaHOAiH4Hmb0C"
  "GqsjGnmKNQYSvAEFg8RevX39CisyYMWBedKeoX6Oia1YUg/kZyJTMDlYZkyvDlUGTtH5lSrgP27D"
  "mT6bF9WLL0fZLBjJdANVdal82xJHeNNGMKK9jxeH+8spnPA1L6gFPlq1Vq1OZ/UIv3fxVubkon3y"
  "gWK9YSnU7eMyzvoIm5W5/DAKnDddjEJ3E9SOt1XhBifLFX9kSMiVvDokW3FNFrrFKAHGgcmo73LY"
  "Sg9t2JrplGgLQN/SpAvOcZ5+WaRL0Rio65EZBEsi0EEZ7Q84dvH2MNTyjq9i+GeSkKqIqbRvO+oS"
  "FXq9UlWsMFUqBX40w47oB5qnVK4UhoFRHLts1cHA32w2U6E61Y2FmCkL2yEpqFaxusdB3SkEONUK"
  "JMkwL0ykxG6bfAMjKGvXOTp99UsXawiApHa8XLfl6hFBdMQLpjwKYtSW637yZQZn1bCz2raoVoa8"
  "fKl2MY+rxaTCREWmJmQsZTJFSYOpChjUw0uKNZ+msjaUj+DGXCt6mrDD/tOqJTqdDv4pay1wKSRe"
  "FrlGF29dMmX0PCdaWL7GkUzK764MkmL6KclHimsmoZZ826d7mDC1GSv0cME+Hrr6IFIXXtIlyYk/"
  "AYRaVfYOI7lstdvy06NuK2iFrV6r34pag9awNXrUenTUCuBx0ArCVtBrBf1WELWCQSsYwrsKvoKC"
  "x7LxLnjtldZA/1YFj2/5DTaCx9gB9c+Qdv8Ir73SGlS96PDVGw1c68QHr73SGlS9VPA9eDPENwGB"
  "R/A4BPABdtKlTvoeeO2V1qDqRYev3mjgWic+eO2V1qDqpYLHN9Q/g3P/PVowuVh9o3+G115pDape"
  "dPjqjQaudeKD115pDapebHqL8F2Pn0riLCmtosMKfkDv5bJHikQCDZ8m/JBeRgp+UJJUST8m/Ije"
  "DCr4YUX8vRIRCl4hQ/Zfoiyo6T/SplY10OjfGv9AUnRPgUfleLz4GUhMRzr8yBy/Di+/3a/AByZX"
  "iWz4CnVVC43KDfiK2CU+9S0Ruvjs8/gDDb4avweel0fbRryARyYJDQz4kb6NmEAsRhpp/ETDctVC"
  "pyLEagmvE0pkwg8lYWnj6bV08hrK3T5yWboBb+7T0OCfgYGfEnmBAT6w6V+HP7KmG7YUhWoo0viV"
  "fGo2KHdlxYcZXqNzA3wgd2lojMd6M5TsxBQWGn60hYkq8KEjJXV4TRgOS3ao04PVv771hhr/HGnL"
  "rsNrT+0GVVclvLGSw5J9Dvz0EJmrUrUouZAJPzCXpYKOTHlhwktuG5kN9C1cwduUq7eo5qbgNdnb"
  "N/CjFlLiR9GDxjgiD/xQV0S6mrYkeRM8DSxmousD1TiPyq8G5TDsYVbwI11/CLQNHfrgBxUHHRB8"
  "pCsPunxU8OWKSXBLuFv9K0rsV/2b+93o39hf/IGKf3r61/fXQIK7+0uHH0rp3q8aDAyytcZTDlV+"
  "N6j4jyV/Nc2lq+G/WhIP/k0+VjUYaKPU4U0xWI3I1IoVvelkG6j59nTlylxfXZgHCv9S5Ice/Pc1"
  "YgnVYHoG5Wv7Nyw3dmiugMZ/Ams8lZiKqv4jW/8ZGPBH+rIHpn4YWuMZ6FqO3sLQ4ip8Wnuvgq8U"
  "U63/SN9dGnjf0s8reG23RDq8uWUseEP+mvCBQZ+RpWpGGvzQ0edDY1nCarXUMjr0U/Fnc3kN/mzA"
  "W4xGb6AxRoY32Wqghh8akl0bf087X/Q1/ISOShyV8OWMNejIPdVq8O56haZKXK5XqdoFJj2E2i7V"
  "6UFXHUOrf7VeOv1r3LBndG+yxMiG11RTrcGRI496Oq+3v1CJBa3/ga3IVvByffsG/NA6lw008IH2"
  "ZR5Pyd2MdVGngvLLgwp+YOiy2hL3tCUr17dvr6IJry89wUcaXUUWeN88MpTwI211rQa6MmKdTxmv"
  "8HDoU87d8+ygNLcMPcqqF15+1ba01MLL1bLhbXuIptEEdLy2T1Le/iUPlQ1snuGFl7t64Dl8eeHl"
  "Lhq44/fYB5hKpXnAPIl7+5d7TDY4+u58RyXXGFiHwX7NeBQVDtzDo6d/bVtYmr9rTyD4iv1Yxjef"
  "vULNkawVo+/jp0SibPA9/KgT9qCC34mfgVzfyAPf8/Y/UvRpWsZqxzMq6TOyzhC18D01nuF36bO0"
  "EHjge975atw2cg/jrj1qpPhJ5DkyOeMpmY5s8D1+Up5oDfh6fkLwmrX3e/xkqMlrW6L56Hmoi1/b"
  "cuLBj35+N/SPentdV8P/wBL6nv7lytj2vRr7oWHfjrTDSLjL3m6Zz3fg07A/R5pSV0Ofhj3ZgvfR"
  "p2EfjkxjTr8Wvu+OJ/DvX9PeGxlWBl//pp4c2VYJb/+hgyCfPdyA7zvjCfz4Mf0F5snO6x8x9LrI"
  "tt9a61sSWI+MpR7jtg9enSr7HuO2F16q532PcdsLL48XfVe4RC48W4gJfOgah33whAvZ4Oi78x2V"
  "x2VbB6wbjzq9913jvKf/o9Kc1vcYk237/EA/BvVN4qmB72v2do9KZsJLgEga/x3mZo9fjXggGxx5"
  "rWwWfKDwP3CcBV74UC3wwGHOLvyRhp6BT+Ww4DV7pmHQqoXvGe6RUZ09toSvhunCW/p8ZJw3+175"
  "4vh3dHeHofL5xm/Yc/qm5d+HT8MM3HeNyR74QNsAjhHAA19NyzXmO/DaK9M/VUNvpv2hb1sxXHhD"
  "7vcdY7gDb8jxfstViYeG/0tZFKRzyuMid+GJ18sGRzv3Y7U8/Qq+W88Pq+XvGfBBDT0MTH+ZZfJz"
  "6dPyj/Q1o1PP7x8caf4meRoL6vejZoy0/Y9Brb8yMsY/3LUfdcuZ7q+sx2eFbx2+Hp9HJjlH1n6J"
  "PPA6OUe20diGN+3Jmqejhn60T5vw3v04sPiM6UkJPfRv6rEmfODsL8veqwdL1PqLNTXNgg8d+tQW"
  "P9LcxbX7S5P/Ax2+Tj/RLYs6fLeGHnRLmwUf+PjP0LLX9Q2rn69/7dOOf9xdr6Hl1zCdy6Gvf0Pv"
  "deB7Nv7Nc5PjvLbwqb2pwIf1/FO3mOrwdfqYZea3PHeRH95G59A1c1bwpp+6X2ngfX//ph2433KP"
  "kC68jX+ffbuED1z8Dx07swYfuvTgs68y/JHln9WCDbz655FlTtbguz79yravOvAWPz+yzq0mfODg"
  "x7HHGs412x6rrybFVox2n0cM8pINdp1HdPIdVPC15xF9e0QGvJ/+de5B4MPd50GDnckGR9+d76hk"
  "tz33POWHD9V8hzvPgyW7D1Vwy3Cn/l95EiK9QW18VGU971vwfvtMZT0fuPAeeaRJw4GMRtqpH+ri"
  "P5INdumHhjpSwXd3rZeuH/ZsT6IX/khDp8/+5sRfVWpyzxQWvvXS9UNDv+zVwWtssufEy/nge8YE"
  "hvXxWqa+XMKPduHT0A8Nhb0WPtA2wGCXftjXz1M2vEc/NA8gkdbgqJZ+TP2w53iFHXhDP3SCi3oO"
  "vLGPevZ5qm/CD3R7Rc9znhr44JW9oueepyIvfKCWK9pprzCO0wZ8UEMPZnxdTw8O8dJzaR3TG9TG"
  "f5bmhr4erlgfP2nYS6oGR7vGY9gTDIONf76GPcEwCNXC6+QZ7bIn9PXzkQ3vpc+BZU/o2eejyAev"
  "b7Bohz2hL6WFTs/RDnuC8tb0a+ADZ79Utmm1XIOd8k6LmKsaHNXTgxWf2XPitSx6G5r6ec91ytvj"
  "N/TznnOeGnjgA4N+rGAJD7yJzoEvLLGCN/Vz3cLs79/Uz3v2earvwAc2/Qw8cR0afGgTtHE+ssYz"
  "Ms+zPed8VAMf6sMZ1utXI/M8azgEfPgZmefZnnM+6tvw5nlW91D4+zfx1jPOO66+MbLOvz3P+cjq"
  "P3DxP6w5//ZN04EDHzj4tOONe/b5yNpfR5ZfrOc57wxMeMOv17PPO5EPPvKNp+s771RS2Wlw5J43"
  "+66jumcHK9rx8Jo/2nKH+fN3dH+06R/dAa9l4+zyR5fwWrrPLn90ufYDPfy/3p5vGqv0fKX68Tv5"
  "Rzv8uZEeYxfp8H5/rhadbPXv9+dWp6+eC+/x51bwfXc8Hn+uKd2s/KzQ37/pzzXh6/oPHQT5/bka"
  "fN8ZT+DHj5v/Ve/PLWOvavLR3Hy36tuK3gY7/IlOWGVPU7lq4I145p4ZRufiZ2hu654Tdhd54AMD"
  "Pd6wOBs+svrv1tD/0LIL9Yz8OF//pt2pZ+fTefvvG+k1tn3VxKdpl9Pgu358mudQE97Fp57RUoIP"
  "6+KjIiO43oV394uVRtMzk+yiWvjIGk+3Bp+23OkZWX++/k25ZsJ7+w9shjUwgj9r4C0E2XJcwdt6"
  "Ws84I7r8/8jRZ9z8goEJb+gtPeOM6+5fW4/qefKn+hq8E+9qhEUHzvgdxdGArxRNhjeMGVU2Wm18"
  "VIWJyIL350uaK1PCj+rof2CH/Vrw9voamuZQh+/Wjv/ITf8d1e0XJ43Agrfp2dpKWgP/frG2qgNf"
  "139oLcCoRt+2WIcJ3/Xj391fkScvT4Pv2QmTdlRnpMGPXPllMVWz/5Ervyym7YEPDHka+fNfTHhj"
  "uYZ18s6UVC68u17q/DVwx+M5Xwwcv0nPyO/2jceVj3bUqwXvyDtTS6iB7zv9d/34d9mP7eLX8W+f"
  "43qGjbzn4OfIm27rzz8NbfO92eDIpQdTs9PrCfj1fzOyz60/UAMfWOgf1On/Q7s+gAVv439o5/v3"
  "zGSNfi183x2PR/+3jgZm/YTQ37+r/5unFB986CDIr/9bRycH3ocft76EHTVtwTv6vx3SoMPbdvKe"
  "nRdj0ZtttzSTW21+PnTsor2WG6KgwzuGVyM/3d5fQ9cQbOezG/qJ60foGT5Se395HC29luPy7unw"
  "Yc14hqbjx8qvdwt8DM3AIxfeWmAr8VuH9xKERloVvC8TomfmhxrnEZ/noefkk1b1QDRUcPWN0S7/"
  "i5mxN5AN6v0v5lr2Kvga/0vP8Hf3DXiff8EgLR7OcJe/1aT1SDY4+u58K39r6Anm98KHar7DHf5W"
  "jReU2f5DX8iBBW/k7w/r41114RNZ8D5/qCHaePiDXfEDpuzsywb18QOmbI4q+O4u/A+1+IHQDR6L"
  "XHjtPBI6xvzIna/GbkM32MmG1+0zhkZdg/+RWU1gUB+v2DP90VEJP9qFH+NYFrrJ7B74QCNQ57Dp"
  "ga/QELY8JXRMePMcGhqcyte/ec51spR7Dryh14XO4dSBN86Voe2PtvBTvuXBR7vif8yzd082ONq5"
  "X6rlGVTw3Xp+pWXMG/BBDT1UJ9qhhB/tpE+NfqsGNfZDPVuvb8H74udLeJ3+o/r4ebMehQIf1vpD"
  "dWPhQG9QE89sZdIPSvjRLvwb/s3QOVwM/PA9fTyjev4zMP2hoekfj/z969slckWGCW+ea0Ln8OKD"
  "7zkfOKrZ7wNL/wwNf3fPkS8Dy84Q2v50B//mOoa2P33gg+9bBGFpERr80KyvFTr+dGv8Q7NeVmj6"
  "0931HZr1oMKWJwXSHI8RjxFqCoV3vYamWh2a/vca+MCg50FdPEYJH1rzHfnzU3papYG+23/g489D"
  "69wROocLH7xJP4Oa+I2e7q+PHPiuj56H1jqGLTelVIcfmceI0PTXu/gfmceU0D3seOBt9Az98WM9"
  "zV/fd+G9+LHr1YSGv77vGY9dn8ry11v0MLLizULbX+/2H7j4H3rjzXq6v94LHzjra9upQk88c2TC"
  "B2Y9MTeFVod3DLuh5U+38OPY58NWVGefN1ztgSxutiOf3YotkA3q89l7xsE1quBr8tk1eK38ZH0+"
  "ux7MEukF6WryQXpmskVVju6o7nwU6RZHA75bW+/OsCeHpv/dN1/Dnhw6/vrIA1/Zh0PT/x75+w80"
  "cvaklJrwpr03NPzXvv5Ne2/YclNQLfjArhc6rPFvGvB9p/+uDz9a2Ri7fqDjb+1ZlmC9nmodPQ9N"
  "+2po+q/r4SOz/Ko3HqNnVU5167v65mvVU+278tQH33frxzr2Vbuij1U/NvT3b9drtVJo6+rT9hz4"
  "oKaebWDT56AmXsKAj5zxd/34N/e1CW/a03pmxSy9/GTN+cI4DA10+G4NPi22bcG7+LTCrELTn96v"
  "he+74wl89GbnxYR21d1BDbz1gaMaerbj1kLjDNqvh+874wn8+LHFad/JbxqY9UW7pkGn74b0m/CG"
  "HmLGmHvrl4a+8fji8cxw9shucOSTd87BL2w5Kc99rT7q0Iz3LlMI/Of9ChMDC94Xf96zVqaEH9XR"
  "v7nyLry9viZlWfAe+jcp14Xvecbj0r+5i2rgrQ8c1dTTHnroP7LqFXvh+854Ah9+Rq68s5ieH15j"
  "txZT9cNHVv1wv7wb2P5Hq95439+/jZ5hnbwb2P5EC96HH1femVKzBr7v1D8P6/sPnQLr9fXYXXln"
  "ahU18JHTf9ePf1fe2f5xHd6tJ9x3UzZM+J6tkDkpGFZ9Zqu+rpNyXvGToR2/EZr+bpt+nLDi0KnP"
  "H3ngTf0/MpPi9fE7aaCh6R+P/OMJLPQP6vRzN481NPzLvv5d/TyyQjot+MBloIMa/XzoxGOEtv/a"
  "wqdtlzNzQG355TFMGEmpNv14AhNDy1/cN+nHcYfa8MZ5wbXzmzm19vg9hiojKdiWjyNf/W3HH63X"
  "J3ccOaHrj6769xgKQ91fbK+vx7EXWv7oyIIPvAxl4NZrDcrs65r5jmx/es/reQhbdgr20KrHXtZj"
  "VAncNfYK68OyQX29QRMRgwq+pt6giejIA9/z9n9klD+vv2+ibybfGPXnffYNq2x5VYC+xl9j7VQD"
  "vls7HmMbhQ7TcPBjbNPQYUoDD7zODnu28uyHj6zx+OIn7dAj/QO++EkD3vqAL37SFj0WfNePHzNO"
  "L7SloDuevl1+3in5Zd8vEBrF8Gvrq1iaS3WfS40+b90ME7nw7vgdfd5Wumrg++54HP3KVgX1D/j0"
  "eVvVHDjwdf2HDoKOau67cfV5S0v23aej6/M93aUa1cJr5Dyo0+d1+Mi6ryeooWdHn7cPKQM/fN+9"
  "D8iLz5F7v8+gTp+3j3ID5/6guvuGQmcC9fcZ2fp8z/FfWPCBzbAGNfq8AR85/Xf9+Lelo1uyYOjc"
  "D2IydKcEnAnv3JYxqNHnzegN330iPef+FI/+75QUiIz7m0ameabnKXFmw/eM4Ud18UtW8bxQv0/K"
  "v78Grn24Zy+iHz4yr5+q2V8D9/4v2wg28MP3rf79+2vg2pNtI15Uc3+WdeFW/f1c7v6Kauy9tml0"
  "4MD7xu/ur6jGPty3WZ8D78O/u7+iGvuwlRlWgQ/r5OPAtffaRvKBB95G57BOPg5ce69t5HfG48rH"
  "qMbea7seBg58Xf+hg6CjGnobeeRjVGPv7VuhCEMHPnT4w8i5Hqrn3NfQ1+Bd+3DPLQlowjvXH0U1"
  "+VlV9Qp3PL78rL7P0BBa95X0/Pc3Gegf1OnnQ/sYHTpOvYEH3t6Ogzr93HFrW/D2fnTz0LUauqG/"
  "f1c/N08RPnh7vQbe++P6njpmoe3ldfDp6vPmqcmEt+0qoe2l7pvjOfKIXztEJ9LgPf4F5+KQgQXf"
  "d+ltYBeGKOEDjwJhlxDU4UOPPmCXBCzh3bgys0a1vR9HPv+IUwKir8MHNeOxCpeU8GHNfK3CKAzv"
  "cVSHVliEQT+ewmGhFXYx8ML75jty+duRz9zilICo+vdFVoYtuwSEBu+JbLXguxX9aE/7+mVqdfHt"
  "+jCN29dq4jntyzNL+Jp4OY9ZTi/Z36uF18Oj7KAmH3zPWN1+XfybHZo10Br44t8iK99zaMI79szI"
  "0t8s+MC972/gia+zotDc8QS2vLbvORr44COnfyceT8sFqYE34/EsNlOtb008nl3ZVIcPaujHicez"
  "gxIjD7yNzpp4PDtUMtIaHNXQmxtf1/PKEQ0+cPHpj6+zSz8NHfjAWV83Hs+Ct9bXx//7dgi0A9+z"
  "GIQ/Hs9MXHQaOPVtzETKyAMfGP4FK7I40m7H9OZrRG4afegERUd+eOv2Ta892b6sWIfveul5aNd3"
  "Cp2g8b4Nb+d39Lx6rwnfdz7gy+/w5e2GdhS+23/obrCBN78j8tSdMO/8MfXtyFeY2LhUyNQHIk9c"
  "XNhyS3BE+v2qPnHadwUbw3sCNUIrTcTAj6ewQmiloUQWfODdwAO78BzD+zwboZlHY6yvL9POgu9W"
  "+LEydQb6fbK+fA0rEyjS4X35Gp6wmtC8DzHyw9uTjfz5F757jbX7bfv+/u38C7ekhgUfuMsbefMp"
  "fHVLQjvrzBqPwwiMS9N6zn2+juA04M38XG+gvA2v+RN9cfjanW+he7/wkf98ZF3M7IF3EWo5WnR4"
  "L0KtwoslvH+7RHYhlRI+rLm/WD8aV/cjd+s2cGRp4nw/8sh733TltzLXd+jxg4Qtt6SADu8YqoxL"
  "D83z79DnKLLhNf459N1Hadzv3LfG7ykcHFppuwZ+PI7S0EoL7lnwQQ09WIlqDO+rrByaec0avXkv"
  "fguttOmeCx/65zu09THnYo+hB14773sLbdv3ZRvj91XOC808dOf+64GW5R64yaeRC2/cd+xcQuqH"
  "D/ULvI9850FzZ5TXt3ruWxzY8Pp108O6er/GbZpdNRyn2JrnPnHjPmjP/UcOvH49rF18zNd/qC3A"
  "wCVDB16/PndQd1+Vzeir6839+fihlX89qOC7u+ZbnsAMeNe/Y97uGeof2HF/emSdawKneKY5X+1O"
  "2fJ6dn/+u3mbZnnbblSXz25qmuWEI/sw7uk/0Ogzqrs/1FZ8+9V98Tvoc2CKqcCpR10D36vul3eu"
  "eHPgdXq2iwv5+g+164gj90oCB16/HdkOtnTwf2ReBx25YtCEN/0v2qnM8r/o8Pr90ZGTv2P1H5gb"
  "OKqJV7EPuqr3uvqT9kG6b8DX8U8dGWWDHfzZCkMLnGBIz3g0tAW++4NM+COb3gb+eCcT3uh/5MsX"
  "1k09obEh/fUqdfi+sSHt+pORCW9dx20HT5rjUW+M7oc++6puuuxbEx566mFat4Fa8Lafsevc7uk0"
  "OPKtl7MwQcu5IrOCN+7mIWCnfq+Bf0MtGpQNjnz+99DOT1TwTn6iA6/z576dzOKB17dv3y0p48Dr"
  "2OzX3X+hea50cut7StBo8Jr6IsEHNfGWBnyotnvfLibmgdfiMSwnbA18oMnrvnuFsQNvzrcmHz+0"
  "8wcr+FGd/DVTLfQGbj2EEt66775fUx9eDzXpawKsX5MfHTr5UJEGP6qh/yMrHiMwjD8u/h3EBUYR"
  "FS98aEpUM7/JwqdjuAzcfKheCW9c7CSBo5p6Uwa8tlpRzX03ZmSWtt8jf/6vFcoY6l8w67Dp/dvr"
  "bkZ52vzZKj1hwgfu+g4cORKYNZwceKdQS6AbE6I6+J5BoJHJzw14O/HDgO9a+3HgKvo2fKDj00mj"
  "D1xjhT4etw5/YNcPjEz4I8tuHBjGhMjp37a3OFHakTn+I4tvB3Z9P3c8znZxrszQ4B1FOXDzNfol"
  "vFtHJTBrtlnzde/9CWxjhTHfkRO3ExjGh8jXv6X/9N0rLUz4nktu1qlQg3cOfoGb36H6N1/wavU8"
  "9wENXXgZrhjoNwY6/Moa6bBscOTXz21M6PCm/vP5eG+2WU7WabYUk/vJ03TdmCfTlljN42XSFF/3"
  "hMiT9SZfivt0Oc3uO2cfz76cXTw7v/xydPE8GH0C6M+dYjWHhvut/WanyBZJ406cPBEH8L8nJ6qn"
  "n0UgxqJ7vPfN+OA7fPsuTpfrxqollq+TadESV/zhw0Px8VKs4u08i6djEed5vBXZTDz6+EgciuVm"
  "PherJBevz5+J//yP/y5m6boQ65tEXGUPMOjJnZini3Qt4jX3RZ2LsNsVjd+CTij+8rR5TPDBoNtt"
  "rx5EMYnn6fJaFOtkJdJCvL34AO/hj6tNOp92oJdJtizWOBBxIpbJvTjFITWo4+YxvJ9luQD8rUUK"
  "AN1j+Ocxfxb+PDhoYstP6Wd4J1GdAqIRNfsf9wE5OKPjCuFfxWQB096f5fEi2QdIQgFgR3xDLO7B"
  "lNrWfwJWR1zFy1txhchorOK8SKYiW05gBQ7E5AYQDf+my/Yqvk7ENJlk04R7we4ug/67dnDU7XfE"
  "B8QjdlSsc8BJAZ9OhOzu4u3ZOWGe34l5srxe34gG4jKbT7EneNvGdcF/iQAEk4ikj6bI46X4LRzC"
  "YshOqO+Cur3a5IBloA/oEDubxCtACH5+fdPsiPfJdQqNYiYhOSU5lUWa57AGOBJcq2wOrfJsna23"
  "K+pqnWXz4hCw/wUR8IVbfSnSRWe1FY07WP9pvCaMiXyzLA6LoL9CjPTaq6wIgMRgZM2OuWUAS7CW"
  "BZABk206Ew25Wb7I9+JPfxLWo86Sdgc2MjdYCYBLeFwSHS0oU90vsFtGivTETyIY7SA+SXk4MAnC"
  "/d3hTqrZ06m+o5v06U8pfQgw3Ti4ayIJB/jNb/D/7bmeAOUugZpft3jQ3zSa5jl9s3lOwUyHMCjK"
  "/2C9Lt5K8oFvJA+woWHkOIkMHgF92dixlqMlss0aHn/6bOBnxfhZAX6CEfyL+MFFo3nCQNRMV5+b"
  "2EFntSluGqumNg14asxivlnEF7NGurh+Fq9jYxI4i0X80Mhb1y3ga0jfq/QhmYv2E/EcGNu6F9JS"
  "llOZwuhkRx0gxhjQAk8+wqBeqvkgEehtG/9f51a32zaSpe/zFDXo7TU5FmVRjpK0FGeQtt0db+cP"
  "lgeeGcPboEhKYkyJDEnJJBIDi71YNGbvBgPMI+z1vELv/T5EP8l+51SRLFJ00mk0OiZZVadOnTrn"
  "Oz9VWncoQE+81/SA1YA+7R+Jh6U6yAkJzLyr99c9sZBPWLp9TThTvg1ZfoJml/CViGfo/Adh0MMM"
  "DwngC6sbC2Ohviz4S6kj98gN4Jq98r3AWRstqUm5cZPYBo4Yjh5Zs0COiBZAxB7rQOykaSU5atNM"
  "pBQOht5rH05fgpeyEqJw5dAaP4rB9f4+DaMRjuvKMWoiJ5zjvRwsnj2T1lDNsJW9t5gBs+OBTZDJ"
  "QP48y/Z6wkpH354xxQoIthD5oD+a6JIbjkZtuzkm6DNWDtxV8lJ57LTynPY3tj0WJ2fnp8cXGgwn"
  "OniS1/QddynHitfHx6nYpkKSlGTI7mIf/6yzEAi5jhTigu3NahMyHbjRrZ8E84CAcz6HD/XHshs+"
  "AHudMJXEXPgv6uQI2xr2D+FyZ4FDPtvJINkk2cSEvTBDAu15EMIfoGU4QMc4yNzlRJLxAnj3TKTL"
  "YA6rT3x09jYuzQXYhzeJI08sAOOQweMD8vfSlYh56CwWvieJLJ21t/RDj3C+L87Wmb+AaUIEZW97"
  "+MS6DTz2ksGKvcIiCdRoI146qX8Mnv9tSkqxhXwgiXQsUmcVh74Fvg0v7wmvAAqtfGdtueiSEMrt"
  "H1j2UMS52ZO01Dog1OmbP56Ta83rWOPkckhYO3xSewJviS+v4Av7hCvDnnxOos3aM6g7gALh0SX+"
  "H5p4GYqPH8XjoVkT+IHx5IBoa1R9UnEDWojQTYeHeyBHzuQtzUntWUj1C6n6BVTfg1kVtfMpCaZF"
  "xT/094WwhN1YQ6FWANqKuEY+l+RzkCf2Ra7Tr2bI9Rkud2bIMYMSQD2FhDeanJa2L3ICuvlVWnDn"
  "fRC9LrvePaj/1ZFNSF8nmVidEH76bmmh2gbE1CTtFXsYG+ilta4ZbIzEn/eEu0m0DSEBpI4E93TG"
  "kmgKX0M2DG9i2wcaCvRBAwBuQgTwhgn47U7b8RVNgc4HGhEoMU2IQQc0piStjXKOu9SkpgCUYCgd"
  "fjHfZcBS8o79XDkTTCid0XZCRLGWLXZqWy6F5+E9T98nmeEM1UbTdDOfHYVl+9/AzXm5lOjMK2rW"
  "lCJtkrnj+l0LG47ItkZYFRvxnLKR4eh//4HvBCIp8MyvxhMUgPovP/0kfv6nPTSbq+d5gQkTenpK"
  "1k5Pu4ZTDHTDB8cWo0th69ruYauAEBYhz4718FLlTHk1U6cJ5btzAbBE3pgLhtJja8FsuWZIrKdK"
  "qNp27+BEoYCisFtAoQUn0e059ZRm2aN30jOAxD6tUH6eaMMaSJErqMjtHaSopyDFIjNg0rB7opyz"
  "ks0mje4pKRm0jnligCB9a3SZaYqoNdw92H2aNRV0NtQEqICAFuoQzKIRQcIAYVUK+6u/jXXRKmW7"
  "MjwSji3Bf0QRu5fLD2Qta6WwNz6S3FvyvD//jz3wKP9yZlEYuGRk8NwruCvmY7Py02oOiljWFOHB"
  "gsg0lSWBqDQjL58oO/KKSbVYHS8xdXvCdDOzYt+5GVMKL/7v77y/v/znf//y09/w56/mgTGk97/u"
  "q49D/P0PU8bSTh5QAAFcXyxL+uS9ESXC2VYGSJFOoHx8HLg34v3GgcemjJODBgpS8C+7xlH/EcQW"
  "5yU5d7lZ36QCjgODVxH5+b74fuMkHtw9EU0C6BwtAKFDWPQEIguEOPTB2qZWuow2ocfxlCQXrb2A"
  "iFDWOw8j0tfj777vI6J77bpvMeqVkyyCtVkGItDNrcMJMKZayVJHyEUNFT5I+qlYBFsfgU8yo9ye"
  "qiEcr84hC6eQ66xEgFn20ko6yHs3fr+CR88PM+dP0n75+c9tXFwxh/jaxbjsSJrC2u3MUgPKYTLe"
  "2JQJa58L9bkNde4ZqxP1aOnyrFTmSWPAnLpXFrAzTI2yr1ujBtoomrPVvvhVVA/bVIuOUXZ71LA9"
  "V9eow8+NWhGHWIZVg/UcYecib8lnVez2g6ouaidBGwZqz47K3cVOYVT1vusjPHYShEWYE8xBWha/"
  "zgc7WOYVdV9aGVbb0behNCBPqGf7Fom8UsqKfws5Uq92RvxGjFi0eoofMf5e2sUO7T9/njbxDIkR"
  "7UKj3REOfsDs41JZJeu0kz9QQjAu1VpOq76TpY4bHvd+vauN4NqUIefdRM/6VIL0iawPeRynHc8p"
  "1fItfu7RV7xtg2ijEjaJi5S2Fdya+CuHMsJE5XdX8TUVSuUkMRDlzeVrSbjOI/t1EhufcLhrxB4H"
  "tQh0jdUJMlePZKklt2g4ZyfUym4hE8qpUoINleZOKc6ysmDlcwhmJUB0ir+gvz//8yFQXDgc8zP8"
  "1Zlc6vteX0ydla9ysCprQz5a5tbEx1jOfFXsexBBvk+RgRMGi3W5vCv6fN1X3CD7RMY4pLIxelBO"
  "68scWRZ9UvYQsuosxUQpJYnQVlkfiByrwmV2G6zHesUySlXBkoqV4O1H6amRKjieeNjjSirNbtbp"
  "I7UcaW0wwbKmsZtIdIXjagO6UwnVqKUTUlhlQtFOJ5rE9JSCx2lJxT1ROm+/jC+gBA2ey48wUSy6"
  "DKVTNh31oQ4AvyCeftETL/RommAEPSwa/VTYj0wilgXrjd/KaUqGK4ZyyVBeMZTvhvi/Kuy+7InL"
  "ZshNTOXEVN7NVCMklxlYOzS/PzCnfZXqs+sCEAq/UuH5JQfnb5vB+WVH5L8TmHdPUOWVolY0no4j"
  "b04BH3SF8lIFmZX7g3kZyst8USaP9IKksjOAb8axnbnlJ8L53xDM/5Yw+65ZapUeaCZrUGPufdeu"
  "IJ4wmrRLiD2J7aocLzHpLVwJ9b2NEk/mthaQXDy/ENOzi9OpPFQg0GoCFR3DBTFwO44SwK45ltRq"
  "x7QgmL35kSrysthryOPCA/UmWYN4EcO6N2mwoK4PVFGdmrCzGP57SW0WUO3RScgLMKB7VD8ksOX2"
  "hn8ydFfZZkv3l+z4WuVOlgCd3qVmyQ7yJPAGDtdXQS++/n3NsJHCgqlBvHl9ZNnizXffHe3b8D5B"
  "5pNJzsIN3IRn6WVXOlCg0EvF16+c9OZimSDtSOC4CmLHjyVCWnI3+GQKuryKSeMfWTJmk8SoFopc"
  "4DF5w3QTw8WnKdbBbmgtphfnb15/fzq9sGgrrben5xadBV2+OT+Bw/Q2MZ1mZUtJityXLLwvizRw"
  "ERyo4zsHSrXFRkNIWWClWJpwQydYyYPDxTJKs1TzS6+eT3/48eIFpfZGc41UeTepZGmPgBG0zxuu"
  "Ts4dUKYUrdS08pOBjiZtUibsx6Pa67zyPR1LtaOGsiDXE7ZWdru5aRbloIkcpWgD6csBU9bG0fYd"
  "dpdOOwunVT2sJ6JPtt9fWH2hyiUNSLUlonK1s1XtqM95Ch2Ts6ukrHMac/lMg6/xMC+bqkc+FKLl"
  "H060cLfBna24kxx8EYfRVVlmlexkVwZ5ck6aLktOMr0TvbKj0frssFcWZ9ul2Zkyu0ND/ltqhF6j"
  "5QPF1snhb1tlFTesZvoKYN6lGZg0nTxnrDroFeEpqSZ61AESkfvdemrWcE82l47BMWxhKp/h5G/G"
  "UGyF6LoIug9M63ZC77N113Fao1scxVys3krd5zBDHZXdLgNYKbV8gD/9V3KqLBF3fx8uq+Tbnehc"
  "MUp2nW9+2TF3fQTXVNKug99SAyVy10e/pCGNc298+gOCUbhpW3fT7U4mZv4Iidji6VMRm7o2KpmW"
  "VWvlrwFxO35L+Fs/KVReBRdGm0Wua05C+VIHBqBhoJ9HoUyD2OnSHZAQ6aVyyVg+kjkkM2x/ZbzF"
  "r+xhKUZRXEoX/WMs0yHEy0iNTH0Ld3CUb7m0sZEDmTKL7BOhjCOa+lPRKMUvuxSDj8V/BVJ+2C3Q"
  "EHZk5Ry7KQFX5pDyk/S4dlJwzN/qbnOwTCj0qZOqy3vOqbholTMf+eTeArzihPKAjKs9OQf6OwNs"
  "jqt1XnYsoUpgBoQ+AyrxcBrz4p7S+0AurxHHq2yjpsA5xyXpPSeAVzwOADag+rhBWUmWm9VjYe6Q"
  "smtSdjcpm0hleSeRu+bKamKF/YmV8cbZX74yu2tlWfFla7Kba9KH16tJl02fuO0qN6nQhYDt5gZG"
  "c/+5Wrps5vAgH+imzsdqN2Tn3DLR/WfKLudOD3oQeU47kXpq9udBGBqUtmsDEI6eqf5na723Bv7u"
  "PQR/Jeqnrpx5YH4B7KupCbPaHoCD9lg2VKLVyL6TZN8RJ1P8Zam6V++uOZ9dkCQx/gp+G9+uO6Om"
  "LhKkPpKMzP+meCxzQHomhqh5wjKVH4JJlf6pQ/uEDiuO5AXF/jyJVsYHdU9vTOHEXU8YP/bEO4bl"
  "d3QrDzmr4fDVy6Ny3hmphHx0rrWN2qQcWrevoul7uXPvSn5+R5fOmLdyB2i1RI8X2SxaKMVZ0RZU"
  "iz8Q39R1GGp7WuZIz1fx90h0uqmQ1ihx1cfA6VCVl1r6Ut71USrm6vpC07p8cy+gJIXiH0Pz6/9e"
  "OXkX63kqPlWJ2VGoLj3lQoUMMtyOSMSUWkauu6FnktEUKpQOTbnQdLKDG9UxjlHJ1+IBDSmrXk/r"
  "ZJTe75d1LoNVovY1VYXIexrqwwEh4cdy7dXlug+C4ogeZOONBdJqbOxY7OPffhZ9F+S+Z9h0jYYn"
  "RoN80NoUNt1bRSOPebhbR5MXQ8gBHzbG7xQGCzW+VRqU4wttfG3ZpWlXtv2uo9pXiaqSVS2sHWkp"
  "z8JHNLl0LVxSzPlQreCjmmKgnp+SezOFNK131X3NukxEMV+WROsF1xCQL1ixXuNRmb9Ktq36Zuzh"
  "QEz92BxTFUBFnZ2lALrf6KwLUVHkcgC1D/dlriKWTiorA4qdmyChM0s+mYbgSQv64i9+Eoks2fiq"
  "qIDuwhgOrOHoAIm/ZY8eCScMMSjOtPrCrAgZoz7ctRAoZQTaZM1KsLshsORBV5Rlede1+v+OGmHq"
  "aZ/g5hnffsGT2ehe2tedFgfL6s6b2TvE530+Yk0NHmM2LnlzxzpnW093k7Zm1W66jG7P/XQTImer"
  "Snee7/b4ojQF/GXVLs2QALhlpYyqOMLh2/FIIvjen+dnYA6SorvaLF8jgho4GQS2DfzbsmC3SHx/"
  "zZ5wvYA28FZWO0vmOmNvoxd/ym035JXs8tpdeZ185dzQOYmUEh3kc7mQspPTP709Pb4AP66T0om1"
  "vIKfBAuzPGRRt9Qfj4lta73h2d1lEJOboFMOXhvW2aNzHeIljLCHTvXrgFpPXAqOvcjd0HWHvpv4"
  "0PHTkC8/GHuus9466Z4JF7Dt3wZeRinIJb8t/WCxJBf3QkuZc4JZNC787Bi46OegMfT2NLcIqR1R"
  "PzXT2cpZ+HTRl0r6L7R+8g4wX//9zMXejvOZ7lu+2xakVSg2HI16WhRYHmvSjWDtOrB2F1heuZJv"
  "h/TGd1OV7mNt8SarFxasesTxoFFa2Ky5+NBtnNDkPquFqXrWJmY0P3zkUqHKd2jmecQViz2bboyu"
  "onWUxshw9z4ziy4l1mDao8ZEz0rw5BPWwZOxCJ2ZH7LOpcI6HHwtDJ7bfvjLf/3NHvSkNtojerOp"
  "Xkz4SoZHkHvjV1c3/FVgIUtfg8+E7tukEatoZYPQQDqq5I9kouqii67HtVTD0gLpByXJYubw1tpP"
  "Bj381x+NTPqNiWwY9KjpyUh9V74PEpT8TTPAFEshrJso+b9URmC3R5wDRoy0T372EayuX8gH+5D+"
  "Nyvh1WIjg4YBYSfUWgBhTnIjZgveKpiH51DBIaNys5ghBV6N9epBThs9ZTAxeJcaDjy7HSpDW/lO"
  "ukn8CzJGDDKlHet9Qz6n63OBEzsX8jkiL8Ae1MukzKIUSylDKdhHuvyoG8si5LsQTM4S3/SYo306"
  "iLVts5tqtXVfzeezh4MB79ZXg8F8Phq1ZihXA/I5TWE2XY+U8lHpDjDvHh/678nfyUi9L++uUyN/"
  "2Nuxokc7VlRKt0O4PKkm3s8LrSkwaMsjEhNtA8Q0fGh2UvlqMB9oQ+upIdlhPWwLvcSIvpum1IVG"
  "MmdjezD4euIFaRw6xRhq5d5MZo57s+BLAmOIezCZcYJiJY4XbNIxhDCR8aaVRTG97qkZAkLpPThk"
  "6Yw1AVGhTXMr8AfKp3xbnHmGNsQsS7kYYdKwfuJzVe4Svtpwt4AOP4QX/Bdj7zZx4j2z78R0G/94"
  "GYQetyM2cNJi7YoqQkhhXcd8tsf3istY4Pz04uwcEFJ6z9FY3Qgju+ZBwplFiNsM6ZPpRxfHl8cn"
  "p8flBbh94agLCvx5SkdekExBNWhm2pOuWn22+KyICPfFD3RaBf/riHVkRTG++mGoAoKIw0Rskp+s"
  "+bJJGMKde/4CGwA+8Mf16WpHIYCJS59+3OWsGQlvUwLAbRRQuOre+5u4Wwo1aKflL9TiJZ1f8akS"
  "nRmlIYebgWfxz85MfXxbtIn/HkFcdgmChhRslhRaIrtHU73ETHsUt6ydbbAgGK/D/vKnUmU/ykpv"
  "nSCr+/bLpr6azNhLXYq+9qqSVxgt5ExyUY77fhMgvqo7tGfpO553Sr9QeBnAy6/9xNhL/BC2S78q"
  "NFTlvoM1/j1iezo1kqarsq47qaWtnizdTSyPfhmAUoQwzzdZZNEER6+pyC25vsO2Z4jZDLoBIOmc"
  "8r4xIcItwwfapCndbITb902aHRte2djuErdBGkgPgjgaaU69VmVz1di65zSjn41Qdi+Hh/6e/tO9"
  "WjZHUjpmUyWggvj/wb2ibPy4KfGtcuvIxbXZJVX+9s2bC3H8/OXLKYuPzwRg/eQ1oRQBbMq4OPmL"
  "SDahPxHf2vYTJNBpigTgAQBjFg6BFxSFcjDKmP7tH89enkwepMggjucLYhiAtQb8Xk7pBflCkh0j"
  "ZE4cem2t7emBnPQZnmaRV9DfZbYKn/0/bTDOD1uuAQA="
;
static const unsigned PAGE_GZ_LEN = 32465;

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
static const char PAGE_BUILD[] = "S14P-1917";   // keep in sync with the page BUILD
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
  Serial.println("\n=== poc_survey S14P-1917: AE lock default-off revert + fast master + relaxed guard ===");

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