#pragma once
// ESP32-S3-WROOM-1 N16R8, no expansion board. Numbers are GPIO, not header positions.
constexpr int TFT_SCLK=12, TFT_MOSI=11, TFT_MISO=13;
constexpr int TFT_CS=10, TFT_DC=9, TFT_RST=8, TFT_BL=7;
constexpr int DAC_BCK=4, DAC_LRCK=5, DAC_DIN=6;
constexpr int COUNTER_IN1=39, COUNTER_IN2=40, COUNTER_IN3=41, COUNTER_IN4=42;
// Physical keys: PREV14, NEXT15, Play16, Pause17, Stop18, Pilot21; INPUT_PULLUP.
// GPIO35..37 belong to octal PSRAM. GPIO19/20 remain free for native USB.
