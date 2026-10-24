// LVGL 8.3 config for the wall panel. Anything not set here uses
// LVGL's defaults from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 1
// Widgets live in PSRAM so internal RAM stays free for TLS (calendar fetch).
#define LV_MEM_CUSTOM_INCLUDE <esp32-hal-psram.h>
#define LV_MEM_CUSTOM_ALLOC ps_malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC ps_realloc

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_INDEV_DEF_READ_PERIOD 20

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
// Default font with accented letters (lists, keyboard, checkbox).
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(font_text_18)
#define LV_FONT_DEFAULT &font_text_18

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#endif // LV_CONF_H
