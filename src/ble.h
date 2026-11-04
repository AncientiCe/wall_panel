// BLE sensor beacons (Holyiot): a short scan every 30 seconds, names saved in
// NVS, and a rolling in-RAM history. Everything is safe to call from the UI
// task; scanning runs on its own task.
#pragma once
#include <Arduino.h>

#define BLE_MAX_SENSORS 8
#define BLE_SCAN_PERIOD_MS 30000UL // how often we scan
#define BLE_SCAN_SECONDS 6         // how long each scan listens
#define BLE_PRESS_MS 60000UL       // the bulb stays lit this long after a press

// History: one sample every 2 minutes, kept for 24 hours.
#define BLE_HIST_STEP_S 120
#define BLE_HIST_LEN 720
#define BLE_HIST_EMPTY INT16_MIN

struct SensorInfo
{
  char mac[18];  // "AA:BB:CC:DD:EE:FF"
  char name[24]; // user name, empty = not named yet
  bool hasReading;
  float temp; // C
  float humidity; // %
  int rssi;
  uint32_t ageS;       // seconds since last heard, UINT32_MAX = never this boot
  bool pressedRecently; // button pressed within BLE_PRESS_MS
};

void bleBegin();
int bleGetSensors(SensorInfo *out, int max);
void bleSetName(const char *mac, const char *name);
// Copies up to `count` newest history points (oldest first) covering the last
// `count * BLE_HIST_STEP_S` seconds. temp and humidity are in tenths (0.1 C, 0.1 %);
// missing points are BLE_HIST_EMPTY. Returns the number of points written.
int bleGetHistory(const char *mac, int count, int16_t *temp, int16_t *hum);
// Scans once for `seconds` and prints every advertisement to Serial.
void bleDump(int seconds);
