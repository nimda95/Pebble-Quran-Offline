#pragma once
// Translation view (served by the phone): ScrollLayer + TextLayers, one ayah
// at a time; Up/Down scroll and move to the previous/next ayah.
#include <pebble.h>

void textview_open(uint16_t ayah);
void textview_settings_changed(void);   // re-style / re-fetch after a settings change
