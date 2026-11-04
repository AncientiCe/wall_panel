#include "ble.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

enum class Kind { Temp, Humidity, Button };

// The sensor's scan response carries service data 0x5242:
//   41 64 <MAC, 6 bytes> 06 06 <type> <hi> <lo>
// and rotates through types: 01 temperature, 03 humidity (value = hi.lo as
// decimal, e.g. 16 1D = 22.29), 04 button (non-zero = pressed), 06 unused.
struct Reading
{
  Kind kind;
  float value;
};

struct Sensor
{
  char mac[18];
  char name[24];
  bool hasReading, hasTemp, hasHum;
  float temp;
  float humidity;
  int rssi;
  uint32_t lastSeenMs; // 0 = not heard since boot
  uint32_t lastPressMs;
  int16_t histT[BLE_HIST_LEN];
  int16_t histH[BLE_HIST_LEN];
  int histHead; // index of the next slot to write
  uint32_t lastHistMs;
};

static Sensor *sensors; // BLE_MAX_SENSORS, in PSRAM
static int sensorCount = 0;
static SemaphoreHandle_t lock;
static Preferences prefs;
static volatile bool dumping = false;
static volatile int dumpSeconds = 0; // set by bleDump, served by the scan task

// ---- decoding ----

// Turns one advertisement into a reading. Returns false for anything that is
// not one of our sensors' temperature, humidity or button frames.
static bool decode(NimBLEAdvertisedDevice *dev, Reading &out)
{
  NimBLEUUID want((uint16_t)0x5242);
  for (int i = 0; i < dev->getServiceDataCount(); i++)
  {
    if (!(dev->getServiceDataUUID(i) == want))
      continue;
    std::string d = dev->getServiceData(i);
    if (d.size() != 13 || (uint8_t)d[0] != 0x41)
      return false;
    uint8_t type = d[10];
    uint8_t hi = d[11], lo = d[12];
    switch (type)
    {
    case 0x01:
      out = {Kind::Temp, hi + lo / 100.0f};
      return true;
    case 0x03:
      out = {Kind::Humidity, hi + lo / 100.0f};
      return true;
    case 0x04:
      out = {Kind::Button, (hi || lo) ? 1.0f : 0.0f};
      return true;
    }
    return false;
  }
  return false;
}

// ---- storage ----

static void macKey(const char *mac, char *key)
{
  int j = 0;
  for (int i = 0; mac[i] && j < 12; i++)
    if (mac[i] != ':')
      key[j++] = mac[i];
  key[j] = 0;
}

static void saveList()
{
  String list;
  for (int i = 0; i < sensorCount; i++)
  {
    if (i)
      list += ',';
    list += sensors[i].mac;
  }
  prefs.putString("macs", list);
}

static void loadNameFor(Sensor &s)
{
  char key[13];
  macKey(s.mac, key);
  String n = prefs.getString(key, "");
  strlcpy(s.name, n.c_str(), sizeof(s.name));
}

static Sensor *findLocked(const char *mac)
{
  for (int i = 0; i < sensorCount; i++)
    if (strcasecmp(sensors[i].mac, mac) == 0)
      return &sensors[i];
  return nullptr;
}

static Sensor *addLocked(const char *mac)
{
  if (sensorCount >= BLE_MAX_SENSORS)
    return nullptr;
  Sensor &s = sensors[sensorCount++];
  memset(&s, 0, sizeof(s));
  strlcpy(s.mac, mac, sizeof(s.mac));
  for (int i = 0; s.mac[i]; i++)
    s.mac[i] = toupper(s.mac[i]);
  for (int i = 0; i < BLE_HIST_LEN; i++)
  {
    s.histT[i] = BLE_HIST_EMPTY;
    s.histH[i] = BLE_HIST_EMPTY;
  }
  loadNameFor(s);
  return &s;
}

// ---- scanning ----

static void dumpDevice(NimBLEAdvertisedDevice *dev)
{
  Serial.printf("ADV %s rssi %d name '%s'\n", dev->getAddress().toString().c_str(), dev->getRSSI(),
                dev->haveName() ? dev->getName().c_str() : "");
  const uint8_t *p = dev->getPayload();
  size_t n = dev->getPayloadLength();
  Serial.print("  raw ");
  for (size_t i = 0; i < n; i++)
    Serial.printf("%02X", p[i]);
  Serial.println();
}

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks
{
  void onResult(NimBLEAdvertisedDevice *dev) override
  {
    if (dumping)
    {
      dumpDevice(dev);
      return;
    }
    Reading r;
    if (!decode(dev, r))
      return;
    std::string mac = dev->getAddress().toString();
    xSemaphoreTake(lock, portMAX_DELAY);
    Sensor *s = findLocked(mac.c_str());
    if (!s)
    {
      s = addLocked(mac.c_str());
      if (s)
        saveList();
    }
    if (s)
    {
      uint32_t now = millis() ? millis() : 1;
      s->lastSeenMs = now;
      s->rssi = dev->getRSSI();
      if (r.kind == Kind::Temp)
      {
        s->temp = r.value;
        s->hasTemp = true;
      }
      else if (r.kind == Kind::Humidity)
      {
        s->humidity = r.value;
        s->hasHum = true;
      }
      else if (r.value > 0)
        s->lastPressMs = now;
      s->hasReading = s->hasTemp && s->hasHum;
    }
    xSemaphoreGive(lock);
  }
};

