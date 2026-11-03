#include "state.h"
#include "qdata.h"

// Persist keys (the whole app must stay under 4 KB of persisted data).
#define KEY_SETTINGS 10
#define KEY_POSITION 11
#define KEY_BOOKMARKS 12
#define KEY_RUN 13
// Version 1 keys, migrated once and deleted.
#define OLD_KEY_STATE 1
#define OLD_KEY_BOOKMARKS 2
#define OLD_TRANS_FIRST 100
#define OLD_TRANS_LAST 140

Settings g_set;
Position g_pos;

typedef struct __attribute__((packed)) {
  uint8_t count;
  uint16_t slots[BOOKMARK_SLOTS];
} BookmarkStore;
static BookmarkStore s_bm;

typedef struct __attribute__((packed)) {
  uint8_t running;       // 1 while the app is open
  uint8_t crashes;       // consecutive unclean exits
} RunState;
static RunState s_run;
static bool s_crashed;
static AppTimer *s_save_timer;

static void defaults(void) {
  memset(&g_set, 0, sizeof(g_set));
  g_set.version = SETTINGS_VERSION;
  g_set.zoom = ZOOM_DEFAULT;
  g_set.header = HEADER_POSITION;
  g_set.scroll = SCROLL_PAGE;
  g_set.theme = THEME_LIGHT;
  g_set.light = LIGHT_WARM;
  g_set.render = RENDER_AUTO;
  g_set.font = FONT_QURAN;
  g_set.sysfont = 1;
  g_set.trans = 1;
  g_set.glance = 1;
  g_set.paper = 0xFE;     // pastel yellow
  g_set.ink = 0xC4;       // dark green
}

void state_sanitize(void) {
  if (g_set.zoom >= QD_ZOOMS) g_set.zoom = ZOOM_DEFAULT;
  if (g_set.header >= HEADER_N) g_set.header = HEADER_POSITION;
  if (g_set.scroll >= SCROLL_N) g_set.scroll = SCROLL_PAGE;
  if (g_set.theme >= THEME_N) g_set.theme = THEME_LIGHT;
  if (g_set.light >= LIGHT_N) g_set.light = LIGHT_WARM;
  if (g_set.render >= RENDER_N) g_set.render = RENDER_AUTO;
  if (g_set.font >= FONT_N) g_set.font = FONT_QURAN;
  if (g_set.sysfont >= SYSFONT_N) g_set.sysfont = 1;
  if (g_set.trans >= TRANS_N) g_set.trans = 1;
  if (g_set.glance > 1) g_set.glance = 1;
  g_set.paper |= 0xC0; g_set.ink |= 0xC0;
  if (g_set.paper == g_set.ink) g_set.ink = (uint8_t)(g_set.paper ^ 0x3F);
  if (g_pos.ayah >= QD_N_AYAH) { g_pos.ayah = 0; g_pos.line = 0; }
  if (g_pos.zoom >= QD_ZOOMS && (g_pos.zoom < POS_SYS_BASE || g_pos.zoom >= POS_SYS_BASE + SYSFONT_N)) {
    g_pos.zoom = g_set.zoom; g_pos.line = 0;
  }
  if (s_bm.count > BOOKMARK_SLOTS) s_bm.count = 0;
  for (int i = 0; i < s_bm.count; i++) if (s_bm.slots[i] >= QD_N_AYAH) { s_bm.count = 0; break; }
}

static void migrate_v1(void) {
  // v1 layout: version, zoom, header, view, u16 ayah, u16 line, i16 px, scroll, prefetch
  uint8_t old[13];
  memset(old, 0, sizeof(old));
  if (persist_read_data(OLD_KEY_STATE, old, sizeof(old)) >= 8) {
    uint8_t zoom = old[1];
    if (old[0] == 1 && zoom <= 2) zoom += 2;
    else if (old[0] == 2 && zoom <= 3) zoom++;
    g_set.zoom = zoom;
    g_set.header = old[2];
    g_set.scroll = old[10];
    g_pos.ayah = (uint16_t)(old[4] | (old[5] << 8));
    g_pos.line = 0;
    g_pos.zoom = g_set.zoom;
  }
  if (persist_exists(OLD_KEY_BOOKMARKS)) {
    uint8_t b[1 + 2 * 10];
    memset(b, 0, sizeof(b));
    persist_read_data(OLD_KEY_BOOKMARKS, b, sizeof(b));
    s_bm.count = b[0] > 10 ? 0 : b[0];
    for (int i = 0; i < s_bm.count; i++) s_bm.slots[i] = (uint16_t)(b[1 + 2 * i] | (b[2 + 2 * i] << 8));
  }
  persist_delete(OLD_KEY_STATE);
  persist_delete(OLD_KEY_BOOKMARKS);
  for (uint32_t k = OLD_TRANS_FIRST; k <= OLD_TRANS_LAST; k++)
    if (persist_exists(k)) persist_delete(k);
}

