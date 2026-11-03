#include "i18n.h"
#include "state.h"
#include "ui.h"
#include "qdata.h"
#include "names_ar.h"

#define HDR 8
static uint8_t s_blk[I18N_BLOCK_MAX + 1];     // the last byte stays 0
static int s_cur = -1;                         // language in s_blk, -1 = none

static uint32_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int system_lang(void) {
  static const char *const CODES[] = I18N_CODES;
  const char *loc = i18n_get_system_locale();
  if (!loc) return LANG_EN;
  for (int i = 0; i < LANG_N - 1; i++)
    if (strncmp(loc, CODES[i], 2) == 0) return LANG_EN + i;
  if (strncmp(loc, "in", 2) == 0) return LANG_ID;     // old code for Indonesian
  return LANG_EN;
}

static int wanted(void) {
  int l = g_lang == LANG_AUTO ? system_lang() : g_lang;
  if (l <= LANG_AUTO || l >= LANG_N) l = LANG_EN;
  if (l == LANG_AR && !ui_system_arabic_available()) l = LANG_EN;
  return l;
}

static bool load(int lang) {
  ResHandle h = resource_get_handle(RESOURCE_ID_DATA_LANG);
  uint8_t hdr[HDR + 8 * (LANG_N - 1)];
  if (!h || resource_load_byte_range(h, 0, hdr, sizeof(hdr)) != sizeof(hdr)) return false;
  if (memcmp(hdr, "QLG1", 4) || hdr[4] < LANG_N - 1 || rd16(hdr + 6) != S_N) return false;
  const uint8_t *e = hdr + HDR + 8 * (lang - LANG_EN);
  uint32_t off = rd32(e), size = rd32(e + 4);
  if (size < 2 * S_N || size > I18N_BLOCK_MAX) return false;
  memset(s_blk, 0, sizeof(s_blk));
  s_cur = -1;
  if (resource_load_byte_range(h, off, s_blk, size) != size) return false;
  for (int i = 0; i < S_N; i++) if (rd16(s_blk + 2 * i) >= size) return false;
  s_cur = lang;
  return true;
}

bool i18n_update(void) {
  int l = wanted();
  if (l == s_cur) return false;
  if (!load(l) && l != LANG_EN) load(LANG_EN);
  APP_LOG(APP_LOG_LEVEL_INFO, "ui language %d (setting %d, locale %s)", s_cur, g_lang, i18n_get_system_locale());
  return true;
}

bool i18n_ready(void) { return s_cur >= 0; }
int i18n_lang(void) { return s_cur >= 0 ? s_cur : LANG_EN; }

const char *i18n_str(int id) {
  if (s_cur < 0 || id < 0 || id >= S_N) return "";
  return (const char *)s_blk + rd16(s_blk + 2 * id);
}

const char *i18n_lang_name(int lang) {
  static const char *const NAMES[] = I18N_NAMES;
  if (lang <= LANG_AUTO || lang >= LANG_N) return _(S_LANG_AUTO);
  if (lang == LANG_AR && !ui_system_arabic_available()) return _(S_LANG_AR_NO_FONT);
  return NAMES[lang - LANG_EN];
}

const char *i18n_surah_name(int si) {
  if (si < 0 || si >= QD_N_SURAH) si = 0;
  return i18n_lang() == LANG_AR ? NAMES_AR[si] : qmeta_surah(si)->translit;
}
