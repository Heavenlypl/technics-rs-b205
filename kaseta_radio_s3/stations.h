#pragma once
#include <stddef.h>
// Exact media/speak pairs from the user-provided HA Node-RED function.
struct StationPreset {const char *speak;const char *media;};
static constexpr StationPreset STATION_PRESETS[] = {
  {"Radio trzy pięć siedem","357"},
  {"Nowy świat","Radio Nowy Świat"},
  {"Global","Radio Paradise: Global Mix (flac)"},
  {"Ameryka","Radio Paradise: KFAT (flac)"},
  {"Lekki Rock","Radio Paradise MAIN Flac"},
  {"Luźna muza","Radio Paradise: Mellow Mix (flac)"},
  {"Ciężki Rock","Radio Paradise: Rock Mix (flac)"},
};
static constexpr size_t STATION_COUNT=sizeof(STATION_PRESETS)/sizeof(STATION_PRESETS[0]);
