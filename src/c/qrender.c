#include "qrender.h"
#include <string.h>

static uint8_t *s_cv;           // 2bpp scratch canvas
static uint32_t s_cv_bytes;
static int s_cv_rows;
static int s_w = QR_DEFAULT_W, s_stride = QR_STRIDE_FOR(QR_DEFAULT_W);

void qr_set_width(int w) {
  if (w < 32) w = 32;
  if (w == s_w) return;
  s_w = w;
  s_stride = QR_STRIDE_FOR(w);
  qr_flush_layout_cache();
}
int qr_width(void) { return s_w; }
int qr_stride(void) { return s_stride; }

uint32_t qr_canvas_bytes(int zoom) {
  return (uint32_t)(qmeta_zoom(zoom)->line_h + 2 * QR_PAD) * s_stride;
}
void qr_set_canvas(uint8_t *buf, uint32_t bytes) {
  s_cv = buf; s_cv_bytes = bytes; s_cv_rows = (int)(bytes / s_stride);
}

// ---------------------------------------------------------------- codec
int qr_pack(const uint8_t *b, int n, uint8_t *out, int cap) {
  int i = 0, o = 0;
  while (i < n) {
    if (b[i] == 0) {
      int j = i;
      while (j < n && b[j] == 0 && j - i < 128) j++;
      if (o + 1 > cap) return -1;
      out[o++] = (uint8_t)(j - i - 1);
      i = j;
      continue;
    }
    int j = i;
    while (j < n && b[j] == b[i] && j - i < 66) j++;
    if (j - i >= 3) {
      if (o + 2 > cap) return -1;
      out[o++] = (uint8_t)(0xC0 | (j - i - 3));
      out[o++] = b[i];
      i = j;
      continue;
    }
    j = i;
    int len = 0;
    while (j < n && len < 64) {
      if (b[j] == 0) break;
      if (j + 2 < n && b[j] == b[j + 1] && b[j] == b[j + 2]) break;
      j++; len++;
    }
    if (o + 1 + len > cap) return -1;
    out[o++] = (uint8_t)(0x80 | (len - 1));
    memcpy(out + o, b + i, len);
    o += len;
    i = j;
  }
  return o;
}

int qr_unpack(const uint8_t *s, int clen, uint8_t *dst, int n) {
  int i = 0, o = 0;
  while (i < clen && o < n) {
    uint8_t t = s[i++];
    if (t < 0x80) {
      int k = t + 1; if (o + k > n) return -1;
      memset(dst + o, 0, k); o += k;
    } else if (t < 0xC0) {
      int k = (t & 63) + 1; if (o + k > n || i + k > clen) return -1;
      memcpy(dst + o, s + i, k); o += k; i += k;
    } else {
      int k = (t & 63) + 3; if (o + k > n || i >= clen) return -1;
      memset(dst + o, s[i++], k); o += k;
    }
  }
  return o;
}

// ---------------------------------------------------------------- blitting
static void blit(uint16_t gi, int x, int ybase) {
  const QGlyph *g = qeng_glyph_by_index(gi);
  if (!g || !g->w || !g->h) return;
  const uint8_t *bmp = qeng_glyph_bitmap(gi);
  if (!bmp) return;
  int stride = (g->w + 3) / 4;
  int x0 = x + g->left, y0 = ybase - g->top;
  for (int r = 0; r < g->h; r++) {
    int yy = y0 + r;
    if (yy < 0 || yy >= s_cv_rows) continue;
    const uint8_t *row = bmp + r * stride;
    uint8_t *dst = s_cv + yy * s_stride;
    for (int cb = 0; cb < stride; cb++) {
      uint8_t b = row[cb];
      if (!b) continue;
      for (int k = 0; k < 4; k++) {
        int lv = (b >> (2 * k)) & 3;
        if (!lv) continue;
        int col = cb * 4 + k;
        if (col >= g->w) break;
        int xx = x0 + col;
        if (xx < 0 || xx >= s_w) continue;
        int sh = (xx & 3) * 2;
        uint8_t *d = &dst[xx >> 2];
        if (lv > ((*d >> sh) & 3)) *d = (uint8_t)((*d & ~(3 << sh)) | (lv << sh));
      }
    }
  }
}

