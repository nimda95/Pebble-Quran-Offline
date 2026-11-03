#include "picker.h"
#include "qdata.h"
#include "reader.h"
#include "ui.h"
#include "i18n.h"

#define PAD_TOP 84
#define ROWS 4
#define COLS 3
enum { KEY_DEL = 9, KEY_ZERO = 10, KEY_GO = 11 };

static Window *s_win;
static TextLayer *s_title, *s_number, *s_hint;
static Layer *s_pad;
static int s_surah, s_value, s_max;
static bool s_fresh;                  // next typed digit replaces the value
static int s_pressed = -1;            // key shown pressed after a tap
static AppTimer *s_press_timer;
static char s_title_buf[40], s_number_buf[8], s_hint_buf[128];

static void refresh(void) {
  if (s_value > 0) snprintf(s_number_buf, sizeof(s_number_buf), "%d", s_value);
  else snprintf(s_number_buf, sizeof(s_number_buf), "-");
  text_layer_set_text(s_number, s_number_buf);
  layer_mark_dirty(s_pad);
}

static void go(void) {
  if (s_value < 1 || s_value > s_max) { vibes_short_pulse(); return; }
  uint16_t g = qmeta_surah(s_surah)->first_global + s_value - 1;
  window_stack_remove(s_win, false);
  reader_jump_to(g);
}

static void step(int d) {
  int v = s_value + d;
  if (v < 1) v = 1;
  if (v > s_max) v = s_max;
  s_value = v;
  s_fresh = true;
  refresh();
}

static void press_key(int key) {
  if (key == KEY_GO) { go(); return; }
  if (key == KEY_DEL) {
    s_value /= 10;
    s_fresh = false;
  } else {
    int digit = key == KEY_ZERO ? 0 : key + 1;
    int v = s_fresh ? digit : s_value * 10 + digit;
    if (v == 0 || v > s_max) { vibes_short_pulse(); return; }   // stays within the surah
    s_value = v;
    s_fresh = false;
  }
  refresh();
}

// ---------------------------------------------------------------- numpad
static GRect key_rect(int key) {
  GRect b = layer_get_bounds(s_pad);
  int w = b.size.w / COLS, h = b.size.h / ROWS;
  return GRect((key % COLS) * w + 2, (key / COLS) * h + 2, w - 4, h - 4);
}

