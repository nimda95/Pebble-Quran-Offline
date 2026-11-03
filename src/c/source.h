#pragma once
// Supplies rendered line strips for the reader. On every page change it checks
// whether the phone is connected and answering: if so the phone renders,
// otherwise the watch renders the lines itself. Lines around the reading
// position are prefetched so page turns come straight from the cache.
#include <pebble.h>

typedef struct { uint16_t g; uint8_t l; } LinePos;

bool line_next(int zoom, LinePos *p);       // false at the end of the Quran
bool line_prev(int zoom, LinePos *p);       // false at the beginning

typedef void (*SourceUpdateHandler)(void);

bool source_open(SourceUpdateHandler on_update);   // allocates the cache
void source_close(void);                           // frees cache + engine
void source_set_position(int zoom, LinePos top, int visible);
const uint8_t *source_get(LinePos p);
bool source_using_phone(void);
bool source_failed(void);                          // watch renderer could not start (memory)
const char *source_status(void);
void source_counts(uint32_t *phone_lines, uint32_t *watch_lines);
void source_connection_changed(void);
