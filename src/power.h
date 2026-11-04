// Backlight and night mode. At night the backlight sits at the night
// brightness setting (80-100%) whether or not the screen is touched.
#pragma once
#include <stdint.h>

void powerBegin();
// Called by the touch driver with the raw pressed state. Returns true when
// the touch must not reach LVGL (it woke the screen).
bool powerOnTouch(bool pressed);
void powerUpdate(bool night);
