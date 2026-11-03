#include "reader.h"
#include "source.h"
#include "strips.h"
#include "systext.h"
#include "state.h"
#include "qdata.h"
#include "ui.h"
#include "menus.h"
#include "textview.h"
#include "comm.h"
#include "profile.h"
#include "qrender.h"
#include "i18n.h"

#define MAX_VIS 16
#define SYS_WAIT_MS 2500             // system-font text this late -> our glyphs for this page
#define SYS_HOLD_S 30                // ... and for this long afterwards
#define SYS_AHEAD 12                 // ayahs of text kept ahead in system-font mode

static Window *s_win;
static Layer *s_page, *s_text;
static TextLayer *s_hdr;
static StatusBarLayer *s_sb;
static char s_hdr_buf[64];

static int s_zoom, s_vis = 1, s_pitch = 1, s_text_top, s_hdr_h;
static LinePos s_top;
static bool s_open;                  // reader window is on the stack (cache allocated)
static bool s_mem_error;
static bool s_sys;                   // this page uses the Pebble system font
static time_t s_sys_hold;
static AppTimer *s_sys_timer;

bool reader_is_open(void) { return s_open; }
uint16_t reader_current_ayah(void) { return s_top.g; }

// ---------------------------------------------------------------- lines (both modes)
static int blk(uint16_t g) { return s_sys ? systext_block_lines(g) : qmeta_block_lines(s_zoom, g); }

static bool nxt(LinePos *p) {
  int b = blk(p->g);
  if (b <= 0) return false;                       // text not here yet
  if (p->l + 1 < b) { p->l++; return true; }
  if (p->g + 1 >= QD_N_AYAH) return false;
  p->g++; p->l = 0;
  return true;
}

static bool prv(LinePos *p) {
  if (p->l > 0) { p->l--; return true; }
  if (p->g == 0) return false;
  int b = blk(p->g - 1);
  if (b <= 0) return false;
  p->g--; p->l = (uint8_t)(b - 1);
  return true;
}

static bool want_sys(void) { return systext_usable() && time(NULL) >= s_sys_hold; }
static uint8_t pos_key(void) { return s_sys ? (uint8_t)(POS_SYS_BASE + g_set.sysfont) : (uint8_t)s_zoom; }

// ---------------------------------------------------------------- layout
static void compute_layout(void) {
  GRect b = layer_get_bounds(window_get_root_layer(s_win));
  s_zoom = g_set.zoom;
  s_sys = want_sys();
  const Profile *pf = profile_get();
  s_hdr_h = g_set.header == HEADER_OFF ? 0 : STATUS_BAR_LAYER_HEIGHT;
  int top = s_hdr_h > pf->band_top ? s_hdr_h : pf->band_top;
  int bottom = pf->band_bot < b.size.h ? pf->band_bot : b.size.h;
  int avail = bottom - top;
  int lh = s_sys ? systext_pitch() : qmeta_zoom(s_zoom)->line_h;
  if (lh < 8) lh = 8;
  s_vis = avail / lh;
  if (s_vis < 1) s_vis = 1;
  if (s_vis > MAX_VIS) s_vis = MAX_VIS;
  // our glyphs: spread spare pixels between lines; system font: keep its own line spacing
  s_pitch = s_sys ? lh : avail / s_vis;
  s_text_top = top + (avail - s_vis * s_pitch) / 2;
  // marks below the last line need some room; nothing else is drawn there
  int below = s_sys ? s_pitch / 2 : 0;
  if (s_text_top + s_vis * s_pitch + below > bottom) below = bottom - s_text_top - s_vis * s_pitch;
  layer_set_frame(s_text, GRect(pf->col_x, s_text_top, pf->col_w, s_vis * s_pitch + (below > 0 ? below : 0)));
  layer_set_hidden(s_text, !s_sys);

  if (g_pos.zoom != pos_key()) { g_pos.line = 0; g_pos.zoom = pos_key(); }
  if (g_pos.ayah >= QD_N_AYAH) g_pos.ayah = 0;
  int bl = blk(g_pos.ayah);
  if (bl > 0 && g_pos.line >= bl) g_pos.line = (uint8_t)(bl - 1);
  s_top = (LinePos){ g_pos.ayah, g_pos.line };
}

