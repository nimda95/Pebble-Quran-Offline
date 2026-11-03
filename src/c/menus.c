#include "menus.h"
#include "state.h"
#include "qdata.h"
#include "reader.h"
#include "textview.h"
#include "comm.h"
#include "source.h"
#include "ui.h"
#include "picker.h"
#include "strips.h"
#include "qrender.h"
#include "names_ar.h"

// All lists are standard MenuLayers drawn with menu_cell_basic_draw. Windows
// are created once and kept, so popping one from inside its own callback never
// frees memory that is still in use.

typedef struct ListWin {
  Window *win;
  MenuLayer *menu;
  uint16_t (*rows)(void);
  void (*draw)(GContext *ctx, const Layer *cell, int row);
  void (*select)(int row);
  void (*select_long)(int row);
  int (*initial)(void);                 // row to select when shown, -1 to keep
  const char *header;                   // optional section header
  int16_t cell_h;                       // 0 = default height
} ListWin;

static uint16_t lw_sections(MenuLayer *m, void *c) { return 1; }
static uint16_t lw_rows(MenuLayer *m, uint16_t s, void *c) { return ((ListWin *)c)->rows(); }
static int16_t lw_header_h(MenuLayer *m, uint16_t s, void *c) {
  return ((ListWin *)c)->header ? MENU_CELL_BASIC_HEADER_HEIGHT : 0;
}
static void lw_draw_header(GContext *ctx, const Layer *cell, uint16_t s, void *c) {
  if (((ListWin *)c)->header) menu_cell_basic_header_draw(ctx, cell, ((ListWin *)c)->header);
}
static void lw_draw(GContext *ctx, const Layer *cell, MenuIndex *i, void *c) { ((ListWin *)c)->draw(ctx, cell, i->row); }
static int16_t lw_cell_h(MenuLayer *m, MenuIndex *i, void *c) {
  ListWin *lw = c;
  return lw->cell_h;
}
static void lw_select(MenuLayer *m, MenuIndex *i, void *c) { if (((ListWin *)c)->select) ((ListWin *)c)->select(i->row); }
static void lw_select_long(MenuLayer *m, MenuIndex *i, void *c) {
  if (((ListWin *)c)->select_long) ((ListWin *)c)->select_long(i->row);
}

static void lw_load(Window *w) {
  ListWin *lw = window_get_user_data(w);
  if (lw->menu) return;                 // kept from an earlier push
  Layer *root = window_get_root_layer(w);
  lw->menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(lw->menu, lw, (MenuLayerCallbacks){
    .get_num_sections = lw_sections, .get_num_rows = lw_rows,
    .get_header_height = lw_header_h, .draw_header = lw_draw_header,
    .draw_row = lw_draw, .select_click = lw_select, .select_long_click = lw_select_long,
    .get_cell_height = lw->cell_h ? lw_cell_h : NULL });
  menu_layer_set_click_config_onto_window(lw->menu, w);
  layer_add_child(root, menu_layer_get_layer(lw->menu));
}

static void lw_appear(Window *w) {
  ListWin *lw = window_get_user_data(w);
  menu_layer_reload_data(lw->menu);
  int r = lw->initial ? lw->initial() : -1;
  if (r >= 0 && r < lw->rows()) menu_layer_set_selected_index(lw->menu, MenuIndex(0, r), MenuRowAlignCenter, false);
}

static void lw_push(ListWin *lw) {
  if (!lw->win) {
    lw->win = window_create();
    window_set_user_data(lw->win, lw);
    // no unload handler: the MenuLayer may be popped from inside its own select callback
    window_set_window_handlers(lw->win, (WindowHandlers){ .load = lw_load, .appear = lw_appear });
  }
  if (window_stack_contains_window(lw->win)) window_stack_remove(lw->win, false);
  window_stack_push(lw->win, true);
}

static void lw_reload(ListWin *lw) { if (lw->menu) menu_layer_reload_data(lw->menu); }

static void pos_text(uint16_t g, char *buf, int n) {
  int si = qmeta_surah_of(g);
  snprintf(buf, n, "%s %d:%d", qmeta_surah(si)->translit, si + 1, qmeta_ayah_no(g));
}

