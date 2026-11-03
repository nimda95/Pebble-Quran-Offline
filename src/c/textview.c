#include "textview.h"
#include "comm.h"
#include "state.h"
#include "qdata.h"
#include "ui.h"
#include "strips.h"
#include "i18n.h"

#define TV_MARGIN 6
#define REQ_TIMEOUT_MS 8000
#define RETRY_MS 300

// One ayah's translation. The entry on screen is only freed after the layer
// points at its replacement.
typedef struct { int16_t ayah; bool error; char *text; } TEnt;

static Window *s_win;
static ScrollLayer *s_scroll;
static TextLayer *s_ref, *s_tr;
static uint16_t s_ayah;
static char s_ref_buf[48];
static TEnt s_shown, s_next;          // current ayah, and the prefetched next one
static uint32_t s_req;
static int s_req_ayah = -1;
static AppTimer *s_timer;

static bool is_open(void) { return s_win && window_stack_contains_window(s_win); }
static void clear(TEnt *e) { free(e->text); e->text = NULL; e->ayah = -1; e->error = false; }
static bool have(const TEnt *e, uint16_t g) { return e->ayah == (int16_t)g && (e->text || e->error); }

static int place(TextLayer *t, int y, GFont f, GTextAlignment al, const char *text) {
  int w = layer_get_bounds(window_get_root_layer(s_win)).size.w - 2 * TV_MARGIN;
  text_layer_set_font(t, f);
  text_layer_set_text_alignment(t, al);
  text_layer_set_text(t, text);
  layer_set_frame(text_layer_get_layer(t), GRect(TV_MARGIN, y, w, 20000));
  GSize sz = text_layer_get_content_size(t);
  layer_set_frame(text_layer_get_layer(t), GRect(TV_MARGIN, y, w, sz.h + 6));
  return y + sz.h + 10;
}

static void render(void) {
  int si = qmeta_surah_of(s_ayah);
  snprintf(s_ref_buf, sizeof(s_ref_buf), "%s  %d:%d", i18n_surah_name(si), si + 1, qmeta_ayah_no(s_ayah));
  const char *tr;
  if (have(&s_shown, s_ayah) && s_shown.text) tr = s_shown.text;
  else if (have(&s_shown, s_ayah)) tr = _(S_TR_ERROR);
  else if (!g_set.trans) tr = _(S_TR_NONE);
  else if (!comm_phone_ready()) tr = _(S_TR_NEED_PHONE);
  else tr = _(S_LOADING);
  int y = place(s_ref, 2, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GTextAlignmentCenter, s_ref_buf);
  y = place(s_tr, y, fonts_get_system_font(FONT_KEY_GOTHIC_24), GTextAlignmentLeft, tr);
  GRect b = layer_get_bounds(window_get_root_layer(s_win));
  scroll_layer_set_content_size(s_scroll, GSize(b.size.w, y + 10));
}

static void timer_cb(void *d);

static void request(uint16_t ayah) {
  if (!g_set.trans || !comm_phone_ready() || s_req_ayah == (int)ayah) return;
  if (comm_request_text(++s_req, ayah, 2, TF_TRANS)) {      // this ayah and the next
    s_req_ayah = ayah;
    if (s_timer) app_timer_reschedule(s_timer, REQ_TIMEOUT_MS);
    else s_timer = app_timer_register(REQ_TIMEOUT_MS, timer_cb, NULL);
  } else if (!s_timer) {
    s_timer = app_timer_register(RETRY_MS, timer_cb, NULL); // outbox busy: retry
  }
}

static void timer_cb(void *d) {
  s_timer = NULL;
  s_req_ayah = -1;
  if (is_open() && !have(&s_shown, s_ayah)) request(s_ayah);
}

// replaces *e; the previous string is returned so the caller frees it after re-rendering
static char *take(TEnt *e, uint16_t g, const char *p, int len, bool error) {
  char *old = e->text;
  e->text = NULL;
  e->ayah = (int16_t)g;
  e->error = error || len <= 0;
  if (!e->error) {
    e->text = malloc(len + 1);
    if (e->text) { memcpy(e->text, p, len); e->text[len] = 0; }
    else e->error = true;
  }
  return old;
}