static void update_header(void) {
  bool clock = g_set.header == HEADER_CLOCK;
  layer_set_hidden(status_bar_layer_get_layer(s_sb), !clock);
  layer_set_hidden(text_layer_get_layer(s_hdr), clock || g_set.header == HEADER_OFF);
  ui_style_status_bar(s_sb);
  text_layer_set_background_color(s_hdr, ui_bg());
  text_layer_set_text_color(s_hdr, ui_fg());
  if (g_set.header == HEADER_POSITION || g_set.header == HEADER_POSITION_JUZ) {
    int si = qmeta_surah_of(s_top.g);
    const char *bm = bookmark_find(s_top.g) >= 0 ? " \xe2\x80\xa2" : "";
    if (g_set.header == HEADER_POSITION_JUZ) {
      char juz[24];
      snprintf(juz, sizeof(juz), _(S_JUZ_N), qmeta_juz_of(s_top.g) + 1);
      snprintf(s_hdr_buf, sizeof(s_hdr_buf), "%s %d:%d  %s%s", i18n_surah_name(si), si + 1,
               qmeta_ayah_no(s_top.g), juz, bm);
    } else {
      snprintf(s_hdr_buf, sizeof(s_hdr_buf), "%s  %d:%d%s", i18n_surah_name(si), si + 1,
               qmeta_ayah_no(s_top.g), bm);
    }
    text_layer_set_text(s_hdr, s_hdr_buf);
  }
}

// ---------------------------------------------------------------- drawing
static void dots(GContext *ctx, int cx, int cy) {
  graphics_context_set_fill_color(ctx, strips_color(2));
  for (int d = -1; d <= 1; d++) graphics_fill_circle(ctx, GPoint(cx + d * 12, cy), 2);
}

static void page_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, strips_color(0));
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  if (!s_sys) {
    bool missing[MAX_VIS];
    memset(missing, 0, sizeof(missing));
    int lh = qmeta_zoom(s_zoom)->line_h;
    GBitmap *fb = graphics_capture_frame_buffer(ctx);
    if (fb) {
      GRect fbb = gbitmap_get_bounds(fb);
      bool ok = gbitmap_get_format(fb) == GBitmapFormat8Bit;
      uint8_t *data = gbitmap_get_data(fb);
      int stride = gbitmap_get_bytes_per_row(fb);
      LinePos p = s_top;
      for (int k = 0; k < s_vis; k++) {
        const uint8_t *rec = source_get(p);
        int y_box = s_text_top + k * s_pitch + (s_pitch - lh) / 2;
        if (rec && ok) strips_draw(rec, QR_STRIDE_FOR(profile_get()->col_w), data, stride, fbb.size.w, fbb.size.h,
                                   profile_get()->col_x, y_box);
        else missing[k] = true;
        if (!line_next(s_zoom, &p)) break;
      }
      graphics_release_frame_buffer(ctx, fb);
    }
    for (int k = 0; k < s_vis; k++)
      if (missing[k]) dots(ctx, b.size.w / 2, s_text_top + k * s_pitch + s_pitch / 2);
    if (s_mem_error || source_failed()) {
      graphics_context_set_text_color(ctx, ui_fg());
      graphics_draw_text(ctx, _(S_NOT_ENOUGH_MEMORY), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                         GRect(0, b.size.h / 2 - 12, b.size.w, 24), GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter, NULL);
    }
  }
  if (g_set.header == HEADER_POSITION || g_set.header == HEADER_POSITION_JUZ) {
    // dotted separator matching the clock status bar
    graphics_context_set_stroke_color(ctx, ui_fg());
    for (int x = 0; x < b.size.w; x += 2) graphics_draw_pixel(ctx, GPoint(x, s_hdr_h - 1));
  }
}

