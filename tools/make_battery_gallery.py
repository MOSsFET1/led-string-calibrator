#!/usr/bin/env python3
"""make_battery_gallery.py — build a single-file dark-theme gallery over the
S14R-0003c battery corpus (runs/daemon/runs/run0 + run8..run11).

READ-ONLY on the run dirs. All outputs go under runs/daemon/analysis/gallery/:
  thumbs/<run>/<stem>.jpg   (~220 px wide, JPEG q62)
  index.html                (ONE self-contained page: vanilla JS/CSS, no CDN)

Grouping / global linear order: E5 idle -> E1 (subgrouped by L rung ascending)
-> E2 (same) -> E3 settle jumps -> E4 (subgrouped per epoch run8..run11 with
its L in the header). Within a subgroup items sort by natural numeric k.

Lightbox: click a thumbnail -> full-size image via relative path '../../runs/
<run>/<file>' (works over file://); LEFT/RIGHT arrow buttons and keyboard
ArrowLeft/ArrowRight step prev/next across the WHOLE corpus in the global
order (wraps around, never dumps back to grid); counter 'n / 528'; caption
line with wire label + t + exp; Esc or backdrop click closes; group headers
collapsible; total and per-group counts in the headers.

Run with the PIL venv:
  /home/nellie/.hermes/hermes-agent/venv/bin/python3 tools/make_battery_gallery.py
Options: --force-thumbs  rebuild all thumbnails even when cached.
"""
import argparse
import html
import json
import os
import re
import sys
import time
from pathlib import Path

from PIL import Image

REPO     = Path(__file__).resolve().parent.parent
RUNS     = REPO / 'runs' / 'daemon' / 'runs'
GAL      = REPO / 'runs' / 'daemon' / 'analysis' / 'gallery'
RUN_DIRS = ['run0', 'run8', 'run9', 'run10', 'run11']
E4_L     = {8: 80, 9: 100, 10: 120, 11: 150}     # epoch -> rung L (e4-union report)
E3_SEQ   = ['5to120', '120to5']
THUMB_W  = 220
THUMB_Q  = 62
RS       = getattr(Image, 'Resampling', Image).LANCZOS

SEC_META = [
    ('E5', 'E5 · idle AE trace'),
    ('E1', 'E1 · all-ON L ladder'),
    ('E2', 'E2 · 50%-duty L ladder'),
    ('E3', 'E3 · settle jumps'),
    ('E4', 'E4 · CWC bursts (real 24-plane rigs)'),
]

RX_IDLE = re.compile(r'cal_idle_(\d+)$')
RX_LAD  = re.compile(r'cal_E([12])_L(\d+)_(\d+)$')
RX_E3   = re.compile(r'cal_E3_(.+)_t(\d+)$')
RX_E4   = re.compile(r'cwc_r(\d+)_(p(\d+)|master)$')


def load_meta(jpg):
    mp = jpg.with_name(jpg.stem + '.meta.json')
    if not mp.exists():
        return {}, True
    try:
        return json.loads(mp.read_text(encoding='utf-8')), False
    except Exception:
        return {}, True


def mk(jpg, sec, sub, k, cap, anomalies):
    meta, missing = load_meta(jpg)
    if missing:
        anomalies.append('meta missing/unreadable: %s/%s' % (jpg.parent.name, jpg.name))
    return dict(
        run=jpg.parent.name, file=jpg.name, stem=jpg.stem,
        label=str(meta.get('label', jpg.stem)),
        t=str(meta.get('t', '?')),
        exp=str(meta.get('exp', '?')),
        W=int(meta.get('W') or 406), H=int(meta.get('H') or 720),
        sec=sec, sub=sub, k=k, cap=cap,
    )


