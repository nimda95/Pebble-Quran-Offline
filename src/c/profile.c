#include "profile.h"

static Profile s_p;
static bool s_ready;

static int isqrt(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

const Profile *profile_get(void) {
  if (s_ready) return &s_p;
  s_ready = true;
  const int w = PBL_DISPLAY_WIDTH, h = PBL_DISPLAY_HEIGHT;
#if defined(PBL_ROUND)
  // a column of ~77% of the diameter (200 px on a 260 px screen), and the rows
  // where it fits entirely inside the circle
  s_p.col_w = (int16_t)(w * 10 / 13);
  s_p.col_x = (int16_t)((w - s_p.col_w) / 2);
  int r = w / 2, c = s_p.col_w / 2, half = isqrt(r * r - c * c);
  s_p.band_top = (int16_t)(h / 2 - half);
  s_p.band_bot = (int16_t)(h / 2 + half);
#else
  (void)isqrt;
  s_p.col_x = 0;
  s_p.col_w = (int16_t)w;
  s_p.band_top = 0;
  s_p.band_bot = (int16_t)h;
#endif
  return &s_p;
}