static void pad_draw(Layer *l, GContext *ctx) {
  static const char *LABELS[ROWS * COLS] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", NULL, "0", NULL };
  for (int k = 0; k < ROWS * COLS; k++) {
    const char *label = k == KEY_DEL ? _(S_KEY_DEL) : k == KEY_GO ? _(S_KEY_GO) : LABELS[k];
    GRect r = key_rect(k);
    bool on = k == s_pressed;
    bool go_key = k == KEY_GO;
    graphics_context_set_fill_color(ctx, on ? GColorBlack : (go_key ? GColorDarkGreen : GColorLightGray));
    graphics_fill_rect(ctx, r, 6, GCornersAll);
    graphics_context_set_text_color(ctx, (on || go_key) ? GColorWhite : GColorBlack);
    GFont f = fonts_get_system_font(k >= KEY_DEL && k != KEY_ZERO ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_24_BOLD);
    int th = k >= KEY_DEL && k != KEY_ZERO ? 18 : 24;
    graphics_draw_text(ctx, label, f, GRect(r.origin.x, r.origin.y + (r.size.h - th) / 2 - 4, r.size.w, th + 6),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void release_cb(void *d) { s_press_timer = NULL; s_pressed = -1; layer_mark_dirty(s_pad); }

static void go_cb(void *d) { go(); }

static void on_tap(const Recognizer *r, RecognizerEvent ev) {
  if (ev != RecognizerEvent_Completed || layer_get_hidden(s_pad)) return;
  GPoint p = tap_recognizer_get_tap_point(r);
  GRect pf = layer_get_frame(s_pad);
  if (!grect_contains_point(&pf, &p)) return;
  GPoint q = GPoint(p.x - pf.origin.x, p.y - pf.origin.y);
  int col = q.x / (pf.size.w / COLS), row = q.y / (pf.size.h / ROWS);
  if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return;
  int key = row * COLS + col;
  s_pressed = key;
  if (s_press_timer) app_timer_reschedule(s_press_timer, 150);
  else s_press_timer = app_timer_register(150, release_cb, NULL);
  // leaving the window frees this recognizer: do it after the callback returns
  if (key == KEY_GO) app_timer_register(0, go_cb, NULL);
  else press_key(key);
}

// ---------------------------------------------------------------- buttons
static void up(ClickRecognizerRef r, void *c) { step(click_recognizer_is_repeating(r) ? 5 : 1); }
static void down(ClickRecognizerRef r, void *c) { step(click_recognizer_is_repeating(r) ? -5 : -1); }
static void select(ClickRecognizerRef r, void *c) { go(); }
static void clicks(void *c) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 120, up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 120, down);
  window_single_click_subscribe(BUTTON_ID_SELECT, select);
}

static void load(Window *w) {
  // the window owns (and destroys on unload) its recognizers: attach a new one each load
  if (ui_touch_api()) {
    Recognizer *tap = tap_recognizer_create(on_tap, NULL);
    if (tap) { window_set_touch_bridge_disabled(w, true); window_attach_recognizer(w, tap); }
  }
  if (s_title) return;                  // layers are kept between pushes
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_title = text_layer_create(GRect(4, 0, b.size.w - 8, 24));
  text_layer_set_font(s_title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  s_number = text_layer_create(GRect(0, 18, b.size.w, 48));
  text_layer_set_font(s_number, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
  s_hint = text_layer_create(GRect(4, 64, b.size.w - 8, 18));
  text_layer_set_font(s_hint, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  TextLayer *all[] = { s_title, s_number, s_hint };
  for (int i = 0; i < 3; i++) {
    text_layer_set_text_alignment(all[i], GTextAlignmentCenter);
    text_layer_set_background_color(all[i], GColorClear);
    layer_add_child(root, text_layer_get_layer(all[i]));
  }
  s_pad = layer_create(GRect(2, PAD_TOP, b.size.w - 4, b.size.h - PAD_TOP - 2));
  layer_set_update_proc(s_pad, pad_draw);
  layer_add_child(root, s_pad);
}

void picker_push(int si, int initial) {
  if (si < 0 || si >= QD_N_SURAH) si = 0;
  if (!s_win) {
    s_win = window_create();
    window_set_window_handlers(s_win, (WindowHandlers){ .load = load });   // never unloaded/destroyed
    window_set_click_config_provider(s_win, clicks);
  }
  const QSurah *s = qmeta_surah(si);
  s_surah = si;
  s_max = s->ayah_count;
  s_value = (initial >= 1 && initial <= s_max) ? initial : 1;
  s_fresh = true;
  s_pressed = -1;
  if (window_stack_contains_window(s_win)) window_stack_remove(s_win, false);
  window_stack_push(s_win, true);
  snprintf(s_title_buf, sizeof(s_title_buf), "%d. %s", si + 1, i18n_surah_name(si));
  // numpad only when touch input reaches apps; otherwise a buttons-only layout
  bool touch = ui_touch_available();
  GRect b = layer_get_bounds(window_get_root_layer(s_win));
  layer_set_hidden(s_pad, !touch);
  layer_set_frame(text_layer_get_layer(s_title), GRect(4, touch ? 0 : 36, b.size.w - 8, 24));
  layer_set_frame(text_layer_get_layer(s_number), GRect(0, touch ? 18 : 74, b.size.w, 48));
  layer_set_frame(text_layer_get_layer(s_hint), GRect(4, touch ? 64 : 126, b.size.w - 8, touch ? 18 : 60));
  int n = snprintf(s_hint_buf, sizeof(s_hint_buf), _(S_AYAH_RANGE), s_max);
  if (!touch && n > 0 && n < (int)sizeof(s_hint_buf))
    snprintf(s_hint_buf + n, sizeof(s_hint_buf) - n, "\n%s", _(S_PICKER_HELP));
  text_layer_set_text(s_title, s_title_buf);
  text_layer_set_text(s_hint, s_hint_buf);
  refresh();
}
