#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Open-Meteo's current conditions refresh every 15 minutes; polling every 5
// catches each update promptly and stays far below the free-tier limit.
#define WEATHER_PERIOD_MS (5UL * 60 * 1000)
#define CALENDAR_PERIOD_MS (10UL * 60 * 1000)
#define CONNECT_TIMEOUT_MS 20000
#define MAX_SCAN 20

enum class CmdType
{
  Scan,
  Connect,
  Forget,
  Scene,
  RefreshWeather,
  RefreshCalendar,
  RefreshHoroscopes,
};

const char *ZODIAC_SIGNS[ZODIAC_COUNT] = {"Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
                                          "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"};

struct Cmd
{
  CmdType type;
  char a[65];
  char b[65];
};

static QueueHandle_t cmdQueue;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static NetStatus status = {};
static ScanEntry scan[MAX_SCAN];
static int scanCount = 0;

// Events are too big to copy under the spinlock, so they get a mutex.
static SemaphoreHandle_t eventsLock;
static CalEvent *events;      // MAX_EVENTS, in PSRAM
static CalEvent *freshEvents; // parse buffer, in PSRAM
static int eventCount = 0;

// Today's horoscopes for the chosen signs; guarded by eventsLock too.
static Horoscope *horoscopes; // ZODIAC_COUNT, in PSRAM
static int horoscopeCount = 0;

static WebServer server(80);

static Preferences prefs;
static float lat = NAN, lon = NAN;
static Settings settings = {};

static void loadSettings()
{
  Settings s = {};
  strlcpy(s.calendarUrl, prefs.getString("calUrl", "").c_str(), sizeof(s.calendarUrl));
#ifdef SCENE_URL
  strlcpy(s.sceneUrl, prefs.getString("sceneUrl", SCENE_URL).c_str(), sizeof(s.sceneUrl));
#else
  strlcpy(s.sceneUrl, prefs.getString("sceneUrl", "").c_str(), sizeof(s.sceneUrl));
#endif
  s.zodiacMask = prefs.getUShort("zodiac", 0);
  s.nightAtSunset = prefs.getBool("nightSunset", true);
  s.nightStart = prefs.getInt("nightStart", 20 * 60 + 30);
  s.nightEnd = prefs.getInt("nightEnd", 6 * 60 + 30);
  portENTER_CRITICAL(&mux);
  settings = s;
  portEXIT_CRITICAL(&mux);
}

// ---- time zone ----

// IANA name (from IP geolocation) to POSIX TZ for the ESP32 libc.
struct TzMap
{
  const char *iana;
  const char *posix;
};

static const char *TZ_CET = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char *TZ_EET = "EET-2EEST,M3.5.0/3,M10.5.0/4";
static const TzMap TZ_TABLE[] = {
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris", TZ_CET}, {"Europe/Berlin", TZ_CET}, {"Europe/Madrid", TZ_CET},
    {"Europe/Rome", TZ_CET}, {"Europe/Amsterdam", TZ_CET}, {"Europe/Brussels", TZ_CET},
    {"Europe/Vienna", TZ_CET}, {"Europe/Zurich", TZ_CET}, {"Europe/Stockholm", TZ_CET},
    {"Europe/Oslo", TZ_CET}, {"Europe/Copenhagen", TZ_CET}, {"Europe/Warsaw", TZ_CET},
    {"Europe/Prague", TZ_CET}, {"Europe/Budapest", TZ_CET}, {"Europe/Belgrade", TZ_CET},
    {"Europe/Zagreb", TZ_CET}, {"Europe/Ljubljana", TZ_CET}, {"Europe/Bratislava", TZ_CET},
    {"Europe/Luxembourg", TZ_CET}, {"Europe/Malta", TZ_CET},
    {"Europe/Bucharest", TZ_EET}, {"Europe/Athens", TZ_EET}, {"Europe/Helsinki", TZ_EET},
    {"Europe/Kyiv", TZ_EET}, {"Europe/Kiev", TZ_EET}, {"Europe/Sofia", TZ_EET},
    {"Europe/Riga", TZ_EET}, {"Europe/Tallinn", TZ_EET}, {"Europe/Vilnius", TZ_EET},
    {"Europe/Chisinau", "EET-2EEST,M3.5.0,M10.5.0/3"},
    {"Europe/Istanbul", "<+03>-3"},
    {"Europe/Moscow", "MSK-3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "MST7"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"}, {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Asia/Tokyo", "JST-9"}, {"Asia/Kolkata", "IST-5:30"}, {"Asia/Shanghai", "CST-8"},
    {"Asia/Singapore", "<+08>-8"}, {"Asia/Dubai", "<+04>-4"},
};

// Unknown zones fall back to a fixed offset (no DST rules).
static void tzFor(const char *iana, long offsetSec, char *out, size_t outLen)
{
  for (const TzMap &m : TZ_TABLE)
  {
    if (strcmp(m.iana, iana) == 0)
    {
      strlcpy(out, m.posix, outLen);
      return;
    }
  }
  long a = labs(offsetSec);
  snprintf(out, outLen, "LOC%c%ld:%02ld", offsetSec >= 0 ? '-' : '+', a / 3600, (a % 3600) / 60);
}

static void applyTz(const char *tz)
{
  configTzTime(tz, "pool.ntp.org", "time.google.com");
}

// ---- HTTP helpers ----

static bool getJson(const char *url, JsonDocument &doc)
{
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url))
    return false;
  int code = http.GET();
  if (code != 200)
  {
    Serial.printf("GET failed: http %d\n", code);
    http.end();
    return false;
  }
  // getString() decodes chunked replies; the raw stream would not.
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err)
  {
    Serial.printf("GET failed: json %s\n", err.c_str());
    return false;
  }
  return true;
}

