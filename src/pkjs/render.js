/* Phone-side port of src/c/qdata.c + src/c/qrender.c.
 * Produces strip records byte-identical to the watch's own renderer, so the
 * watch can mix phone-rendered and self-rendered lines on one screen.
 * ES5 only (PebbleKit JS runtimes vary). */
var B64 = require('./data_blob');

var N_AYAH = 6236, N_SURAH = 114, N_JUZ = 30, N_HIZB = 60, ZOOMS = 5;
var DEFAULT_W = 200, MARGIN = 6, PAD = 10, REC_HDR = 8;   // the line width is per request (screen)
var DIGIT_PSEUDO = 0xFF00;
var FILES = ['TEXT', 'LEX', 'TUP15', 'TUP20', 'TUP26', 'TUP28', 'TUP32',
             'ATL15', 'ATL20', 'ATL26', 'ATL28', 'ATL32', 'META'];

// ------------------------------------------------------------------ base64
var B64CH = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
var B64LUT = null;
function b64decode(s) {
  if (!B64LUT) {
    B64LUT = new Uint8Array(128);
    for (var i = 0; i < 64; i++) B64LUT[B64CH.charCodeAt(i)] = i;
  }
  var len = s.length, pad = 0;
  if (s.charAt(len - 1) === '=') pad++;
  if (s.charAt(len - 2) === '=') pad++;
  var out = new Uint8Array((len / 4) * 3 - pad), o = 0;
  for (var k = 0; k < len; k += 4) {
    var n = (B64LUT[s.charCodeAt(k)] << 18) | (B64LUT[s.charCodeAt(k + 1)] << 12) |
            (B64LUT[s.charCodeAt(k + 2)] << 6) | B64LUT[s.charCodeAt(k + 3)];
    if (o < out.length) out[o++] = (n >> 16) & 255;
    if (o < out.length) out[o++] = (n >> 8) & 255;
    if (o < out.length) out[o++] = n & 255;
  }
  return out;
}

var RES = {};
function res(name) {
  if (!RES[name]) RES[name] = b64decode(B64[name]);
  return RES[name];
}
function rd16(b, p) { return b[p] | (b[p + 1] << 8); }
function rd32(b, p) { return (b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | (b[p + 3] << 24)) >>> 0; }
function s8(v) { return v > 127 ? v - 256 : v; }

// ------------------------------------------------------------------ huffman
function BitRd(buf, pos, end) { this.b = buf; this.pos = pos; this.end = end; this.acc = 0; this.n = 0; }
BitRd.prototype.sym = function (counts) {
  var code = 0, first = 0, base = 0;
  for (var l = 1; l <= 32; l++) {
    if (this.n === 0) {
      if (this.pos >= this.end) return -1;
      this.acc = this.b[this.pos++]; this.n = 8;
    }
    code = (code * 2) + ((this.acc >> (this.n - 1)) & 1);
    this.n--;
    var cnt = counts[l];
    if (code - first < cnt) return base + (code - first);
    base += cnt;
    first = (first + cnt) * 2;
  }
  return -1;
};

// ------------------------------------------------------------------ meta
var M = null;
function meta() {
  if (M) return M;
  var t = res('TEXT'), lx = res('LEX'), m = res('META');
  var i, p;
  M = {};
  M.textCounts = [0]; for (i = 0; i < 32; i++) M.textCounts.push(rd16(t, 8 + 2 * i));
  M.textIdx = 72;
  M.textStreamBase = 72 + 4 * N_AYAH + 4;
  M.textStreamLen = rd32(t, 72 + 4 * N_AYAH);
  M.nVocab = rd16(t, 6);
  M.lexNSyms = rd16(lx, 6); M.lexEnd = rd16(lx, 8); M.lexBlock = rd16(lx, 10); M.lexNBlocks = rd16(lx, 12);
  M.lexCounts = [0]; for (i = 0; i < 32; i++) M.lexCounts.push(rd16(lx, 16 + 2 * i));
  M.lexStreamBase = 80 + 4 * M.lexNBlocks + 4;
  M.lexStreamLen = rd32(lx, 80 + 4 * M.lexNBlocks);
  p = 8;
  M.bism = [rd16(m, p), rd16(m, p + 2), rd16(m, p + 4), rd16(m, p + 6)];
  M.signGid = rd16(m, p + 8);
  p += 10 + 20;
  M.zooms = [];
  for (i = 0; i < ZOOMS; i++) {
    M.zooms.push({ ppem: m[p], lineH: m[p + 1], ascent: m[p + 2], space: m[p + 3], digitRaise: s8(m[p + 4]) });
    p += 6;
  }
  M.surahs = [];
  for (i = 0; i < N_SURAH; i++) {
    var tr = '';
    for (var k = 0; k < 15 && m[p + 8 + k]; k++) tr += String.fromCharCode(m[p + 8 + k]);
    M.surahs.push({ first: rd16(m, p), count: rd16(m, p + 2), bism: m[p + 4],
                    nameLen: m[p + 5], nameOff: rd16(m, p + 6), translit: tr });
    p += 24;
  }
  M.nameHeap = p + 2 * N_JUZ + 2 * N_HIZB + 2;
  return M;
}

