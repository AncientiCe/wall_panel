// Background network task: Wi-Fi (credentials saved in NVS), IP-based
// location and time zone, weather, and scene POSTs. All calls are safe from
// the UI task; the network work itself runs on core 0.
#pragma once
#include <Arduino.h>

#define WX_HOURS 12
#define WX_DAYS 5

struct Weather
{
  bool valid;
  float temp;
  float feels;
  float wind;
  int humidity;
  int code;
  float tmax;
  float tmin;
  int updatedMin; // local time of the data, minutes after midnight
  int rainInMin; // minutes until rain starts in the next 2h, -1 if none
  float rainNext2h; // mm over the next 2h
  int sunriseMin; // today's local sunrise, minutes after midnight, -1 unknown
  int sunsetMin;

  int hourCount;
  int8_t hour[WX_HOURS]; // local hour of day, 0-23
  float hourTemp[WX_HOURS];
  int8_t hourRain[WX_HOURS]; // precipitation probability %
  int hourCode[WX_HOURS];

  int dayCount;
  int8_t dayWeekday[WX_DAYS]; // 0 = Sunday
  float dayMax[WX_DAYS];
  float dayMin[WX_DAYS];
  int8_t dayRain[WX_DAYS];
  int dayCode[WX_DAYS];
};

enum class WifiState
{
  NoCreds,
  Connecting,
  Connected,
  Failed,
};

#define MAX_EVENTS 200

#define ZODIAC_COUNT 12
extern const char *ZODIAC_SIGNS[ZODIAC_COUNT]; // "Aries" ... "Pisces"

struct Horoscope
{
  int sign; // index into ZODIAC_SIGNS
  char text[720];
};

struct CalEvent
{
  time_t start;
  time_t end;
  bool allDay;
  uint32_t colour;
  char title[61];
};

enum class CalState
{
  NotSet, // no calendar URL saved
  Loading,
  Ok,
  Error,
};

struct NetStatus
{
  WifiState wifi;
  char ssid[33];
  char ip[16];
  CalState calendar;
  uint32_t calendarVersion;
  uint32_t horoscopeVersion;
  bool scanning;
  uint32_t scanVersion;
  uint32_t weatherVersion;
  Weather weather;
};

struct ScanEntry
{
  char ssid[33];
  int rssi;
  bool open;
};

// Saved on the device, edited from the settings web page.
struct Settings
{
  char calendarUrl[320];
  char sceneUrl[160];
  uint16_t zodiacMask; // bit i = ZODIAC_SIGNS[i] is shown
  bool nightAtSunset;  // start night mode at sunset instead of nightStart
  int nightStart;     // minutes after midnight
  int nightEnd;
};

void netBegin();
void netGetSettings(Settings &out);
void netGetStatus(NetStatus &out);
int netGetScan(ScanEntry *out, int max);
void netRequestScan();
void netConnect(const char *ssid, const char *pass);
void netForget();
void netSendScene(const char *scene);
void netRefreshWeather();
int netGetEvents(CalEvent *out, int max);
int netGetHoroscopes(Horoscope *out, int max);
