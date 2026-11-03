#include "ui.h"
#include "state.h"
#include "strips.h"

GColor ui_bg(void) { return strips_color(0); }
GColor ui_fg(void) { return strips_color(3); }

ActionMenuConfig ui_action_menu_config(ActionMenuLevel *root, ActionMenuDidCloseCb did_close) {
  return (ActionMenuConfig){
    .root_level = root,
    .colors = { .background = GColorBlack, .foreground = GColorWhite },
    .align = ActionMenuAlignTop,
    .did_close = did_close,
  };
}

void ui_style_status_bar(StatusBarLayer *s) {
  status_bar_layer_set_colors(s, ui_bg(), ui_fg());
  status_bar_layer_set_separator_mode(s, StatusBarLayerSeparatorModeDotted);
}

// ---------------------------------------------------------------- backlight
static bool s_light;

static bool fw_at_least(int major, int minor) {
  WatchInfoVersion v = watch_info_get_firmware_version();
  return v.major > major || (v.major == major && v.minor >= minor);
}

bool ui_touch_api(void) { return fw_at_least(4, 33); }
bool ui_touch_available(void) { return ui_touch_api() && touch_service_is_enabled(); }

void ui_backlight_set(bool on) {
  if (on == s_light) return;
  s_light = on;
  bool tint = fw_at_least(4, 33);   // colour API is newer than the backlight API
  if (on) {
    uint32_t rgb = light_rgb(g_set.light);
    if (tint) { if (rgb) light_set_color_rgb888(rgb); else light_set_system_color(); }
    light_enable(true);
  } else {
    light_enable(false);
    if (tint) light_set_system_color();
  }
}
bool ui_backlight_is_on(void) { return s_light; }

// ---------------------------------------------------------------- Arabic glyphs
bool ui_system_arabic_available(void) {
  static int8_t s_cached = -1;
  if (s_cached >= 0) return s_cached;
  s_cached = 0;
  if (fw_at_least(4, 20)) {
    // Missing glyphs are drawn as the U+25AF box; compare widths.
    GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24);
    GRect box = GRect(0, 0, 180, 60);
    GSize ar = graphics_text_layout_get_content_size("\xd8\xa8\xd8\xb3\xd9\x85", f, box,
                                                     GTextOverflowModeWordWrap, GTextAlignmentLeft);
    GSize bx = graphics_text_layout_get_content_size("\xe2\x96\xaf\xe2\x96\xaf\xe2\x96\xaf", f, box,
                                                     GTextOverflowModeWordWrap, GTextAlignmentLeft);
    s_cached = (ar.w > 0 && ar.w != bx.w) ? 1 : 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "system Arabic glyphs: %s (%d vs %d px)", s_cached ? "yes" : "no", ar.w, bx.w);
  }
  return s_cached;
}

// ---------------------------------------------------------------- dialog
static Window *s_dlg;
static TextLayer *s_dlg_title, *s_dlg_body;
static ScrollLayer *s_dlg_scroll;
static char s_dlg_title_buf[64], s_dlg_body_buf[300];

static void dlg_close(ClickRecognizerRef r, void *c) { window_stack_remove(s_dlg, true); }
static void dlg_clicks(void *c) {
  window_single_click_subscribe(BUTTON_ID_SELECT, dlg_close);
}

static void dlg_load(Window *w) {
  if (s_dlg_scroll) return;             // kept between pushes
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_dlg_scroll = scroll_layer_create(b);
  scroll_layer_set_click_config_onto_window(s_dlg_scroll, w);
  scroll_layer_set_callbacks(s_dlg_scroll, (ScrollLayerCallbacks){ .click_config_provider = dlg_clicks });
  s_dlg_title = text_layer_create(GRect(8, 8, b.size.w - 16, 60));
  text_layer_set_font(s_dlg_title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  s_dlg_body = text_layer_create(GRect(8, 40, b.size.w - 16, 2000));
  text_layer_set_font(s_dlg_body, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  TextLayer *tls[] = { s_dlg_title, s_dlg_body };
  for (int i = 0; i < 2; i++) {
    text_layer_set_background_color(tls[i], GColorClear);
    scroll_layer_add_child(s_dlg_scroll, text_layer_get_layer(tls[i]));
  }
  layer_add_child(root, scroll_layer_get_layer(s_dlg_scroll));
}

static void dlg_appear(Window *w) {
  text_layer_set_text(s_dlg_title, s_dlg_title_buf);
  GSize ts = text_layer_get_content_size(s_dlg_title);
  GRect bb = layer_get_bounds(window_get_root_layer(w));
  layer_set_frame(text_layer_get_layer(s_dlg_title), GRect(8, 6, bb.size.w - 16, ts.h + 6));
  text_layer_set_text(s_dlg_body, s_dlg_body_buf);
  layer_set_frame(text_layer_get_layer(s_dlg_body), GRect(8, ts.h + 12, bb.size.w - 16, 2000));
  GSize bs = text_layer_get_content_size(s_dlg_body);
  layer_set_frame(text_layer_get_layer(s_dlg_body), GRect(8, ts.h + 12, bb.size.w - 16, bs.h + 8));
  scroll_layer_set_content_size(s_dlg_scroll, GSize(bb.size.w, ts.h + bs.h + 30));
  scroll_layer_set_content_offset(s_dlg_scroll, GPointZero, false);
}

void ui_dialog_show(const char *title, const char *body) {
  snprintf(s_dlg_title_buf, sizeof(s_dlg_title_buf), "%s", title);
  snprintf(s_dlg_body_buf, sizeof(s_dlg_body_buf), "%s", body);
  if (!s_dlg) {
    s_dlg = window_create();
    window_set_window_handlers(s_dlg, (WindowHandlers){ .load = dlg_load, .appear = dlg_appear });
  }
  if (window_stack_contains_window(s_dlg)) { dlg_appear(s_dlg); return; }
  window_stack_push(s_dlg, true);
}