// Tuple syms of a surah's Arabic name (same as qmeta_surah_name()).
function surahNameSyms(idx) {
  var mm = meta(), m = res('META'), s = mm.surahs[idx], out = [];
  var n = Math.min(s.nameLen, 32);
  for (var i = 0; i < n; i++) out.push(rd16(m, mm.nameHeap + 2 * (s.nameOff + i)));
  return out;
}

function surahOf(g) {
  var s = meta().surahs, lo = 0, hi = N_SURAH - 1;
  while (lo < hi) {
    var mid = (lo + hi + 1) >> 1;
    if (s[mid].first <= g) lo = mid; else hi = mid - 1;
  }
  return lo;
}
function ayahNo(g) { return g - meta().surahs[surahOf(g)].first + 1; }

function blockLines(zoom, g, width) { return layoutBlockLines(zoom, g, width); }

// ------------------------------------------------------------------ text
function ayahWords(g) {
  var mm = meta(), t = res('TEXT');
  var packed = rd32(t, mm.textIdx + 4 * g);
  var off = packed & 0xFFFFFF, cnt = packed >>> 24;
  var end = g + 1 < N_AYAH ? (rd32(t, mm.textIdx + 4 * (g + 1)) & 0xFFFFFF) : mm.textStreamLen;
  var span = Math.min(end - off, 512);
  if (span <= 0) return [];
  var br = new BitRd(t, mm.textStreamBase + off, mm.textStreamBase + off + span);
  var out = [];
  for (var i = 0; i < cnt && i < 160; i++) {
    var s = br.sym(mm.textCounts);
    if (s < 0) break;
    out.push(s);
  }
  return out;
}

var WORDS = {};          // word id -> syms (zoom independent)
function wordSyms(wid) {
  var w = WORDS[wid];
  if (w) return w;
  var mm = meta(), lx = res('LEX');
  var blk = Math.floor(wid / mm.lexBlock), skip = wid % mm.lexBlock;
  var off = rd32(lx, 80 + 4 * blk);
  var end = blk + 1 < mm.lexNBlocks ? rd32(lx, 80 + 4 * (blk + 1)) : mm.lexStreamLen;
  var span = Math.min(end - off, 512);
  var br = new BitRd(lx, mm.lexStreamBase + off, mm.lexStreamBase + off + span);
  var cur = [];
  // decode the whole block once and cache every word in it
  var base = blk * mm.lexBlock, idx = 0;
  while (idx <= skip || idx < mm.lexBlock) {
    var s = br.sym(mm.lexCounts);
    if (s < 0) break;
    if (s === mm.lexEnd) {
      WORDS[base + idx] = cur.length > 24 ? cur.slice(0, 24) : cur;
      cur = []; idx++;
      if (base + idx >= mm.nVocab) break;
    } else {
      cur.push(s);
    }
  }
  return WORDS[wid] || [];
}

// ------------------------------------------------------------------ zoom engine
var ZT = [], ENG = {};
function engine(zoom, width) {
  width = width || DEFAULT_W;
  var key = zoom + ':' + width;
  if (ENG[key]) return ENG[key];
  var t = zoomTables(zoom), e = {};
  for (var k in t) e[k] = t[k];
  e.W = width;
  e.STRIDE = (width + 3) >> 2;
  e.USABLE = width - 2 * MARGIN;
  e.cv = new Uint8Array(e.rows * e.STRIDE);
  e.layouts = {}; e.layoutKeys = []; e.bism = null; e.blocks = {};
  ENG[key] = e;
  return e;
}

