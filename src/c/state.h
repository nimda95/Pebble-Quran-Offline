#pragma once
#include <pebble.h>

#define BOOKMARK_SLOTS 20

enum { HEADER_OFF, HEADER_POSITION, HEADER_POSITION_JUZ, HEADER_CLOCK, HEADER_N };
enum { SCROLL_LINE, SCROLL_PAGE, SCROLL_N };
enum { THEME_LIGHT, THEME_DARK, THEME_NIGHT, THEME_AMBER, THEME_SEPIA, THEME_BLUE, THEME_CUSTOM, THEME_N };
enum { RENDER_AUTO, RENDER_WATCH, RENDER_N };
enum { FONT_QURAN, FONT_SYSTEM, FONT_N };
enum { LIGHT_DEFAULT, LIGHT_WARM, LIGHT_AMBER, LIGHT_RED, LIGHT_GREEN, LIGHT_BLUE, LIGHT_N };

#define SETTINGS_VERSION 4
#define ZOOM_DEFAULT 3                 // 28 px
#define SYSFONT_N 2                    // 24 / 28

// Sent to the phone as raw bytes (CMD_SETTINGS): keep field order stable.
typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t zoom;          // 0..4 -> 15/20/26/28/32 px
  uint8_t header;
  uint8_t scroll;
  uint8_t theme;
  uint8_t light;         // backlight colour preset
  uint8_t render;
  uint8_t font;          // Arabic text: bundled Quran font or system font
  uint8_t sysfont;       // system font size index
  uint8_t trans;         // translation edition index (0 = none)
  uint8_t glance;        // show the reading position in the launcher
  uint8_t paper, ink;    // custom theme colours (GColor8 argb)
} Settings;

#define POS_SYS_BASE 100          // Position.zoom for system-font line positions (100 + size)

typedef struct __attribute__((packed)) {
  uint16_t ayah;         // global ayah (0..6235) at the top of the screen
  uint8_t line;          // display line within that ayah's block
  uint8_t zoom;          // zoom the line index refers to (or POS_SYS_BASE + system font size)
} Position;

extern Settings g_set;
extern Position g_pos;

void state_load(void);
void state_save_settings(void);
void state_save_position(void);
void state_save_position_soon(void);     // debounced
void settings_reset(void);               // defaults; keeps position and bookmarks
void state_sanitize(void);               // clamp every field to its valid range

// Crash detection: returns true if the previous run did not exit cleanly.
// In that case the settings were already reset to defaults.
bool state_crashed_last_time(void);
void state_mark_clean_exit(void);

int  bookmarks_count(void);
uint16_t bookmark_get(int i);
int  bookmark_find(uint16_t ayah);       // -1 if absent
bool bookmark_add(uint16_t ayah);        // false if full or duplicate
void bookmark_remove(int i);

#define TRANS_N 15                       // 0 = none; ids live in src/pkjs/index.js
const char *trans_name(int i);

uint32_t light_rgb(int preset);          // 0 for system default
const char *light_name(int preset);
const char *theme_name(int t);
void theme_colors(int t, uint8_t *paper, uint8_t *ink);   // GColor8 argb
const char *header_name(int h);