// Location and time zone from the public IP. Location is not printed on
// serial on purpose.
static bool geolocate()
{
  JsonDocument doc;
  if (!getJson("http://ip-api.com/json/?fields=status,lat,lon,timezone,offset", doc))
    return false;
  if (strcmp(doc["status"] | "", "success") != 0)
    return false;
  lat = doc["lat"] | NAN;
  lon = doc["lon"] | NAN;
  char tz[64];
  tzFor(doc["timezone"] | "", doc["offset"] | 0L, tz, sizeof(tz));
  prefs.putFloat("lat", lat);
  prefs.putFloat("lon", lon);
  prefs.putString("tz", tz);
  applyTz(tz);
  Serial.printf("geolocated, tz %s\n", tz);
  return true;
}

static bool fetchWeather()
{
  char url[640];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,weather_code"
           "&minutely_15=precipitation&forecast_minutely_15=8"
           "&hourly=temperature_2m,precipitation_probability,weather_code&forecast_hours=%d"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code,sunrise,sunset"
           "&timezone=auto&forecast_days=%d",
           (double)lat, (double)lon, WX_HOURS, WX_DAYS);
  JsonDocument doc;
  if (!getJson(url, doc))
    return false;
  Weather w = {};
  w.valid = true;
  JsonObject cur = doc["current"];
  w.temp = cur["temperature_2m"] | NAN;
  w.feels = cur["apparent_temperature"] | NAN;
  w.humidity = cur["relative_humidity_2m"] | -1;
  w.wind = cur["wind_speed_10m"] | NAN;
  w.code = cur["weather_code"] | -1;
  // Use the API's own local timestamp; the panel clock may not be synced yet.
  const char *ct = cur["time"] | "";
  int uh, um;
  w.updatedMin = (strlen(ct) >= 16 && sscanf(ct + 11, "%d:%d", &uh, &um) == 2) ? uh * 60 + um : -1;

  // 15-minute precipitation for the next 2 hours.
  w.rainInMin = -1;
  w.rainNext2h = 0;
  JsonArray rain = doc["minutely_15"]["precipitation"];
  for (size_t i = 0; i < rain.size(); i++)
  {
    float mm = rain[i] | 0.0f;
    w.rainNext2h += mm;
    if (w.rainInMin < 0 && mm >= 0.1f)
      w.rainInMin = i * 15;
  }

  // Times are local ("2026-10-24T15:00") because of timezone=auto.
  JsonObject hourly = doc["hourly"];
  JsonArray htime = hourly["time"];
  w.hourCount = min((int)htime.size(), WX_HOURS);
  for (int i = 0; i < w.hourCount; i++)
  {
    const char *t = htime[i] | "";
    w.hour[i] = strlen(t) >= 13 ? atoi(t + 11) : 0;
    w.hourTemp[i] = hourly["temperature_2m"][i] | NAN;
    w.hourRain[i] = hourly["precipitation_probability"][i] | 0;
    w.hourCode[i] = hourly["weather_code"][i] | -1;
  }

  JsonObject daily = doc["daily"];
  JsonArray dtime = daily["time"];
  w.dayCount = min((int)dtime.size(), WX_DAYS);
  for (int i = 0; i < w.dayCount; i++)
  {
    struct tm d = {};
    sscanf(dtime[i] | "2000-01-01", "%d-%d-%d", &d.tm_year, &d.tm_mon, &d.tm_mday);
    d.tm_year -= 1900;
    d.tm_mon -= 1;
    d.tm_hour = 12;
    mktime(&d);
    w.dayWeekday[i] = d.tm_wday;
    w.dayMax[i] = daily["temperature_2m_max"][i] | NAN;
    w.dayMin[i] = daily["temperature_2m_min"][i] | NAN;
    w.dayRain[i] = daily["precipitation_probability_max"][i] | 0;
    w.dayCode[i] = daily["weather_code"][i] | -1;
  }
  // "2026-10-24T18:22" in local time.
  auto minuteOfDay = [](const char *t) {
    int h, m;
    return (t && strlen(t) >= 16 && sscanf(t + 11, "%d:%d", &h, &m) == 2) ? h * 60 + m : -1;
  };
  w.sunriseMin = minuteOfDay(daily["sunrise"][0]);
  w.sunsetMin = minuteOfDay(daily["sunset"][0]);
  w.tmax = w.dayCount ? w.dayMax[0] : NAN;
  w.tmin = w.dayCount ? w.dayMin[0] : NAN;
  portENTER_CRITICAL(&mux);
  status.weather = w;
  status.weatherVersion++;
  portEXIT_CRITICAL(&mux);
  Serial.printf("weather: %.1fC code %d\n", w.temp, w.code);
  return true;
}