static int marker_width(void) {
  const QGlyph *g = qeng_glyph_by_index(qeng_sign_gi());
  return g ? g->adv : 0;
}

// Draws a tuple sequence left to right from pen (the left edge of its advance box).
static void draw_syms(const uint16_t *syms, int n, int pen, int baseline) {
  for (int i = 0; i < n; i++) {
    const QTuple *t = qeng_tuple(syms[i]);
    if (!t) continue;
    if (t->gi != QD_NO_GLYPH) blit(t->gi, pen + t->dx, baseline - t->dy);
    pen += t->adv;
  }
}

static int syms_width(const uint16_t *syms, int n) {
  int w = 0;
  for (int i = 0; i < n; i++) {
    const QTuple *t = qeng_tuple(syms[i]);
    if (t) w += t->adv;
  }
  return w;
}

static void draw_word(uint16_t wid, int pen, int baseline) {
  const uint16_t *syms;
  int n = qeng_word_tuples(wid, &syms, NULL);
  uint16_t local[QD_MAX_TUPLES_PER_WORD];     // cache slot may be reused while blitting
  memcpy(local, syms, n * sizeof(uint16_t));
  draw_syms(local, n, pen, baseline);
}

// Full-ink primitives (level 3), clipped to the canvas.
static void hline(int x0, int x1, int y) {
  if (y < 0 || y >= s_cv_rows) return;
  if (x0 < 0) x0 = 0;
  if (x1 > s_w - 1) x1 = s_w - 1;
  uint8_t *row = s_cv + y * s_stride;
  for (int x = x0; x <= x1; x++) row[x >> 2] |= (uint8_t)(3 << ((x & 3) * 2));
}

static void diamond(int cx, int cy, int r) {      // filled, |dx| + |dy| <= r
  for (int dy = -r; dy <= r; dy++) {
    int half = r - (dy < 0 ? -dy : dy);
    hline(cx - half, cx + half, cy + dy);
  }
}

static void draw_marker(int number, int pen_left, int baseline) {
  uint16_t sgi = qeng_sign_gi();
  const QGlyph *sg = qeng_glyph_by_index(sgi);
  if (!sg) return;
  int sign_adv = sg->adv;
  blit(sgi, pen_left, baseline);
  uint8_t digs[4];
  int nd = 0;
  if (number <= 0) number = 1;
  while (number > 0 && nd < 4) { digs[nd++] = (uint8_t)(number % 10); number /= 10; }
  int dw = 0;
  uint16_t dgi[4];
  for (int i = 0; i < nd; i++) {
    const QGlyph *g = qeng_glyph_by_gid((uint16_t)(QD_DIGIT_PSEUDO + digs[i]));
    dgi[i] = g ? qeng_glyph_index(g) : QD_NO_GLYPH;
    if (g) dw += g->adv;
  }
  int x = pen_left + (sign_adv - dw) / 2;
  int by = baseline - qmeta_zoom(qeng_zoom())->digit_raise;
  for (int i = nd - 1; i >= 0; i--) {          // most significant first
    if (dgi[i] == QD_NO_GLYPH) continue;
    blit(dgi[i], x, by);
    x += qeng_glyph_by_index(dgi[i])->adv;
  }
}

// ---------------------------------------------------------------- layout
// Tokens are an ayah's words followed by its end-of-ayah marker. The last word
// is kept on the same line as the marker; a token that does not fit starts a
// new line unless it is the first on its line.
#define LC_MAXL 96
typedef struct {
  int16_t g; int8_t zoom; uint8_t n_lines; uint8_t n_words; int16_t marker;
  uint8_t line_start[LC_MAXL + 1];
  uint16_t wids[QD_MAX_WORDS_PER_AYAH];
} Layout;

#define LC_N 2
static Layout s_lc[LC_N];
static uint8_t s_lc_next;
static Layout s_bism;           // bismillah layout (g = -2 when valid)

void qr_flush_layout_cache(void) {
  for (int i = 0; i < LC_N; i++) s_lc[i].g = -1;
  s_bism.g = -1;
}

static int tok_width(const Layout *L, int i) {
  if (i < L->n_words) {
    const uint16_t *s; int w = 0;
    qeng_word_tuples(L->wids[i], &s, &w);
    return w;
  }
  return marker_width();
}

