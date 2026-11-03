// node tools/host/test_render.js <strips.bin> <zoom> <g0> <g1> [width] [LINES.bin]
// Checks that the phone renderer produces byte-identical strip records to the C
// renderer (qtool strips ... same width), and optionally that its line counts
// match a LINES table made for that width.
var fs = require('fs'), path = require('path');
var R = require(path.join(__dirname, '..', '..', 'src', 'pkjs', 'render.js'));
var ref = fs.readFileSync(process.argv[2]);
var zoom = +process.argv[3], g0 = +process.argv[4], g1 = +process.argv[5], width = +(process.argv[6] || 200);
var lines = process.argv[7] ? fs.readFileSync(process.argv[7]) : null;
if (lines && (lines.toString('latin1', 0, 4) !== 'QLN2' || (lines[8] | (lines[9] << 8)) !== width)) {
  console.log('LINES table is not for width ' + width); process.exit(1);
}
var out = [], t0 = Date.now(), n = 0, bad = -1;
for (var g = g0; g < g1; g++) {
  var bl = R.blockLines(zoom, g, width);
  if (lines && bad < 0 && lines[12 + zoom * R.N_AYAH + g] !== bl) bad = g;
  for (var l = 0; l < bl; l++) { R.renderStrip(zoom, g, l, out, width); n++; }
}
var ms = Date.now() - t0;
if (bad >= 0) console.log('line count of ayah ' + bad + ' differs from the LINES table');
var ok = out.length === ref.length && bad < 0;
var firstDiff = -1;
for (var i = 0; i < Math.min(out.length, ref.length); i++) if (out[i] !== ref[i]) { ok = false; firstDiff = i; break; }
console.log('width ' + width + ' zoom ' + zoom + ' ayahs ' + g0 + '-' + g1 + ': ' + n + ' lines, ' + out.length + ' bytes, ' +
            (ok ? 'IDENTICAL' : 'DIFFERENT (len ' + out.length + ' vs ' + ref.length + ', first diff @' + firstDiff + ')') +
            ', ' + (n ? (ms / n).toFixed(3) : 0) + ' ms/line');
process.exit(ok ? 0 : 1);
