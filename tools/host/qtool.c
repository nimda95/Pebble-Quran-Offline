// Host build of the watch renderer (src/c/qdata.c + qrender.c).
//   qtool lines  <datadir> <out LINES.bin> [width]   precomputed display-line counts
//   qtool strips <datadir> <zoom> <g0> <g1> <out> [width]   strip records, for JS tests
//   qtool oldcmp <datadir> <zoom> <old-dump>   compare against the old renderer
//   qtool names  <datadir> <out NAMES.bin>     pre-rendered surah names (20 px zoom)
//   qtool stats  <datadir>                     sizes / overflow / cache stats
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qdata.h"
#include "qrender.h"

static const char *NAMES[QRES_COUNT] = {"TEXT", "LEX", "TUP15", "TUP20", "TUP26", "TUP28", "TUP32",
  "ATL15", "ATL20", "ATL26", "ATL28", "ATL32", "META", "LINES", "NAMES"};
static unsigned char *R[QRES_COUNT];
static uint32_t RS[QRES_COUNT];
static const char *DIR;

static int load(int i) {
  if (R[i]) return 1;
  char p[1024];
  snprintf(p, sizeof p, "%s/%s.bin", DIR, NAMES[i]);
  FILE *f = fopen(p, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END); RS[i] = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
  R[i] = malloc(RS[i] ? RS[i] : 1);
  if (fread(R[i], 1, RS[i], f) != RS[i]) { fclose(f); return 0; }
  fclose(f);
  return 1;
}
uint32_t qres_read(int res, uint32_t off, void *buf, uint32_t len) {
  if (!load(res) || off >= RS[res]) return 0;
  if (off + len > RS[res]) len = RS[res] - off;
  memcpy(buf, R[res] + off, len);
  return len;
}
uint32_t qres_size(int res) { return load(res) ? RS[res] : 0; }
void *qmalloc(uint32_t n) { return malloc(n); }
void qfree(void *p) { free(p); }

static uint8_t *canvas_for(int zoom) {
  static uint8_t *cv; static uint32_t sz;
  uint32_t need = qr_canvas_bytes(zoom);
  if (need > sz) { free(cv); cv = malloc(need); sz = need; }
  qr_set_canvas(cv, need);
  return cv;
}

