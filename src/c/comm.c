#include "comm.h"
#include "state.h"
#include "data_version.h"
#include "profile.h"

static bool s_ready;
static bool s_settings_pending;
static CommVoidHandler s_ready_cb, s_settings_cb;
static CommLinesHandler s_lines_cb;
#define TEXT_HANDLERS 3
static CommTextHandler s_text_cbs[TEXT_HANDLERS];
static time_t s_last_hello;

bool comm_phone_connected(void) { return connection_service_peek_pebble_app_connection(); }
bool comm_phone_ready(void) { return s_ready && comm_phone_connected(); }
void comm_mark_unresponsive(void) { s_ready = false; }

void comm_set_ready_handler(CommVoidHandler h) { s_ready_cb = h; }
void comm_set_lines_handler(CommLinesHandler h) { s_lines_cb = h; }
void comm_add_text_handler(CommTextHandler h) {
  for (int i = 0; i < TEXT_HANDLERS; i++) if (s_text_cbs[i] == h) return;
  for (int i = 0; i < TEXT_HANDLERS; i++) if (!s_text_cbs[i]) { s_text_cbs[i] = h; return; }
}
void comm_set_settings_handler(CommVoidHandler h) { s_settings_cb = h; }

static bool begin(DictionaryIterator **it, uint8_t cmd) {
  if (!comm_phone_connected()) return false;
  if (app_message_outbox_begin(it) != APP_MSG_OK) return false;
  dict_write_uint8(*it, MESSAGE_KEY_CMD, cmd);
  return true;
}

static void send_hello(void) {
  DictionaryIterator *it;
  s_last_hello = time(NULL);
  if (!begin(&it, CMD_HELLO)) return;
  dict_write_uint32(it, MESSAGE_KEY_VER, DATA_VERSION);
  dict_write_uint32(it, MESSAGE_KEY_COUNT, app_message_inbox_size_maximum());
  app_message_outbox_send();
}

void comm_maybe_hello(void) {
  if (!s_ready && comm_phone_connected() && time(NULL) - s_last_hello >= 15) send_hello();
}

void comm_send_settings(void) {
  DictionaryIterator *it;
  if (!begin(&it, CMD_SETTINGS)) { s_settings_pending = true; return; }
  dict_write_data(it, MESSAGE_KEY_DATA, (const uint8_t *)&g_set, sizeof(g_set));
  if (app_message_outbox_send() != APP_MSG_OK) s_settings_pending = true;
  else s_settings_pending = false;
}

bool comm_request_lines(uint32_t req, int zoom, uint16_t ayah, uint8_t line, int count) {
  if (!comm_phone_ready()) return false;
  DictionaryIterator *it;
  if (!begin(&it, CMD_LINES_REQ)) return false;
  dict_write_uint32(it, MESSAGE_KEY_REQ, req);
  dict_write_uint8(it, MESSAGE_KEY_ZOOM, (uint8_t)zoom);
  dict_write_uint16(it, MESSAGE_KEY_AYAH, ayah);
  dict_write_uint8(it, MESSAGE_KEY_LINE, line);
  dict_write_uint16(it, MESSAGE_KEY_COUNT, (uint16_t)count);
  dict_write_uint16(it, MESSAGE_KEY_WIDTH, (uint16_t)profile_get()->col_w);
  return app_message_outbox_send() == APP_MSG_OK;
}

bool comm_request_text(uint32_t req, uint16_t first, int count, uint8_t flags) {
  if (!comm_phone_ready()) return false;
  DictionaryIterator *it;
  if (!begin(&it, CMD_TEXT_REQ)) return false;
  dict_write_uint32(it, MESSAGE_KEY_REQ, req);
  dict_write_uint16(it, MESSAGE_KEY_AYAH, first);
  dict_write_uint16(it, MESSAGE_KEY_COUNT, (uint16_t)count);
  dict_write_uint8(it, MESSAGE_KEY_FLAGS, flags);
  dict_write_uint8(it, MESSAGE_KEY_LINE, g_set.trans);
  return app_message_outbox_send() == APP_MSG_OK;
}

