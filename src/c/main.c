#include <pebble.h>
#include "qdata.h"
#include "state.h"
#include "comm.h"
#include "menus.h"
#include "reader.h"
#include "strips.h"
#include "ui.h"
#include "textview.h"

static char s_glance_buf[48];

static void glance_cb(AppGlanceReloadSession *session, size_t limit, void *ctx) {
  if (!g_set.glance || limit < 1) return;
  int si = qmeta_surah_of(g_pos.ayah);
  snprintf(s_glance_buf, sizeof(s_glance_buf), "%s %d:%d", qmeta_surah(si)->translit, si + 1,
           qmeta_ayah_no(g_pos.ayah));
  AppGlanceSlice slice = {
    .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = s_glance_buf },
    .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
  };
  AppGlanceResult r = app_glance_add_slice(session, slice);
  if (r != APP_GLANCE_RESULT_SUCCESS) APP_LOG(APP_LOG_LEVEL_WARNING, "app glance: %d", r);
}

static void on_phone(void) {
  reader_connection_changed();
  main_menu_refresh();
}

static void on_phone_settings(void) {
  reader_settings_changed();
  textview_settings_changed();
  main_menu_refresh();
}

static void init(void) {
  bool data_ok = qmeta_init();
  if (!data_ok) APP_LOG(APP_LOG_LEVEL_ERROR, "data resources missing or damaged");
  state_load();
  strips_set_theme(g_set.theme);
  if (ui_touch_api()) app_touch_navigation_enable(true);   // menus follow the system touch setting
  comm_init();
  comm_set_ready_handler(on_phone);
  comm_set_settings_handler(on_phone_settings);
  // Always start on the menu: a problem on some page can never lock the app.
  main_menu_push();
  if (!data_ok) {
    ui_dialog_show("Data missing", "The Quran data in this app is damaged. Please reinstall the app.");
  } else if (state_crashed_last_time()) {
    vibes_short_pulse();
    ui_dialog_show("Settings reset",
                   "Quran closed unexpectedly last time, so its settings were reset to the defaults. "
                   "Your reading position and bookmarks were kept.");
  }
}

static void deinit(void) {
  app_glance_reload(glance_cb, NULL);
  state_mark_clean_exit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
