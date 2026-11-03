#include "systext.h"
#include "comm.h"
#include "state.h"
#include "qdata.h"
#include "ui.h"
#include "names_ar.h"
#include "profile.h"

#define ST_MAX 48
#define ST_BYTES 14000               // cached text (UTF-8) kept on the watch
#define REQ_AYAHS 24
#define REQ_TIMEOUT_MS 4000
#define MARGIN 6
// The firmware draws a right-to-left line through a small buffer and cuts it
// after ~32 code points / ~62 bytes. Lines are broken here, below that limit.
#define LINE_MAX_CP 30
#define LINE_MAX_BYTES 58
#define JOIN '\x1f'                  // the phone joins the ayah number to the last word with this
#define MAX_LINES 96

typedef struct {
  int16_t ayah;
  uint8_t n_lines;
  uint16_t len;
  char *text;
  uint16_t *brk;                     // n_lines + 1 byte offsets (line starts, then the end)
} SEnt;

static SEnt s_e[ST_MAX];
static uint32_t s_bytes;
static bool s_open;
static SysTextUpdate s_cb;
static uint32_t s_req;
static bool s_inflight;
static AppTimer *s_timer;
static uint16_t s_focus;
static int s_ahead = 12;
static GFont s_font;
static int s_font_idx = -1, s_pitch = 28;
static int8_t s_digits = -1;         // Arabic-Indic digits available in the system font
static uint16_t s_bism_brk[8];
static uint8_t s_bism_lines = 1;

static const QSurah *surah_of(uint16_t g) { return qmeta_surah(qmeta_surah_of(g)); }
static bool opens_surah(uint16_t g) { return surah_of(g)->first_global == g; }
static int head_lines(uint16_t g) {
  if (!opens_surah(g)) return 0;
  return 1 + (surah_of(g)->bismillah ? s_bism_lines : 0);   // decorated header, then the basmala
}

static GSize measure(const char *t, int w) {
  return graphics_text_layout_get_content_size(t, s_font, GRect(0, 0, w, 400),
                                               GTextOverflowModeWordWrap, GTextAlignmentRight);
}

// copies text[a, b) for drawing (joiners become spaces)
static const char *slice(const char *text, int a, int b) {
  static char buf[LINE_MAX_BYTES + 64];
  int n = b - a;
  if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
  for (int i = 0; i < n; i++) buf[i] = text[a + i] == JOIN ? ' ' : text[a + i];
  buf[n] = 0;
  return buf;
}

static int cps(const char *t, int a, int b) {        // UTF-8 code points in [a, b)
  int n = 0;
  for (int i = a; i < b; i++) if (((uint8_t)t[i] & 0xC0) != 0x80) n++;
  return n;
}

static bool fits(const char *t, int a, int b) {
  if (b - a > LINE_MAX_BYTES || cps(t, a, b) > LINE_MAX_CP) return false;
  return measure(slice(t, a, b), 1000).w <= profile_get()->col_w - 2 * MARGIN;
}

// Greedy line breaking at spaces. Returns the number of lines; brk gets n + 1 offsets.
static int wrap(const char *t, int len, uint16_t *brk, int max) {
  int n = 0, start = 0, end = -1;          // end: end of the last word that fits on this line
  int i = 0;
  brk[0] = 0;
  while (i <= len) {
    if (i < len && t[i] != ' ') { i++; continue; }
    // a word ends at i
    if (fits(t, start, i)) {
      end = i;
    } else if (end < 0) {
      end = i;                             // a single word longer than a line: on its own
    } else {
      if (n + 1 >= max) break;
      brk[++n] = (uint16_t)(end + 1);      // next line starts after the space
      start = end + 1;
      end = -1;
      continue;                            // the same word again, now first on its line
    }
    i++;
  }
  brk[++n] = (uint16_t)len;
  return n;
}