// a cstring tuple, or NULL if the tuple is missing or not NUL-terminated
static const char *tstr(Tuple *t) {
  if (!t || t->type != TUPLE_CSTRING || t->length == 0) return NULL;
  if (t->value->cstring[t->length - 1] != 0) return NULL;
  return t->value->cstring;
}

static uint32_t tu32(Tuple *t) {
  if (!t) return 0;
  switch (t->length) {
    case 1: return t->value->uint8;
    case 2: return t->value->uint16;
    default: return t->value->uint32;
  }
}

static void inbox(DictionaryIterator *it, void *ctx) {
  Tuple *t = dict_find(it, MESSAGE_KEY_CMD);
  if (!t) return;
  switch (tu32(t)) {
    case CMD_READY: {
      uint32_t ver = tu32(dict_find(it, MESSAGE_KEY_VER));
      s_ready = ver == DATA_VERSION;
      if (!s_ready) APP_LOG(APP_LOG_LEVEL_WARNING, "phone data version %lu != %lu; rendering on watch",
                            (unsigned long)ver, (unsigned long)DATA_VERSION);
      comm_send_settings();
      if (s_ready_cb) s_ready_cb();
      break;
    }
    case CMD_LINES: {
      Tuple *td = dict_find(it, MESSAGE_KEY_DATA);
      if (s_lines_cb)
        s_lines_cb(tu32(dict_find(it, MESSAGE_KEY_REQ)), (int)tu32(dict_find(it, MESSAGE_KEY_ZOOM)),
                   td ? td->value->data : NULL, td ? td->length : 0,
                   (tu32(dict_find(it, MESSAGE_KEY_FLAGS)) & 1) != 0);
      break;
    }
    case CMD_SETTINGS: {
      Tuple *td = dict_find(it, MESSAGE_KEY_DATA);
      if (td && td->length >= sizeof(Settings)) {
        Settings s;
        memcpy(&s, td->value->data, sizeof(s));
        if (s.version == SETTINGS_VERSION) {
          g_set = s;
          state_sanitize();
          state_save_settings();
          if (s_settings_cb) s_settings_cb();
        }
      }
      break;
    }
    case CMD_TEXT: {
      uint32_t first = tu32(dict_find(it, MESSAGE_KEY_AYAH));
      uint32_t count = tu32(dict_find(it, MESSAGE_KEY_COUNT));
      if (first >= 6236 || count > 200) break;
      const char *ar = tstr(dict_find(it, MESSAGE_KEY_TEXT));
      const char *tr = tstr(dict_find(it, MESSAGE_KEY_TEXT2));
      uint8_t err = (uint8_t)tu32(dict_find(it, MESSAGE_KEY_FLAGS));
      uint32_t req = tu32(dict_find(it, MESSAGE_KEY_REQ));
      for (int i = 0; i < TEXT_HANDLERS; i++)
        if (s_text_cbs[i]) s_text_cbs[i](req, (uint16_t)first, (int)count, ar, tr, err);
      break;
    }
  }
}

static void dropped(AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "message from phone dropped (%d)", (int)reason);
}

static void sent(DictionaryIterator *it, void *ctx) {
  if (s_settings_pending) comm_send_settings();
}

static void connection(bool connected) {
  if (!connected) s_ready = false;
  else send_hello();
  if (s_ready_cb) s_ready_cb();
}

void comm_init(void) {
  app_message_register_inbox_received(inbox);
  app_message_register_outbox_sent(sent);
  app_message_register_inbox_dropped(dropped);
  uint32_t in = app_message_inbox_size_maximum();
  if (in > 8200) in = 8200;
  app_message_open(in, 256);
  connection_service_subscribe((ConnectionHandlers){ .pebble_app_connection_handler = connection });
  send_hello();
}
