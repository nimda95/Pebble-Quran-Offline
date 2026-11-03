#pragma once
#include <pebble.h>

void reader_open(void);                 // shows the reader at g_pos (pops windows above it)
void reader_resume(void);               // continue reading in the chosen view
void reader_jump_to(uint16_t ayah);     // moves the position (and opens the reader)
void reader_settings_changed(void);     // re-layout after zoom/theme/header changes
void reader_connection_changed(void);
bool reader_is_open(void);
uint16_t reader_current_ayah(void);
