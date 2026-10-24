#include "power.h"
#include <Arduino.h>

#define PIN_BL 38
#define BL_CHANNEL 0

#define LEVEL_DAY 255
#define LEVEL_NIGHT 40
#define NIGHT_AWAKE_MS 30000

static int level = -1;
static bool screenOff = false;
static bool swallowing = false;
static uint32_t lastTouch = 0;

static void setLevel(int v)
{
  if (v == level)
    return;
  level = v;
  ledcWrite(BL_CHANNEL, v);
}

void powerBegin()
{
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_BL, BL_CHANNEL);
  setLevel(LEVEL_DAY);
}

bool powerOnTouch(bool pressed)
{
  if (pressed)
    lastTouch = millis();
  if (screenOff && pressed)
  {
    screenOff = false;
    swallowing = true;
    setLevel(LEVEL_NIGHT);
  }
  if (swallowing)
  {
    if (!pressed)
      swallowing = false;
    return true;
  }
  return false;
}

void powerUpdate(bool night)
{
  if (!night)
  {
    screenOff = false;
    setLevel(LEVEL_DAY);
  }
  else if (lastTouch && millis() - lastTouch < NIGHT_AWAKE_MS)
  {
    screenOff = false;
    setLevel(LEVEL_NIGHT);
  }
  else
  {
    screenOff = true;
    setLevel(0);
  }
}
