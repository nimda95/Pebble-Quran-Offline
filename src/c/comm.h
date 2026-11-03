#pragma once
// AppMessage protocol with the phone (src/pkjs/index.js).
#include <pebble.h>

enum {
  CMD_READY = 1,       // js -> watch: VER (data version)
  CMD_HELLO = 2,       // watch -> js: VER (asks the phone to answer READY)
  CMD_LINES_REQ = 3,   // watch -> js: REQ, ZOOM, AYAH, LINE, COUNT, WIDTH (line width in px)
  CMD_LINES = 4,       // js -> watch: REQ, ZOOM, DATA (strip records), FLAGS bit0 = last
  CMD_SETTINGS = 5,    // both ways: DATA (Settings bytes)
  CMD_TEXT_REQ = 6,    // watch -> js: REQ, AYAH, COUNT, FLAGS (TF_*), LINE (translation edition)
  CMD_TEXT = 7,        // js -> watch: REQ, AYAH (first), COUNT, FLAGS (TF_*_ERROR),
                       //   TEXT (Arabic) / TEXT2 (translation): one entry per ayah, separated by TEXT_SEP
};

#define TF_ARABIC 1
#define TF_TRANS 2
#define TF_ARABIC_DIGITS 4     // number ayah markers with Arabic-Indic digits
#define TF_ARABIC_ERROR 1
#define TF_TRANS_ERROR 2
#define TEXT_SEP '\x1e'

typedef void (*CommLinesHandler)(uint32_t req, int zoom, const uint8_t *data, int len, bool last);
typedef void (*CommTextHandler)(uint32_t req, uint16_t first, int count, const char *arabic, const char *trans, uint8_t errors);
typedef void (*CommVoidHandler)(void);

void comm_init(void);
bool comm_phone_connected(void);     // Bluetooth link to the phone app is up
bool comm_phone_ready(void);         // and our JS answered with the right data version
void comm_set_ready_handler(CommVoidHandler h);
void comm_set_lines_handler(CommLinesHandler h);
void comm_add_text_handler(CommTextHandler h);     // several views listen
void comm_set_settings_handler(CommVoidHandler h);

bool comm_request_lines(uint32_t req, int zoom, uint16_t ayah, uint8_t line, int count);
bool comm_request_text(uint32_t req, uint16_t first, int count, uint8_t flags);
void comm_maybe_hello(void);         // re-handshake (rate limited) while the phone is quiet
void comm_send_settings(void);
void comm_mark_unresponsive(void);   // phone stopped answering; wait for a new READY
