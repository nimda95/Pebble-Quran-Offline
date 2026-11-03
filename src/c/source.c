#include "source.h"
#include "strips.h"
#include "comm.h"
#include "state.h"
#include "qdata.h"
#include "qrender.h"
#include "profile.h"

#define URGENT_TIMEOUT_MS 2500       // phone silent this long -> watch renders the page
#define PREFETCH_TIMEOUT_MS 5000
#define LOCAL_HOLD_S 30              // after the phone missed a page, stay on the watch this long
#define MAX_REQ_LINES 16
#define LOCAL_STEP_MS 25
#define REC_BUF 2048
#define PHONE_CACHE 32000            // strip cache while the phone renders
#define LOCAL_CACHE 16000            // ... while the watch renders (engine needs ~40 KB)
#define HEAP_RESERVE 20000           // always leave this much for windows and menus
#define BYTES_PER_LINE 900           // budget per cached line (average strip is 540-820 B)

static SourceUpdateHandler s_on_update;
static bool s_open;
static int s_zoom = -1;
static LinePos s_top;
static int s_vis = 1, s_ahead = 2, s_behind = 1;
static bool s_phone;                 // current page is served by the phone
static time_t s_local_until;         // watch keeps rendering until then

static uint32_t s_req_id;
static bool s_inflight, s_inflight_urgent;
static AppTimer *s_req_timer, *s_local_timer, *s_retry_timer;
static int s_timeouts;

static uint8_t *s_canvas;            // local renderer scratch
static uint8_t *s_rec;               // local renderer output record
static bool s_engine_failed;
static uint32_t s_n_phone, s_n_local;

// ---------------------------------------------------------------- lines
bool line_next(int zoom, LinePos *p) {
  if (p->l + 1 < qmeta_block_lines(zoom, p->g)) { p->l++; return true; }
  if (p->g + 1 >= QD_N_AYAH) return false;
  p->g++; p->l = 0;
  return true;
}

bool line_prev(int zoom, LinePos *p) {
  if (p->l > 0) { p->l--; return true; }
  if (p->g == 0) return false;
  p->g--;
  p->l = (uint8_t)(qmeta_block_lines(zoom, p->g) - 1);
  return true;
}

// The prefetch window must fit in the cache, or prefetching would evict
// its own lines and never settle.
static void size_window(void) {
  int fit = (int)(strips_capacity() / BYTES_PER_LINE) - 1;
  int window = 4 * s_vis;
  if (window > fit) window = fit;
  if (window < s_vis) window = s_vis;
  int extra = window - s_vis;
  s_ahead = extra * 2 / 3;
  s_behind = extra - s_ahead;
}

// first line in [from, from+count) that is not cached; run = consecutive missing lines
static bool first_missing(LinePos from, int count, LinePos *out, int *run) {
  LinePos p = from;
  for (int i = 0; i < count; i++) {
    if (!strips_get(s_zoom, p.g, p.l)) {
      *out = p;
      int n = 1;
      LinePos q = p;
      while (i + n < count && n < MAX_REQ_LINES && line_next(s_zoom, &q) && !strips_get(s_zoom, q.g, q.l)) n++;
      *run = n;
      return true;
    }
    if (!line_next(s_zoom, &p)) break;
  }
  return false;
}

static LinePos behind_start(int *n) {
  LinePos p = s_top;
  *n = 0;
  for (int i = 0; i < s_behind; i++) { if (!line_prev(s_zoom, &p)) break; (*n)++; }
  return p;
}

static bool visible_missing(void) {
  LinePos p; int run;
  return first_missing(s_top, s_vis, &p, &run);
}

static bool phone_allowed(void) {
  return g_set.render == RENDER_AUTO && comm_phone_ready() && time(NULL) >= s_local_until;
}

