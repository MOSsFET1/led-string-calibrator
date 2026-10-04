// Offline validation of the S14R-0004 page tear-guard port:
//  - part 1: single-frame robust rule over the 200-frame corpus (4 torn,
//    196 clean) — expect FP 0 / FN 0 with the paper's firing arms/rows;
//  - part 2: p00 pair-diff behavior on real p00 master frames (grab-vs-grab
//    diff on identical painted content, plus a control pair of genuinely
//    different planes). Live rig calibration of the pair rule happens on the
//    next battery; this only characterises the metric.
// Drives the PAGE'S OWN FUNCTIONS (extracted verbatim from page/survey.html).
const fs = require('fs');
const CORPUS = process.argv[2] || '/tmp/tearguard_corpus';
const pageSrc = fs.readFileSync(require('path').join(__dirname, 'page_impl_port.js'), 'utf8');
let W = 0, H = 0;   // page module reads module-scope W/H - set per frame
eval(pageSrc.replace('let tearStats', 'var tearStats'));

function scanRaw(path) {
  const buf = fs.readFileSync(path);
  W = buf.readUInt32LE(0); H = buf.readUInt32LE(4);
  const d = new Uint8ClampedArray(buf.buffer, 8, W * H * 4);
  const r = robustTearScan({ data: d });
  return { tear: r.tear, arm: r.arm, y: r.y, rg: r.rg, bg: r.bg, gr: r.gr, sL: r.sL };
}
function readImg(path) {
  const buf = fs.readFileSync(path);
  const w = buf.readUInt32LE(0), h = buf.readUInt32LE(4);
  return { w, h, data: new Uint8ClampedArray(buf.buffer, 8, w * h * 4) };
}
function pairRaw(pathA, pathB) {
  const A = readImg(pathA), B = readImg(pathB);
  if (A.w !== B.w || A.h !== B.h) throw new Error('dim mismatch');
  W = A.w; H = A.h;
  return robustTearPair(A, B);
}

// ---------- part 1: corpus single-frame rule ----------
const TORN = new Set(['cwc_r8_p07@e700a', 'cwc_r10_p05@e700c', 'cwc_r10_p21@e700c', 'cwc_r12_p15@e300']);
const rows = [];
for (const f of fs.readdirSync(CORPUS).filter(f => f.endsWith('.raw')).sort()) {
  const r = scanRaw('/tmp/tearguard_corpus/' + f);
  r.stem = f.slice(0, -4);
  r.torn = TORN.has(r.stem);
  rows.push(r);
}
// combined rule score: max over arms of (score/threshold); gray counts ONLY
// when both its gates fire (that is how it can fire a tear)
const ratio = r => Math.max(r.rg / 6.0, r.bg / 3.5, (r.gr > 8 && r.sL > 15) ? r.gr / 8.0 : 0);
const clean = rows.filter(r => !r.torn), torn = rows.filter(r => r.torn);
const fp = clean.filter(r => r.tear).length;
const fn = torn.filter(r => !r.tear).length;
const tMin = Math.min(...torn.map(ratio));
const cMax = Math.max(...clean.map(ratio));
console.log('corpus n=' + rows.length + ' (clean ' + clean.length + ', torn ' + torn.length + ')');
console.log('FP=' + fp + '/' + clean.length + '  FN=' + fn + '/' + torn.length);
console.log('clean max combined ratio=' + cMax.toFixed(3) +
            '  tear min combined ratio=' + tMin.toFixed(3) +
            '  margin=' + (tMin / cMax).toFixed(2) + 'x');
for (const r of torn) console.log('  torn:', r.stem.padEnd(20), r.arm, 'y=' + r.y,
                                  'rg', r.rg, 'bg', r.bg, 'gray', r.gr, 'sLVL', r.sL);
const wc = clean.slice().sort((a, b) => ratio(b) - ratio(a))[0];
console.log('  worst clean:', wc.stem, 'combined', ratio(wc).toFixed(3),
            '(rg', wc.rg, 'bg', wc.bg, 'gray', wc.gr, 'sLVL', wc.sL + ')');
for (const tag of ['@e700', '@e300']) {
  const cl = clean.filter(r => r.stem.includes(tag));
  const tr = torn.filter(r => r.stem.includes(tag));
  if (cl.length)
    console.log(tag.slice(1) + ': FP ' + cl.filter(r => r.tear).length + '/' + cl.length +
      (tr.length ? ', FN ' + tr.filter(r => !r.tear).length + '/' + tr.length +
      ', margin ' + (Math.min(...tr.map(ratio)) / Math.max(...cl.map(ratio))).toFixed(2) + 'x' : ''));
}

// ---------- part 2: p00 pair rule on real frames ----------
console.log('');
console.log('p00 pair-diff rule (offline stand-ins for the rig grab-vs-grab):');
const C = CORPUS;
const pairs = [
  ['self r9 p00 vs itself (degenerate clean)', 'cwc_r9_p00@e700b', 'cwc_r9_p00@e700b'],
  ['self r10 p00 vs itself (degenerate clean)', 'cwc_r10_p00@e700c', 'cwc_r10_p00@e700c'],
  ['p00 r9 vs p00 r10 (same paint, two grabs)', 'cwc_r9_p00@e700b', 'cwc_r10_p00@e700c'],
  ['p00 r11 vs p00 r10 (same paint, two grabs)', 'cwc_r11_p00@e700d', 'cwc_r10_p00@e700c'],
  ['p00 r9 exp700 vs p00 r9 exp300 (AE shift)', 'cwc_r9_p00@e700b', 'cwc_r9_p00@e300'],
  ['CONTROL p00 r10 vs p01 r10 (different planes)', 'cwc_r10_p00@e700c', 'cwc_r10_p01@e700c'],
];
for (const [tag, a, b] of pairs) {
  const r = pairRaw(C + '/' + a + '.raw', C + '/' + b + '.raw');
  console.log('  ' + tag.padEnd(48), r.tear ? 'TEAR ' : 'clean', r.arms);
}
