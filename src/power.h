// Backlight and night mode. At night the screen dims after a touch and
// switches off when idle; the touch that wakes it is swallowed so it
// doesn't also press a button.
#pragma once
#include <stdint.h>

void powerBegin();
// Called by the touch driver with the raw pressed state. Returns true when
// the touch must not reach LVGL (it woke the screen).
bool powerOnTouch(bool pressed);
void powerUpdate(bool night);