// ---------------------------------------------------------------- local rendering
static bool ensure_engine(void) {
  if (qeng_ready() && qeng_zoom() == s_zoom && s_canvas) return true;
  if (s_engine_failed) return false;
  free(s_canvas); s_canvas = NULL;
  qeng_close();
  if (strips_capacity() > LOCAL_CACHE) { strips_resize(LOCAL_CACHE); size_window(); }
  if (!s_rec) s_rec = malloc(REC_BUF);
  qr_set_width(profile_get()->col_w);
  uint32_t cvb = qr_canvas_bytes(s_zoom);
  s_canvas = malloc(cvb);
  if (!s_rec || !s_canvas) goto fail;
  // glyph pool: as large as possible while keeping the reserve
  static const uint32_t pools[] = { 14000, 10000, 6000, 4000 };
  for (unsigned i = 0; i < ARRAY_LENGTH(pools); i++) {
    if (heap_bytes_free() < pools[i] + 26000 + HEAP_RESERVE && i + 1 < ARRAY_LENGTH(pools)) continue;
    if (qeng_open(s_zoom, pools[i])) goto ok;
  }
  APP_LOG(APP_LOG_LEVEL_WARNING, "low memory (%u free): shrinking cache", (unsigned)heap_bytes_free());
  strips_resize(9000);
  size_window();
  if (qeng_open(s_zoom, 4000)) goto ok;
fail:
  APP_LOG(APP_LOG_LEVEL_ERROR, "cannot open renderer: %u bytes free", (unsigned)heap_bytes_free());
  free(s_canvas); s_canvas = NULL;
  s_engine_failed = true;
  return false;
ok:
  qr_set_canvas(s_canvas, cvb);
  qr_flush_layout_cache();
  APP_LOG(APP_LOG_LEVEL_INFO, "watch renderer ready (zoom %d), %u bytes free", s_zoom, (unsigned)heap_bytes_free());
  return true;
}

static void release_engine(void) {
  if (!qeng_ready() && !s_canvas) return;
  qeng_close();
  free(s_canvas); s_canvas = NULL;
  free(s_rec); s_rec = NULL;
  if (heap_bytes_free() > PHONE_CACHE + HEAP_RESERVE) strips_resize(PHONE_CACHE);
  size_window();
  APP_LOG(APP_LOG_LEVEL_INFO, "phone renders again; %u bytes free", (unsigned)heap_bytes_free());
}

static bool render_local(LinePos p, bool force) {
  if (!ensure_engine()) return false;
  int n = qr_render_strip(p.g, p.l, s_rec, REC_BUF);
  if (n <= 0) return false;
  s_n_local++;
  return strips_put(s_rec, n, force);
}

static void local_step(void *d);

static void schedule_local(void) {
  if (!s_local_timer) s_local_timer = app_timer_register(LOCAL_STEP_MS, local_step, NULL);
}

// one prefetch line per tick so buttons stay responsive
static void local_step(void *d) {
  s_local_timer = NULL;
  if (!s_open || s_phone || s_engine_failed) return;
  LinePos p, b; int run, nb;
  b = behind_start(&nb);
  if (first_missing(s_top, s_vis + s_ahead, &p, &run) || (nb && first_missing(b, nb, &p, &run))) {
    if (render_local(p, false)) schedule_local();      // stop once the cache is full
  }
}

static void render_visible_locally(void) {
  LinePos p; int run;
  int guard = 0;
  while (first_missing(s_top, s_vis, &p, &run) && guard++ < 64) {
    if (!render_local(p, true)) break;
  }
}

// ---------------------------------------------------------------- phone requests
static void pump(void);

static void cancel_request(void) {
  s_inflight = false;
  if (s_req_timer) { app_timer_cancel(s_req_timer); s_req_timer = NULL; }
}

static void req_timeout(void *d) {
  s_req_timer = NULL;
  bool urgent = !s_inflight || s_inflight_urgent;
  s_inflight = false;
  if (!s_open) return;
  if (urgent && visible_missing()) {
    if (++s_timeouts >= 2) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "phone not answering; rendering on the watch");
      comm_mark_unresponsive();
    }
    s_local_until = time(NULL) + LOCAL_HOLD_S;
    s_phone = false;
    render_visible_locally();
    if (s_on_update) s_on_update();
  }
  pump();
}

static void retry_cb(void *d) { s_retry_timer = NULL; pump(); }

static void arm_deadline(bool urgent) {
  if (s_req_timer) app_timer_reschedule(s_req_timer, urgent ? URGENT_TIMEOUT_MS : PREFETCH_TIMEOUT_MS);
  else s_req_timer = app_timer_register(urgent ? URGENT_TIMEOUT_MS : PREFETCH_TIMEOUT_MS, req_timeout, NULL);
}

static bool send_request(LinePos p, int count, bool urgent) {
  uint32_t id = ++s_req_id;
  if (!comm_request_lines(id, s_zoom, p.g, p.l, count)) {
    // outbox busy: try again shortly (an urgent deadline is already running)
    if (!s_retry_timer) s_retry_timer = app_timer_register(80, retry_cb, NULL);
    return false;
  }
  s_inflight = true;
  s_inflight_urgent = urgent;
  arm_deadline(urgent);
  return true;
}

