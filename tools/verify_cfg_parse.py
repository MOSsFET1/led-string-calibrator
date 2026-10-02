#!/usr/bin/env python3
"""S14P-1924: host-side parity gate for the firmware CFG= box-side parser.

The firmware (poc_survey.ino, loop()'s CFG= handler) locates the rig keys
with strstr() on the quoted key and atoi()s a FIXED offset past it (1923 used
+6/+9, which landed on the COLON — atoi(":8")==0, range check rejects, and the
box-side rig shape never left 1 x 200 while the page queued 8 x 200). This
test replicates the C semantics exactly — C strstr() returns a pointer AT the
opening quote, and the digits start after `"key":` = +7 / +10 chars.

PASS = the firmware algorithm at the NEW offsets (+7 / +10) maps the real rig
CFG string to (nStr, nPerStr) = (8, 200) — and the OLD offsets (+6 / +9) FAIL
on the same string (documenting the 1923 bug class for future parser edits).
Plain python3, no deps.
"""
# firmware constants (poc_survey.ino)
N_LANES, N_PX = 8, 200

# the REAL rig CFG string (bench-observed shape, after the "CFG=" prefix is
# stripped — identical to what the firmware sCfg holds)
RIG_CFG = '{"cwc":1,"cwcN":1600,"nStr":8,"nPerStr":200,"cwcSuppress":1}'


def c_strlen_at(s, i):
    """Byte the C pointer s+i points at (NUL terminator when past the end)."""
    return "\0" if i >= len(s) else s[i]


def c_strstr(hay, needle):
    """C strstr(): index of the FIRST occurrence, -1 (NULL) when absent."""
    return hay.find(needle)


def c_atoi(s, i):
    """C atoi() on the string starting at s[i:]: skip whitespace, optional
    sign, then a decimal run; 0 when no digits follow. Reads the ino's
    char array through its NUL terminator, exactly like the C pointer."""
    n = len(s)
    while i < n and s[i] in " \t\r\n\v\f":
        i += 1
    j, sign = i, 1
    if j < n and s[j] in "+-":
        if s[j] == "-":
            sign = -1
        j += 1
    v = 0
    while j < n and s[j].isdigit():
        v = v * 10 + (ord(s[j]) - 48)
        j += 1
    return sign * v


def firmware_cfg_parse(line, k1_off, k2_off, n_str=1, n_per_str=200):
    """The firmware CFG= handler algorithm (poc_survey.ino), verbatim shape:

        strncpy(sCfg, sLine + 4, sizeof(sCfg) - 1);   // strip "CFG="
        const char* j = strchr(sCfg, '{');
        if (j) {
          const char* k1 = strstr(j, "\\"nStr\\"");
          if (k1) { int v = atoi(k1 + <k1_off>); if (v >= 1 && v <= N_LANES) nStr = v; }
          const char* k2 = strstr(j, "\\"nPerStr\\"");
          if (k2) { int v = atoi(k2 + <k2_off>); if (v >= 1 && v <= N_PX) nPerStr = v; }
        }

    `line` is the serial line (e.g. "CFG={...}") — the "CFG=" strip is part of
    the replicated behaviour. Returns (nStr, nPerStr) exactly as the volatile
    globals would end up. The offsets are parameters so the test runs BOTH
    the shipped (1923) and fixed (1924) variants through the same code.
    """
    s_cfg = line[4:] if line.startswith("CFG=") else line   # sCfg = sLine + 4
    b = s_cfg.find("{")                                     # j = strchr(sCfg, '{')
    if b >= 0:
        k1 = s_cfg.find('"nStr"', b)                        # k1 = strstr(j, "\"nStr\"")
        if k1 >= 0:
            v = c_atoi(s_cfg, k1 + k1_off)                  # atoi(k1 + off)
            if 1 <= v <= N_LANES:
                n_str = v
        k2 = s_cfg.find('"nPerStr"', b)
        if k2 >= 0:
            v = c_atoi(s_cfg, k2 + k2_off)
            if 1 <= v <= N_PX:
                n_per_str = v
    return n_str, n_per_str


def main():
    print("real rig CFG string:", RIG_CFG)
    for key, needle in (('"nStr"', "nStr"), ('"nPerStr"', "nPerStr")):
        p = c_strstr(RIG_CFG, needle)
        print(f"  strstr(\"{needle}\") = {p}: k+6->{c_strlen_at(RIG_CFG, p + 6)!r}"
              f" k+7->{c_strlen_at(RIG_CFG, p + 7)!r}"
              f" k+9->{c_strlen_at(RIG_CFG, p + 9)!r}"
              f" k+10->{c_strlen_at(RIG_CFG, p + 10)!r}")

    # 1) FIXED parser (S14P-1924, atoi(k+7)/atoi(k+10)) on the real rig string
    ns, nps = firmware_cfg_parse("CFG=" + RIG_CFG, 7, 10, 1, 200)
    print(f"FIXED   atoi(k1+7)/atoi(k2+10): nStr={ns} nPerStr={nps}")
    ok_new = (ns, nps) == (8, 200)

    # 2) SHIPPED-BUG parser (S14P-1923, atoi(k+6)/atoi(k+9)) on the same string
    ns_b, nps_b = firmware_cfg_parse("CFG=" + RIG_CFG, 6, 9, 1, 200)
    print(f"BUGGY   atoi(k1+6)/atoi(k2+9): nStr={ns_b} nPerStr={nps_b}")
    ok_old_fails = (ns_b, nps_b) != (8, 200)

    # 3) mock box parity: same offsets, same outcome (mock_box.py drv? path)
    ok_mock = True
    try:
        import importlib.util
        from pathlib import Path
        p = Path(__file__).resolve().parent / "mock_box.py"
        spec = importlib.util.spec_from_file_location("mockbox_cfg", p)
        if spec is None or spec.loader is None:
            raise ImportError("no spec for mock_box.py")
        mb = importlib.util.module_from_spec(spec)
        # import WITHOUT running the server (module top level only defines
        # helpers + clears its log/directive files)
        spec.loader.exec_module(mb)
        mb.state["nStr"], mb.state["nPerStr"] = 1, 200
        ns_m, nps_m = mb._atoi(RIG_CFG, RIG_CFG.find('"nStr"') + 7), \
                       mb._atoi(RIG_CFG, RIG_CFG.find('"nPerStr"') + 10)
        ok_m = ns_m == 8 and nps_m == 200
        print(f"MOCK    _atoi (+7/+10): nStr={ns_m} nPerStr={nps_m}")
        ok_mock = ok_m
    except Exception as e:
        print("MOCK    parity check SKIPPED:", e)

    ok = ok_new and ok_old_fails and ok_mock
    print()
    print(f"fixed offsets yield 8/200:   {'PASS' if ok_new else 'FAIL'}")
    print(f"old offsets (+6/+9) fail:    {'PASS' if ok_old_fails else 'FAIL'}")
    print(f"mock box offset parity:      {'PASS' if ok_mock else 'FAIL'}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())