// One history point per BLE_HIST_STEP_S, written for every known sensor so
// gaps show up as gaps.
static void recordHistory()
{
  uint32_t now = millis();
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int i = 0; i < sensorCount; i++)
  {
    Sensor &s = sensors[i];
    if (s.lastHistMs && now - s.lastHistMs < BLE_HIST_STEP_S * 1000UL)
      continue;
    if (!s.hasReading && !s.lastHistMs)
      continue; // wait for the first full reading before starting the history
    s.lastHistMs = now;
    bool fresh = s.hasReading && s.lastSeenMs && now - s.lastSeenMs < BLE_HIST_STEP_S * 1000UL + BLE_SCAN_PERIOD_MS;
    s.histT[s.histHead] = fresh ? (int16_t)lroundf(s.temp * 10) : BLE_HIST_EMPTY;
    s.histH[s.histHead] = fresh ? (int16_t)lroundf(s.humidity * 10) : BLE_HIST_EMPTY;
    s.histHead = (s.histHead + 1) % BLE_HIST_LEN;
  }
  xSemaphoreGive(lock);
}

static void scanTask(void *)
{
  NimBLEScan *scan = NimBLEDevice::getScan();
  static ScanCallbacks cb;
  scan->setAdvertisedDeviceCallbacks(&cb, true); // duplicates: every advert refreshes the reading
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(99);
  vTaskDelay(pdMS_TO_TICKS(3000)); // let the BLE host finish syncing
  uint32_t nextScan = 0;
  for (;;)
  {
    // The NimBLE scan object is not thread-safe, so every scan runs here.
    if (dumpSeconds > 0)
    {
      int secs = dumpSeconds;
      dumping = true;
      scan->setActiveScan(true); // also ask for scan responses
      Serial.printf("BLE dump for %d s\n", secs);
      scan->start(secs, false);
      scan->clearResults();
      scan->setActiveScan(false);
      Serial.println("BLE dump done");
      dumping = false;
      dumpSeconds = 0;
    }
    else if ((int32_t)(millis() - nextScan) >= 0)
    {
      scan->setActiveScan(true); // readings are in the scan response
      scan->start(BLE_SCAN_SECONDS, false);
      scan->clearResults();
      recordHistory();
      nextScan = millis() + BLE_SCAN_PERIOD_MS - BLE_SCAN_SECONDS * 1000UL;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void bleBegin()
{
  lock = xSemaphoreCreateMutex();
  sensors = (Sensor *)heap_caps_calloc(BLE_MAX_SENSORS, sizeof(Sensor), MALLOC_CAP_SPIRAM);
  prefs.begin("sensors", false);

  String list = prefs.getString("macs", "");
  int start = 0;
  while (start < (int)list.length())
  {
    int comma = list.indexOf(',', start);
    if (comma < 0)
      comma = list.length();
    String mac = list.substring(start, comma);
    if (mac.length() == 17)
      addLocked(mac.c_str());
    start = comma + 1;
  }

  NimBLEDevice::init("");
  xTaskCreatePinnedToCore(scanTask, "ble", 6144, nullptr, 1, nullptr, 0);
}

// ---- UI-facing ----

int bleGetSensors(SensorInfo *out, int max)
{
  uint32_t now = millis();
  xSemaphoreTake(lock, portMAX_DELAY);
  int n = sensorCount < max ? sensorCount : max;
  for (int i = 0; i < n; i++)
  {
    const Sensor &s = sensors[i];
    SensorInfo &o = out[i];
    strlcpy(o.mac, s.mac, sizeof(o.mac));
    strlcpy(o.name, s.name, sizeof(o.name));
    o.hasReading = s.hasReading;
    o.temp = s.temp;
    o.humidity = s.humidity;
    o.rssi = s.rssi;
    o.ageS = s.lastSeenMs ? (now - s.lastSeenMs) / 1000 : UINT32_MAX;
    o.pressedRecently = s.lastPressMs && now - s.lastPressMs < BLE_PRESS_MS;
  }
  xSemaphoreGive(lock);
  return n;
}

void bleSetName(const char *mac, const char *name)
{
  xSemaphoreTake(lock, portMAX_DELAY);
  Sensor *s = findLocked(mac);
  if (s)
  {
    strlcpy(s->name, name, sizeof(s->name));
    char key[13];
    macKey(s->mac, key);
    if (s->name[0])
      prefs.putString(key, s->name);
    else
      prefs.remove(key);
  }
  xSemaphoreGive(lock);
}

int bleGetHistory(const char *mac, int count, int16_t *temp, int16_t *hum)
{
  if (count > BLE_HIST_LEN)
    count = BLE_HIST_LEN;
  xSemaphoreTake(lock, portMAX_DELAY);
  Sensor *s = findLocked(mac);
  int n = 0;
  if (s)
  {
    for (int i = 0; i < count; i++)
    {
      int idx = (s->histHead - count + i + BLE_HIST_LEN * 2) % BLE_HIST_LEN;
      temp[i] = s->histT[idx];
      hum[i] = s->histH[idx];
    }
    n = count;
  }
  xSemaphoreGive(lock);
  return n;
}

void bleDump(int seconds)
{
  dumpSeconds = seconds;
}