// System-font page: a layer exactly the size of the visible lines clips the
// parts of ayahs that are above or below the page.
static void text_update(Layer *layer, GContext *ctx) {
  if (!s_sys) return;
  GRect b = layer_get_bounds(layer);
  LinePos p = s_top;
  int k = 0;
  while (k < s_vis) {
    int bl = systext_block_lines(p.g);
    if (bl <= 0) { dots(ctx, b.size.w / 2, k * s_pitch + s_pitch / 2); break; }
    if (p.l >= bl) p.l = (uint8_t)(bl - 1);
    int n = bl - p.l;
    if (n > s_vis - k) n = s_vis - k;
    systext_draw(ctx, p.g, p.l, n, k * s_pitch, b.size.w, ui_fg());
    k += n;
    if (p.g + 1 >= QD_N_AYAH) break;
    p.g++; p.l = 0;
  }
}

static void on_source_update(void) { if (s_page && !s_sys) layer_mark_dirty(s_page); }

static void start(void);

static void sys_wait_cb(void *d) {
  s_sys_timer = NULL;
  if (s_sys && s_page && systext_block_lines(s_top.g) <= 0) {
    // the phone did not deliver the text in time: our glyphs take over for a while
    s_sys_hold = time(NULL) + SYS_HOLD_S;
    start();
  }
}

static void on_text_update(void) {
  if (!s_sys || !s_text) return;
  if (systext_block_lines(s_top.g) > 0 && s_sys_timer) { app_timer_cancel(s_sys_timer); s_sys_timer = NULL; }
  layer_mark_dirty(s_text);
}

// ---------------------------------------------------------------- navigation
static void feed_sources(void) {
  if (s_sys) {
    systext_want(s_top.g, SYS_AHEAD);
    if (systext_block_lines(s_top.g) <= 0 && !s_sys_timer)
      s_sys_timer = app_timer_register(SYS_WAIT_MS, sys_wait_cb, NULL);
  } else {
    source_set_position(s_zoom, s_top, s_vis);
  }
}

static void set_top(LinePos p) {
  // connectivity is checked on every page change: switch fonts if it changed
  if (want_sys() != s_sys) {
    g_pos.ayah = p.g; g_pos.line = 0;
    start();
    return;
  }
  s_top = p;
  g_pos.ayah = p.g; g_pos.line = p.l; g_pos.zoom = pos_key();
  state_save_position_soon();
  update_header();
  feed_sources();
  layer_mark_dirty(s_page);
  if (s_sys) layer_mark_dirty(s_text);
}

static void move(int dir, int n) {
  LinePos p = s_top;
  int moved = 0;
  for (int i = 0; i < n; i++) {
    if (dir > 0 ? nxt(&p) : prv(&p)) moved++;
    else break;
  }
  if (moved || want_sys() != s_sys) set_top(p);
}

static void up_click(ClickRecognizerRef r, void *c) { move(-1, g_set.scroll == SCROLL_PAGE ? s_vis : 1); }
static void down_click(ClickRecognizerRef r, void *c) { move(1, g_set.scroll == SCROLL_PAGE ? s_vis : 1); }
static void select_long(ClickRecognizerRef r, void *c) { ui_backlight_set(!ui_backlight_is_on()); }

// swipe up = next page, swipe down = previous page (whatever the scroll setting)
static void on_swipe(const Recognizer *r, RecognizerEvent ev) {
  if (ev != RecognizerEvent_Completed || !ui_touch_available()) return;
  SwipeDirection d = swipe_recognizer_get_direction(r);
  if (d == SwipeDirection_Up) move(1, s_vis);
  else if (d == SwipeDirection_Down) move(-1, s_vis);
}

// ---------------------------------------------------------------- action menu
enum { ACT_BOOKMARK = 1, ACT_TRANSLATION, ACT_FONT, ACT_GOTO_AYAH, ACT_GOTO_SURAH, ACT_LIGHT,
       ACT_ZOOM0 = 20, ACT_THEME0 = 40 };

