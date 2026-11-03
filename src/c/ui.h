#pragma once
// Shared UI helpers: theme colours, backlight, system-font Arabic detection, dialogs.
#include <pebble.h>

// Reading-page colours (theme). Menus and dialogs keep the standard system look.
GColor ui_bg(void);
GColor ui_fg(void);
void ui_style_status_bar(StatusBarLayer *s);
ActionMenuConfig ui_action_menu_config(ActionMenuLevel *root, ActionMenuDidCloseCb did_close);

bool ui_touch_api(void);              // firmware has the touch APIs
bool ui_touch_available(void);        // ... and touch input currently reaches apps

void ui_backlight_set(bool on);       // on: tinted with the chosen colour, stays lit
bool ui_backlight_is_on(void);

// True when system fonts can draw Arabic (needs a language pack with Arabic
// glyphs, e.g. "English + Arabic glyphs", on firmware 4.20+).
bool ui_system_arabic_available(void);

// Simple message window (title + body); SELECT or BACK closes it.
void ui_dialog_show(const char *title, const char *body);