static void setup_font(void) {
  if (s_font_idx == g_set.sysfont) return;
  s_font_idx = g_set.sysfont;
  s_font = fonts_get_system_font(g_set.sysfont ? FONT_KEY_GOTHIC_28 : FONT_KEY_GOTHIC_24);
  // line spacing as the engine wraps paragraphs: a word just wider than half a
  // line gets a line of its own, so four of them take exactly four lines
  const int tw = profile_get()->col_w - 2 * MARGIN;
  static char word[2 * 40 + 1], four[4 * (2 * 40 + 1)];
  int k = 4;
  for (; k < 40; k++) {
    for (int i = 0; i < k; i++) { word[2 * i] = (char)0xd8; word[2 * i + 1] = (char)0xa8; }
    word[2 * k] = 0;
    if (measure(word, 1000).w > tw / 2 + 6) break;
  }
  int h1 = measure(word, tw).h;
  snprintf(four, sizeof(four), "%s %s %s %s", word, word, word, word);
  int h4 = measure(four, tw).h;
  s_pitch = h4 > h1 ? (h4 - h1 + 1) / 3 : h1;
  if (s_pitch < 12) s_pitch = 12;
  s_bism_lines = (uint8_t)wrap(NAMES_AR_BASMALA, strlen(NAMES_AR_BASMALA), s_bism_brk, ARRAY_LENGTH(s_bism_brk));
  if (s_digits < 0) {
    GSize d = measure("\xd9\xa1\xd9\xa2\xd9\xa3", 180), b = measure("\xe2\x96\xaf\xe2\x96\xaf\xe2\x96\xaf", 180);
    s_digits = d.w > 0 && d.w != b.w;
  }
}

static void drop(SEnt *e);

static void rewrap(SEnt *e) {
  static uint16_t tmp[MAX_LINES + 1];
  free(e->brk);
  e->brk = NULL;
  int n = wrap(e->text, e->len, tmp, MAX_LINES);
  e->brk = malloc(sizeof(uint16_t) * (n + 1));
  if (!e->brk) { drop(e); return; }
  memcpy(e->brk, tmp, sizeof(uint16_t) * (n + 1));
  e->n_lines = (uint8_t)n;
}

static void font_changed(void) {
  int before = s_font_idx;
  setup_font();
  if (before == s_font_idx) return;
  for (int i = 0; i < ST_MAX; i++) if (s_e[i].text) rewrap(&s_e[i]);
}

bool systext_usable(void) {
  return g_set.font == FONT_SYSTEM && ui_system_arabic_available() && comm_phone_ready();
}

int systext_pitch(void) { font_changed(); return s_pitch; }

static SEnt *find(uint16_t g) {
  for (int i = 0; i < ST_MAX; i++) if (s_e[i].text && s_e[i].ayah == (int16_t)g) return &s_e[i];
  return NULL;
}

int systext_block_lines(uint16_t g) {
  SEnt *e = find(g);
  return e ? head_lines(g) + e->n_lines : 0;
}

static void drop(SEnt *e) {
  if (!e->text) return;
  s_bytes -= e->len + 1;
  free(e->text);
  free(e->brk);
  e->text = NULL;
  e->brk = NULL;
  e->ayah = -1;
}

static int32_t dist(int16_t g) {
  int32_t d = (int32_t)g - s_focus;
  return d < 0 ? -3 * d : d;          // keep more ahead than behind
}

static void insert(uint16_t g, const char *t, int len) {
  if (find(g) || len <= 0) return;
  while (s_bytes + len + 1 > ST_BYTES) {
    SEnt *far = NULL;
    for (int i = 0; i < ST_MAX; i++)
      if (s_e[i].text && (!far || dist(s_e[i].ayah) > dist(far->ayah))) far = &s_e[i];
    if (!far || dist(far->ayah) <= dist((int16_t)g)) return;
    drop(far);
  }
  SEnt *slot = NULL;
  for (int i = 0; i < ST_MAX; i++) if (!s_e[i].text) { slot = &s_e[i]; break; }
  if (!slot) {
    for (int i = 0; i < ST_MAX; i++) if (!slot || dist(s_e[i].ayah) > dist(slot->ayah)) slot = &s_e[i];
    if (dist(slot->ayah) <= dist((int16_t)g)) return;
    drop(slot);
  }
  slot->text = malloc(len + 1);
  if (!slot->text) return;
  memcpy(slot->text, t, len);
  slot->text[len] = 0;
  slot->len = (uint16_t)len;
  slot->ayah = (int16_t)g;
  s_bytes += len + 1;
  rewrap(slot);
}

static void request_more(void);

static void timeout_cb(void *d) { s_timer = NULL; s_inflight = false; request_more(); }

static void request_more(void) {
  if (!s_open || s_inflight || !comm_phone_ready()) return;
  int from = -1, to = s_focus + s_ahead;
  if (to > QD_N_AYAH) to = QD_N_AYAH;
  for (int g = s_focus; g < to && from < 0; g++) if (!find(g)) from = g;              // ahead first
  for (int g = s_focus > 2 ? s_focus - 2 : 0; g < s_focus && from < 0; g++) if (!find(g)) from = g;
  if (from < 0) return;
  int n = 0;
  while (n < REQ_AYAHS && from + n < QD_N_AYAH && !find(from + n)) n++;
  uint8_t flags = TF_ARABIC | (s_digits > 0 ? TF_ARABIC_DIGITS : 0);
  if (comm_request_text(++s_req, (uint16_t)from, n, flags)) {
    s_inflight = true;
    if (s_timer) app_timer_reschedule(s_timer, REQ_TIMEOUT_MS);
    else s_timer = app_timer_register(REQ_TIMEOUT_MS, timeout_cb, NULL);
  } else if (!s_timer) {
    s_timer = app_timer_register(300, timeout_cb, NULL);   // outbox busy: retry soon
  }
}

