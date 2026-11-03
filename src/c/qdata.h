#pragma once
// Quran data access: metadata (always loaded, small) and the per-zoom glyph
// engine tables (loaded only when the watch has to render text itself).
// Builds for the watch and for the host tools (-DHOST_BUILD).

#include <stdint.h>
#include <stdbool.h>

#define QD_N_AYAH   6236
#define QD_N_SURAH  114
#define QD_N_JUZ    30
#define QD_N_HIZB   60
#define QD_ZOOMS    5
#define QD_MAX_WORDS_PER_AYAH 160
#define QD_MAX_TUPLES_PER_WORD 24        // longest word in the data has 21
#define QD_DIGIT_PSEUDO 0xFF00u
#define QD_NO_GLYPH 0xFFFFu

// Resources, in the order the platform layer maps them.
enum { QRES_TEXT, QRES_LEX, QRES_TUP0, QRES_TUP1, QRES_TUP2, QRES_TUP3, QRES_TUP4,
       QRES_ATL0, QRES_ATL1, QRES_ATL2, QRES_ATL3, QRES_ATL4, QRES_META,
       QRES_LINES, QRES_NAMES, QRES_COUNT };

typedef struct { uint16_t first_global, ayah_count;
                 uint8_t bismillah, name_len; uint16_t name_off;
                 char translit[16]; } QSurah;
typedef struct { uint8_t ppem, line_h, ascent, space_w; int8_t digit_raise; } QZoom;

// Packed tuple: glyph index (into the zoom's glyph directory), advance, offsets.
typedef struct __attribute__((packed)) { uint16_t gi; uint8_t adv; int8_t dx, dy; } QTuple;
typedef struct { uint16_t gid, off, adv; uint8_t w, h; int8_t left, top; } QGlyph;

// ---- metadata (META + LINES), no engine needed ----
bool qmeta_init(void);
const QSurah *qmeta_surah(int idx);
int  qmeta_surah_of(uint16_t g);
int  qmeta_ayah_no(uint16_t g);                 // 1-based number within its surah
uint16_t qmeta_juz_start(int j);
uint16_t qmeta_hizb_start(int h);
int  qmeta_juz_of(uint16_t g);
int  qmeta_hizb_of(uint16_t g);
const QZoom *qmeta_zoom(int z);
uint16_t qmeta_bismillah_word(int i);           // 0..3
int  qmeta_block_lines(int zoom, uint16_t g);   // display lines of ayah g (incl. header, bismillah)
int  qmeta_lines_width(void);                   // line width the LINES table was made for
int  qmeta_surah_name(int idx, uint16_t *out_syms, int max);
// Pre-rendered name of surah idx (NAMES.bin, made by `qtool names` at the 20 px
// zoom): copies its record (see qr_render_name() in qrender.h; y0 is relative
// to the baseline) into buf and returns its length, or -1 on error / if it
// does not fit in cap. Needs no engine.
int  qmeta_name_record(int idx, uint8_t *buf, int cap);

// ---- text (works without the engine) ----
int  qdata_ayah_words(uint16_t g, uint16_t *out_ids, int max);

// ---- engine: per-zoom tables + caches ----
// glyph_pool_bytes sizes the glyph bitmap cache. Returns false on low memory
// (nothing stays allocated in that case).
bool qeng_open(int zoom, uint32_t glyph_pool_bytes);
void qeng_close(void);
bool qeng_ready(void);
int  qeng_zoom(void);

int  qeng_word_tuples(uint16_t wid, const uint16_t **out_syms, int *out_width);
const QTuple *qeng_tuple(uint16_t sym);
const QGlyph *qeng_glyph_by_index(uint16_t gi);
const QGlyph *qeng_glyph_by_gid(uint16_t gid);   // NULL if absent
uint16_t qeng_glyph_index(const QGlyph *g);
const uint8_t *qeng_glyph_bitmap(uint16_t gi);   // 2bpp rows, LSB-first; NULL on error
uint16_t qeng_sign_gi(void);

// ---- stats (host tools / debug) ----
typedef struct { uint32_t reads, word_hits, word_misses, glyph_hits, glyph_misses, pool_flushes; } QStats;
extern QStats g_qstats;

// ---- platform layer (implemented per build flavor) ----
uint32_t qres_read(int res, uint32_t off, void *buf, uint32_t len);
uint32_t qres_size(int res);
void *qmalloc(uint32_t n);
void qfree(void *p);

// ---- canonical Huffman bit reader (MSB-first) ----
typedef struct { const uint8_t *buf; uint32_t len, pos; uint32_t acc; int n; } QBitRd;
void qbr_init(QBitRd *b, const uint8_t *buf, uint32_t len);
int  qbr_sym(QBitRd *b, const uint16_t *counts);   // counts[1..32]; -1 on error