// ---------------------------------------------------------------- surahs
static uint16_t surah_rows(void) { return QD_N_SURAH; }
// Arabic name under the transliteration: the system font when it has Arabic,
// otherwise our own pre-rendered glyphs (NAMES resource).
static void surah_draw(GContext *ctx, const Layer *cell, int row) {
  const QSurah *s = qmeta_surah(row);
  char t[24];
  snprintf(t, sizeof(t), "%d. %s", row + 1, s->translit);
  menu_cell_basic_draw(ctx, cell, t, " ", NULL);
  GRect b = layer_get_bounds((Layer *)cell);
  bool hl = menu_cell_layer_is_highlighted(cell);
  GColor fg = hl ? GColorWhite : GColorBlack, bg = hl ? GColorBlack : GColorWhite;
  if (ui_system_arabic_available()) {
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, NAMES_AR[row], fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(4, 26, b.size.w - 12, 26),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    return;
  }
  static uint8_t rec[300];
  if (qmeta_name_record(row, rec, sizeof(rec)) <= 0) return;
  GPoint org = layer_convert_point_to_screen((Layer *)cell, GPointZero);
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  if (gbitmap_get_format(fb) == GBitmapFormat8Bit) {
    GRect fbb = gbitmap_get_bounds(fb);
    int x_off = org.x + b.size.w - 8 - QR_NAME_RIGHT;          // name ends 8 px from the cell edge
    strips_draw_colors(rec, QR_STRIDE_FOR(QR_NAME_W), gbitmap_get_data(fb), gbitmap_get_bytes_per_row(fb), fbb.size.w, fbb.size.h,
                       x_off, org.y + 46, bg, fg);
  }
  graphics_release_frame_buffer(ctx, fb);
}
static void surah_select(int row) { goto_ayah_push(row); }
static void surah_long(int row) { reader_jump_to(qmeta_surah(row)->first_global); }
static int surah_initial(void) { return qmeta_surah_of(g_pos.ayah); }
static ListWin s_surahs = { .rows = surah_rows, .draw = surah_draw, .select = surah_select,
                            .select_long = surah_long, .initial = surah_initial, .cell_h = 56 };
void surah_list_push(void) { lw_push(&s_surahs); }

// ---------------------------------------------------------------- juz / hizb
static bool s_part_hizb;
static uint16_t part_rows(void) { return s_part_hizb ? QD_N_HIZB : QD_N_JUZ; }
static uint16_t part_start(int row) { return s_part_hizb ? qmeta_hizb_start(row) : qmeta_juz_start(row); }
static void part_draw(GContext *ctx, const Layer *cell, int row) {
  char t[16], sub[32];
  snprintf(t, sizeof(t), "%s %d", s_part_hizb ? "Hizb" : "Juz'", row + 1);
  pos_text(part_start(row), sub, sizeof(sub));
  menu_cell_basic_draw(ctx, cell, t, sub, NULL);
}
static void part_select(int row) { reader_jump_to(part_start(row)); }
static int part_initial(void) { return s_part_hizb ? qmeta_hizb_of(g_pos.ayah) : qmeta_juz_of(g_pos.ayah); }
static ListWin s_parts = { .rows = part_rows, .draw = part_draw, .select = part_select, .initial = part_initial };
static void parts_push(bool hizb) { s_part_hizb = hizb; lw_push(&s_parts); }

