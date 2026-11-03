#include "qdata.h"
#include <string.h>

QStats g_qstats;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t rres(int res, uint32_t off, void *buf, uint32_t len) {
  g_qstats.reads++;
  return qres_read(res, off, buf, len);
}

// ---------------------------------------------------------------- bit reader
void qbr_init(QBitRd *b, const uint8_t *buf, uint32_t len) {
  b->buf = buf; b->len = len; b->pos = 0; b->acc = 0; b->n = 0;
}

// Canonical Huffman, MSB-first. counts[l] = number of codes of length l.
int qbr_sym(QBitRd *b, const uint16_t *counts) {
  uint32_t code = 0, first = 0, base = 0;
  for (int l = 1; l <= 32; l++) {
    if (b->n == 0) {
      if (b->pos >= b->len) return -1;
      b->acc = b->buf[b->pos++]; b->n = 8;
    }
    code = (code << 1) | ((b->acc >> (b->n - 1)) & 1);
    b->n--;
    uint32_t cnt = counts[l];
    if (code - first < cnt) return (int)(base + (code - first));
    base += cnt;
    first = (first + cnt) << 1;
  }
  return -1;
}

// ---------------------------------------------------------------- META
static QSurah s_surah[QD_N_SURAH];
static uint16_t s_juz[QD_N_JUZ], s_hizb[QD_N_HIZB];
static QZoom s_zooms[QD_ZOOMS];
static uint16_t s_bism[4], s_sign_gid;
static uint32_t s_nameheap_base;
static bool s_meta_ok;

// TEXT: QTX1 | u16 n_ayah | u16 n_vocab | u16 counts[32] | u32 index[n_ayah] | u32 stream_len | stream
#define TEXT_HDR (4 + 2 + 2 + 64)
static uint16_t s_text_counts[33];
static uint32_t s_text_stream_base, s_text_stream_len;
static uint16_t s_n_vocab;

// LEX: QLX1 | u16 n_words | u16 n_syms | u16 end_sym | u16 block | u16 n_blocks | u16 pad |
//      u16 counts[32] | u32 block_index[n_blocks] | u32 stream_len | stream
#define LEX_HDR (4 + 12 + 64)
static uint16_t s_lex_counts[33];
static uint16_t s_lex_end, s_lex_block, s_lex_nblocks, s_lex_nsyms;
static uint32_t s_lex_stream_base, s_lex_stream_len;

bool qmeta_init(void) {
  if (s_meta_ok) return true;
  uint8_t hdr[16];
  if (rres(QRES_TEXT, 0, hdr, 8) != 8 || memcmp(hdr, "QTX1", 4)) return false;
  if (rd16(hdr + 4) != QD_N_AYAH) return false;
  s_n_vocab = rd16(hdr + 6);
  uint8_t cbuf[64];
  if (rres(QRES_TEXT, 8, cbuf, 64) != 64) return false;
  s_text_counts[0] = 0;
  for (int i = 0; i < 32; i++) s_text_counts[i + 1] = rd16(cbuf + 2 * i);
  s_text_stream_base = TEXT_HDR + 4u * QD_N_AYAH + 4;
  rres(QRES_TEXT, TEXT_HDR + 4u * QD_N_AYAH, hdr, 4);
  s_text_stream_len = rd32(hdr);

  if (rres(QRES_LEX, 0, hdr, 16) != 16 || memcmp(hdr, "QLX1", 4)) return false;
  if (rd16(hdr + 4) != s_n_vocab) return false;
  s_lex_nsyms = rd16(hdr + 6);
  s_lex_end = rd16(hdr + 8);
  s_lex_block = rd16(hdr + 10);
  s_lex_nblocks = rd16(hdr + 12);
  if (!s_lex_block) return false;
  if (rres(QRES_LEX, 16, cbuf, 64) != 64) return false;
  s_lex_counts[0] = 0;
  for (int i = 0; i < 32; i++) s_lex_counts[i + 1] = rd16(cbuf + 2 * i);
  s_lex_stream_base = LEX_HDR + 4u * s_lex_nblocks + 4;
  rres(QRES_LEX, LEX_HDR + 4u * s_lex_nblocks, hdr, 4);
  s_lex_stream_len = rd32(hdr);

  // META is small (~4 KB); parse it in pieces to avoid a big allocation.
  uint8_t m[24];
  uint32_t p = 0;
  if (rres(QRES_META, p, m, 8) != 8 || memcmp(m, "QMT1", 4)) return false;
  if (m[4] != QD_N_SURAH || m[5] != QD_N_JUZ || m[6] != QD_N_HIZB || m[7] != QD_ZOOMS) return false;
  p = 8;
  rres(QRES_META, p, m, 10); p += 10;
  for (int i = 0; i < 4; i++) s_bism[i] = rd16(m + 2 * i);
  s_sign_gid = rd16(m + 8);
  p += 20;                                   // digit pseudo-gids (fixed 0xFF00 + d)
  for (int z = 0; z < QD_ZOOMS; z++) {
    rres(QRES_META, p, m, 6); p += 6;
    s_zooms[z].ppem = m[0]; s_zooms[z].line_h = m[1];
    s_zooms[z].ascent = m[2]; s_zooms[z].space_w = m[3];
    s_zooms[z].digit_raise = (int8_t)m[4];
  }
  for (int i = 0; i < QD_N_SURAH; i++) {
    rres(QRES_META, p, m, 24); p += 24;
    s_surah[i].first_global = rd16(m);
    s_surah[i].ayah_count = rd16(m + 2);
    s_surah[i].bismillah = m[4];
    s_surah[i].name_len = m[5];
    s_surah[i].name_off = rd16(m + 6);
    memcpy(s_surah[i].translit, m + 8, 16);
    s_surah[i].translit[15] = 0;
  }
  uint8_t jb[2 * QD_N_HIZB];
  rres(QRES_META, p, jb, 2 * QD_N_JUZ); p += 2 * QD_N_JUZ;
  for (int i = 0; i < QD_N_JUZ; i++) s_juz[i] = rd16(jb + 2 * i);
  rres(QRES_META, p, jb, 2 * QD_N_HIZB); p += 2 * QD_N_HIZB;
  for (int i = 0; i < QD_N_HIZB; i++) s_hizb[i] = rd16(jb + 2 * i);
  p += 2;                                    // name heap length
  s_nameheap_base = p;
  s_meta_ok = true;
  return true;
}

