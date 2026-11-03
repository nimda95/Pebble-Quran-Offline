#include "strips.h"
#include "qrender.h"
#include "state.h"

#define SC_MAX_ENT 192

typedef struct { uint16_t ayah; uint8_t line, zoom; uint16_t off, len; } SEnt;

static uint8_t *s_arena;
static uint32_t s_cap, s_used;
static SEnt *s_ent;                 // sorted by arena offset
static int s_n;
static int s_focus_zoom;
static uint16_t s_focus_ayah;
static uint8_t s_focus_line;

bool strips_init(uint32_t cap) {
  strips_deinit();
  if (cap > 60000) cap = 60000;
  s_ent = malloc(sizeof(SEnt) * SC_MAX_ENT);
  s_arena = s_ent ? malloc(cap) : NULL;
  if (!s_arena) { strips_deinit(); return false; }
  s_cap = cap;
  s_used = 0;
  s_n = 0;
  return true;
}

void strips_deinit(void) {
  free(s_arena); s_arena = NULL;
  free(s_ent); s_ent = NULL;
  s_cap = s_used = 0;
  s_n = 0;
}

static int victim(void);

bool strips_resize(uint32_t cap) {
  if (cap > 60000) cap = 60000;
  if (!s_arena) return strips_init(cap);
  if (cap == s_cap) return true;
  // drop the farthest entries until the rest fits, then move into a new arena
  while (s_used > cap && s_n > 0) {
    int v = victim();
    uint32_t off = s_ent[v].off, len = s_ent[v].len;
    memmove(s_arena + off, s_arena + off + len, s_used - off - len);
    s_used -= len;
    for (int k = v + 1; k < s_n; k++) { s_ent[k - 1] = s_ent[k]; s_ent[k - 1].off -= len; }
    s_n--;
  }
  // shrinking happens in place; growing may move the arena
  uint8_t *na = realloc(s_arena, cap);
  if (!na) return cap < s_cap;   // a failed shrink keeps the old (larger) arena
  s_arena = na;
  s_cap = cap;
  return true;
}

uint32_t strips_capacity(void) { return s_cap; }
void strips_clear(void) { s_used = 0; s_n = 0; }
int strips_count(void) { return s_n; }

void strips_set_focus(int zoom, uint16_t ayah, uint8_t line) {
  s_focus_zoom = zoom; s_focus_ayah = ayah; s_focus_line = line;
}

// Approximate distance in lines from the focus; lines behind count double.
static int32_t distance(int zoom, uint16_t ayah, uint8_t line) {
  if (zoom != s_focus_zoom) return 1000000;
  int32_t d = ((int32_t)ayah - s_focus_ayah) * 16 + ((int32_t)line - s_focus_line);
  return d < 0 ? -2 * d + 1 : d;
}

static void remove_at(int i) {
  uint32_t off = s_ent[i].off, len = s_ent[i].len;
  memmove(s_arena + off, s_arena + off + len, s_used - off - len);
  s_used -= len;
  for (int k = i + 1; k < s_n; k++) { s_ent[k - 1] = s_ent[k]; s_ent[k - 1].off -= len; }
  s_n--;
}

static int32_t s_victim_d;
static int victim(void) {
  int best = -1;
  s_victim_d = -1;
  for (int i = 0; i < s_n; i++) {
    int32_t d = distance(s_ent[i].zoom, s_ent[i].ayah, s_ent[i].line);
    if (d > s_victim_d) { s_victim_d = d; best = i; }
  }
  return best;
}

const uint8_t *strips_get(int zoom, uint16_t ayah, uint8_t line) {
  if (!s_arena) return NULL;
  for (int i = s_n - 1; i >= 0; i--) {
    const SEnt *e = &s_ent[i];
    if (e->ayah == ayah && e->line == line && e->zoom == zoom) return s_arena + e->off;
  }
  return NULL;
}

bool strips_put(const uint8_t *rec, int len, bool force) {
  if (!s_arena || len < QR_REC_HDR || (uint32_t)len > s_cap) return false;
  uint16_t ayah = (uint16_t)(rec[0] | (rec[1] << 8));
  uint8_t line = rec[2], zoom = rec[3];
  int clen = rec[6] | (rec[7] << 8);
  if (clen + QR_REC_HDR != len) return false;
  if (strips_get(zoom, ayah, line)) return true;
  int32_t mine = distance(zoom, ayah, line);
  while (s_cap - s_used < (uint32_t)len || s_n >= SC_MAX_ENT) {
    int v = victim();
    if (v < 0) return false;
    if (!force && s_victim_d <= mine) return false;   // would evict something more useful
    remove_at(v);
  }
  memcpy(s_arena + s_used, rec, len);
  s_ent[s_n] = (SEnt){ .ayah = ayah, .line = line, .zoom = zoom, .off = (uint16_t)s_used, .len = (uint16_t)len };
  s_n++;
  s_used += len;
  return true;
}