// ---------------------------------------------------------------- bookmarks
static int s_bm_pending = -1;
static uint16_t bm_rows(void) { return bookmarks_count() ? bookmarks_count() : 1; }
static void bm_draw(GContext *ctx, const Layer *cell, int row) {
  if (!bookmarks_count()) {
    menu_cell_basic_draw(ctx, cell, "No bookmarks", "Add one from the reader menu", NULL);
    return;
  }
  uint16_t g = bookmark_get(row);
  int si = qmeta_surah_of(g);
  char sub[24];
  snprintf(sub, sizeof(sub), "%d:%d  Juz' %d", si + 1, qmeta_ayah_no(g), qmeta_juz_of(g) + 1);
  menu_cell_basic_draw(ctx, cell, qmeta_surah(si)->translit, sub, NULL);
}
static void bm_select(int row) { if (bookmarks_count()) reader_jump_to(bookmark_get(row)); }
static ListWin s_bms;
static void bm_menu_noop(ActionMenu *m, const ActionMenuItem *a, void *c) {}
static void bm_menu_closed(ActionMenu *m, const ActionMenuItem *item, void *c) {
  int act = item ? (int)(intptr_t)action_menu_item_get_action_data(item) : 0;
  action_menu_hierarchy_destroy(action_menu_get_root_level(m), NULL, NULL);
  if (act == 1 && s_bm_pending >= 0) reader_jump_to(bookmark_get(s_bm_pending));
  if (act == 2 && s_bm_pending >= 0) { bookmark_remove(s_bm_pending); vibes_short_pulse(); lw_reload(&s_bms); }
  s_bm_pending = -1;
}
static void bm_long(int row) {
  if (!bookmarks_count()) return;
  s_bm_pending = row;
  ActionMenuLevel *root = action_menu_level_create(2);
  action_menu_level_add_action(root, "Open", bm_menu_noop, (void *)1);
  action_menu_level_add_action(root, "Delete bookmark", bm_menu_noop, (void *)2);
  ActionMenuConfig cfg = ui_action_menu_config(root, bm_menu_closed);
  action_menu_open(&cfg);
}
static ListWin s_bms = { .rows = bm_rows, .draw = bm_draw, .select = bm_select, .select_long = bm_long };

// ---------------------------------------------------------------- go to ayah
void goto_ayah_push(int si) {
  picker_push(si, qmeta_surah_of(g_pos.ayah) == si ? qmeta_ayah_no(g_pos.ayah) : 1);
}

// ---------------------------------------------------------------- settings
typedef struct {
  const char *title;
  int n;
  const char *(*label)(int i);
  uint8_t *field;
} Choice;

static char s_zoom_lbl[QD_ZOOMS][12];
static const char *zoom_label(int i) {
  snprintf(s_zoom_lbl[i], sizeof(s_zoom_lbl[i]), "%d px", qmeta_zoom(i)->ppem);
  return s_zoom_lbl[i];
}
static const char *font_label(int i) { return i == FONT_SYSTEM ? "System font" : "Quran font"; }
static const char *sysfont_label(int i) { return i ? "Large (28)" : "Medium (24)"; }
static const char *scroll_label(int i) { return i == SCROLL_PAGE ? "By page" : "By line"; }
static const char *render_label(int i) { return i == RENDER_WATCH ? "Watch only" : "Phone when connected"; }
static const char *glance_label(int i) { return i ? "On" : "Off"; }

enum { SET_ZOOM, SET_FONT, SET_SYSFONT, SET_SCROLL, SET_HEADER, SET_THEME, SET_LIGHT, SET_TRANS,
       SET_RENDER, SET_GLANCE, SET_STATUS, SET_RESET, SET_ABOUT, SET_N };

static const Choice CHOICES[] = {
  [SET_ZOOM] = { "Text size", QD_ZOOMS, zoom_label, &g_set.zoom },
  [SET_FONT] = { "Arabic text", FONT_N, font_label, &g_set.font },
  [SET_SYSFONT] = { "System font size", SYSFONT_N, sysfont_label, &g_set.sysfont },
  [SET_SCROLL] = { "Scrolling", SCROLL_N, scroll_label, &g_set.scroll },
  [SET_HEADER] = { "Header", HEADER_N, header_name, &g_set.header },
  [SET_THEME] = { "Theme", THEME_N, theme_name, &g_set.theme },
  [SET_LIGHT] = { "Backlight colour", LIGHT_N, light_name, &g_set.light },
  [SET_TRANS] = { "Translation", TRANS_N, trans_name, &g_set.trans },
  [SET_RENDER] = { "Rendering", RENDER_N, render_label, &g_set.render },
  [SET_GLANCE] = { "Position in app list", 2, glance_label, &g_set.glance },
};

static const Choice *s_choice;
static ListWin s_settings, s_choices;

static uint16_t choice_rows(void) { return (uint16_t)s_choice->n; }
static void choice_draw(GContext *ctx, const Layer *cell, int row) {
  menu_cell_basic_draw(ctx, cell, s_choice->label(row), row == *s_choice->field ? "Selected" : NULL, NULL);
}
static void choice_select(int row) {
  *s_choice->field = (uint8_t)row;
  state_save_settings();
  comm_send_settings();
  reader_settings_changed();
  textview_settings_changed();
  window_stack_pop(true);
}
static int choice_initial(void) { return *s_choice->field; }
static ListWin s_choices = { .rows = choice_rows, .draw = choice_draw, .select = choice_select, .initial = choice_initial };

