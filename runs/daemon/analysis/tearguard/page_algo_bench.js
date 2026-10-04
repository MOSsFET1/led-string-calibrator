// FINAL validation: page-exact algorithm on ALL 100 frames. Full-column row means,
// 3-tap smoothing, 3 arms {gray>=16, |G-R|>=3.5, |B-G|>=3.5}. Reports FP/FN + clean maxima + timing.
const fs = require('fs');
const frames = JSON.parse(fs.readFileSync('/tmp/tearguard/frames_all.json', 'utf8'));
const T = { gray: 16.0, rg: 3.5, bg: 3.5 };
const TORN = new Set(['cwc_r8_p07', 'cwc_r10_p05', 'cwc_r10_p21']);

function tearCheck(d, w, h) {
  const u32 = new Uint32Array(d.buffer, d.byteOffset, w * h);
  const g = new Float32Array(h), rg = new Float32Array(h), bg = new Float32Array(h);
  let o = 0;
  for (let y = 0; y < h; y++) {
    let sg = 0, srg = 0, sbg = 0;
    for (let x = 0; x < w; x++) {
      const v = u32[o + x];
      const R = v & 255, G = (v >>> 8) & 255, B = (v >>> 16) & 255;
      sg += R + G + B; srg += R - G; sbg += B - G;
    }
    o += w;
    g[y] = sg / (3 * w); rg[y] = srg / w; bg[y] = sbg / w;
  }
  const out = {};
  for (const k of ['gray', 'rg', 'bg']) {
    const v = k === 'gray' ? g : (k === 'rg' ? rg : bg);
    let m = 0, my = -1;
    for (let i = 1; i < h - 2; i++) {
      const d1 = Math.abs((v[i - 1] + 2 * v[i] + v[i + 1]) - (v[i] + 2 * v[i + 1] + v[i + 2])) * 0.25;
      if (d1 > m) { m = d1; my = i; }
    }
    out[k] = m; out[k + '_y'] = my;
  }
  out.tear = out.gray > T.gray || Math.abs(out.rg) > T.rg || Math.abs(out.bg) > T.bg;
  out.tear_y = out.tear ? (Math.abs(out.rg) > T.rg ? out.rg_y : (Math.abs(out.bg) > T.bg ? out.bg_y : out.gray_y)) : -1;
  out.tear_arm = out.tear ? (Math.abs(out.rg) > T.rg ? 'rg' : (Math.abs(out.bg) > T.bg ? 'bg' : 'gray')) : '-';
  out.arms = `${out.gray.toFixed(2)}/${out.rg.toFixed(2)}/${out.bg.toFixed(2)}`;
  return out;
}

let fp = [], fn = [];
const cleanMax = { gray: 0, rg: 0, bg: 0 }, cleanName = {};
const minTear = { gray: 1e9, rg: 1e9, bg: 1e9 };
const res = {};
const t0 = process.hrtime.bigint();
for (const [name, w, h] of frames) {
  const d = new Uint8ClampedArray(fs.readFileSync(`/tmp/tearguard/all/${name}.raw`).buffer);
  const r = tearCheck(d, w, h);
  res[name] = r;
  const torn = TORN.has(name);
  for (const k of ['gray', 'rg', 'bg']) {
    if (!torn) { if (r[k] > cleanMax[k]) { cleanMax[k] = r[k]; cleanName[k] = name; } }
    else { minTear[k] = Math.min(minTear[k], r[k]); }
  }
  if (!torn && r.tear) fp.push(name + '[' + r.arms + ']');
  if (torn && !r.tear) fn.push(name + '[' + r.arms + ']');
}
const t1 = process.hrtime.bigint();
console.log(`n=${frames.length}  FP=${fp.length} ${JSON.stringify(fp)}  FN=${fn.length} ${JSON.stringify(fn)}`);
console.log('clean max:', `gray=${cleanMax.gray.toFixed(2)} (${cleanName.gray})`, `rg=${cleanMax.rg.toFixed(2)} (${cleanName.rg})`, `bg=${cleanMax.bg.toFixed(2)} (${cleanName.bg})`);
console.log('min tear :', `gray=${minTear.gray.toFixed(2)}`, `rg=${minTear.rg.toFixed(2)}`, `bg=${minTear.bg.toFixed(2)}`);
console.log('torn verdicts:');
for (const n of ['cwc_r8_p07', 'cwc_r10_p05', 'cwc_r10_p21'])
  console.log(`  ${n}: ${res[n].arms} arm=${res[n].tear_arm} y=${res[n].tear_y}`);
console.log(`timing: ${(Number(t1 - t0) / 1e6 / frames.length).toFixed(2)} ms/frame full-res (node desktop), 25-frame burst overhead ≈ ${(Number(t1 - t0) / 1e6 * 25).toFixed(0)} ms`);
// per-run max clean D across runs for the report appendix
const perRun = {};
for (const [name] of frames) {
  const run = name.match(/cwc_(r\d+)/)[1];
  if (TORN.has(name)) continue;
  const m = Math.max(res[name].gray / 16, Math.abs(res[name].rg) / 3.5, Math.abs(res[name].bg) / 3.5);
  perRun[run] = perRun[run] && perRun[run][0] > m ? perRun[run] : [m, name];
}
console.log('per-run normalized clean max (max arm / its threshold):', JSON.stringify(Object.fromEntries(Object.entries(perRun).map(([k, v]) => [k, `${v[0].toFixed(2)} @${v[1]}`]))));