static void postScene(const char *scene)
{
  Settings s;
  netGetSettings(s);
  if (!s.sceneUrl[0])
  {
    Serial.printf("scene %s: no scene URL configured\n", scene);
    return;
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.printf("scene %s: wifi down\n", scene);
    return;
  }
  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(s.sceneUrl))
    return;
  http.addHeader("Content-Type", "application/json");
  char body[96];
  snprintf(body, sizeof(body), "{\"scene\":\"%s\"}", scene);
  int code = http.POST(body);
  http.end();
  Serial.printf("scene %s: http %d\n", scene, code);
}

// ---- calendar ----

// Last calendar problem in plain words, shown on the settings page.
static char calError[120] = "";

static void setCalError(const char *msg)
{
  strlcpy(calError, msg, sizeof(calError));
}

static void setCalState(CalState s)
{
  portENTER_CRITICAL(&mux);
  status.calendar = s;
  portEXIT_CRITICAL(&mux);
}

// Titles match when their letters and digits match, ignoring case, spaces
// (including non-breaking ones) and punctuation.
static bool sameTitle(const char *a, const char *b)
{
  for (;;)
  {
    while (*a && !isalnum((unsigned char)*a) && !((unsigned char)*a & 0x80))
      a++;
    while (*b && !isalnum((unsigned char)*b) && !((unsigned char)*b & 0x80))
      b++;
    if ((unsigned char)*a == 0xC2 && (unsigned char)a[1] == 0xA0)
    {
      a += 2;
      continue;
    }
    if ((unsigned char)*b == 0xC2 && (unsigned char)b[1] == 0xA0)
    {
      b += 2;
      continue;
    }
    if (!*a || !*b)
      return !*a && !*b;
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return false;
    a++;
    b++;
  }
}

