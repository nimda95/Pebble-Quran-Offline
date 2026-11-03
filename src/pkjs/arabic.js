/* Arabic text (Tanzil "Quran Simple", annotation marks removed) for the
 * system-font reading mode, decoded from the bundled word dictionary.
 * The basmala of each surah is not part of its first ayah (the watch shows it
 * as its own line), except for Al-Fatihah where it is ayah 1. ES5 only. */
var A = require('./arabic_blob');

var CH = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function b64(s) {
  var lut = {}, i;
  for (i = 0; i < 64; i++) lut[CH.charAt(i)] = i;
  var pad = s.charAt(s.length - 1) === '=' ? (s.charAt(s.length - 2) === '=' ? 2 : 1) : 0;
  var out = new Uint8Array(s.length / 4 * 3 - pad), o = 0;
  for (i = 0; i < s.length; i += 4) {
    var n = (lut[s.charAt(i)] << 18) | (lut[s.charAt(i + 1)] << 12) |
            ((lut[s.charAt(i + 2)] || 0) << 6) | (lut[s.charAt(i + 3)] || 0);
    if (o < out.length) out[o++] = (n >> 16) & 255;
    if (o < out.length) out[o++] = (n >> 8) & 255;
    if (o < out.length) out[o++] = n & 255;
  }
  return out;
}

var vocab = null, text = null, starts = null, counts = null;

function init() {
  if (vocab) return;
  var vb = b64(A.VOCAB), i, w = '';
  vocab = [];
  for (i = 0; i < vb.length; i++) {
    if (vb[i] === 0) { vocab.push(w); w = ''; }
    else w += A.CHARS.charAt(vb[i] - 1);
  }
  text = b64(A.TEXT);
  counts = b64(A.COUNTS);
  starts = new Uint32Array(counts.length);
  var p = 0;
  for (var g = 0; g < counts.length; g++) {
    starts[g] = p;
    for (var k = 0; k < counts[g]; k++) p += (text[p] & 0x80) ? 2 : 1;
  }
}

function ayah(g) {
  init();
  if (g < 0 || g >= counts.length) return '';
  var p = starts[g], words = [];
  for (var k = 0; k < counts[g]; k++) {
    var i = text[p++];
    if (i & 0x80) i = ((i & 0x7F) << 8) | text[p++];
    words.push(vocab[i]);
  }
  return words.join(' ');
}

module.exports = { ayah: ayah, init: init };