static void layout_walk(Layout *L) {
  int space = qmeta_zoom(qeng_zoom())->space_w;
  int n_tok = L->n_words + (L->marker ? 1 : 0);
  int mw = L->marker ? marker_width() : 0;
  int xr = s_w - QR_MARGIN;
  int line = 0;
  bool first = true;
  L->line_start[0] = 0;
  for (int i = 0; i < n_tok; i++) {
    int w = tok_width(L, i);
    int eff = w;
    if (L->marker && i == L->n_words - 1) eff = w + space + mw;
    bool is_marker = L->marker && i == L->n_words;
    if (!first && !is_marker && xr - eff < QR_MARGIN) {
      if (line + 1 < LC_MAXL) line++;
      L->line_start[line] = (uint8_t)i;
      xr = s_w - QR_MARGIN;
      first = true;
    }
    xr -= w + space;
    first = false;
  }
  L->line_start[line + 1] = (uint8_t)n_tok;
  L->n_lines = (uint8_t)(line + 1);
}

static const Layout *ayah_layout(uint16_t g) {
  int zoom = qeng_zoom();
  for (int i = 0; i < LC_N; i++)
    if (s_lc[i].g == (int16_t)g && s_lc[i].zoom == zoom) return &s_lc[i];
  Layout *L = &s_lc[s_lc_next];
  s_lc_next = (uint8_t)((s_lc_next + 1) % LC_N);
  L->g = -1;
  int n = qdata_ayah_words(g, L->wids, QD_MAX_WORDS_PER_AYAH);
  if (n <= 0) return NULL;
  L->n_words = (uint8_t)n;
  L->marker = (int16_t)qmeta_ayah_no(g);
  layout_walk(L);
  L->g = (int16_t)g; L->zoom = (int8_t)zoom;
  return L;
}

static const Layout *bism_layout(void) {
  int zoom = qeng_zoom();
  if (s_bism.g == -2 && s_bism.zoom == zoom) return &s_bism;
  for (int i = 0; i < 4; i++) s_bism.wids[i] = qmeta_bismillah_word(i);
  s_bism.n_words = 4;
  s_bism.marker = 0;
  layout_walk(&s_bism);
  s_bism.g = -2; s_bism.zoom = (int8_t)zoom;
  return &s_bism;
}

int qr_bism_lines(uint16_t g) {
  const QSurah *s = qmeta_surah(qmeta_surah_of(g));
  if (!s->bismillah || g != s->first_global) return 0;
  return bism_layout()->n_lines;
}

int qr_sep_lines(uint16_t g) {
  return g == qmeta_surah(qmeta_surah_of(g))->first_global ? 1 : 0;
}

int qr_head_lines(uint16_t g) { return qr_sep_lines(g) + qr_bism_lines(g); }

int qr_layout_block_lines(uint16_t g) {
  const Layout *L = ayah_layout(g);
  return qr_head_lines(g) + (L ? L->n_lines : 1);
}

static void draw_line(const Layout *L, int k, int baseline, bool center_single) {
  if (k < 0 || k >= L->n_lines) return;
  int space = qmeta_zoom(qeng_zoom())->space_w;
  int shift = 0;
  if (center_single && L->n_lines == 1) {
    int total = 0;
    for (int i = 0; i < L->n_words; i++) total += tok_width(L, i);
    total += (L->n_words - 1) * space;
    shift = -((s_w - 2 * QR_MARGIN) - total) / 2;
  }
  int xr = s_w - QR_MARGIN;
  for (int i = L->line_start[k]; i < L->line_start[k + 1]; i++) {
    int w = tok_width(L, i);
    if (i < L->n_words) draw_word(L->wids[i], xr - w + shift, baseline);
    else draw_marker(L->marker, xr - w + shift, baseline);
    xr -= w + space;
  }
}

