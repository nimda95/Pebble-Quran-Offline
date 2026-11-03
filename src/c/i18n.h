#pragma once
// UI language. The strings live in a resource (resources/data/LANG.bin, made
// by tools/gen_lang.py from lang/*.json); only the language in use is in RAM.
#include <pebble.h>
#include "i18n_ids.h"

// Picks the language from g_lang (LANG_AUTO: the watch's system locale) and
// loads it. Arabic needs Arabic glyphs in the system font, else English is used.
// Returns true if the language in use changed.
bool i18n_update(void);
bool i18n_ready(void);                 // strings loaded (false only if LANG.bin is broken)
int  i18n_lang(void);                  // language in use, LANG_EN...

const char *i18n_str(int id);          // "" if not loaded
#define _(id) i18n_str(id)

const char *i18n_lang_name(int lang);  // for the language menu (LANG_AUTO..LANG_N-1)
const char *i18n_surah_name(int si);   // Arabic name in the Arabic UI, transliterated otherwise
