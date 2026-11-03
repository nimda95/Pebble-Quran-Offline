#pragma once
// Arabic in the Pebble system font for the reader. The text comes from the
// phone in batches of ayahs; each ayah's block (surah header, basmala, text)
// is measured with the system text engine to know its line count.
#include <pebble.h>

typedef void (*SysTextUpdate)(void);

void systext_open(SysTextUpdate on_update);
void systext_close(void);
bool systext_usable(void);                  // system font chosen, glyphs present, phone ready
int  systext_pitch(void);                   // line height in px
int  systext_block_lines(uint16_t g);       // 0 while the ayah's text has not arrived
void systext_want(uint16_t top, int ahead_ayahs);   // request texts around the reading position
// Draws block lines [first, first+n) of ayah g, line `first` at y, in layer coordinates.
void systext_draw(GContext *ctx, uint16_t g, int first, int n, int y, int width, GColor ink);
