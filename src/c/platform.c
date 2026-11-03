// Watch implementation of the qdata platform layer.
#include <pebble.h>
#include "qdata.h"

static const uint32_t RES_IDS[QRES_COUNT] = {
  RESOURCE_ID_DATA_TEXT, RESOURCE_ID_DATA_LEX,
  RESOURCE_ID_DATA_TUP15, RESOURCE_ID_DATA_TUP20, RESOURCE_ID_DATA_TUP26,
  RESOURCE_ID_DATA_TUP28, RESOURCE_ID_DATA_TUP32,
  RESOURCE_ID_DATA_ATL15, RESOURCE_ID_DATA_ATL20, RESOURCE_ID_DATA_ATL26,
  RESOURCE_ID_DATA_ATL28, RESOURCE_ID_DATA_ATL32,
  RESOURCE_ID_DATA_META, RESOURCE_ID_DATA_LINES, RESOURCE_ID_DATA_NAMES,
};
static ResHandle s_handles[QRES_COUNT];

uint32_t qres_read(int res, uint32_t off, void *buf, uint32_t len) {
  if (res < 0 || res >= QRES_COUNT) return 0;
  if (!s_handles[res]) s_handles[res] = resource_get_handle(RES_IDS[res]);
  return (uint32_t)resource_load_byte_range(s_handles[res], off, buf, len);
}

uint32_t qres_size(int res) {
  if (res < 0 || res >= QRES_COUNT) return 0;
  if (!s_handles[res]) s_handles[res] = resource_get_handle(RES_IDS[res]);
  return (uint32_t)resource_size(s_handles[res]);
}

void *qmalloc(uint32_t n) { return malloc(n); }
void qfree(void *p) { free(p); }