const QSurah *qmeta_surah(int idx) { return &s_surah[idx]; }

int qmeta_surah_of(uint16_t g) {
  int lo = 0, hi = QD_N_SURAH - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (s_surah[mid].first_global <= g) lo = mid; else hi = mid - 1;
  }
  return lo;
}

int qmeta_ayah_no(uint16_t g) { return g - s_surah[qmeta_surah_of(g)].first_global + 1; }
uint16_t qmeta_juz_start(int j) { return s_juz[j]; }
uint16_t qmeta_hizb_start(int h) { return s_hizb[h]; }

static int part_of(const uint16_t *starts, int n, uint16_t g) {
  int r = 0;
  for (int i = 0; i < n; i++) if (starts[i] <= g) r = i;
  return r;
}
int qmeta_juz_of(uint16_t g) { return part_of(s_juz, QD_N_JUZ, g); }
int qmeta_hizb_of(uint16_t g) { return part_of(s_hizb, QD_N_HIZB, g); }
const QZoom *qmeta_zoom(int z) { return &s_zooms[z]; }
uint16_t qmeta_bismillah_word(int i) { return s_bism[i]; }

int qmeta_surah_name(int idx, uint16_t *out, int max) {
  const QSurah *s = &s_surah[idx];
  int n = s->name_len;
  if (n > max) n = max;
  if (n > 32) n = 32;
  uint8_t buf[64];
  rres(QRES_META, s_nameheap_base + 2u * s->name_off, buf, 2u * n);
  for (int i = 0; i < n; i++) out[i] = rd16(buf + 2 * i);
  return n;
}

// NAMES: QNM1 | u16 count | u16 0 | u32 offsets[count + 1] (absolute; last = end) | records
#define NAMES_HDR 8
static int8_t s_names_ok;        // 0 = not checked yet, 1 = ok, -1 = bad

int qmeta_name_record(int idx, uint8_t *buf, int cap) {
  if (idx < 0 || idx >= QD_N_SURAH || !buf || cap <= 0) return -1;
  uint8_t e[8];
  if (!s_names_ok) {
    s_names_ok = (rres(QRES_NAMES, 0, e, 8) == 8 && !memcmp(e, "QNM1", 4) &&
                  rd16(e + 4) == QD_N_SURAH) ? 1 : -1;
  }
  if (s_names_ok < 0) return -1;
  if (rres(QRES_NAMES, NAMES_HDR + 4u * idx, e, 8) != 8) return -1;
  uint32_t off = rd32(e), end = rd32(e + 4);
  if (end <= off || end - off > (uint32_t)cap) return -1;
  uint32_t len = end - off;
  if (rres(QRES_NAMES, off, buf, len) != len) return -1;
  return (int)len;
}

