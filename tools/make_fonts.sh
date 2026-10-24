#!/usr/bin/env bash
# Regenerates src/fonts/*.c with lv_font_conv (needs Node). Run from the
# project root after `pio run` has fetched LVGL into .pio/libdeps.
set -euo pipefail

FD=.pio/libdeps/panel4848/lvgl/scripts/built_in_font
CONV="npx -y lv_font_conv@1.5.2"
OUT=src/fonts

# Text: ASCII + Latin-1 + Latin Extended-A (German, Romanian, ...), Romanian
# comma-below letters, dashes, quotes, bullet, ellipsis, euro. LVGL's own
# symbols come from the built-in font of the same size via .fallback.
TEXT=0x20-0x7E,0xA0-0x17F,0x218-0x21B,0x2013-0x2014,0x2018-0x201E,0x2022,0x2026,0x20AC
for size in 14 16 18 20 24; do
  $CONV --font "$FD/Montserrat-Medium.ttf" -r "$TEXT" --size $size --bpp 4 --format lvgl --no-compress -o "$OUT/font_text_$size.c"
  sed -i "s|\.dsc = &font_dsc |.fallback = \&lv_font_montserrat_$size,\n    .dsc = \&font_dsc |" "$OUT/font_text_$size.c"
done

# Big digits: clock (with '-' for the --:-- placeholder) and temperatures.
$CONV --font "$FD/Montserrat-Medium.ttf" -r 0x20,0x2D,0x30-0x3A --size 120 --bpp 4 --format lvgl --no-compress -o "$OUT/font_clock_120.c"
$CONV --font "$FD/Montserrat-Medium.ttf" -r 0x20,0x2D,0x30-0x3A,0xB0 --size 72 --bpp 4 --format lvgl --no-compress -o "$OUT/font_num_72.c"

# Font Awesome 5 icons used by the UI (see the ICON_* defines in ui.cpp).
ICONS=0xf185,0xf186,0xf0c2,0xf6c4,0xf6c3,0xf73d,0xf740,0xf2dc,0xf0e7,0xf75f,0xf72e,0xf043,0xf2ed,0xf073,0xf0f3,0xf0e9,0xf743,0xf73c,0xf1eb,0xf013
for size in 96 48 28 20; do
  $CONV --font "$FD/FontAwesome5-Solid+Brands+Regular.woff" -r "$ICONS" --size $size --bpp 4 --format lvgl --no-compress -o "$OUT/font_icons_$size.c"
done
