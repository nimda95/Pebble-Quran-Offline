#pragma once
// Arabic micro-renderer: RTL word-wrap layout and rendering of single display
// lines into compressed 2bpp strips. The phone (src/pkjs/render.js) implements
// the exact same algorithm, so both produce byte-identical strips.
#include "qdata.h"

// The line width is a runtime setting (qr_set_width), so one engine serves any
// screen: e.g. 200 px on Pebble Time 2, a 200 px column on Round 2, 144 on Pebble 2.
#define QR_DEFAULT_W 200
#define QR_MARGIN 6
#define QR_STRIDE_FOR(w) (((w) + 3) / 4)   // 2bpp row bytes for a line width
#define QR_NAME_W 200                 // surah-name records are made at this width...
#define QR_NAME_RIGHT (QR_NAME_W - 2) // ...and end here (room for overhanging marks)
#define QR_PAD 10                     // rows rendered above/below the line box

// Strip record (as cached on the watch and sent by the phone):
//   u16 ayah | u8 line | u8 zoom | i8 y0 | u8 h | u16 clen | clen bytes
// y0 is the first ink row relative to the top of the line box (baseline - ascent);
// h rows of 50 bytes each, packed LSB-first (pixel x at bits (x&3)*2 of byte x/4),
// level 0 = paper .. 3 = ink, compressed with qr_pack.
// (Name records from qr_render_name() use the same layout with y0 relative to
// the baseline instead.)
#define QR_REC_HDR 8
typedef struct { uint16_t ayah; uint8_t line, zoom; int8_t y0; uint8_t h; uint16_t clen; } QStripHdr;

// Display lines of an ayah's block, via layout. A block is, in order:
//   1 surah separator line   (first ayah of every surah, all 114)
//   the bismillah lines      (first ayah of a surah that opens with one)
//   the ayah's own lines
// so for a surah's first ayah, block line 0 is the separator.
// qr_sep_lines() is 0 or 1, qr_head_lines() = separator + bismillah lines.
// Requires the engine for that zoom. Used by the build tools to make LINES.bin;
// the app itself reads the precomputed qmeta_block_lines().
int qr_sep_lines(uint16_t g);
int qr_bism_lines(uint16_t g);
int qr_head_lines(uint16_t g);
int qr_layout_block_lines(uint16_t g);

// Renders line `line` of ayah g's block at the engine's zoom and writes a strip
// record into out (cap bytes). Returns the record size, or -1 on failure.
int qr_render_strip(uint16_t g, int line, uint8_t *out, int cap);

// Renders surah surah_idx's Arabic name (as on the separator line) at the
// engine's zoom, right-aligned with its right edge at x = QR_NAME_RIGHT, and writes a
// record laid out like a strip record: ayah = surah_idx, line = 0, zoom = engine
// zoom, BUT y0 is the first ink row relative to the BASELINE (not the line-box
// top), so the record can be drawn at any baseline. Returns the record size,
// or -1 on failure. Used at build time for NAMES.bin (see qmeta_name_record()).
int qr_render_name(int surah_idx, uint8_t *out, int cap);

// Scratch canvas must be provided before rendering (H rows of qr_stride() bytes,
// H >= line_h + 2*QR_PAD for the zoom). Returns needed bytes for a zoom.
void qr_set_width(int w);            // line width in px (flushes the layout cache)
int  qr_width(void);
int  qr_stride(void);
uint32_t qr_canvas_bytes(int zoom);
void qr_set_canvas(uint8_t *buf, uint32_t bytes);

// PackBits-style codec for 2bpp data.
//   0x00..0x7F: n+1 zero bytes | 0x80..0xBF: (n&63)+1 literal bytes follow |
//   0xC0..0xFF: next byte repeated (n&63)+3 times
int qr_pack(const uint8_t *src, int n, uint8_t *dst, int cap);
int qr_unpack(const uint8_t *src, int clen, uint8_t *dst, int n);

void qr_flush_layout_cache(void);