// Fetches the Apps Script feed (see tools/calendar-feed.gs). Google answers
// with a redirect to script.googleusercontent.com, so redirects are followed.
static void fetchCalendar()
{
  Settings s;
  netGetSettings(s);
  if (!s.calendarUrl[0])
  {
    setCalState(CalState::NotSet);
    return;
  }
  if (status.calendar != CalState::Ok)
    setCalState(CalState::Loading);

  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(tls, s.calendarUrl))
  {
    setCalState(CalState::Error);
    return;
  }
  int code = http.GET();
  if (code != 200)
  {
    if (code == 401 || code == 403)
      setCalError("Google refused access. In the deployment, set Who has access to Anyone.");
    else
      snprintf(calError, sizeof(calError), "The feed answered HTTP %d. Check the URL.", code);
    Serial.printf("calendar: http %d\n", code);
    http.end();
    setCalState(CalState::Error);
    return;
  }
  String body = http.getString();
  http.end();
  // A web page instead of JSON means a sign-in or error page: the /dev URL,
  // or access not set to Anyone. The body is not logged (it may be private).
  if (body.startsWith("<"))
  {
    setCalError("Got a web page instead of events. Use the Web app URL ending in /exec, with access set to Anyone.");
    Serial.printf("calendar: html page, %u bytes\n", body.length());
    setCalState(CalState::Error);
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err || doc["error"].is<const char *>())
  {
    if (!err && strcmp(doc["error"], "bad token") == 0)
      setCalError("Wrong token. The ?token= in the URL must match TOKEN in the script.");
    else
      snprintf(calError, sizeof(calError), "Unreadable feed (%s).", err ? err.c_str() : (const char *)doc["error"]);
    Serial.printf("calendar: %s\n", calError);
    setCalState(CalState::Error);
    return;
  }
  calError[0] = 0;

  CalEvent *fresh = freshEvents;
  int n = 0;
  for (JsonObject ev : doc["events"].as<JsonArray>())
  {
    if (n == MAX_EVENTS)
      break;
    CalEvent &e = fresh[n];
    e.start = ev["s"] | 0L;
    e.end = ev["e"] | 0L;
    e.allDay = (ev["a"] | 0) != 0;
    e.colour = strtoul(ev["c"] | "7a8bff", nullptr, 16);
    const char *title = ev["t"] | "";
    while (*title == ' ')
      title++;
    strlcpy(e.title, title, sizeof(e.title));
    for (int k = strlen(e.title) - 1; k >= 0 && e.title[k] == ' '; k--)
      e.title[k] = 0;
    // The same event in two calendars shows up twice; keep one.
    bool dup = false;
    for (int j = 0; j < n && !dup; j++)
      dup = labs((long)(fresh[j].start - e.start)) < 60 && sameTitle(fresh[j].title, e.title);
    if (!dup)
      n++;
  }
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  memcpy(events, fresh, sizeof(CalEvent) * n);
  eventCount = n;
  xSemaphoreGive(eventsLock);

  portENTER_CRITICAL(&mux);
  status.calendar = CalState::Ok;
  status.calendarVersion++;
  portEXIT_CRITICAL(&mux);
  Serial.printf("calendar: %d events\n", n);
}

// ---- horoscopes ----

// freehoroscopeapi.com only has today's reading (its TOMORROW option
// returns the same text), so only today is fetched.
static void fetchHoroscopes()
{
  Settings s;
  netGetSettings(s);
  static Horoscope fresh[ZODIAC_COUNT];
  int n = 0;
  for (int i = 0; i < ZODIAC_COUNT; i++)
  {
    if (!(s.zodiacMask & (1 << i)))
      continue;
    char url[128];
    snprintf(url, sizeof(url), "https://freehoroscopeapi.com/api/v1/get-horoscope/daily?sign=%s&day=TODAY", ZODIAC_SIGNS[i]);
    WiFiClientSecure tls;
    tls.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(tls, url))
      continue;
    int code = http.GET();
    if (code != 200)
    {
      Serial.printf("horoscope %s: http %d\n", ZODIAC_SIGNS[i], code);
      http.end();
      continue;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err)
      continue;
    fresh[n].sign = i;
    strlcpy(fresh[n].text, doc["data"]["horoscope"] | "", sizeof(fresh[n].text));
    n++;
  }
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  memcpy(horoscopes, fresh, sizeof(Horoscope) * n);
  horoscopeCount = n;
  xSemaphoreGive(eventsLock);
  portENTER_CRITICAL(&mux);
  status.horoscopeVersion++;
  portEXIT_CRITICAL(&mux);
  Serial.printf("horoscopes: %d\n", n);
}

// ---- settings web page ----

static String htmlEscape(const String &in)
{
  String out;
  for (char c : in)
  {
    switch (c)
    {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    default: out += c;
    }
  }
  return out;
}

static String hhmm(int minutes)
{
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return buf;
}

