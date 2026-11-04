#include "power.h"
#include <Arduino.h>
#include "net.h"

#define PIN_BL 38
#define BL_CHANNEL 0

#define LEVEL_DAY 255

static int level = -1;

static void setLevel(int v)
{
  if (v == level)
    return;
  level = v;
  ledcWrite(BL_CHANNEL, v);
  Serial.printf("backlight %d\n", v);
}

void powerBegin()
{
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_BL, BL_CHANNEL);
  setLevel(LEVEL_DAY);
}

// The screen never goes fully dark, so a touch always goes straight through.
bool powerOnTouch(bool pressed)
{
  (void)pressed;
  return false;
}

void powerUpdate(bool night)
{
  static int prevNight = -1;
  if (prevNight != -1 && prevNight != (int)night)
    netSetNightBrightness(60); // sunset/sunrise puts it back to 60%
  prevNight = night;
  if (!night)
  {
    setLevel(LEVEL_DAY);
    return;
  }
  Settings s;
  netGetSettings(s);
  setLevel(max(1, s.nightBrightness * 255 / 100));
}
