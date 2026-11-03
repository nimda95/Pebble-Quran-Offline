#pragma once
// Per-platform layout profile. Everything that depends on the screen lives here,
// so the renderer and the reader stay generic (rectangular and round screens).
#include <pebble.h>

typedef struct {
  int16_t col_x, col_w;        // text column: strips are col_w pixels wide
  int16_t band_top, band_bot;  // rows where the whole column is visible (round screens)
} Profile;

const Profile *profile_get(void);