// LINES: QLN2 | u16 n_ayah | u8 n_zoom | u8 0 | u16 line width | u16 0 | u8 lines[n_zoom][n_ayah]
// (one table per screen width; resource tags pick the platform's file)
#define LINES_HDR 12
static uint8_t s_lines_buf[64];
static int s_lines_key = -1;

int qmeta_lines_width(void) {
  uint8_t h[LINES_HDR];
  if (qres_read(QRES_LINES, 0, h, LINES_HDR) != LINES_HDR || memcmp(h, "QLN2", 4)) return 0;
  return rd16(h + 8);
}

int qmeta_block_lines(int zoom, uint16_t g) {
  if (zoom < 0 || zoom >= QD_ZOOMS || g >= QD_N_AYAH) return 1;
  int key = zoom * 1024 + g / 64;
  if (key != s_lines_key) {
    uint32_t off = LINES_HDR + (uint32_t)zoom * QD_N_AYAH + (g / 64) * 64u;
    uint32_t len = 64;
    uint32_t end = LINES_HDR + (uint32_t)(zoom + 1) * QD_N_AYAH;
    if (off + len > end) len = end - off;
    if (rres(QRES_LINES, off, s_lines_buf, len) != len) { s_lines_key = -1; return 1; }
    s_lines_key = key;
  }
  int v = s_lines_buf[g % 64];
  return v ? v : 1;
}

// ---------------------------------------------------------------- TEXT
int qdata_ayah_words(uint16_t g, uint16_t *out, int max) {
  if (g >= QD_N_AYAH) return 0;
  uint8_t e[8];
  uint32_t n_idx = (g + 1 < QD_N_AYAH) ? 8 : 4;
  rres(QRES_TEXT, TEXT_HDR + 4u * g, e, n_idx);
  uint32_t packed = rd32(e);
  uint32_t off = packed & 0xFFFFFF;
  int cnt = (int)(packed >> 24);
  if (cnt > max) cnt = max;
  uint32_t end = (n_idx == 8) ? (rd32(e + 4) & 0xFFFFFF) : s_text_stream_len;
  uint8_t buf[512];
  uint32_t span = (end > off) ? end - off : 0;
  if (span > sizeof(buf)) span = sizeof(buf);
  if (span == 0) return 0;
  rres(QRES_TEXT, s_text_stream_base + off, buf, span);
  QBitRd br; qbr_init(&br, buf, span);
  for (int i = 0; i < cnt; i++) {
    int s = qbr_sym(&br, s_text_counts);
    if (s < 0) return i;
    out[i] = (uint16_t)s;
  }
  return cnt;
}

// ---------------------------------------------------------------- engine
static int s_zoom = -1;
static uint16_t s_n_syms, s_n_glyphs;
static QTuple *s_tup;               // n_syms
static QGlyph *s_gdir;              // n_glyphs, sorted by gid
static uint32_t s_atl_bmp_base;
static uint16_t s_sign_gi = QD_NO_GLYPH;

// glyph bitmap pool: direct slot per glyph index, flushed when full
static uint8_t *s_pool;
static uint16_t *s_slot;            // n_glyphs; 0xFFFF = not loaded
static uint32_t s_pool_size, s_pool_used;

// word cache: 2-way set associative
#define WC_SETS 64
typedef struct { uint16_t wid; uint8_t n; uint8_t pad; uint16_t width;
                 uint16_t syms[QD_MAX_TUPLES_PER_WORD]; } WEnt;
static WEnt *s_wc;                  // WC_SETS * 2
static uint8_t s_wc_lru[WC_SETS];   // way to evict next

// LEX fast decode table: (sym << 4) | len for codes up to FT_BITS long
#define FT_BITS 10
static uint16_t *s_ft;              // 1 << FT_BITS entries

static void build_fast_table(void) {
  memset(s_ft, 0, sizeof(uint16_t) << FT_BITS);
  uint32_t code = 0, base = 0;
  for (int l = 1; l <= FT_BITS; l++) {
    uint32_t cnt = s_lex_counts[l];
    for (uint32_t k = 0; k < cnt; k++) {
      uint32_t sym = base + k;
      uint32_t c = code + k;
      if (sym < 4096) {
        uint32_t lo = c << (FT_BITS - l), hi = (c + 1) << (FT_BITS - l);
        for (uint32_t t = lo; t < hi; t++) s_ft[t] = (uint16_t)((sym << 4) | l);
      }
    }
    base += cnt;
    code = (code + cnt) << 1;
  }
}