def scan_corpus(anomalies):
    buckets = {}
    dims = set()
    for run in RUN_DIRS:
        d = RUNS / run
        if not d.is_dir():
            sys.exit('missing run dir: %s' % d)
        for jpg in sorted(d.glob('*.jpg')):
            stem = jpg.stem
            m = RX_IDLE.fullmatch(stem)
            if m:
                k = int(m.group(1))
                rec = mk(jpg, 'E5', None, k, '%02d' % k, anomalies)
            else:
                m = RX_LAD.fullmatch(stem)
                if m:
                    sec, L, k = 'E%s' % m.group(1), int(m.group(2)), int(m.group(3))
                    rec = mk(jpg, sec, 'L=%d' % L, (L, k), 'k=%d' % k, anomalies)
                else:
                    m = RX_E3.fullmatch(stem)
                    if m:
                        seq, t = m.group(1), int(m.group(2))
                        if seq not in E3_SEQ:
                            sys.exit('unknown E3 sequence in stem: %s' % stem)
                        rec = mk(jpg, 'E3', None, (E3_SEQ.index(seq), t),
                                 '%s t%03d' % (seq, t), anomalies)
                    else:
                        m = RX_E4.fullmatch(stem)
                        if m:
                            n, p = int(m.group(1)), m.group(2)
                            if n not in E4_L:
                                sys.exit('unknown CWC epoch run%d — extend E4_L' % n)
                            if p == 'master':
                                rec = mk(jpg, 'E4', 'epoch %s · L=%d' % (jpg.parent.name, E4_L[n]),
                                         1000, 'master', anomalies)
                            else:
                                k = int(m.group(3))
                                rec = mk(jpg, 'E4', 'epoch %s · L=%d' % (jpg.parent.name, E4_L[n]),
                                         k, 'p%02d' % k, anomalies)
                        else:
                            sys.exit('unmatched stem %s/%s — extend the regexes' % (run, stem))
            buckets.setdefault((rec['sec'], rec['sub']), []).append(rec)
            dims.add((rec['W'], rec['H']))
    return buckets, dims


def order_sections(buckets):
    subs_by_sec = {}
    for sec_id, _ in SEC_META:
        keys = [k for k in buckets if k[0] == sec_id]
        if not keys:
            sys.exit('corpus missing an expected section: %s' % sec_id)
        if sec_id in ('E5', 'E3'):
            subs_by_sec[sec_id] = [{'label': None,
                                    'items': sorted(buckets[(sec_id, None)], key=lambda r: r['k'])}]
        elif sec_id in ('E1', 'E2'):
            subs = []
            for key in sorted(keys, key=lambda k: int(k[1].split('=')[1])):
                subs.append({'label': key[1], 'items': sorted(buckets[key], key=lambda r: r['k'])})
            subs_by_sec[sec_id] = subs
        else:  # E4 epochs in run order
            subs = []
            for run in ('run8', 'run9', 'run10', 'run11'):
                n = int(run[3:])
                key = ('E4', 'epoch %s · L=%d' % (run, E4_L[n]))
                if key not in buckets:
                    sys.exit('missing E4 epoch %s' % run)
                subs.append({'label': 'epoch %s · L=%d' % (run, E4_L[n]),
                             'items': sorted(buckets[key], key=lambda r: r['k'])})
            subs_by_sec[sec_id] = subs
    return subs_by_sec


def note_for(sec_id, subs):
    if sec_id == 'E1':
        ls = [s['label'][2:] for s in subs]
        per = sorted({len(s['items']) for s in subs})
        return 'rungs L = %s · %s snaps per rung' % (', '.join(ls),
                                                     per[0] if len(per) == 1 else '/'.join(map(str, per)))
    if sec_id == 'E2':
        ls = [s['label'][2:] for s in subs]
        per = sorted({len(s['items']) for s in subs})
        return 'duty 0.5 rungs L = %s · %s snaps per rung' % (', '.join(ls),
                                                              per[0] if len(per) == 1 else '/'.join(map(str, per)))
    if sec_id == 'E3':
        return 'L 5↔120 fine traces · 5to120 then 120to5 · t000..t003 each'
    if sec_id == 'E4':
        return ' · '.join('%s↔L%s' % (s['label'].split(' · ')[0], s['label'].split('L=')[1])
                          for s in subs) + ' · master + p00..p23 per epoch'
    return '60 frames · exp readback 799.98'


def cell_html(it):
    return ('<div class="cell thumb" data-i="%d" title="%s">'
            '<img src="%s" alt="%s" loading="lazy" decoding="async">'
            '<div class="cap">%s</div></div>' % (
                it['i'], html.escape(it['label'], quote=True), it['thumb_rel'],
                html.escape(it['file'], quote=True), html.escape(it['cap'])))


