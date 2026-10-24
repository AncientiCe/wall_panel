// LVGL screens: the glance screen plus Wi-Fi setup. Call only from the
// task that runs lv_timer_handler().
#pragma once

void uiBegin();
void uiUpdate();
// Serial debug helpers: show page 0-3, or the idle (logo) screen.
void uiShowPage(int page);
void uiShowIdle();