static uint16_t set_rows(void) { return SET_N; }
static void set_draw(GContext *ctx, const Layer *cell, int row) {
  char sub[40];
  if (row < SET_STATUS) {
    const Choice *c = &CHOICES[row];
    const char *v = c->label(*c->field);
    if (row == SET_FONT && !ui_system_arabic_available()) v = "Quran font (no system Arabic)";
    if (row == SET_LIGHT) { snprintf(sub, sizeof(sub), "%s, hold Select", v); v = sub; }
    menu_cell_basic_draw(ctx, cell, c->title, v, NULL);
    return;
  }
  switch (row) {
    case SET_STATUS:
      {
        uint32_t np, nw;
        source_counts(&np, &nw);
        char title[32];
        snprintf(title, sizeof(title), "Status (%lu/%lu)", (unsigned long)np, (unsigned long)nw);
        snprintf(sub, sizeof(sub), "Renders on: %s", source_status());
        menu_cell_basic_draw(ctx, cell, title, sub, NULL);
      }
      break;
    case SET_RESET: menu_cell_basic_draw(ctx, cell, "Reset settings", "Keeps position, bookmarks", NULL); break;
    case SET_ABOUT:
      snprintf(sub, sizeof(sub), "v2.0  %u KB free", (unsigned)(heap_bytes_free() / 1024));
      menu_cell_basic_draw(ctx, cell, "About", sub, NULL);
      break;
  }
}
static void set_select(int row) {
  if (row < SET_STATUS) {
    s_choice = &CHOICES[row];
    s_choices.header = s_choice->title;
    lw_push(&s_choices);
    return;
  }
  if (row == SET_RESET) {
    settings_reset();
    comm_send_settings();
    reader_settings_changed();
    vibes_short_pulse();
    lw_appear(s_settings.win);
  }
}
static ListWin s_settings = { .rows = set_rows, .draw = set_draw, .select = set_select };
void settings_push(void) { lw_push(&s_settings); }

// ---------------------------------------------------------------- main menu
enum { MM_CONTINUE, MM_SURAHS, MM_JUZ, MM_HIZB, MM_BOOKMARKS, MM_SETTINGS, MM_N };

static uint16_t mm_rows(void) { return MM_N; }
static void mm_draw(GContext *ctx, const Layer *cell, int row) {
  char sub[40];
  switch (row) {
    case MM_CONTINUE:
      pos_text(g_pos.ayah, sub, sizeof(sub));
      menu_cell_basic_draw(ctx, cell, "Continue reading", sub, NULL);
      break;
    case MM_SURAHS: menu_cell_basic_draw(ctx, cell, "Surahs", "Pick a surah, then the ayah", NULL); break;
    case MM_JUZ: menu_cell_basic_draw(ctx, cell, "Juz'", "30 parts", NULL); break;
    case MM_HIZB: menu_cell_basic_draw(ctx, cell, "Hizb", "60 parts", NULL); break;
    case MM_BOOKMARKS:
      snprintf(sub, sizeof(sub), "%d saved", bookmarks_count());
      menu_cell_basic_draw(ctx, cell, "Bookmarks", sub, NULL);
      break;
    case MM_SETTINGS: menu_cell_basic_draw(ctx, cell, "Settings", NULL, NULL); break;
  }
}
static void mm_select(int row) {
  switch (row) {
    case MM_CONTINUE: reader_resume(); break;
    case MM_SURAHS: surah_list_push(); break;
    case MM_JUZ: parts_push(false); break;
    case MM_HIZB: parts_push(true); break;
    case MM_BOOKMARKS: lw_push(&s_bms); break;
    case MM_SETTINGS: settings_push(); break;
  }
}
static ListWin s_main = { .rows = mm_rows, .draw = mm_draw, .select = mm_select };

void main_menu_push(void) { lw_push(&s_main); }

void main_menu_refresh(void) {
  lw_reload(&s_main);
  lw_reload(&s_settings);
}