function zoomTables(zoom) {
  if (ZT[zoom]) return ZT[zoom];
  var ppem = meta().zooms[zoom].ppem;
  var tup = res('TUP' + ppem), atl = res('ATL' + ppem);
  var nSyms = rd16(tup, 6), nGlyphs = rd16(atl, 6);
  var e = { zoom: zoom, z: meta().zooms[zoom], atl: atl, bmpBase: 8 + 12 * nGlyphs };
  e.gid = new Uint16Array(nGlyphs); e.gw = new Uint8Array(nGlyphs); e.gh = new Uint8Array(nGlyphs);
  e.gleft = new Int8Array(nGlyphs); e.gtop = new Int8Array(nGlyphs);
  e.gadv = new Uint16Array(nGlyphs); e.goff = new Uint32Array(nGlyphs);
  var byGid = {};
  for (var i = 0; i < nGlyphs; i++) {
    var r = 8 + 12 * i;
    e.gid[i] = rd16(atl, r); e.gw[i] = atl[r + 2]; e.gh[i] = atl[r + 3];
    e.gleft[i] = s8(atl[r + 4]); e.gtop[i] = s8(atl[r + 5]);
    e.gadv[i] = rd16(atl, r + 6); e.goff[i] = rd32(atl, r + 8);
    byGid[e.gid[i]] = i;
  }
  e.byGid = byGid;
  e.tgi = new Int32Array(nSyms); e.tadv = new Uint8Array(nSyms);
  e.tdx = new Int8Array(nSyms); e.tdy = new Int8Array(nSyms);
  for (i = 0; i < nSyms; i++) {
    var q = 8 + 5 * i, gid = rd16(tup, q);
    e.tgi[i] = byGid[gid] !== undefined ? byGid[gid] : -1;
    e.tadv[i] = tup[q + 2]; e.tdx[i] = s8(tup[q + 3]); e.tdy[i] = s8(tup[q + 4]);
  }
  e.nSyms = nSyms;
  e.signGi = byGid[meta().signGid] !== undefined ? byGid[meta().signGid] : -1;
  e.widths = {};                         // word widths depend on the zoom only
  e.rows = e.z.lineH + 2 * PAD;
  ZT[zoom] = e;
  return e;
}

function wordWidth(e, wid) {
  var w = e.widths[wid];
  if (w !== undefined) return w;
  var syms = wordSyms(wid);
  w = 0;
  for (var i = 0; i < syms.length; i++) if (syms[i] < e.nSyms) w += e.tadv[syms[i]];
  e.widths[wid] = w;
  return w;
}

function blit(e, gi, x, ybase) {
  if (gi < 0) return;
  var gw = e.gw[gi], gh = e.gh[gi];
  if (!gw || !gh) return;
  var stride = (gw + 3) >> 2, bmp = e.bmpBase + e.goff[gi], atl = e.atl, cv = e.cv, rows = e.rows;
  var x0 = x + e.gleft[gi], y0 = ybase - e.gtop[gi];
  for (var r = 0; r < gh; r++) {
    var yy = y0 + r;
    if (yy < 0 || yy >= rows) continue;
    var row = bmp + r * stride, dst = yy * e.STRIDE;
    for (var cb = 0; cb < stride; cb++) {
      var b = atl[row + cb];
      if (!b) continue;
      for (var k = 0; k < 4; k++) {
        var lv = (b >> (2 * k)) & 3;
        if (!lv) continue;
        var col = cb * 4 + k;
        if (col >= gw) break;
        var xx = x0 + col;
        if (xx < 0 || xx >= e.W) continue;
        var sh = (xx & 3) * 2, d = dst + (xx >> 2);
        if (lv > ((cv[d] >> sh) & 3)) cv[d] = (cv[d] & ~(3 << sh)) | (lv << sh);
      }
    }
  }
}

function markerWidth(e) { return e.signGi >= 0 ? e.gadv[e.signGi] : 0; }

function drawSyms(e, syms, pen, baseline) {
  for (var i = 0; i < syms.length; i++) {
    var s = syms[i];
    if (s >= e.nSyms) continue;
    if (e.tgi[s] >= 0) blit(e, e.tgi[s], pen + e.tdx[s], baseline - e.tdy[s]);
    pen += e.tadv[s];
  }
}