void state_load(void) {
  defaults();
  memset(&g_pos, 0, sizeof(g_pos));
  g_pos.zoom = g_set.zoom;
  memset(&s_bm, 0, sizeof(s_bm));

  if (persist_exists(KEY_SETTINGS)) {
    Settings s;
    memset(&s, 0, sizeof(s));
    persist_read_data(KEY_SETTINGS, &s, sizeof(s));
    if (s.version == SETTINGS_VERSION) g_set = s;
  } else if (persist_exists(OLD_KEY_STATE) || persist_exists(OLD_KEY_BOOKMARKS)) {
    migrate_v1();
  }
  if (persist_exists(KEY_POSITION)) persist_read_data(KEY_POSITION, &g_pos, sizeof(g_pos));
  if (persist_exists(KEY_BOOKMARKS)) persist_read_data(KEY_BOOKMARKS, &s_bm, sizeof(s_bm));

  memset(&s_run, 0, sizeof(s_run));
  if (persist_exists(KEY_RUN)) persist_read_data(KEY_RUN, &s_run, sizeof(s_run));
  s_crashed = s_run.running == 1;
  if (s_crashed) {
    s_run.crashes++;
    APP_LOG(APP_LOG_LEVEL_WARNING, "previous run did not exit cleanly (%d in a row); resetting settings",
            s_run.crashes);
    defaults();
  }
  state_sanitize();
  s_run.running = 1;
  persist_write_data(KEY_RUN, &s_run, sizeof(s_run));
  state_save_settings();
  state_save_position();
  persist_write_data(KEY_BOOKMARKS, &s_bm, sizeof(s_bm));
}

bool state_crashed_last_time(void) { return s_crashed; }

void state_mark_clean_exit(void) {
  if (s_save_timer) { app_timer_cancel(s_save_timer); s_save_timer = NULL; }
  state_save_position();
  s_run.running = 0;
  s_run.crashes = 0;
  persist_write_data(KEY_RUN, &s_run, sizeof(s_run));
}

void state_save_settings(void) { persist_write_data(KEY_SETTINGS, &g_set, sizeof(g_set)); }
void state_save_position(void) { persist_write_data(KEY_POSITION, &g_pos, sizeof(g_pos)); }

static void save_cb(void *d) { s_save_timer = NULL; state_save_position(); }
void state_save_position_soon(void) {
  if (s_save_timer) app_timer_reschedule(s_save_timer, 1500);
  else s_save_timer = app_timer_register(1500, save_cb, NULL);
}

void settings_reset(void) {
  defaults();
  state_save_settings();
}

// ---------------------------------------------------------------- bookmarks
static void bm_save(void) { persist_write_data(KEY_BOOKMARKS, &s_bm, sizeof(s_bm)); }
int bookmarks_count(void) { return s_bm.count; }
uint16_t bookmark_get(int i) { return s_bm.slots[i]; }

int bookmark_find(uint16_t ayah) {
  for (int i = 0; i < s_bm.count; i++) if (s_bm.slots[i] == ayah) return i;
  return -1;
}

bool bookmark_add(uint16_t ayah) {
  if (s_bm.count >= BOOKMARK_SLOTS || bookmark_find(ayah) >= 0) return false;
  // keep bookmarks in Quran order
  int i = s_bm.count;
  while (i > 0 && s_bm.slots[i - 1] > ayah) { s_bm.slots[i] = s_bm.slots[i - 1]; i--; }
  s_bm.slots[i] = ayah;
  s_bm.count++;
  bm_save();
  return true;
}

void bookmark_remove(int i) {
  if (i < 0 || i >= s_bm.count) return;
  for (int j = i; j < s_bm.count - 1; j++) s_bm.slots[j] = s_bm.slots[j + 1];
  s_bm.count--;
  bm_save();
}

// ---------------------------------------------------------------- names
static const uint32_t LIGHT_RGB[LIGHT_N] = { 0, 0xFFD9A0, 0xFFA030, 0xFF2000, 0x40FF60, 0x4080FF };
static const char *LIGHT_NAMES[LIGHT_N] = { "Default", "Warm white", "Amber", "Red", "Green", "Blue" };
static const char *THEME_NAMES[THEME_N] = { "Light", "Dark", "Night (red)", "Night (amber)", "Sepia (cream)",
                                            "Midnight blue", "Custom (phone)" };
// paper, ink per preset (GColor8 argb: 0b11rrggbb)
static const uint8_t THEME_COLORS[THEME_N - 1][2] = {
  { 0xFF, 0xC0 },   // white / black
  { 0xC0, 0xFF },   // black / white
  { 0xC0, 0xF0 },   // black / red
  { 0xC0, 0xF8 },   // black / chrome yellow
  { 0xFE, 0xC0 },   // pastel yellow (cream) / black
  { 0xC1, 0xFE },   // oxford blue / pastel yellow
};
void theme_colors(int t, uint8_t *paper, uint8_t *ink) {
  if (t == THEME_CUSTOM) { *paper = g_set.paper; *ink = g_set.ink; return; }
  if (t < 0 || t >= THEME_N) t = THEME_LIGHT;
  *paper = THEME_COLORS[t][0]; *ink = THEME_COLORS[t][1];
}
static const char *HEADER_NAMES[HEADER_N] = { "Off", "Surah & ayah", "Surah, ayah & juz'", "Clock" };

static const char *TRANS_NAMES[TRANS_N] = {
  "None", "English: Sahih Intl.", "English: M. Khattab", "English: Abdel Haleem", "English: Yusuf Ali",
  "French: Hamidullah", "German: Bubenheim", "Spanish: Isa Garcia", "Turkish: Diyanet",
  "Indonesian: Kemenag", "Malay: Basmeih", "Dutch: Siregar", "Italian: Piccardo",
  "Portuguese: El Hayek", "Bosnian: Korkut" };
const char *trans_name(int i) { return (i >= 0 && i < TRANS_N) ? TRANS_NAMES[i] : ""; }

uint32_t light_rgb(int p) { return (p >= 0 && p < LIGHT_N) ? LIGHT_RGB[p] : 0; }
const char *light_name(int p) { return (p >= 0 && p < LIGHT_N) ? LIGHT_NAMES[p] : ""; }
const char *theme_name(int t) { return (t >= 0 && t < THEME_N) ? THEME_NAMES[t] : ""; }
const char *header_name(int h) { return (h >= 0 && h < HEADER_N) ? HEADER_NAMES[h] : ""; }