static void on_text(uint32_t req, uint16_t first, int count, const char *ar, const char *tr, uint8_t err) {
  if (!is_open() || req != s_req) return;          // closed, superseded, or another view's reply
  if (!tr && !(err & TF_TRANS_ERROR)) return;
  s_req_ayah = -1;
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  bool error = (err & TF_TRANS_ERROR) != 0;
  const char *p = tr ? tr : "";
  for (int i = 0; i < count; i++) {
    const char *q = strchr(p, TEXT_SEP);
    int len = q ? (int)(q - p) : (int)strlen(p);
    uint16_t g = (uint16_t)(first + i);
    if (g == s_ayah) {
      char *old = take(&s_shown, g, p, len, error);
      render();
      free(old);
    } else if (g == s_ayah + 1) {
      free(take(&s_next, g, p, len, error));
    }
    if (!q) break;
    p = q + 1;
  }
}

static void show(uint16_t ayah) {
  g_pos.ayah = ayah;                               // the reader continues from here
  g_pos.line = 0;
  state_save_position_soon();
  char *old = NULL;
  if (!have(&s_shown, ayah)) {
    old = s_shown.text;
    if (have(&s_next, ayah)) { s_shown = s_next; s_next.text = NULL; s_next.ayah = -1; }
    else { s_shown.text = NULL; s_shown.ayah = -1; s_shown.error = false; }
  }
  s_ayah = ayah;
  render();
  free(old);                                       // the layer no longer points at it
  scroll_layer_set_content_offset(s_scroll, GPointZero, false);
  if (!have(&s_shown, ayah)) request(ayah);
  else if (!have(&s_next, ayah + 1) && ayah + 1 < QD_N_AYAH) request(ayah + 1);
}

static void scroll_step(int dir) {
  GPoint off = scroll_layer_get_content_offset(s_scroll);
  GSize cs = scroll_layer_get_content_size(s_scroll);
  int vh = layer_get_bounds(scroll_layer_get_layer(s_scroll)).size.h;
  int step = vh - 40;
  if (dir > 0) {
    int max = cs.h - vh;
    if (-off.y < max - 24) {
      off.y -= step;
      if (-off.y > max) off.y = -max;
      scroll_layer_set_content_offset(s_scroll, off, true);
    } else if (s_ayah + 1 < QD_N_AYAH) {
      show(s_ayah + 1);
    }
  } else if (off.y < -24) {
    off.y += step;
    if (off.y > 0) off.y = 0;
    scroll_layer_set_content_offset(s_scroll, off, true);
  } else if (s_ayah > 0) {
    show(s_ayah - 1);
  }
}

static void up(ClickRecognizerRef r, void *c) { scroll_step(-1); }
static void down(ClickRecognizerRef r, void *c) { scroll_step(1); }
static void select_long(ClickRecognizerRef r, void *c) { ui_backlight_set(!ui_backlight_is_on()); }

static void clicks(void *c) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 200, up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 200, down);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long, NULL);
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_scroll = scroll_layer_create(b);
  s_ref = text_layer_create(GRect(0, 0, b.size.w, 20));
  s_tr = text_layer_create(GRect(0, 0, b.size.w, 20));
  TextLayer *all[] = { s_ref, s_tr };
  for (int i = 0; i < 2; i++) {
    text_layer_set_overflow_mode(all[i], GTextOverflowModeWordWrap);
    text_layer_set_background_color(all[i], GColorClear);
    scroll_layer_add_child(s_scroll, text_layer_get_layer(all[i]));
  }
  layer_add_child(root, scroll_layer_get_layer(s_scroll));
}

static void appear(Window *w) {
  strips_set_theme(g_set.theme);
  window_set_background_color(w, ui_bg());
  text_layer_set_text_color(s_ref, ui_fg());
  text_layer_set_text_color(s_tr, ui_fg());
}

static void unload(Window *w) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  s_req_ayah = -1;
  scroll_layer_destroy(s_scroll);
  text_layer_destroy(s_ref);
  text_layer_destroy(s_tr);
  clear(&s_shown);
  clear(&s_next);
}

void textview_settings_changed(void) {
  if (!is_open()) return;
  appear(s_win);
  clear(&s_next);
  char *old = s_shown.text;                         // the edition may have changed: fetch again
  s_shown.text = NULL; s_shown.ayah = -1; s_shown.error = false;
  s_req_ayah = -1;
  render();
  free(old);
  request(s_ayah);
}

void textview_open(uint16_t ayah) {
  if (!s_win) {
    s_win = window_create();
    window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .appear = appear, .unload = unload });
    window_set_click_config_provider(s_win, clicks);
    s_shown.ayah = s_next.ayah = -1;
    comm_add_text_handler(on_text);
  }
  if (window_stack_contains_window(s_win)) window_stack_remove(s_win, false);
  window_stack_push(s_win, true);
  s_ayah = 0xFFFF;
  show(ayah);
}
