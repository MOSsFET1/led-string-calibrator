const TEAR_SKIP = 120, TEAR_FLOOR = 1.0;
const TEAR_THR = { rg: 6.0, bg: 3.5, grayJ: 8.0, grayS: 15.0 };   // one global k if ever retuned
function tearMedianF32(a) {           // numeric median of a Float32Array (copies)
  const b = Float32Array.from(a); b.sort();
  const n = b.length;
  return n & 1 ? b[(n - 1) >> 1] : 0.5 * (b[n >> 1] + b[(n >> 1) - 1]);
}
function robustTearScan(imgData) {    // imgData = procCx.getImageData(...) RGBA
  const d = imgData.data;
  const u32 = new Uint32Array(d.buffer, d.byteOffset, W * H);
  const g = new Float32Array(H), rg = new Float32Array(H), bg = new Float32Array(H);
  let o = 0;
  for (let y = 0; y < H; y++) {       // full-width row means (no subsampling)
    let sg = 0, srg = 0, sbg = 0;
    for (let x = 0; x < W; x++) {
      const v = u32[o + x];
      const R = v & 255, G = (v >>> 8) & 255, B = (v >>> 16) & 255;
      sg += R + G + B; srg += R - G; sbg += B - G;
    }
    o += W;
    g[y] = sg / (3 * W); rg[y] = srg / W; bg[y] = sbg / W;
  }
  // page-exact jump series: one 3-tap smooth implicit in the 4-tap formula,
  // j[i] straddles rows i..i+1, i in [1, H-3]
  const jumps = (v) => {
    const j = new Float32Array(H - 3);
    for (let i = 1; i < H - 2; i++)
      j[i - 1] = 0.25 * Math.abs((v[i - 1] + 2 * v[i] + v[i + 1]) - (v[i] + 2 * v[i + 1] + v[i + 2]));
    return j;
  };
  const jG = jumps(g), jR = jumps(rg), jB = jumps(bg);
  const lo = TEAR_SKIP - 1;           // index 0 of j = row pair (1,2); lo keeps rows >= SKIP
  const scan = (j) => {               // {max, argmax, MAD} over the scanned range
    let m = 0, mi = -1; const a = [];
    for (let k = lo; k < j.length; k++) { const dv = j[k]; a.push(dv); if (dv > m) { m = dv; mi = k; } }
    const med = tearMedianF32(a);
    const dev = Float32Array.from(a, (x) => Math.abs(x - med));
    return { max: m, i: mi, mad: 1.4826 * tearMedianF32(dev) };
  };
  const sG = scan(jG), sR = scan(jR), sB = scan(jB);
  const yc = sG.i + 1;                // gray-jump candidate row
  const seg = (a, b) => { const t = []; for (let y = a; y < b; y++) t.push(g[y]); return t; };
  const shift = Math.abs(tearMedianF32(seg(Math.min(H, yc + 20), Math.min(H, yc + 60)))
                       - tearMedianF32(seg(Math.max(0, yc - 60), Math.max(0, yc - 20))));
  const sLVL = shift / (sG.mad + TEAR_FLOOR);
  const rgR = sR.max / (sR.mad + TEAR_FLOOR);
  const bgR = sB.max / (sB.mad + TEAR_FLOOR);
  const gR  = sG.max / (sG.mad + TEAR_FLOOR);
  const tear = rgR > TEAR_THR.rg || bgR > TEAR_THR.bg || (gR > TEAR_THR.grayJ && sLVL > TEAR_THR.grayS);
  let arm = null, yTear = -1;
  if (tear) {
    // report the arm that actually crossed (gray only when BOTH gates fire)
    if (rgR > TEAR_THR.rg) { arm = 'rg'; yTear = sR.i + 1; }
    else if (bgR > TEAR_THR.bg) { arm = 'bg'; yTear = sB.i + 1; }
    else { arm = 'gray'; yTear = sG.i + 1; }
  }
  return { tear, arm, y: yTear,
           rg: +rgR.toFixed(2), bg: +bgR.toFixed(2), gr: +gR.toFixed(2),
           sL: +sLVL.toFixed(1),
           arms: 'rg ' + rgR.toFixed(2) + ' bg ' + bgR.toFixed(2) +
                 ' gray ' + gR.toFixed(2) + ' sLVL ' + sLVL.toFixed(1) };
}
// p00 CERTIFICATION + chain TELEMETRY: the robust rule applied to the
// row-mean profile of the PER-PIXEL DIFF between two grabs. A tear in either
// grab makes a step in the diff that identical content otherwise never shows.
// Same thresholds — live calibration of the pair rule happens on the rig at
// the next battery (the corpus validated the single-frame rule only).
function robustTearPair(imgA, imgB) {
  const a = imgA.data, b = imgB.data;
  const diff = procCx.createImageData(W, H);   // createImageData: iOS-safe (new ImageData isn't)
  for (let i = 0; i < a.length; i += 4) {       // channel-wise |a-b|, alpha full
    diff.data[i]     = Math.abs(a[i]     - b[i]);
    diff.data[i + 1] = Math.abs(a[i + 1] - b[i + 1]);
    diff.data[i + 2] = Math.abs(a[i + 2] - b[i + 2]);
    diff.data[i + 3] = 255;
  }
  return robustTearScan(diff);
}
let tearStats = { nTears: 0, nRegrabbed: 0, nFlagged: 0, p00: 'p00-certified' };

// node-only stub (page runs in a browser with a real procCx):
const procCx = { createImageData: (w, h) => ({ data: new Uint8ClampedArray(w*h*4) }) };