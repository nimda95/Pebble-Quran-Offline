#pragma once
// Cache of rendered line strips (records from qrender.h, from the phone or the
// local renderer) and drawing of strips into the frame buffer.
#include <pebble.h>

bool strips_init(uint32_t cap_bytes);       // allocates the arena (frees any old one)
bool strips_resize(uint32_t cap_bytes);     // keeps the entries nearest the focus
void strips_deinit(void);
uint32_t strips_capacity(void);
void strips_clear(void);

// Position used to choose eviction victims (farthest first).
void strips_set_focus(int zoom, uint16_t ayah, uint8_t line);

// Copies a record in. Only evicts lines farther from the focus than the new one
// (unless force), so a prefetch can never push out what it is meant to keep.
// Returns false if the record was not stored.
bool strips_put(const uint8_t *rec, int len, bool force);
const uint8_t *strips_get(int zoom, uint16_t ayah, uint8_t line);
int strips_count(void);

// Palette for levels 0..3 (0 = paper). Used by strips_draw and by UI colours.
void strips_set_theme(int theme);
GColor strips_color(int level);

// Draws a record with its line box top at y_box into an 8-bit frame buffer,
// blending darker-wins so overlapping diacritics of adjacent lines combine.
// rs: the record's row bytes (QR_STRIDE_FOR(line width)); x_off: column position.
void strips_draw(const uint8_t *rec, int rs, uint8_t *fb, int stride, int fb_w, int fb_h, int x_off, int y_box);
// Same with explicit colours (y is the record's reference row).
void strips_draw_colors(const uint8_t *rec, int rs, uint8_t *fb, int stride, int fb_w, int fb_h, int x_off, int y,
                        GColor paper, GColor ink);