static void on_text(uint32_t req, uint16_t first, int count, const char *ar, const char *tr, uint8_t err) {
  if (!s_open || !ar) return;
  if (req == s_req) {
    s_inflight = false;
    if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  }
  font_changed();
  const char *p = ar;
  for (int i = 0; i < count && first + i < QD_N_AYAH; i++) {
    const char *q = strchr(p, TEXT_SEP);
    int len = q ? (int)(q - p) : (int)strlen(p);
    insert((uint16_t)(first + i), p, len);
    if (!q) break;
    p = q + 1;
  }
  if (s_cb) s_cb();
  request_more();
}

void systext_open(SysTextUpdate cb) {
  s_cb = cb;
  if (s_open) return;
  s_open = true;
  for (int i = 0; i < ST_MAX; i++) s_e[i].ayah = -1;
  comm_add_text_handler(on_text);
}

void systext_close(void) {
  s_open = false;
  s_inflight = false;
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  for (int i = 0; i < ST_MAX; i++) drop(&s_e[i]);
  s_bytes = 0;
}

void systext_want(uint16_t top, int ahead) {
  font_changed();
  s_focus = top;
  s_ahead = ahead < 4 ? 4 : ahead;
  request_more();
}

// ---------------------------------------------------------------- drawing
static void draw_line(GContext *ctx, const char *s, int y, int width, GTextAlignment al) {
  graphics_draw_text(ctx, s, s_font, GRect(MARGIN, y, width - 2 * MARGIN, s_pitch + s_pitch / 2),
                     GTextOverflowModeWordWrap, al, NULL);
}

static void draw_header(GContext *ctx, uint16_t g, int y, int width, GColor ink) {
  // decorated rule with the surah name in the middle, as in printed mushafs
  static char name[48];
  snprintf(name, sizeof(name), "\xd8\xb3\xd9\x88\xd8\xb1\xd8\xa9 %s", NAMES_AR[qmeta_surah_of(g)]);
  GSize sz = measure(name, 1000);
  int nx = (width - sz.w) / 2;
  draw_line(ctx, name, y, width, GTextAlignmentCenter);
  int mid = y + s_pitch / 2 + 2;
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_fill_color(ctx, ink);
  int l0 = MARGIN, l1 = nx - 10, r0 = nx + sz.w + 10, r1 = width - MARGIN;
  if (l1 > l0 + 8) {
    graphics_draw_line(ctx, GPoint(l0, mid - 2), GPoint(l1, mid - 2));
    graphics_draw_line(ctx, GPoint(l0, mid + 2), GPoint(l1, mid + 2));
    graphics_draw_line(ctx, GPoint(r0, mid - 2), GPoint(r1, mid - 2));
    graphics_draw_line(ctx, GPoint(r0, mid + 2), GPoint(r1, mid + 2));
    graphics_fill_circle(ctx, GPoint(l1 + 3, mid), 3);
    graphics_fill_circle(ctx, GPoint(r0 - 3, mid), 3);
    graphics_fill_circle(ctx, GPoint(l0, mid), 2);
    graphics_fill_circle(ctx, GPoint(r1, mid), 2);
  }
}

void systext_draw(GContext *ctx, uint16_t g, int first, int n, int y, int width, GColor ink) {
  SEnt *e = find(g);
  if (!e) return;
  font_changed();
  graphics_context_set_text_color(ctx, ink);
  int head = head_lines(g);
  for (int L = first; L < first + n; L++) {
    int ly = y + (L - first) * s_pitch;
    if (L < head) {
      if (L == 0) draw_header(ctx, g, ly, width, ink);
      else {
        int b = L - 1;
        draw_line(ctx, slice(NAMES_AR_BASMALA, s_bism_brk[b], s_bism_brk[b + 1]), ly, width, GTextAlignmentCenter);
      }
      continue;
    }
    int t = L - head;
    if (t >= e->n_lines) break;
    int a = e->brk[t], b = e->brk[t + 1];
    while (b > a && e->text[b - 1] == ' ') b--;
    draw_line(ctx, slice(e->text, a, b), ly, width, GTextAlignmentRight);
  }
}