static void pool_flush(void) {
  if (s_slot) memset(s_slot, 0xFF, sizeof(uint16_t) * s_n_glyphs);
  s_pool_used = 0;
  g_qstats.pool_flushes++;
}

void qeng_close(void) {
  qfree(s_tup); s_tup = NULL;
  qfree(s_gdir); s_gdir = NULL;
  qfree(s_pool); s_pool = NULL;
  qfree(s_slot); s_slot = NULL;
  qfree(s_wc); s_wc = NULL;
  qfree(s_ft); s_ft = NULL;
  s_zoom = -1;
}

bool qeng_ready(void) { return s_zoom >= 0; }
int qeng_zoom(void) { return s_zoom; }

static const QGlyph *find_gid(uint16_t gid) {
  int lo = 0, hi = s_n_glyphs - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (s_gdir[mid].gid == gid) return &s_gdir[mid];
    if (s_gdir[mid].gid < gid) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

bool qeng_open(int zoom, uint32_t pool_bytes) {
  if (zoom < 0 || zoom >= QD_ZOOMS) return false;
  if (s_zoom == zoom) return true;
  qeng_close();
  if (!qmeta_init()) return false;
  if (pool_bytes > 65000) pool_bytes = 65000;
  int tres = QRES_TUP0 + zoom, ares = QRES_ATL0 + zoom;
  uint8_t hdr[8];
  if (rres(tres, 0, hdr, 8) != 8 || memcmp(hdr, "QTP1", 4)) return false;
  s_n_syms = rd16(hdr + 6);
  if (rres(ares, 0, hdr, 8) != 8 || memcmp(hdr, "QAT1", 4)) return false;
  s_n_glyphs = rd16(hdr + 6);

  // largest blocks first to limit fragmentation
  s_pool = qmalloc(pool_bytes);
  s_tup = qmalloc(sizeof(QTuple) * s_n_syms);
  s_wc = qmalloc(sizeof(WEnt) * WC_SETS * 2);
  s_gdir = qmalloc(sizeof(QGlyph) * s_n_glyphs);
  s_ft = qmalloc(sizeof(uint16_t) << FT_BITS);
  s_slot = qmalloc(sizeof(uint16_t) * s_n_glyphs);
  if (!s_pool || !s_tup || !s_wc || !s_gdir || !s_ft || !s_slot) { qeng_close(); return false; }
  s_pool_size = pool_bytes;

  // glyph directory, read in chunks: gid u16, w, h, left, top, adv u16, off u32
  uint8_t chunk[12 * 32];
  for (int i = 0; i < s_n_glyphs; i += 32) {
    int n = s_n_glyphs - i; if (n > 32) n = 32;
    rres(ares, 8 + 12u * i, chunk, 12u * n);
    for (int k = 0; k < n; k++) {
      const uint8_t *r = chunk + 12 * k;
      QGlyph *g = &s_gdir[i + k];
      uint32_t off = rd32(r + 8);
      if (off > 0xFFFF) { qeng_close(); return false; }
      g->gid = rd16(r); g->w = r[2]; g->h = r[3];
      g->left = (int8_t)r[4]; g->top = (int8_t)r[5];
      g->adv = rd16(r + 6); g->off = (uint16_t)off;
    }
  }
  s_atl_bmp_base = 8 + 12u * s_n_glyphs;

  // tuples: file layout (gid u16, adv, dx, dy) matches QTuple; map gid -> index
  if (rres(tres, 8, s_tup, 5u * s_n_syms) != 5u * s_n_syms) { qeng_close(); return false; }
  for (int i = 0; i < s_n_syms; i++) {
    const QGlyph *g = find_gid(s_tup[i].gi);
    s_tup[i].gi = g ? (uint16_t)(g - s_gdir) : QD_NO_GLYPH;
  }
  const QGlyph *sg = find_gid(s_sign_gid);
  s_sign_gi = sg ? (uint16_t)(sg - s_gdir) : QD_NO_GLYPH;

  for (int i = 0; i < WC_SETS * 2; i++) s_wc[i].wid = 0xFFFF;
  memset(s_wc_lru, 0, sizeof(s_wc_lru));
  build_fast_table();
  s_zoom = zoom;
  pool_flush();
  g_qstats.pool_flushes = 0;
  return true;
}

const QTuple *qeng_tuple(uint16_t sym) { return sym < s_n_syms ? &s_tup[sym] : NULL; }
const QGlyph *qeng_glyph_by_index(uint16_t gi) { return gi < s_n_glyphs ? &s_gdir[gi] : NULL; }
const QGlyph *qeng_glyph_by_gid(uint16_t gid) { return find_gid(gid); }
uint16_t qeng_glyph_index(const QGlyph *g) { return (uint16_t)(g - s_gdir); }
uint16_t qeng_sign_gi(void) { return s_sign_gi; }

const uint8_t *qeng_glyph_bitmap(uint16_t gi) {
  if (gi >= s_n_glyphs) return NULL;
  const QGlyph *g = &s_gdir[gi];
  uint32_t len = (uint32_t)((g->w + 3) / 4) * g->h;
  if (len == 0) return s_pool;
  if (s_slot[gi] != 0xFFFF) { g_qstats.glyph_hits++; return s_pool + s_slot[gi]; }
  if (len > s_pool_size) return NULL;
  if (s_pool_used + len > s_pool_size) pool_flush();
  g_qstats.glyph_misses++;
  uint32_t off = s_pool_used;
  if (rres(QRES_ATL0 + s_zoom, s_atl_bmp_base + g->off, s_pool + off, len) != len) return NULL;
  s_slot[gi] = (uint16_t)off;
  s_pool_used += len;
  return s_pool + off;
}

// fast LEX reader: MSB-aligned 32-bit accumulator
typedef struct { const uint8_t *p, *end; uint32_t acc; int n; } FastRd;

static inline void fr_fill(FastRd *r) {
  while (r->n <= 24 && r->p < r->end) { r->acc |= (uint32_t)(*r->p++) << (24 - r->n); r->n += 8; }
}

static int fr_sym(FastRd *r) {
  fr_fill(r);
  uint16_t e = s_ft[r->acc >> (32 - FT_BITS)];
  int len = e & 15;
  if (e && len <= r->n) {
    r->acc <<= len; r->n -= len;
    return e >> 4;
  }
  uint32_t code = 0, first = 0, base = 0;
  for (int l = 1; l <= 32; l++) {
    if (r->n == 0) { fr_fill(r); if (r->n == 0) return -1; }
    code = (code << 1) | (r->acc >> 31);
    r->acc <<= 1; r->n--;
    uint32_t cnt = s_lex_counts[l];
    if (code - first < cnt) return (int)(base + (code - first));
    base += cnt;
    first = (first + cnt) << 1;
  }
  return -1;
}

int qeng_word_tuples(uint16_t wid, const uint16_t **out, int *width) {
  if (!s_wc || wid >= s_n_vocab) return 0;
  int set = (wid ^ (wid >> 6)) & (WC_SETS - 1);
  WEnt *w0 = &s_wc[set * 2];
  for (int way = 0; way < 2; way++) {
    if (w0[way].wid == wid) {
      s_wc_lru[set] = (uint8_t)(way ^ 1);
      g_qstats.word_hits++;
      *out = w0[way].syms;
      if (width) *width = w0[way].width;
      return w0[way].n;
    }
  }
  g_qstats.word_misses++;
  uint32_t blk = wid / s_lex_block;
  int skip = wid % s_lex_block;
  uint8_t e[8];
  rres(QRES_LEX, LEX_HDR + 4u * blk, e, (blk + 1 < s_lex_nblocks) ? 8 : 4);
  uint32_t off = rd32(e);
  uint32_t end = (blk + 1 < s_lex_nblocks) ? rd32(e + 4) : s_lex_stream_len;
  uint8_t buf[512];
  uint32_t span = (end > off) ? end - off : 0;
  if (span > sizeof(buf)) span = sizeof(buf);
  if (span == 0) return 0;
  rres(QRES_LEX, s_lex_stream_base + off, buf, span);
  FastRd r = { buf, buf + span, 0, 0 };
  WEnt *we = &w0[s_wc_lru[set]];
  s_wc_lru[set] ^= 1;
  we->wid = 0xFFFF;
  int n = 0;
  while (1) {
    int s = fr_sym(&r);
    if (s < 0) return 0;
    if (s == s_lex_end) {
      if (skip == 0) break;
      skip--;
    } else if (skip == 0 && n < QD_MAX_TUPLES_PER_WORD) {
      we->syms[n++] = (uint16_t)s;
    }
  }
  int wsum = 0;
  for (int i = 0; i < n; i++) {
    const QTuple *t = qeng_tuple(we->syms[i]);
    if (t) wsum += t->adv;
  }
  we->wid = wid; we->n = (uint8_t)n; we->width = (uint16_t)wsum;
  *out = we->syms;
  if (width) *width = wsum;
  return n;
}