function symsWidth(e, syms) {
  var w = 0;
  for (var i = 0; i < syms.length; i++) if (syms[i] < e.nSyms) w += e.tadv[syms[i]];
  return w;
}

function drawWord(e, wid, pen, baseline) { drawSyms(e, wordSyms(wid), pen, baseline); }

// Full-ink primitives (level 3), clipped to the canvas.
function hline(e, x0, x1, y) {
  if (y < 0 || y >= e.rows) return;
  if (x0 < 0) x0 = 0;
  if (x1 > e.W - 1) x1 = e.W - 1;
  var cv = e.cv, row = y * e.STRIDE;
  for (var x = x0; x <= x1; x++) cv[row + (x >> 2)] |= 3 << ((x & 3) * 2);
}

function diamond(e, cx, cy, r) {
  for (var dy = -r; dy <= r; dy++) {
    var half = r - (dy < 0 ? -dy : dy);
    hline(e, cx - half, cx + half, cy + dy);
  }
}

// Surah separator line; see draw_separator() in src/c/qrender.c for the geometry.
function drawSeparator(e, surahIdx, baseline) {
  var z = e.z, syms = surahNameSyms(surahIdx);
  var w = symsWidth(e, syms);
  var nl = ((e.W - w) / 2) | 0, nr = nl + w;
  drawSyms(e, syms, nl, baseline);
  var ym = baseline - ((z.ascent / 3) | 0);
  var d = ((z.ppem + 4) / 8) | 0, r = d + 1, gap = 3 + ((z.ppem / 6) | 0);
  var cl = nl - 1 - gap - r, cr = nr + gap + r;
  var xl = MARGIN, xr = e.W - MARGIN - 1;
  if (cl - r >= xl) {
    hline(e, xl, cl, ym - d); hline(e, xl, cl, ym + d);
    diamond(e, cl, ym, r);
  }
  if (cr + r <= xr) {
    hline(e, cr, xr, ym - d); hline(e, cr, xr, ym + d);
    diamond(e, cr, ym, r);
  }
}

function drawMarker(e, number, penLeft, baseline) {
  if (e.signGi < 0) return;
  var signAdv = e.gadv[e.signGi];
  blit(e, e.signGi, penLeft, baseline);
  var digs = [];
  if (number <= 0) number = 1;
  while (number > 0 && digs.length < 4) { digs.push(number % 10); number = Math.floor(number / 10); }
  var dw = 0, dgi = [];
  for (var i = 0; i < digs.length; i++) {
    var gi = e.byGid[DIGIT_PSEUDO + digs[i]];
    dgi.push(gi === undefined ? -1 : gi);
    if (gi !== undefined) dw += e.gadv[gi];
  }
  var x = penLeft + ((signAdv - dw) / 2 | 0);
  var by = baseline - e.z.digitRaise;
  for (i = digs.length - 1; i >= 0; i--) {
    if (dgi[i] < 0) continue;
    blit(e, dgi[i], x, by);
    x += e.gadv[dgi[i]];
  }
}

// ------------------------------------------------------------------ layout
function tokWidth(e, L, i) { return i < L.wids.length ? wordWidth(e, L.wids[i]) : markerWidth(e); }

function layoutWalk(e, L) {
  var space = e.z.space, nWords = L.wids.length, nTok = nWords + (L.marker ? 1 : 0);
  var mw = L.marker ? markerWidth(e) : 0;
  var xr = e.W - MARGIN, line = 0, first = true;
  L.starts = [0];
  for (var i = 0; i < nTok; i++) {
    var w = tokWidth(e, L, i), eff = w;
    if (L.marker && i === nWords - 1) eff = w + space + mw;
    var isMarker = L.marker && i === nWords;
    if (!first && !isMarker && xr - eff < MARGIN) {
      line++;
      L.starts[line] = i;
      xr = e.W - MARGIN;
      first = true;
    }
    xr -= w + space;
    first = false;
  }
  L.starts[line + 1] = nTok;
  L.nLines = line + 1;
  return L;
}

function ayahLayout(e, g) {
  var L = e.layouts[g];
  if (L) return L;
  L = layoutWalk(e, { wids: ayahWords(g), marker: ayahNo(g) });
  var keys = e.layoutKeys;
  keys.push(g);
  if (keys.length > 64) delete e.layouts[keys.shift()];
  e.layouts[g] = L;
  return L;
}