static void pump(void) {
  if (!s_open || s_zoom < 0) return;
  comm_maybe_hello();
  s_phone = phone_allowed();
  if (s_phone) release_engine();
  if (!s_phone) {
    cancel_request();
    if (visible_missing()) {
      render_visible_locally();
      if (s_on_update) s_on_update();
    }
    schedule_local();
    return;
  }
  LinePos p; int run;
  if (first_missing(s_top, s_vis, &p, &run)) {
    if (s_inflight && s_inflight_urgent) return;
    if (!s_req_timer || !s_inflight) arm_deadline(true);   // deadline starts now, even if the outbox is busy
    int n; LinePos q;
    first_missing(p, s_vis + s_ahead, &q, &n);             // the missing visible lines and what follows
    send_request(p, n, true);
    return;
  }
  if (s_inflight) return;
  if (s_req_timer) { app_timer_cancel(s_req_timer); s_req_timer = NULL; }
  if (first_missing(s_top, s_vis + s_ahead, &p, &run)) { send_request(p, run, false); return; }
  int nb;
  LinePos b = behind_start(&nb);
  if (nb > 0 && first_missing(b, nb, &p, &run)) send_request(p, run, false);
}

static void on_lines(uint32_t req, int zoom, const uint8_t *data, int len, bool last) {
  if (!s_open) return;
  bool visible_before = visible_missing();
  int off = 0;
  while (data && off + QR_REC_HDR <= len) {
    int clen = data[off + 6] | (data[off + 7] << 8);
    int rl = QR_REC_HDR + clen;
    if (off + rl > len) break;
    if (data[off + 3] == s_zoom && strips_put(data + off, rl, false)) s_n_phone++;
    off += rl;
  }
  if (req == s_req_id && s_inflight) {
    s_timeouts = 0;
    if (last) cancel_request();
    else arm_deadline(s_inflight_urgent);                    // progress: extend the deadline
  }
  if (visible_before && s_on_update) s_on_update();
  if (!s_inflight) pump();
}

// ---------------------------------------------------------------- API
bool source_open(SourceUpdateHandler cb) {
  s_on_update = cb;
  s_engine_failed = false;
  static const uint32_t caps[] = { PHONE_CACHE, 22000, 16000 };
  bool ok = false;
  for (unsigned i = 0; i < ARRAY_LENGTH(caps) && !ok; i++) ok = strips_init(caps[i]);
  if (!ok) return false;
  s_open = true;
  s_zoom = -1;
  comm_set_lines_handler(on_lines);
  return true;
}

void source_close(void) {
  s_open = false;
  cancel_request();
  if (s_local_timer) { app_timer_cancel(s_local_timer); s_local_timer = NULL; }
  if (s_retry_timer) { app_timer_cancel(s_retry_timer); s_retry_timer = NULL; }
  qeng_close();
  free(s_canvas); s_canvas = NULL;
  free(s_rec); s_rec = NULL;
  strips_deinit();
  s_zoom = -1;
}

void source_set_position(int zoom, LinePos top, int visible) {
  if (!s_open) return;
  if (zoom != s_zoom) {
    strips_clear();
    cancel_request();
    if (qeng_ready() && qeng_zoom() != zoom) { qeng_close(); free(s_canvas); s_canvas = NULL; }
    s_engine_failed = false;
  }
  s_zoom = zoom;
  s_top = top;
  s_vis = visible < 1 ? 1 : visible;
  size_window();
  strips_set_focus(zoom, top.g, top.l);
  pump();
}

const uint8_t *source_get(LinePos p) { return strips_get(s_zoom, p.g, p.l); }
bool source_using_phone(void) { return s_phone; }
bool source_failed(void) { return s_engine_failed && !s_phone; }

const char *source_status(void) {
  if (g_set.render == RENDER_WATCH) return "Watch (phone off)";
  if (comm_phone_ready()) return time(NULL) < s_local_until ? "Watch (phone slow)" : "Phone";
  if (comm_phone_connected()) return "Watch (phone app silent)";
  return "Watch (no phone)";
}

void source_counts(uint32_t *p, uint32_t *w) { *p = s_n_phone; *w = s_n_local; }

void source_connection_changed(void) {
  if (comm_phone_ready()) s_local_until = 0;     // a fresh handshake: try the phone again
  if (s_open) pump();
}