static char s_zoom_labels[QD_ZOOMS][24];
static char s_theme_labels[THEME_N][48];

static void act_noop(ActionMenu *m, const ActionMenuItem *a, void *c) {}

static void apply_settings_change(void) {
  state_save_settings();
  comm_send_settings();
  reader_settings_changed();
}

static void menu_closed(ActionMenu *m, const ActionMenuItem *item, void *c) {
  int act = item ? (int)(intptr_t)action_menu_item_get_action_data(item) : 0;
  action_menu_hierarchy_destroy(action_menu_get_root_level(m), NULL, NULL);
  switch (act) {
    case ACT_BOOKMARK: {
      int i = bookmark_find(s_top.g);
      if (i >= 0) bookmark_remove(i);
      else bookmark_add(s_top.g);
      vibes_short_pulse();
      update_header();
      break;
    }
    case ACT_TRANSLATION: textview_open(s_top.g); break;
    case ACT_FONT:
      g_set.font = g_set.font == FONT_SYSTEM ? FONT_QURAN : FONT_SYSTEM;
      s_sys_hold = 0;
      apply_settings_change();
      break;
    case ACT_GOTO_AYAH: goto_ayah_push(qmeta_surah_of(s_top.g)); break;
    case ACT_GOTO_SURAH: surah_list_push(); break;
    case ACT_LIGHT: ui_backlight_set(!ui_backlight_is_on()); break;
    default:
      if (act >= ACT_ZOOM0 && act < ACT_ZOOM0 + QD_ZOOMS) {
        g_set.zoom = (uint8_t)(act - ACT_ZOOM0);
        apply_settings_change();
      } else if (act >= ACT_THEME0 && act < ACT_THEME0 + THEME_N) {
        g_set.theme = (uint8_t)(act - ACT_THEME0);
        apply_settings_change();
      }
  }
}

static void open_action_menu(void) {
  ActionMenuLevel *root = action_menu_level_create(9);
  ActionMenuLevel *sizes = action_menu_level_create(QD_ZOOMS);
  ActionMenuLevel *themes = action_menu_level_create(THEME_N);
  if (!root || !sizes || !themes) {
    if (root) action_menu_hierarchy_destroy(root, NULL, NULL);
    if (sizes) action_menu_hierarchy_destroy(sizes, NULL, NULL);
    if (themes) action_menu_hierarchy_destroy(themes, NULL, NULL);
    return;
  }
  bool bm = bookmark_find(s_top.g) >= 0;
  action_menu_level_add_action(root, _(bm ? S_BOOKMARK_REMOVE : S_BOOKMARK_ADD), act_noop, (void *)ACT_BOOKMARK);
  action_menu_level_add_action(root, _(S_TRANSLATION), act_noop, (void *)ACT_TRANSLATION);
  if (ui_system_arabic_available())
    action_menu_level_add_action(root, _(g_set.font == FONT_SYSTEM ? S_USE_QURAN_FONT : S_USE_SYSTEM_FONT),
                                 act_noop, (void *)ACT_FONT);
  action_menu_level_add_action(root, _(S_GOTO_AYAH), act_noop, (void *)ACT_GOTO_AYAH);
  action_menu_level_add_action(root, _(S_GOTO_SURAH), act_noop, (void *)ACT_GOTO_SURAH);
  for (int z = 0; z < QD_ZOOMS; z++) {
    snprintf(s_zoom_labels[z], sizeof(s_zoom_labels[z]), "%d px%s", qmeta_zoom(z)->ppem,
             z == g_set.zoom ? _(S_NOW) : "");
    action_menu_level_add_action(sizes, s_zoom_labels[z], act_noop, (void *)(intptr_t)(ACT_ZOOM0 + z));
  }
  action_menu_level_add_child(root, sizes, _(S_TEXT_SIZE));
  for (int t = 0; t < THEME_N; t++) {
    snprintf(s_theme_labels[t], sizeof(s_theme_labels[t]), "%s%s", theme_name(t), t == g_set.theme ? _(S_NOW) : "");
    action_menu_level_add_action(themes, s_theme_labels[t], act_noop, (void *)(intptr_t)(ACT_THEME0 + t));
  }
  action_menu_level_add_child(root, themes, _(S_THEME));
  action_menu_level_add_action(root, _(ui_backlight_is_on() ? S_BACKLIGHT_OFF : S_BACKLIGHT_ON), act_noop,
                               (void *)ACT_LIGHT);
  ActionMenuConfig cfg = ui_action_menu_config(root, menu_closed);
  action_menu_open(&cfg);
}

