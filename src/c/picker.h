#pragma once
// Ayah picker shown after choosing a surah: type the ayah on a touch numpad,
// or step it with Up/Down; Select opens the reader there.
#include <pebble.h>

void picker_push(int surah_idx, int initial_ayah);
