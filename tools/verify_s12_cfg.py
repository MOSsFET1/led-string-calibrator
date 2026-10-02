#!/usr/bin/env python3
"""S12 standing rule: size every message class at max N against its limit.
This one sizes the CFG=<json> channel against the firmware sCfg buffer.
(S14P-1924: box-side parse offsets verified separately — tools/verify_cfg_parse.py.)

The widest real CFG payload is the page's FULL numeric key set (survey.html
CFG object, 1923 adds nStr/nPerStr) with every value at its 5-digit max /
declared maximum. 320 B sCfg must hold it + '\\0' with slack for future keys.

PASS = max-JSON (chars) + 1 <= sCfg capacity.
"""
import json

# Every page CFG key that applyCfg() accepts (number-typed), at max.
KEYS_MAX = {
    "allB": 65535, "accB": 65535, "evBias": -65535, "mergeR": 65535,
    "bBurstN": 65535, "bBurstGap": 65535, "bBurstHold": 65535, "bBurstB": 65535,
    "bComp": 1, "cwc": 1, "cwcN": 1600, "cwcSettle": 65535,
    "cwcAmpGate": 65535, "cwcMarginGate": 65535, "cwcSuppress": 65535,
    "cwcMaskThr": 65535, "cwcTestMode": 1, "cwcTestLed": 1600,
    "cwcGuardConf": 65535, "cwcGuardRem": 65535, "cwcGuardStep": 65535,
    "cwcNccPeakMargin": 65535, "cwcAeLock": 1,
    "nStr": 8, "nPerStr": 200,
}
MAX_KEYS = 32                      # headroom for future numeric keys
CAPACITIES = {"sCfg_current": 96, "sCfg_after_widen": 640}

worst = json.dumps(KEYS_MAX, separators=(",", ":"))
headroom = json.dumps(dict(KEYS_MAX, **{f"k{i}": 65535 for i in range(MAX_KEYS - len(KEYS_MAX))}),
                      separators=(",", ":"))
worst_n = len(worst)
head_n = len(headroom)
print(f"max-JSON chars ({len(KEYS_MAX)} keys): {worst_n}")
print(f"  + null terminator: {worst_n + 1}")
print(f"  32-key headroom JSON chars: {head_n} (+1 = {head_n + 1})")
for name, cap in zip(CAPACITIES, CAPACITIES.values()):
    ok = worst_n + 1 <= cap
    print(f"  {name} = {cap} B: {'HOLDS' if ok else 'TRUNCATES'}"
          f"{'' if ok else ' (max ' + str(cap - 1) + ' chars)'}")
print("PASS" if worst_n + 1 <= CAPACITIES["sCfg_after_widen"] else "FAIL")