static void open_zoom(int z) {
  if (!qeng_open(z, 24 * 1024)) { fprintf(stderr, "engine open failed (zoom %d)\n", z); exit(1); }
  canvas_for(z);
  qr_flush_layout_cache();
}

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: see source\n"); return 2; }
  DIR = argv[2];
  if (!qmeta_init()) { fprintf(stderr, "meta init failed\n"); return 1; }
  const char *cmd = argv[1];

  if (!strcmp(cmd, "lines")) {
    FILE *f = fopen(argv[3], "wb");
    int width = argc > 4 ? atoi(argv[4]) : QR_DEFAULT_W;
    qr_set_width(width);
    fwrite("QLN2", 1, 4, f); put16(f, QD_N_AYAH); fputc(QD_ZOOMS, f); fputc(0, f); put16(f, width); put16(f, 0);
    for (int z = 0; z < QD_ZOOMS; z++) {
      open_zoom(z);
      int mx = 0; long tot = 0;
      for (int g = 0; g < QD_N_AYAH; g++) {
        int n = qr_layout_block_lines((uint16_t)g);
        if (n > 255 || n < 1) { fprintf(stderr, "bad line count %d at %d\n", n, g); return 1; }
        if (n > mx) mx = n;
        tot += n;
        fputc(n, f);
      }
      fprintf(stderr, "zoom %d: %ld lines, max %d per ayah\n", z, tot, mx);
    }
    fclose(f);
    return 0;
  }

  if (!strcmp(cmd, "strips")) {
    int z = atoi(argv[3]), g0 = atoi(argv[4]), g1 = atoi(argv[5]);
    if (argc > 7) qr_set_width(atoi(argv[7]));
    open_zoom(z);
    FILE *f = fopen(argv[6], "wb");
    uint8_t rec[4096];
    for (int g = g0; g < g1 && g < QD_N_AYAH; g++) {
      int n = qr_layout_block_lines((uint16_t)g);
      for (int l = 0; l < n; l++) {
        int r = qr_render_strip((uint16_t)g, l, rec, sizeof rec);
        if (r < 0) { fprintf(stderr, "render failed %d:%d\n", g, l); return 1; }
        fwrite(rec, 1, r, f);
      }
    }
    fclose(f);
    return 0;
  }

  if (!strcmp(cmd, "oldcmp")) {
    // old dump: per line: u16 g, u8 l, u16 H, then 200*H levels; line box top at row 64
    int z = atoi(argv[3]);
    open_zoom(z);
    FILE *f = fopen(argv[4], "rb");
    uint8_t hdr[5], rec[4096], px[QR_STRIDE_FOR(QR_DEFAULT_W) * 128];
    uint8_t *old = malloc(200 * 400);
    long lines = 0, bad = 0, clipped = 0;
    while (fread(hdr, 1, 5, f) == 5) {
      int g = hdr[0] | (hdr[1] << 8), l = hdr[2], H = hdr[3] | (hdr[4] << 8);
      if (fread(old, 1, 200 * H, f) != (size_t)(200 * H)) break;
      // the old dump only has ayah lines (no separator, no bismillah); map to block lines
      int hl = qr_head_lines((uint16_t)g);
      int r = qr_render_strip((uint16_t)g, l + hl, rec, sizeof rec);
      if (r < 0) { bad++; continue; }
      int y0 = (int8_t)rec[4], h = rec[5], clen = rec[6] | (rec[7] << 8);
      memset(px, 0, sizeof px);
      if (h) qr_unpack(rec + QR_REC_HDR, clen, px, h * qr_stride());
      int mism = 0;
      for (int row = 0; row < H; row++) {
        int ry = row - 64 - y0;   // row in new strip
        for (int x = 0; x < 200; x++) {
          int ov = old[row * 200 + x];
          int nv = (ry >= 0 && ry < h) ? (px[ry * qr_stride() + x / 4] >> ((x & 3) * 2)) & 3 : 0;
          if (ov != nv) {
            int in_window = (row - 64) >= -QR_PAD && (row - 64) < qmeta_zoom(z)->line_h + QR_PAD;
            if (in_window) mism++; else clipped++;
          }
        }
      }
      if (mism) { if (bad < 5) fprintf(stderr, "mismatch %d:%d (%d px)\n", g, l, mism); bad++; }
      lines++;
    }
    printf("oldcmp zoom %d: %ld lines, %ld mismatching, %ld px clipped outside window\n", z, lines, bad, clipped);
    return bad ? 1 : 0;
  }

  if (!strcmp(cmd, "names")) {
    // QNM1 | u16 count | u16 0 | u32 offsets[count + 1] (absolute, last = end) | records
    enum { NAME_ZOOM = 1, HDR = 8 + 4 * (QD_N_SURAH + 1) };
    open_zoom(NAME_ZOOM);
    static uint8_t recs[QD_N_SURAH * 4096];
    uint32_t offs[QD_N_SURAH + 1], pos = 0;
    int maxr = 0;
    for (int s = 0; s < QD_N_SURAH; s++) {
      offs[s] = HDR + pos;
      int r = qr_render_name(s, recs + pos, 4096);
      if (r < 0) { fprintf(stderr, "name render failed %d\n", s); return 1; }
      if (r > maxr) maxr = r;
      pos += (uint32_t)r;
    }
    offs[QD_N_SURAH] = HDR + pos;
    FILE *f = fopen(argv[3], "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
    fwrite("QNM1", 1, 4, f); put16(f, QD_N_SURAH); put16(f, 0);
    for (int s = 0; s <= QD_N_SURAH; s++) put32(f, offs[s]);
    fwrite(recs, 1, pos, f);
    fclose(f);
    // read the file back through qmeta_name_record()
    free(R[QRES_NAMES]); R[QRES_NAMES] = NULL;
    f = fopen(argv[3], "rb");
    fseek(f, 0, SEEK_END); RS[QRES_NAMES] = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    R[QRES_NAMES] = malloc(RS[QRES_NAMES]);
    if (fread(R[QRES_NAMES], 1, RS[QRES_NAMES], f) != RS[QRES_NAMES]) { fclose(f); return 1; }
    fclose(f);
    uint8_t rb[4096];
    for (int s = 0; s < QD_N_SURAH; s++) {
      int n = qmeta_name_record(s, rb, sizeof rb);
      if (n != (int)(offs[s + 1] - offs[s]) || memcmp(rb, recs + offs[s] - HDR, n)) {
        fprintf(stderr, "readback mismatch at surah %d\n", s); return 1;
      }
    }
    printf("names: %d records at zoom %d, %u bytes, max record %d B\n",
           QD_N_SURAH, NAME_ZOOM, (unsigned)offs[QD_N_SURAH], maxr);
    return 0;
  }

  if (!strcmp(cmd, "stats")) {
    uint8_t rec[4096];
    for (int z = 0; z < QD_ZOOMS; z++) {
      open_zoom(z);
      memset(&g_qstats, 0, sizeof g_qstats);
      long n = 0, bytes = 0, maxb = 0, edge = 0;
      int miny = 99, maxy = -99;
      const QZoom *zz = qmeta_zoom(z);
      for (int g = 0; g < QD_N_AYAH; g += 1) {
        int nl = qr_layout_block_lines((uint16_t)g);
        for (int l = 0; l < nl; l++) {
          int r = qr_render_strip((uint16_t)g, l, rec, sizeof rec);
          if (r < 0) { fprintf(stderr, "fail %d:%d\n", g, l); return 1; }
          int y0 = (int8_t)rec[4], h = rec[5];
          if (h) { if (y0 < miny) miny = y0; if (y0 + h > maxy) maxy = y0 + h; }
          if (h && (y0 <= -QR_PAD || y0 + h >= zz->line_h + QR_PAD)) edge++;
          n++; bytes += r; if (r > maxb) maxb = r;
        }
      }
      printf("zoom %d (%dpx, line_h %d): %ld lines, avg rec %ld B, max %ld B, ink rows %d..%d, touching window edge %ld\n",
             z, zz->ppem, zz->line_h, n, bytes / n, maxb, miny, maxy, edge);
      printf("   word hit %u miss %u, glyph hit %u miss %u, pool flushes %u, reads %u\n",
             g_qstats.word_hits, g_qstats.word_misses, g_qstats.glyph_hits, g_qstats.glyph_misses,
             g_qstats.pool_flushes, g_qstats.reads);
    }
    return 0;
  }
  fprintf(stderr, "unknown command\n");
  return 2;
}