// Surah separator: the name centred on the baseline, flanked on each side by a
// double rule running from the margin towards the name and ending in a small
// diamond, a gap away from the name. All sizes derive from the zoom's ppem:
//   d   = (ppem + 4) / 8      rules at rows ym - d and ym + d, ym = baseline - ascent / 3
//   r   = d + 1               diamond radius (|dx| + |dy| <= r)
//   gap = 3 + ppem / 6        blank columns between the name and a diamond's tip
static void draw_separator(int surah_idx, int baseline) {
  const QZoom *z = qmeta_zoom(qeng_zoom());
  uint16_t syms[32];
  int n = qmeta_surah_name(surah_idx, syms, 32);
  int w = syms_width(syms, n);
  int nl = (s_w - w) / 2, nr = nl + w;          // name advance box [nl, nr)
  draw_syms(syms, n, nl, baseline);
  int ym = baseline - z->ascent / 3;
  int d = (z->ppem + 4) / 8, r = d + 1, gap = 3 + z->ppem / 6;
  int cl = nl - 1 - gap - r, cr = nr + gap + r;  // diamond centres
  int xl = QR_MARGIN, xr = s_w - QR_MARGIN - 1;
  if (cl - r >= xl) {
    hline(xl, cl, ym - d); hline(xl, cl, ym + d);
    diamond(cl, ym, r);
  }
  if (cr + r <= xr) {
    hline(cr, xr, ym - d); hline(cr, xr, ym + d);
    diamond(cr, ym, r);
  }
}

static int emit_record(uint16_t key, uint8_t line, int ref_row, uint8_t *out, int cap) {
  if (cap < QR_REC_HDR) return -1;
  int first = -1, last = -1;
  for (int r = 0; r < s_cv_rows; r++) {
    const uint8_t *row = s_cv + r * s_stride;
    for (int c = 0; c < s_stride; c++) if (row[c]) { if (first < 0) first = r; last = r; break; }
  }
  int y0 = 0, h = 0, clen = 0;
  if (first >= 0) {
    y0 = first - ref_row;
    h = last - first + 1;
    clen = qr_pack(s_cv + first * s_stride, h * s_stride, out + QR_REC_HDR, cap - QR_REC_HDR);
    if (clen < 0) return -1;
  }
  out[0] = (uint8_t)(key & 0xFF); out[1] = (uint8_t)(key >> 8);
  out[2] = line; out[3] = (uint8_t)qeng_zoom();
  out[4] = (uint8_t)(int8_t)y0; out[5] = (uint8_t)h;
  out[6] = (uint8_t)(clen & 0xFF); out[7] = (uint8_t)(clen >> 8);
  return QR_REC_HDR + clen;
}

int qr_render_strip(uint16_t g, int line, uint8_t *out, int cap) {
  if (!qeng_ready() || !s_cv || g >= QD_N_AYAH) return -1;
  const QZoom *z = qmeta_zoom(qeng_zoom());
  if ((uint32_t)s_cv_rows * s_stride < qr_canvas_bytes(qeng_zoom())) return -1;
  memset(s_cv, 0, (uint32_t)s_cv_rows * s_stride);
  int baseline = QR_PAD + z->ascent;
  int sl = qr_sep_lines(g), bl = qr_bism_lines(g);
  if (line < sl) {
    draw_separator(qmeta_surah_of(g), baseline);
  } else if (line < sl + bl) {
    draw_line(bism_layout(), line - sl, baseline, true);
  } else {
    const Layout *L = ayah_layout(g);
    if (!L) return -1;
    draw_line(L, line - sl - bl, baseline, false);
  }
  return emit_record(g, (uint8_t)line, QR_PAD, out, cap);
}

int qr_render_name(int surah_idx, uint8_t *out, int cap) {
  if (!qeng_ready() || !s_cv || surah_idx < 0 || surah_idx >= QD_N_SURAH) return -1;
  const QZoom *z = qmeta_zoom(qeng_zoom());
  if ((uint32_t)s_cv_rows * s_stride < qr_canvas_bytes(qeng_zoom())) return -1;
  memset(s_cv, 0, (uint32_t)s_cv_rows * s_stride);
  int baseline = QR_PAD + z->ascent;
  uint16_t syms[32];
  int n = qmeta_surah_name(surah_idx, syms, 32);
  draw_syms(syms, n, QR_NAME_RIGHT - syms_width(syms, n), baseline);
  return emit_record((uint16_t)surah_idx, 0, baseline, out, cap);
}