function bismLayout(e) {
  if (!e.bism) e.bism = layoutWalk(e, { wids: meta().bism.slice(0), marker: 0 });
  return e.bism;
}

// A block is: separator line (first ayah of every surah), bismillah lines
// (first ayah of a surah that opens with one), then the ayah's own lines.
function sepLines(g) { return g === meta().surahs[surahOf(g)].first ? 1 : 0; }

function bismLines(e, g) {
  var s = meta().surahs[surahOf(g)];
  if (!s.bism || g !== s.first) return 0;
  return bismLayout(e).nLines;
}

// Same as qr_layout_block_lines() (the watch reads them from its LINES table).
function layoutBlockLines(zoom, g, width) {
  var e = engine(zoom, width);
  var n = e.blocks[g];
  if (n === undefined) n = e.blocks[g] = sepLines(g) + bismLines(e, g) + ayahLayout(e, g).nLines;
  return n;
}

function drawLine(e, L, k, baseline, centerSingle) {
  if (k < 0 || k >= L.nLines) return;
  var space = e.z.space, shift = 0, nWords = L.wids.length, i;
  if (centerSingle && L.nLines === 1) {
    var total = 0;
    for (i = 0; i < nWords; i++) total += tokWidth(e, L, i);
    total += (nWords - 1) * space;
    shift = (-(e.USABLE - total) / 2) | 0;
  }
  var xr = e.W - MARGIN;
  for (i = L.starts[k]; i < L.starts[k + 1]; i++) {
    var w = tokWidth(e, L, i);
    if (i < nWords) drawWord(e, L.wids[i], xr - w + shift, baseline);
    else drawMarker(e, L.marker, xr - w + shift, baseline);
    xr -= w + space;
  }
}

// ------------------------------------------------------------------ codec
function pack(b, start, n, out) {
  var i = 0, o = out.length, j, len;
  while (i < n) {
    var v = b[start + i];
    if (v === 0) {
      j = i;
      while (j < n && b[start + j] === 0 && j - i < 128) j++;
      out.push(j - i - 1);
      i = j;
      continue;
    }
    j = i;
    while (j < n && b[start + j] === v && j - i < 66) j++;
    if (j - i >= 3) {
      out.push(0xC0 | (j - i - 3), v);
      i = j;
      continue;
    }
    j = i; len = 0;
    while (j < n && len < 64) {
      var c = b[start + j];
      if (c === 0) break;
      if (j + 2 < n && c === b[start + j + 1] && c === b[start + j + 2]) break;
      j++; len++;
    }
    out.push(0x80 | (len - 1));
    for (var k = i; k < j; k++) out.push(b[start + k]);
    i = j;
  }
  return out.length - o;
}

// Appends one strip record for line `line` of ayah g's block to out (array).
function renderStrip(zoom, g, line, out, width) {
  var e = engine(zoom, width), cv = e.cv, rows = e.rows;
  for (var i = 0; i < cv.length; i++) cv[i] = 0;
  var baseline = PAD + e.z.ascent;
  var sl = sepLines(g), bl = bismLines(e, g);
  if (line < sl) drawSeparator(e, surahOf(g), baseline);
  else if (line < sl + bl) drawLine(e, bismLayout(e), line - sl, baseline, true);
  else drawLine(e, ayahLayout(e, g), line - sl - bl, baseline, false);
  var first = -1, last = -1, r, c;
  for (r = 0; r < rows; r++) {
    for (c = 0; c < e.STRIDE; c++) if (cv[r * e.STRIDE + c]) { if (first < 0) first = r; last = r; break; }
  }
  var at = out.length;
  out.push(g & 255, g >> 8, line, zoom, 0, 0, 0, 0);
  if (first >= 0) {
    var h = last - first + 1;
    var clen = pack(cv, first * e.STRIDE, h * e.STRIDE, out);
    out[at + 4] = (first - PAD) & 255;
    out[at + 5] = h;
    out[at + 6] = clen & 255;
    out[at + 7] = clen >> 8;
  }
  return out.length - at;
}

module.exports = {
  N_AYAH: N_AYAH,
  ZOOMS: ZOOMS,
  meta: meta,
  surahOf: surahOf,
  ayahNo: ayahNo,
  blockLines: blockLines,
  layoutBlockLines: layoutBlockLines,
  renderStrip: renderStrip,
  FILES: FILES
};