// ---------------------------------------------------------------- drawing
static uint8_t s_pal[4];            // GColor8 argb per level
static uint8_t s_rev[256];          // colour -> level, for darker-wins blending

// Anti-aliasing levels 1 and 2 are blended between paper and ink per 2-bit channel.
static uint8_t blend(uint8_t paper, uint8_t ink, int k) {
  uint8_t out = 0xC0;
  for (int sh = 0; sh <= 4; sh += 2) {
    int p = (paper >> sh) & 3, i = (ink >> sh) & 3;
    out |= (uint8_t)(((p * (3 - k) + i * k + 1) / 3) << sh);
  }
  return out;
}

void strips_set_theme(int theme) {
  uint8_t paper, ink;
  theme_colors(theme, &paper, &ink);
  for (int k = 0; k < 4; k++) s_pal[k] = blend(paper | 0xC0, ink | 0xC0, k);
  memset(s_rev, 0, sizeof(s_rev));
  for (int i = 1; i < 4; i++) s_rev[s_pal[i]] = (uint8_t)i;
}

GColor strips_color(int level) { return (GColor){ .argb = s_pal[level & 3] }; }

static void draw_with(const uint8_t *rec, int rs, uint8_t *fb, int stride, int fb_w, int fb_h, int x_off, int y_box,
                      const uint8_t *pal, const uint8_t *rev) {
  int y0 = (int8_t)rec[4], h = rec[5];
  int clen = rec[6] | (rec[7] << 8);
  if (!h) return;
  const uint8_t *src = rec + QR_REC_HDR;
  int row = 0, col = 0;                      // position in the h x rs output (rs: record row bytes)
  int y = y_box + y0;
  uint8_t *line = (y >= 0 && y < fb_h) ? fb + y * stride : NULL;
  int total = h * rs, o = 0, i = 0;
#define ADVANCE(k) do { col += (k); while (col >= rs) { col -= rs; row++; y++; \
                         line = (y >= 0 && y < fb_h) ? fb + y * stride : NULL; } o += (k); } while (0)
  while (i < clen && o < total) {
    uint8_t t = src[i++];
    int k;
    if (t < 0x80) { k = t + 1; ADVANCE(k); continue; }
    bool lit = t < 0xC0;
    k = lit ? (t & 63) + 1 : (t & 63) + 3;
    uint8_t rep = 0;
    if (!lit) { if (i >= clen) break; rep = src[i++]; }
    if (lit && i + k > clen) break;          // malformed record: stop
    for (int n = 0; n < k && o < total; n++) {
      uint8_t b = lit ? src[i++] : rep;
      if (b && line) {
        int x = x_off + col * 4;
        for (int p = 0; p < 4; p++, x++) {
          int lv = (b >> (2 * p)) & 3;
          if (!lv || x < 0 || x >= fb_w) continue;
          uint8_t *d = &line[x];
          if (lv > rev[*d]) *d = pal[lv];
        }
      }
      ADVANCE(1);
    }
  }
#undef ADVANCE
}

void strips_draw(const uint8_t *rec, int rs, uint8_t *fb, int stride, int fb_w, int fb_h, int x_off, int y_box) {
  draw_with(rec, rs, fb, stride, fb_w, fb_h, x_off, y_box, s_pal, s_rev);
}

void strips_draw_colors(const uint8_t *rec, int rs, uint8_t *fb, int stride, int fb_w, int fb_h, int x_off, int y,
                        GColor paper, GColor ink) {
  uint8_t pal[4], rev[256];
  for (int k = 0; k < 4; k++) pal[k] = blend(paper.argb | 0xC0, ink.argb | 0xC0, k);
  memset(rev, 0, sizeof(rev));
  for (int i = 1; i < 4; i++) rev[pal[i]] = (uint8_t)i;
  draw_with(rec, rs, fb, stride, fb_w, fb_h, x_off, y, pal, rev);
}
