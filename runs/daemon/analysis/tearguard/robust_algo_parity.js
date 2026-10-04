// Proposed robust tear check — JS parity harness for robust_detection_proposal.md.
// Input: raw RGBA dumps (getImageData byte order: R,G,B,A per pixel).
const fs = require('fs');
const SKIP = 120, FLOOR = 1.0;
const T = { rg: 6.0, bg: 3.5, grayJ: 8.0, grayS: 15.0 };

function medianF32(a) {            // numeric median of Float32Array (copies)
  const b = Float32Array.from(a); b.sort();
  const n = b.length;
  return n & 1 ? b[(n - 1) >> 1] : 0.5 * (b[n >> 1] + b[(n >> 1) - 1]);
}

function robustTearCheck(d, w, h) {
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
  // arm(): page-exact 0003e jump series, i in [1, h-3]; jump j_i straddles rows i..i+1
  function jumps(v) {
    const j = new Float32Array(h - 3);
    for (let i = 1; i < h - 2; i++)
      j[i - 1] = 0.25 * Math.abs((v[i - 1] + 2 * v[i] + v[i + 1]) - (v[i] + 2 * v[i + 1] + v[i + 2]));
    return j;
  }
  const jG = jumps(g), jR = jumps(rg), jB = jumps(bg);
  const lo = SKIP - 1;                       // index 0 of j == row pair (1,2); lo keeps rows >= SKIP
  function scan(j) {                         // returns {max, i, mad} over scanned range
    let m = 0, mi = -1; const arr = [];
    for (let k = lo; k < j.length; k++) { const a = j[k]; arr.push(a); if (a > m) { m = a; mi = k; } }
    const med = medianF32(arr);
    const dev = Float32Array.from(arr, a => Math.abs(a - med));
    const mad = 1.4826 * medianF32(dev);
    return { max: m, i: mi, mad };
  }
  const sG = scan(jG), sR = scan(jR), sB = scan(jB);
  // gray level shift at the gray-jump candidate row yc (= row i+1)
  const yc = sG.i + 1;
  const seg = (a, b) => { const t = []; for (let y = a; y < b; y++) t.push(g[y]); return t; };
  const top = seg(Math.max(0, yc - 60), Math.max(0, yc - 20));
  const bot = seg(Math.min(h, yc + 20), Math.min(h, yc + 60));
  const shift = Math.abs(medianF32(bot) - medianF32(top));
  const devV = Float32Array.from(jG.slice(lo), a => a);   // gray jump MAD == vmad
  const vmad = sG.mad;
  const sLVL = shift / (vmad + FLOOR);
  const rgR = sR.max / (sR.mad + FLOOR);
  const bgR = sB.max / (sB.mad + FLOOR);
  const gR = sG.max / (sG.mad + FLOOR);
  const tear = rgR > T.rg || bgR > T.bg || (gR > T.grayJ && sLVL > T.grayS);
  return { tear, rgR, bgR, gR, sLVL, yc,
           arms: `rg ${rgR.toFixed(2)} bg ${bgR.toFixed(2)} gray ${gR.toFixed(2)} sLVL ${sLVL.toFixed(1)}` };
}

const files = process.argv.slice(2);
for (const f of files) {
  const buf = fs.readFileSync(f);
  const w = buf.readUInt32LE(0), h = buf.readUInt32LE(4);
  const d = new Uint8ClampedArray(buf.buffer, 8, w * h * 4);
  const r = robustTearCheck(d, w, h);
  console.log(f.split('/').pop().padEnd(24), r.tear ? 'TEAR ' : 'clean', r.arms);
}