def section_html(sec_id, title, subs):
    total = sum(len(s['items']) for s in subs)
    out = ['<section id="sec-%s" data-total="%d">' % (sec_id, total)]
    out.append('<header role="button" tabindex="0" aria-expanded="true">'
               '<span class="chev">\u25be</span>'
               '<span class="sec-title">%s</span>'
               '<span class="sec-note">%s</span>'
               '<span class="sec-count">%d</span></header>'
               % (html.escape(title), html.escape(note_for(sec_id, subs)), total))
    out.append('<div class="sec-body">')
    for s in subs:
        out.append('<div class="sub">')
        if s['label']:
            out.append('<h3>%s <span class="sub-count">\u00b7 %d</span></h3>'
                       % (html.escape(s['label']), len(s['items'])))
        out.append('<div class="grid">')
        out.extend(cell_html(it) for it in s['items'])
        out.append('</div></div>')
    out.append('</div></section>')
    return ''.join(out)


HTML_TMPL = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>S14R-0003c battery corpus \u00b7 gallery</title>
<style>
:root{--bg:#0d1117;--panel:#161b22;--fg:#d8dee6;--dim:#8a93a2;--acc:#4aa3ff;--line:#232b38;--hdr:#1a212c}
*{box-sizing:border-box}
html,body{margin:0}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;padding-bottom:70px}
body.noscroll{overflow:hidden}
header.page{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 20px 8px}
header.page h1{margin:0;font-size:16px;font-weight:600}
header.page .count-total{color:var(--dim);font-size:12px;margin-top:3px}
main{padding:4px 0}
section{margin:16px 20px}
section>header{display:flex;align-items:baseline;gap:10px;background:var(--hdr);border:1px solid var(--line);border-radius:8px;padding:9px 14px;cursor:pointer;user-select:none;outline:none}
section>header:hover,section>header:focus{border-color:var(--acc)}
.chev{color:var(--acc);width:14px;flex:none}
.sec-title{font-weight:600;font-size:14px}
.sec-note{color:var(--dim);font-size:12px;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sec-count{color:var(--acc);font-size:13px;font-variant-numeric:tabular-nums}
section.collapsed .sec-body{display:none}
.sec-body{margin:12px 0 4px}
.sub{margin:0 0 20px}
.sub h3{margin:0 0 8px;font-size:12.5px;font-weight:600;color:var(--acc);border-bottom:1px solid var(--line);padding-bottom:4px}
.sub-count{color:var(--dim);font-weight:400}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(132px,1fr));gap:10px}
.cell{background:var(--panel);border:1px solid var(--line);border-radius:6px;overflow:hidden;cursor:zoom-in}
.cell:hover{border-color:var(--acc)}
.cell img{display:block;width:100%;aspect-ratio:406/720;object-fit:cover;background:#04070b}
.cap{font-size:11px;color:var(--dim);padding:3px 6px 4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-variant-numeric:tabular-nums}
#lightbox{display:none;position:fixed;inset:0;z-index:50;background:rgba(4,7,11,.95)}
#lightbox.open{display:block}
.lb-stage{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;padding:36px 82px 84px}
#lbImg{max-width:100%;max-height:calc(100vh - 120px);border:1px solid #2a3342;border-radius:4px;box-shadow:0 10px 44px rgba(0,0,0,.65);background:#000}
.lb-arrow{position:fixed;top:50%;transform:translateY(-50%);z-index:52;width:54px;height:72px;font-size:20px;color:var(--fg);background:rgba(22,27,34,.87);border:1px solid #2a3342;border-radius:10px;cursor:pointer}
.lb-arrow:hover{color:var(--acc);border-color:var(--acc)}
#lbPrev{left:16px}
#lbNext{right:16px}
.lb-counter{position:fixed;top:14px;left:50%;transform:translateX(-50%);z-index:52;background:rgba(22,27,34,.88);border:1px solid #2a3342;border-radius:16px;padding:6px 16px;font-size:13px;font-variant-numeric:tabular-nums}
.lb-x{position:fixed;top:12px;right:16px;z-index:52;height:36px;padding:0 12px;font-size:14px;color:var(--fg);background:rgba(22,27,34,.87);border:1px solid #2a3342;border-radius:8px;cursor:pointer}
.lb-x:hover{color:#ff7b7b;border-color:#ff7b7b}
.lb-x .k{color:var(--dim);font-size:11px;margin-left:5px}
.lb-caption{position:fixed;left:0;right:0;bottom:0;z-index:52;padding:26px 84px 14px;text-align:center;font-size:13px;background:linear-gradient(transparent,rgba(4,7,11,.93) 45%)}
.lb-caption b{color:var(--acc)}
.lb-caption .lb-meta{color:var(--dim)}
</style>
</head>
<body>
<header class="page">
<h1>S14R-0003c calibration battery \u00b7 image corpus gallery</h1>
<div class="count-total">@@TOTAL@@ images (run0 + run8..run11) \u00b7 generated @@STAMP@@ \u00b7 dark bench theme \u00b7 click a thumbnail for full size \u00b7 \u2190/\u2192 step \u00b7 Esc closes</div>
</header>
<main>
@@BODY@@
</main>
<div id="lightbox">
<div class="lb-counter" id="lbCounter">\u2013 / @@TOTAL@@</div>
<button class="lb-x" id="lbClose" title="Close (Esc)">\u2715<span class="k">Esc</span></button>
<button class="lb-arrow" id="lbPrev" title="Previous (\u2190)">\u25c0</button>
<button class="lb-arrow" id="lbNext" title="Next (\u2192)">\u25b6</button>
<div class="lb-stage" id="lbStage"><img id="lbImg" alt="" draggable="false"></div>
<div class="lb-caption" id="lbCaption"></div>
</div>
<script>
@@JS@@
</script>
</body>
</html>
"""

JS_TMPL = r"""(function () {
  var IMGS = @@IMGS@@;                     /* [{r:run, f:file, lb:wire label, t:t, e:exp}] */
  var N = IMGS.length;
  var FULLBASE = '../' + '../runs/';       /* index.html sits at runs/daemon/analysis/gallery/ */
  var lb = document.getElementById('lightbox');
  var img = document.getElementById('lbImg');
  var cap = document.getElementById('lbCaption');
  var cnt = document.getElementById('lbCounter');
  var cur = -1;
  function full(i) { var it = IMGS[i]; return FULLBASE + it.r + '/' + it.f; }
  function openAt(i) {
    cur = ((i % N) + N) % N;               /* wraps: arrows never dump back to the grid */
    var it = IMGS[cur];
    img.src = full(cur);
    img.alt = it.lb;
    cnt.textContent = (cur + 1) + ' / ' + N;
    while (cap.firstChild) cap.removeChild(cap.firstChild);
    var b = document.createElement('b');
    b.textContent = it.lb;
    cap.appendChild(b);
    var s = document.createElement('span');
    s.className = 'lb-meta';
    s.textContent = '  \u00b7  t=' + it.t + '  \u00b7  ' + it.e + '  \u00b7  ' + it.r + '/' + it.f;
    cap.appendChild(s);
    if (!lb.classList.contains('open')) {
      lb.classList.add('open');
      document.body.classList.add('noscroll');
    }
  }
  function closeLb() {
    lb.classList.remove('open');
    document.body.classList.remove('noscroll');
    cur = -1;
  }
  function nav(d) { openAt(cur + d); }
  document.addEventListener('click', function (ev) {
    var t = ev.target.closest ? ev.target.closest('.thumb') : null;
    if (t) { openAt(parseInt(t.getAttribute('data-i'), 10)); return; }
    if (ev.target === lb || ev.target === document.getElementById('lbStage')) closeLb();
  });
  document.getElementById('lbPrev').addEventListener('click', function (ev) { ev.stopPropagation(); nav(-1); });
  document.getElementById('lbNext').addEventListener('click', function (ev) { ev.stopPropagation(); nav(1); });
  document.getElementById('lbClose').addEventListener('click', function (ev) { ev.stopPropagation(); closeLb(); });
  window.addEventListener('keydown', function (ev) {
    if (!lb.classList.contains('open')) return;
    if (ev.key === 'ArrowRight') { ev.preventDefault(); nav(1); }
    else if (ev.key === 'ArrowLeft') { ev.preventDefault(); nav(-1); }
    else if (ev.key === 'Escape') { ev.preventDefault(); closeLb(); }
  });
  Array.prototype.forEach.call(document.querySelectorAll('section > header'), function (h) {
    function toggle() {
      var sec = h.parentElement;
      var collapsed = sec.classList.toggle('collapsed');
      h.querySelector('.chev').textContent = collapsed ? '\u25b8' : '\u25be';
      h.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
    }
    h.addEventListener('click', toggle);
    h.addEventListener('keydown', function (ev) {
      if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); toggle(); }
    });
  });
  img.addEventListener('load', function () {  /* preload the two neighbours */
    var nx = new Image(); nx.src = full((cur + 1) % N);
    var pv = new Image(); pv.src = full((cur - 1 + N) % N);
  });
})();
"""


def make_thumb(it, force, failures):
    src = RUNS / it['run'] / it['file']
    tdir = GAL / 'thumbs' / it['run']
    tdir.mkdir(parents=True, exist_ok=True)
    dst = tdir / (it['stem'] + '.jpg')
    if not force and dst.exists() and dst.stat().st_size > 0 \
            and dst.stat().st_mtime >= src.stat().st_mtime:
        return 'cached'
    try:
        im = Image.open(src)
        im.load()
        w, h = im.size
        if w > THUMB_W:
            im = im.resize((THUMB_W, max(1, round(h * THUMB_W / w))), RS)
        im = im.convert('RGB')
        im.save(dst, 'JPEG', quality=THUMB_Q, optimize=True)
        return 'built'
    except Exception as exc:
        failures.append('%s/%s: %s' % (it['run'], it['file'], exc))
        return 'failed'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--force-thumbs', action='store_true',
                    help='rebuild every thumbnail even when a cached one exists')
    args = ap.parse_args()

    anomalies = []
    buckets, dims = scan_corpus(anomalies)
    if len(dims) > 1:
        anomalies.append('non-uniform image dims in metas: %s' % sorted(dims))

    subs_by_sec = order_sections(buckets)
    flat = []
    for sec_id, _ in SEC_META:
        for s in subs_by_sec[sec_id]:
            for it in s['items']:
                it['i'] = len(flat)
                it['thumb_rel'] = 'thumbs/%s/%s.jpg' % (it['run'], it['stem'])
                flat.append(it)
    total = len(flat)
    if total == 0:
        sys.exit('no items found — check RUN_DIRS / regexes')

    if os.environ.get('GALLERY_VERBOSE', ''):
        for it in flat[:3]:
            print('sample:', it)
    failures = []
    built = cached = 0
    for n, it in enumerate(flat):
        res = make_thumb(it, args.force_thumbs, failures)
        if res == 'built':
            built += 1
        elif res == 'cached':
            cached += 1
        if failures and (n + 1) % 50 == 0:
            pass
    if failures:
        anomalies.extend('thumbnail decode failed: ' + f for f in failures)

    imgs_compact = [{'r': it['run'], 'f': it['file'], 'lb': it['label'],
                     't': it['t'], 'e': it['exp']} for it in flat]
    js = JS_TMPL.replace('@@IMGS@@', json.dumps(imgs_compact, separators=(',', ':')))
    body = ''.join(section_html(sec_id, title, subs_by_sec[sec_id]) for sec_id, title in SEC_META)
    page = (HTML_TMPL
            .replace('@@TOTAL@@', str(total))
            .replace('@@STAMP@@', time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime()))
            .replace('@@BODY@@', body)
            .replace('@@JS@@', js))
    if '@@' in page:
        sys.exit('unsubstituted template token left in page')

    GAL.mkdir(parents=True, exist_ok=True)
    out = GAL / 'index.html'
    out.write_text(page, encoding='utf-8')

    got = sorted('%s/%s' % (p.parent.name, p.name)
                 for p in (GAL / 'thumbs').glob('*/*.jpg'))
    want = sorted('%s/%s.jpg' % (it['run'], it['stem']) for it in flat)
    if got != want:
        missing = sorted(set(want) - set(got))
        extra = sorted(set(got) - set(want))
        anomalies.append('thumb set mismatch: missing=%d extra=%d' % (len(missing), len(extra)))
        if missing[:5]:
            anomalies.append('thumb missing e.g. %s' % missing[:5])
        if extra[:5]:
            anomalies.append('thumb extra e.g. %s' % extra[:5])

    print('scan: ' + ' '.join('%s=%d' % (sid, sum(len(s['items']) for s in subs_by_sec[sid]))
                              for sid, _ in SEC_META) + ' → total %d' % total)
    for sid, _ in SEC_META:
        print('  %s: %s' % (sid, ', '.join('%s(%d)' % (s['label'] or '—', len(s['items']))
                                           for s in subs_by_sec[sid])))
    print('thumbs: built=%d cached=%d failed=%d (width %d, q%d)'
          % (built, cached, len(failures), THUMB_W, THUMB_Q))
    print('html: %s (%d KB) with %d lightbox entries'
          % (out, out.stat().st_size // 1024, page.count('class="cell thumb"')))
    if anomalies:
        print('ANOMALIES:')
        for a in anomalies:
            print('  - %s' % a)
    else:
        print('anomalies: none')


if __name__ == '__main__':
    main()