static int parseHhmm(const String &v, int fallback)
{
  int h, m;
  if (sscanf(v.c_str(), "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60)
    return h * 60 + m;
  return fallback;
}

static String zodiacBoxes(uint16_t mask)
{
  String out = "<div style='display:grid;grid-template-columns:repeat(3,1fr)'>";
  for (int i = 0; i < ZODIAC_COUNT; i++)
    out += String("<label style='font-weight:normal;margin-top:6px'><input type=checkbox name=z value=") + i +
           ((mask & (1 << i)) ? " checked" : "") + "> " + ZODIAC_SIGNS[i] + "</label>";
  return out + "</div>";
}

static void handleRoot()
{
  Settings s;
  netGetSettings(s);
  const char *calState[] = {"not set", "loading", "working", "error"};
  String calInfo = s.calendarUrl[0] ? String("saved, ") + calState[(int)status.calendar] : "not set";
  if (status.calendar == CalState::Error && calError[0])
    calInfo += String(": <b>") + htmlEscape(calError) + "</b>";

  // The calendar URL contains a token, so the page never shows it back.
  String page = String(
      "<!doctype html><html><head><meta name=viewport content='width=device-width'>"
      "<title>Wall panel</title><style>body{font-family:sans-serif;max-width:560px;margin:24px auto;padding:0 16px}"
      "label{display:block;margin-top:16px;font-weight:600}input[type=text],input[type=url]{width:100%;padding:8px}"
      "small{color:#666}button{margin-top:20px;padding:10px 20px}</style></head><body>"
      "<h1>Wall panel</h1><form method=post action=/save>"
      "<label>Google Calendar feed URL</label>"
      "<input type=url name=cal placeholder='leave blank to keep the current one'>"
      "<small>Calendar: ") + calInfo + ". Paste the Apps Script web app URL including ?token=...</small>"
      "<label><input type=checkbox name=calClear> Remove calendar URL</label>"
      "<label>Horoscope signs</label>" + zodiacBoxes(s.zodiacMask) +
      "<label>Scene URL (aice)</label>"
      "<input type=url name=scene value=\"" + htmlEscape(s.sceneUrl) + "\">"
      "<small>Scene buttons POST {\"scene\":\"Away\"} here. Blank disables.</small>"
      "<label>Night mode starts</label>"
      "<label style='font-weight:normal'><input type=radio name=nightMode value=sunset" +
      (s.nightAtSunset ? " checked" : "") + "> At sunset or the time below, whichever is earlier</label>"
      "<label style='font-weight:normal'><input type=radio name=nightMode value=fixed" +
      (s.nightAtSunset ? "" : " checked") + "> Always at the time below</label>"
      "<input type=text name=nightStart size=5 value=" + hhmm(s.nightStart) + ">"
      "<label>Night mode ends</label>"
      "<input type=text name=nightEnd size=5 value=" + hhmm(s.nightEnd) + ">"
      "<small>HH:MM. A fixed start equal to the end turns night mode off.</small>"
      "<br><button>Save</button></form></body></html>";
  server.send(200, "text/html", page);
}

static void handleSave()
{
  Settings s;
  netGetSettings(s);
  String cal = server.arg("cal");
  cal.trim();
  if (server.hasArg("calClear"))
    prefs.putString("calUrl", "");
  else if (!cal.isEmpty())
    prefs.putString("calUrl", cal);
  String scene = server.arg("scene");
  scene.trim();
  prefs.putString("sceneUrl", scene);
  uint16_t zodiac = 0;
  for (int i = 0; i < server.args(); i++)
    if (server.argName(i) == "z")
      zodiac |= 1 << server.arg(i).toInt();
  bool zodiacChanged = zodiac != s.zodiacMask;
  prefs.putUShort("zodiac", zodiac);
  prefs.putBool("nightSunset", server.arg("nightMode") != "fixed");
  prefs.putInt("nightStart", parseHhmm(server.arg("nightStart"), s.nightStart));
  prefs.putInt("nightEnd", parseHhmm(server.arg("nightEnd"), s.nightEnd));
  loadSettings();

  Cmd cmd = {CmdType::RefreshCalendar};
  xQueueSend(cmdQueue, &cmd, 0);
  if (zodiacChanged)
  {
    Cmd hcmd = {CmdType::RefreshHoroscopes};
    xQueueSend(cmdQueue, &hcmd, 0);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// ---- Wi-Fi ----

static void setWifi(WifiState s, const char *ssid)
{
  portENTER_CRITICAL(&mux);
  status.wifi = s;
  if (ssid)
    strlcpy(status.ssid, ssid, sizeof(status.ssid));
  portEXIT_CRITICAL(&mux);
}

static void doScan()
{
  portENTER_CRITICAL(&mux);
  status.scanning = true;
  portEXIT_CRITICAL(&mux);

  // Weak signals sometimes give an empty scan, so retry with a longer dwell
  // per channel. Hidden networks are included (they have empty SSIDs and are
  // skipped below, but asking for them makes the scan active on every channel).
  int n = 0;
  for (int attempt = 0; attempt < 3 && n <= 0; attempt++)
  {
    if (attempt)
      delay(500);
    n = WiFi.scanNetworks(false, true, false, 500);
  }
  ScanEntry found[MAX_SCAN];
  int count = 0;
  for (int i = 0; i < n; i++)
  {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty())
      continue;
    int rssi = WiFi.RSSI(i);
    int existing = -1;
    for (int j = 0; j < count; j++)
      if (ssid == found[j].ssid)
        existing = j;
    if (existing >= 0)
    {
      found[existing].rssi = max(found[existing].rssi, rssi);
      continue;
    }
    if (count == MAX_SCAN)
      continue;
    strlcpy(found[count].ssid, ssid.c_str(), sizeof(found[count].ssid));
    found[count].rssi = rssi;
    found[count].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    count++;
  }
  WiFi.scanDelete();
  std::sort(found, found + count, [](const ScanEntry &x, const ScanEntry &y) { return x.rssi > y.rssi; });

  portENTER_CRITICAL(&mux);
  memcpy(scan, found, sizeof(ScanEntry) * count);
  scanCount = count;
  status.scanning = false;
  status.scanVersion++;
  portEXIT_CRITICAL(&mux);
  Serial.printf("scan: %d networks\n", count);
}

static void netTask(void *)
{
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
#ifdef WIFI_SSID
  if (ssid.isEmpty())
  {
    ssid = WIFI_SSID;
    pass = WIFI_PASS;
  }
#endif
  lat = prefs.getFloat("lat", NAN);
  lon = prefs.getFloat("lon", NAN);
  String tz = prefs.getString("tz", "UTC0");
  setenv("TZ", tz.c_str(), 1);
  tzset();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("wall-panel");
  WiFi.setAutoReconnect(true);

  // A connect in progress: the new credentials are saved only once it works.
  String pendingSsid, pendingPass;
  uint32_t connectStart = 0;

  if (ssid.isEmpty())
  {
    setWifi(WifiState::NoCreds, "");
  }
  else
  {
    setWifi(WifiState::Connecting, ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
  }

  bool online = false;
  bool geoDone = false;
  uint32_t lastWeather = 0;
  bool haveWeather = false;
  uint32_t lastCalendar = 0;
  int horoscopeDay = -1; // local day of year last fetched
  bool serverStarted = false;

  for (;;)
  {
    Cmd cmd;
    if (xQueueReceive(cmdQueue, &cmd, pdMS_TO_TICKS(20)) == pdTRUE)
    {
      switch (cmd.type)
      {
      case CmdType::Scan:
        doScan();
        break;
      case CmdType::Connect:
        pendingSsid = cmd.a;
        pendingPass = cmd.b;
        connectStart = millis();
        setWifi(WifiState::Connecting, cmd.a);
        WiFi.disconnect();
        WiFi.begin(cmd.a, cmd.b);
        break;
      case CmdType::Forget:
        prefs.remove("ssid");
        prefs.remove("pass");
        ssid = "";
        pass = "";
        pendingSsid = "";
        WiFi.disconnect();
        setWifi(WifiState::NoCreds, "");
        break;
      case CmdType::Scene:
        postScene(cmd.a);
        break;
      case CmdType::RefreshWeather:
        lastWeather = 0;
        break;
      case CmdType::RefreshCalendar:
        lastCalendar = 0;
        break;
      case CmdType::RefreshHoroscopes:
        horoscopeDay = -1;
        break;
      }
    }
    if (serverStarted)
      server.handleClient();

    bool connected = WiFi.status() == WL_CONNECTED;
    if (!pendingSsid.isEmpty())
    {
      if (connected)
      {
        ssid = pendingSsid;
        pass = pendingPass;
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        pendingSsid = "";
        Serial.printf("wifi: saved %s\n", ssid.c_str());
      }
      else if (millis() - connectStart > CONNECT_TIMEOUT_MS)
      {
        Serial.printf("wifi: could not join %s\n", pendingSsid.c_str());
        setWifi(WifiState::Failed, pendingSsid.c_str());
        pendingSsid = "";
        WiFi.disconnect();
        if (!ssid.isEmpty())
          WiFi.begin(ssid.c_str(), pass.c_str());
        continue;
      }
    }

    if (connected != online)
    {
      online = connected;
      if (online)
      {
        setWifi(WifiState::Connected, ssid.c_str());
        portENTER_CRITICAL(&mux);
        strlcpy(status.ip, WiFi.localIP().toString().c_str(), sizeof(status.ip));
        portEXIT_CRITICAL(&mux);
        Serial.printf("wifi: connected to %s, settings at http://%s/\n", ssid.c_str(), status.ip);
        if (!serverStarted)
        {
          MDNS.begin("wall-panel");
          server.on("/", HTTP_GET, handleRoot);
          server.on("/save", HTTP_POST, handleSave);
          server.begin();
          serverStarted = true;
        }
      }
      else if (!ssid.isEmpty() && status.wifi != WifiState::Failed)
      {
        setWifi(WifiState::Connecting, ssid.c_str());
      }
    }

    if (!online)
      continue;

    if (!geoDone)
    {
      geoDone = geolocate();
      if (!geoDone && !isnan(lat))
      {
        // Use the saved location when the lookup is down.
        applyTz(prefs.getString("tz", "UTC0").c_str());
        geoDone = true;
      }
    }

    uint32_t period = haveWeather ? WEATHER_PERIOD_MS : 30000;
    if (!isnan(lat) && (lastWeather == 0 || millis() - lastWeather > period))
    {
      haveWeather = fetchWeather();
      lastWeather = millis();
    }

    if (lastCalendar == 0 || millis() - lastCalendar > CALENDAR_PERIOD_MS)
    {
      fetchCalendar();
      lastCalendar = millis();
    }

    // Horoscopes once a day, after the clock is set (and after 01:00, when
    // the source has switched to the new day).
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    if (lt.tm_year > 120 && lt.tm_yday != horoscopeDay && (lt.tm_hour >= 1 || horoscopeDay < 0))
    {
      fetchHoroscopes();
      horoscopeDay = lt.tm_yday;
    }
  }
}

// ---- public API ----

static void send(CmdType type, const char *a = "", const char *b = "")
{
  Cmd cmd;
  cmd.type = type;
  strlcpy(cmd.a, a, sizeof(cmd.a));
  strlcpy(cmd.b, b, sizeof(cmd.b));
  xQueueSend(cmdQueue, &cmd, 0);
}

void netBegin()
{
  prefs.begin("panel", false);
  loadSettings();
  eventsLock = xSemaphoreCreateMutex();
  events = (CalEvent *)ps_malloc(sizeof(CalEvent) * MAX_EVENTS);
  freshEvents = (CalEvent *)ps_malloc(sizeof(CalEvent) * MAX_EVENTS);
  horoscopes = (Horoscope *)ps_malloc(sizeof(Horoscope) * ZODIAC_COUNT);
  status.wifi = WifiState::Connecting;
  status.calendar = settings.calendarUrl[0] ? CalState::Loading : CalState::NotSet;
  cmdQueue = xQueueCreate(6, sizeof(Cmd));
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

void netGetSettings(Settings &out)
{
  portENTER_CRITICAL(&mux);
  out = settings;
  portEXIT_CRITICAL(&mux);
}

void netGetStatus(NetStatus &out)
{
  portENTER_CRITICAL(&mux);
  out = status;
  portEXIT_CRITICAL(&mux);
}

int netGetScan(ScanEntry *out, int max)
{
  portENTER_CRITICAL(&mux);
  int n = min(max, scanCount);
  memcpy(out, scan, sizeof(ScanEntry) * n);
  portEXIT_CRITICAL(&mux);
  return n;
}

void netRequestScan() { send(CmdType::Scan); }
void netConnect(const char *ssid, const char *pass) { send(CmdType::Connect, ssid, pass); }
void netForget() { send(CmdType::Forget); }
void netSendScene(const char *scene) { send(CmdType::Scene, scene); }
void netRefreshWeather() { send(CmdType::RefreshWeather); }

int netGetHoroscopes(Horoscope *out, int max)
{
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  int n = min(max, horoscopeCount);
  memcpy(out, horoscopes, sizeof(Horoscope) * n);
  xSemaphoreGive(eventsLock);
  return n;
}

int netGetEvents(CalEvent *out, int max)
{
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  int n = min(max, eventCount);
  memcpy(out, events, sizeof(CalEvent) * n);
  xSemaphoreGive(eventsLock);
  return n;
}
