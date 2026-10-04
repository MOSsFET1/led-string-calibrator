#!/usr/bin/env python3
"""PASS 2: in-place repair + full verification + manifest (run0, run8..run11).

- healthy iff some complete wire group's payload == disk bytes exactly
- repair iff disk bytes are a strict prefix of EXACTLY ONE wire group for the
  label (prefix-provenance); overwrite in place
- verify: EOI tail, plain PIL decode (no LOAD_TRUNCATED_IMAGES), W/H == meta,
  meta.json consistency vs wire meta
- writes manifest.json + idle_provenance.json; never touches capture.txt
"""
import base64  # noqa: F401  (kept for symmetry with pass1)
import hashlib
import json
import pickle
import time
from pathlib import Path

from PIL import Image

REPO = Path('/home/nellie/projects/led-display/poc_survey')
RUNS = REPO / 'runs/daemon/runs'
WRK = REPO / 'runs/daemon/analysis/run_repair'
CAP = REPO / 'runs/daemon/capture.txt'
DIRS = ['run0', 'run8', 'run9', 'run10', 'run11']

groups = pickle.loads((WRK / 'wire_groups.pkl').read_bytes())

# ---- host stamps for every line (cheap map) -------------------------------
stamps = {}
with open(CAP, 'rb') as f:
    for ln, raw in enumerate(f, 1):
        if raw[:1] == b'[':
            stamps[ln] = raw.split(b'] ', 1)[0].decode()[1:]

def ref_stamp(ref):
    ln = int(ref.rsplit(':', 1)[1])
    return stamps.get(ln, '?')

# ---- per-file processing ---------------------------------------------------
manifest_files = []
dir_counts = {d: {'total': 0, 'healthy': 0, 'repaired': 0, 'unresolvable': 0}
              for d in DIRS}
idle_prov = {}

def sha(b):
    return hashlib.sha256(b).hexdigest()

for d in DIRS:
    for jpg in sorted((RUNS / d).glob('*.jpg')):
        mf = jpg.with_name(jpg.stem + '.meta.json')
        dm = json.loads(mf.read_text())
        lab = dm['label']
        disk = jpg.read_bytes()
        rec = {'dir': d, 'file': jpg.name, 'label': lab,
               'old_size': len(disk), 'sha_before': sha(disk),
               'verdict': None, 'new_size': len(disk), 'sha_after': sha(disk),
               'wire_ref': None, 'wire_stamp': None, 'wire_meta_t': None,
               'meta_check': None, 'decode_ok': None, 'eoi_ok': None,
               'wire_exact_groups': None}
        cands = groups.get(lab, [])
        exact = [c for c in cands if c[1] == disk]
        rec['wire_exact_groups'] = [c[0] for c in exact]

        def decode_check(path_or_bytes, meta):
            try:
                im = Image.open(path_or_bytes)
                im.load()
                wh_ok = (im.width == meta['W'] and im.height == meta['H'])
                return True, wh_ok, f'{im.width}x{im.height}'
            except Exception as e:
                return False, False, str(e)[:80]

        if exact:
            verdict = 'healthy'
            # meta consistency: any exact group's meta == disk meta?
            metas_ok = [c for c in exact
                        if all(c[2].get(k) == dm.get(k) for k in
                               ('label', 'W', 'H', 'exp', 't'))]
            rec['meta_check'] = ('match' if metas_ok
                                 else 'wire-meta-differs(bytes match)')
            rec['wire_ref'] = exact[0][0]
            rec['wire_stamp'] = ref_stamp(exact[0][0])
            rec['wire_meta_t'] = exact[0][2].get('t')
            ok, wh_ok, info = decode_check(disk, dm)
            rec['decode_ok'], rec['eoi_ok'] = ok, disk.endswith(b'\xff\xd9')
            if not (ok and wh_ok and rec['eoi_ok']):
                rec['note'] = f'healthy-bytes but decode issue: {info}'
            if lab.startswith('cal:idle'):
                idle_prov.setdefault(lab, []).append(
                    {'file': f'{d}/{jpg.name}', 'exact_groups': len(exact),
                     'ts': sorted({c[2].get('t') for c in exact})})
        else:
            pfx = [c for c in cands
                   if len(c[1]) > len(disk) and c[1].startswith(disk)]
            if len(pfx) == 1:
                ref, payload, wmeta = pfx[0]
                jpg.write_bytes(payload)
                nb = jpg.read_bytes()
                assert nb == payload, 'write/reread mismatch'
                rec.update(verdict='repaired', new_size=len(payload),
                           sha_after=sha(payload), wire_ref=ref,
                           wire_stamp=ref_stamp(ref),
                           wire_meta_t=wmeta.get('t'))
                metas_ok = all(wmeta.get(k) == dm.get(k) for k in
                               ('label', 'W', 'H', 'exp', 't'))
                rec['meta_check'] = ('match' if metas_ok
                                     else 'wire-meta-differs')
                ok, wh_ok, info = decode_check(jpg, dm)
                rec['decode_ok'], rec['eoi_ok'] = ok, nb.endswith(b'\xff\xd9')
                rec['added_bytes'] = len(payload) - len(disk)
                if ok and wh_ok and nb.endswith(b'\xff\xd9'):
                    rec['verdict'] = 'repaired'
                else:
                    rec['verdict'] = 'unresolvable'
                    rec['note'] = f'post-repair verify failed: {info}'
                print(f'REPAIRED {d}/{jpg.name} <- {ref} [{ref_stamp(ref)}] '
                      f'+{rec["added_bytes"]}B decode_ok={ok} wh_ok={wh_ok} '
                      f'eoi={rec["eoi_ok"]} meta={rec["meta_check"]}')
            else:
                rec['verdict'] = 'unresolvable'
                rec['note'] = (f'{len(pfx)} strictly-longer prefix candidates '
                               f'among {len(cands)} groups')
                print(f'UNRESOLVABLE {d}/{jpg.name}: {rec["note"]}')
        if rec['verdict'] is None:
            rec['verdict'] = 'healthy'
            if rec.get('note'):
                print(f'NOTE {d}/{jpg.name}: {rec["note"]}')
        manifest_files.append(rec)
        dir_counts[d][rec['verdict']] += 1
        dir_counts[d]['total'] += 1