static void select_click(ClickRecognizerRef r, void *c) { open_action_menu(); }

static void click_config(void *c) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 160, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 160, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long, NULL);
}

// ---------------------------------------------------------------- window
static void win_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_page = layer_create(b);
  layer_set_update_proc(s_page, page_update);
  layer_add_child(root, s_page);
  s_text = layer_create(b);
  layer_set_update_proc(s_text, text_update);
  layer_add_child(root, s_text);
  s_hdr = text_layer_create(GRect(0, -2, b.size.w, STATUS_BAR_LAYER_HEIGHT + 1));
  text_layer_set_font(s_hdr, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_hdr, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_hdr));
  s_sb = status_bar_layer_create();
  layer_add_child(root, status_bar_layer_get_layer(s_sb));
  if (ui_touch_api()) {
    Recognizer *sw = swipe_recognizer_create(on_swipe, NULL, SwipeDirection_Up | SwipeDirection_Down);
    if (sw) {
      window_set_touch_bridge_disabled(w, true);
      window_attach_recognizer(w, sw);
    }
  }
}

static void start(void) {
  if (!s_page) return;
  strips_set_theme(g_set.theme);
  window_set_background_color(s_win, strips_color(0));
  compute_layout();
  update_header();
  feed_sources();
  layer_mark_dirty(s_page);
  layer_mark_dirty(s_text);
}

static void win_appear(Window *w) {
  if (!s_open) {
    s_open = source_open(on_source_update);
    s_mem_error = !s_open;
    systext_open(on_text_update);
  }
  start();
}

static void win_unload(Window *w) {
  // Window popped: give all memory back to the menus.
  ui_backlight_set(false);
  if (s_sys_timer) { app_timer_cancel(s_sys_timer); s_sys_timer = NULL; }
  source_close();
  systext_close();
  s_open = false;
  state_save_position();
  layer_destroy(s_page); s_page = NULL;
  layer_destroy(s_text); s_text = NULL;
  text_layer_destroy(s_hdr); s_hdr = NULL;
  status_bar_layer_destroy(s_sb); s_sb = NULL;
}

static void ensure_window(void) {
  if (s_win) return;
  s_win = window_create();
  window_set_window_handlers(s_win, (WindowHandlers){ .load = win_load, .appear = win_appear, .unload = win_unload });
  window_set_click_config_provider(s_win, click_config);
}

void reader_open(void) {
  ensure_window();
  if (window_stack_contains_window(s_win)) {
    for (int i = 0; i < 8 && window_stack_get_top_window() != s_win; i++) window_stack_pop(false);
    start();
  } else {
    window_stack_push(s_win, true);
  }
}

void reader_jump_to(uint16_t ayah) {
  if (ayah >= QD_N_AYAH) ayah = QD_N_AYAH - 1;
  g_pos.ayah = ayah;
  g_pos.line = 0;
  state_save_position();
  reader_open();
}

void reader_resume(void) { reader_open(); }

void reader_settings_changed(void) {
  if (s_win && window_stack_contains_window(s_win) && s_page) start();
}

void reader_connection_changed(void) {
  source_connection_changed();
  if (!s_page) return;
  if (want_sys() != s_sys && !s_sys) start();    // phone back: system font resumes at once
  else layer_mark_dirty(s_page);
}