# ---- final EOI sweep over all files ----------------------------------------
sweep_bad = []
for d in DIRS:
    for jpg in sorted((RUNS / d).glob('*.jpg')):
        b = jpg.read_bytes()
        dm = json.loads(jpg.with_name(jpg.stem + '.meta.json').read_text())
        eoi = b.endswith(b'\xff\xd9')
        try:
            im = Image.open(jpg)
            im.load()
            ok = True
            wh = (im.width, im.height) == (dm['W'], dm['H'])
        except Exception:
            ok, wh = False, False
        if not (eoi and ok and wh):
            sweep_bad.append((d, jpg.name, eoi, ok, wh))
print('EOI/decode/W-H sweep failures:', len(sweep_bad), sweep_bad)

counts_line = ' '.join(
    f"{d}: {dir_counts[d]['healthy']}H/{dir_counts[d]['repaired']}R/"
    f"{dir_counts[d]['unresolvable']}U/{dir_counts[d]['total']}T"
    for d in DIRS)
print('COUNTS', counts_line)

# ---- capture fingerprint + manifest ----------------------------------------
cap_sha = hashlib.sha256(CAP.read_bytes()).hexdigest()
incomplete = json.loads((WRK / 'pass1_stats.json').read_text()) \
    if (WRK / 'pass1_stats.json').exists() else None
manifest = {
    'generated': time.strftime('%Y-%m-%d %H:%M:%S%z'),
    'capture': {'path': str(CAP), 'sha256': cap_sha, 'size': CAP.stat().st_size,
                'note': 'append-only wire log; parsed groups=888 complete / '
                        '3 incomplete (dropped)'},
    'method': 'label->wire-groups from ALL complete FRAME/FJPEG/FEND groups; '
              'healthy iff disk bytes == some complete wire group exactly; '
              'repair iff disk is strict prefix of exactly one wire group '
              '(prefix-provenance), overwritten in place',
    'dirs': dir_counts,
    'eoi_sweep_failures': sweep_bad,
    'capture_incomplete_groups': [
        {'ref': 'capture.txt:2024', 'label': 'cwc:r2:p01',
         'stamp': ref_stamp('capture.txt:2024'),
         'note': 'non-target run2 frame; cut by [PHONE-LOG] end'},
        {'ref': 'capture.txt:40592', 'label': 'cal:idle:10',
         'stamp': ref_stamp('capture.txt:40592'),
         'note': 'morning re-push (08:49:33) cut mid-b64; complete idle:10 '
                 'groups exist (disk file healthy / exact-match)'},
        {'ref': 'capture.txt:45865', 'label': 'cal:idle:08',
         'stamp': ref_stamp('capture.txt:45865'),
         'note': 'pre-leg re-push (09:38:21) cut mid-b64; complete idle:08 '
                 'groups exist (disk file healthy / exact-match)'},
    ],
    'files': manifest_files,
}
(WRK / 'manifest.json').write_text(json.dumps(manifest, indent=1))
(WRK / 'idle_provenance.json').write_text(json.dumps(idle_prov, indent=1))
print('wrote manifest.json, idle_provenance.json; capture sha256', cap_sha[:16])