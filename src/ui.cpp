#include "ui.h"
#include "net.h"
#include "power.h"
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

// Montserrat with Latin-1/Extended-A (umlauts, Romanian), see tools/make_fonts.sh.
LV_FONT_DECLARE(font_text_14);
LV_FONT_DECLARE(font_text_16);
LV_FONT_DECLARE(font_text_18);
LV_FONT_DECLARE(font_text_20);
LV_FONT_DECLARE(font_text_24);
LV_FONT_DECLARE(font_clock_120);
LV_FONT_DECLARE(font_num_72);
LV_FONT_DECLARE(font_icons_96);
LV_FONT_DECLARE(font_icons_48);
LV_FONT_DECLARE(font_icons_28);
LV_FONT_DECLARE(font_icons_20);
LV_IMG_DECLARE(img_logo);

// Palette, taken from the ice logo.
#define COL_BG_TOP 0x12244A
#define COL_BG_BOTTOM 0x050A16
#define COL_TEXT 0xF2F6FF
#define COL_MUTED 0x8C9AB8
#define COL_DIM 0x56627E
#define COL_ACCENT 0x7CC4F5
#define COL_ACCENT_DEEP 0x2F7FD8
#define COL_WARN 0xFFB454
#define COL_OK 0x6FCF97
#define COL_SUN 0xFFC94D
#define COL_MOON 0xE6E0B8
#define COL_CLOUD 0xC9D6EA
#define COL_RAIN 0x6FB7FF
#define COL_SNOW 0xE8F4FF
#define COL_BOLT 0xFFD84D
#define COL_FOG 0x9AA6BD

#define IDLE_MS (90UL * 1000)
#define BANNER_MS (15UL * 60 * 1000)
#define EVENT_SOON_S (10 * 60)
#define RAIN_SOON_MIN 30

// Font Awesome 5 glyphs (UTF-8) in the generated icon fonts.
#define ICON_SUN "\xEF\x86\x85"
#define ICON_MOON "\xEF\x86\x86"
#define ICON_CLOUD "\xEF\x83\x82"
#define ICON_CLOUD_SUN "\xEF\x9B\x84"
#define ICON_CLOUD_MOON "\xEF\x9B\x83"
#define ICON_CLOUD_RAIN "\xEF\x9C\xBD"
#define ICON_SHOWERS "\xEF\x9D\x80"
#define ICON_SNOW "\xEF\x8B\x9C"
#define ICON_BOLT "\xEF\x83\xA7"
#define ICON_FOG "\xEF\x9D\x9F"
#define ICON_WIND "\xEF\x9C\xAE"
#define ICON_DROP "\xEF\x81\x83"
#define ICON_TRASH "\xEF\x8B\xAD"
#define ICON_CALENDAR "\xEF\x81\xB3"
#define ICON_BELL "\xEF\x83\xB3"
#define ICON_UMBRELLA "\xEF\x83\xA9"
#define ICON_SUN_RAIN "\xEF\x9D\x83"
#define ICON_MOON_RAIN "\xEF\x9C\xBC"
#define ICON_WIFI "\xEF\x87\xAB"
#define ICON_COG "\xEF\x80\x93"

static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *WEEKDAYS_LONG[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};
static const char *MONTHS_SHORT[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static lv_obj_t *scrBoot, *scrMain, *scrIdle, *scrWifi, *scrPass;
static lv_obj_t *bootStatus;

#define BOOT_TIMEOUT_MS 30000

// Swipeable pages on the main screen, left to right.
enum Page
{
  PAGE_HOME,
  PAGE_CALENDAR,
  PAGE_WEATHER,
  PAGE_STARS,
  PAGE_CLOCK,
  PAGE_COUNT,
};
static lv_obj_t *tiles, *pageDots[PAGE_COUNT];

// home
static lv_obj_t *lblTime, *lblDate, *lblGear;
static lv_obj_t *homeWxIcon, *homeTemp, *homeWxDesc, *homeWxSub;
#define NEXT_ROWS 5
static lv_obj_t *nextDot[NEXT_ROWS], *nextBin[NEXT_ROWS], *nextTime[NEXT_ROWS], *nextTitle[NEXT_ROWS], *nextEmpty;

// calendar
enum CalView
{
  VIEW_DAY,
  VIEW_WEEK,
  VIEW_MONTH,
};
static CalView calView = VIEW_MONTH;
static struct tm calSel = {}; // selected day (date fields only)
static lv_obj_t *calTitle, *calSeg, *monthBox, *weekBox, *dayBox;
static lv_obj_t *monthCell[42], *monthDay[42], *monthCount[42];
static struct tm monthCellDate[42];
static lv_obj_t *weekRow[7], *weekName[7], *weekCount[7], *weekFirst[7];
static struct tm weekRowDate[7];
static CalEvent *calEvents; // MAX_EVENTS, in PSRAM
static int calCount = 0;

// weather
static lv_obj_t *wxIcon, *wxTemp, *wxDesc, *wxFeels, *wxRain;
static lv_obj_t *wxChipValue[4];
static lv_obj_t *wxHourTime[6], *wxHourIcon[6], *wxHourTemp[6];
static lv_obj_t *wxDayName[WX_DAYS], *wxDayIcon[WX_DAYS], *wxDayTemp[WX_DAYS];

// horoscope
static lv_obj_t *starsDate, *starsList;
static Horoscope *horoscopes; // ZODIAC_COUNT, in PSRAM

// clock
static lv_obj_t *meter, *clockDate, *clockWx, *clockTemp;
static lv_meter_indicator_t *needleHour, *needleMin, *needleSec;

// idle (logo) screen
static lv_obj_t *idleInfo;

// notification banner
static lv_obj_t *banner, *bannerIcon, *bannerText;
static uint32_t bannerUntil = 0;

// Wi-Fi setup
static lv_obj_t *lblWifiStatus, *listNets, *btnForget;
static uint32_t shownScan = 0;
static bool awaitingConnect = false;
static lv_obj_t *lblPassFor, *taPass;
static char chosenSsid[33];
static bool enteringSsid = false;

static Weather lastWeather = {};

// ---- helpers ----

static lv_obj_t *makeScreen()
{
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(COL_BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

// Frosted-glass card: faint white fill and hairline border over the gradient.
static lv_obj_t *makeCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(card, 16, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(card, 28, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 20, 0);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

static lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *text)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
  lv_label_set_text(l, text);
  return l;
}

// Single line cut with "...". LVGL only truncates when the height is fixed;
// with a content height the text wraps instead.
static void oneLine(lv_obj_t *label, lv_coord_t width)
{
  lv_obj_set_size(label, width, lv_font_get_line_height(lv_obj_get_style_text_font(label, 0)));
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

static lv_obj_t *makeCaption(lv_obj_t *parent, const char *text)
{
  lv_obj_t *l = makeLabel(parent, &font_text_14, COL_ACCENT, text);
  lv_obj_set_style_text_letter_space(l, 2, 0);
  return l;
}

static lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_coord_t w, lv_coord_t h, lv_event_cb_t cb)
{
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_radius(btn, 14, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(btn, 22, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT_DEEP), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  if (cb)
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = makeLabel(btn, &font_text_18, COL_TEXT, text);
  lv_obj_center(l);
  return btn;
}

static lv_obj_t *makePill(lv_obj_t *parent)
{
  lv_obj_t *l = makeLabel(parent, &font_text_14, COL_BG_BOTTOM, "");
  lv_obj_set_style_bg_color(l, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(l, 7, 0);
  lv_obj_set_style_pad_ver(l, 1, 0);
  return l;
}

static int roundi(float v) { return (int)lroundf(v); }

// ---- dates ----

static struct tm dateOf(int year, int month0, int day)
{
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month0;
  t.tm_mday = day;
  t.tm_hour = 12; // noon keeps DST shifts from changing the date
  t.tm_isdst = -1;
  mktime(&t);
  return t;
}

static struct tm addDays(const struct tm &d, int days) { return dateOf(d.tm_year + 1900, d.tm_mon, d.tm_mday + days); }
static bool sameDay(const struct tm &a, const struct tm &b) { return a.tm_year == b.tm_year && a.tm_yday == b.tm_yday; }
static int mondayIndex(const struct tm &d) { return (d.tm_wday + 6) % 7; }

static struct tm today()
{
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return dateOf(t.tm_year + 1900, t.tm_mon, t.tm_mday);
}

static time_t startOfDay(const struct tm &d)
{
  struct tm t = d;
  t.tm_hour = t.tm_min = t.tm_sec = 0;
  t.tm_isdst = -1;
  return mktime(&t);
}

static bool onDay(const CalEvent &e, const struct tm &d)
{
  time_t from = startOfDay(d), to = startOfDay(addDays(d, 1));
  return e.start < to && e.end > from;
}

static int countOnDay(const struct tm &d)
{
  int n = 0;
  for (int i = 0; i < calCount; i++)
    n += onDay(calEvents[i], d);
  return n;
}

static void formatEventTime(const CalEvent &e, char *buf, size_t len, bool range)
{
  if (e.allDay)
  {
    strlcpy(buf, "All day", len);
    return;
  }
  struct tm s, en;
  localtime_r(&e.start, &s);
  localtime_r(&e.end, &en);
  if (range)
    snprintf(buf, len, "%02d:%02d - %02d:%02d", s.tm_hour, s.tm_min, en.tm_hour, en.tm_min);
  else
    snprintf(buf, len, "%02d:%02d", s.tm_hour, s.tm_min);
}

// Bin collections are calendar events whose title mentions bins.
static bool isBinEvent(const char *title)
{
  static const char *WORDS[] = {"bin", "recycl", "rubbish", "garbage", "waste", "trash", "compost"};
  for (const char *p = title; *p; p++)
  {
    if (p != title && isalpha((unsigned char)p[-1]))
      continue; // only match at word starts ("cabin" is not a bin)
    for (const char *w : WORDS)
      if (strncasecmp(p, w, strlen(w)) == 0)
        return true;
  }
  return false;
}

// ---- weather icons ----

struct WxLook
{
  const char *glyph;
  uint32_t colour;
  const char *text;
};

static WxLook weatherLook(int code, bool day)
{
  if (code == 0) return {day ? ICON_SUN : ICON_MOON, day ? COL_SUN : COL_MOON, "Clear"};
  if (code <= 2) return {day ? ICON_CLOUD_SUN : ICON_CLOUD_MOON, day ? COL_SUN : COL_MOON, "Partly cloudy"};
  if (code == 3) return {ICON_CLOUD, COL_CLOUD, "Overcast"};
  if (code == 45 || code == 48) return {ICON_FOG, COL_FOG, "Fog"};
  if (code >= 51 && code <= 57) return {ICON_CLOUD_RAIN, COL_RAIN, "Drizzle"};
  if (code >= 61 && code <= 67) return {code >= 65 ? ICON_SHOWERS : ICON_CLOUD_RAIN, COL_RAIN, "Rain"};
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return {ICON_SNOW, COL_SNOW, "Snow"};
  if (code >= 80 && code <= 82) return {day ? ICON_SUN_RAIN : ICON_MOON_RAIN, COL_RAIN, "Showers"};
  if (code >= 95) return {ICON_BOLT, COL_BOLT, "Thunderstorm"};
  return {ICON_CLOUD, COL_CLOUD, "--"};
}

static bool isDaytime(int minuteOfDay, const Weather &w)
{
  if (w.sunriseMin < 0 || w.sunsetMin < 0)
    return minuteOfDay >= 7 * 60 && minuteOfDay < 19 * 60;
  return minuteOfDay >= w.sunriseMin && minuteOfDay < w.sunsetMin;
}

static void setIcon(lv_obj_t *l, const WxLook &look)
{
  lv_label_set_text(l, look.glyph);
  lv_obj_set_style_text_color(l, lv_color_hex(look.colour), 0);
}

// ---- navigation ----

// Fades to a screen unless it is already showing or fading in (uiUpdate
// runs every 500 ms, which would otherwise restart a running fade).
static void fadeTo(lv_obj_t *scr, uint32_t ms)
{
  lv_disp_t *d = lv_disp_get_default();
  if (lv_scr_act() == scr || d->scr_to_load == scr)
    return;
  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, ms, 0, false);
}

static void openWifi();
static void showHome()
{
  lv_obj_set_tile_id(tiles, PAGE_HOME, 0, LV_ANIM_OFF);
  fadeTo(scrMain, 300);
}

static void wakeClicked(lv_event_t *) { showHome(); }

// ---- notifications ----

static void hideBanner()
{
  bannerUntil = 0;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, banner);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); });
  lv_anim_set_values(&a, lv_obj_get_y(banner), -120);
  lv_anim_set_time(&a, 250);
  lv_anim_start(&a);
}

static void bannerClicked(lv_event_t *) { hideBanner(); }

// Shows a banner and wakes the screen from the logo. At night the screen
// stays off; the banner is there when someone taps.
static void notify(const char *glyph, uint32_t colour, const char *text, bool night)
{
  lv_label_set_text(bannerIcon, glyph);
  lv_obj_set_style_text_color(bannerIcon, lv_color_hex(colour), 0);
  lv_label_set_text(bannerText, text);
  bannerUntil = millis() + BANNER_MS;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, banner);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); });
  lv_anim_set_values(&a, -120, 12);
  lv_anim_set_time(&a, 400);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
  if (!night)
  {
    lv_disp_trig_activity(nullptr);
    if (lv_scr_act() == scrIdle)
      showHome();
  }
}

static void buildBanner()
{
  banner = lv_obj_create(lv_layer_top());
  lv_obj_set_size(banner, 448, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(banner, 72, 0);
  lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_y(banner, -120);
  lv_obj_set_style_bg_color(banner, lv_color_hex(0x1A2E57), 0);
  lv_obj_set_style_border_color(banner, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_border_width(banner, 1, 0);
  lv_obj_set_style_radius(banner, 20, 0);
  lv_obj_set_style_shadow_width(banner, 24, 0);
  lv_obj_set_style_shadow_color(banner, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(banner, LV_OPA_50, 0);
  lv_obj_set_style_pad_all(banner, 16, 0);
  lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(banner, bannerClicked, LV_EVENT_CLICKED, nullptr);
  bannerIcon = makeLabel(banner, &font_icons_28, COL_ACCENT, ICON_BELL);
  lv_obj_align(bannerIcon, LV_ALIGN_LEFT_MID, 0, 0);
  bannerText = makeLabel(banner, &font_text_20, COL_TEXT, "");
  lv_obj_set_width(bannerText, 360);
  lv_label_set_long_mode(bannerText, LV_LABEL_LONG_WRAP);
  lv_obj_align(bannerText, LV_ALIGN_LEFT_MID, 48, 0);
}

static void checkNotifications(const Weather &w, bool night)
{
  time_t now = time(nullptr);
  char buf[96];

  // A calendar event starts within 10 minutes.
  static time_t notifiedEvent = 0;
  for (int i = 0; i < calCount; i++)
  {
    const CalEvent &e = calEvents[i];
    if (e.allDay || e.start <= now || e.start - now > EVENT_SOON_S || e.start == notifiedEvent)
      continue;
    notifiedEvent = e.start;
    snprintf(buf, sizeof(buf), "In %d min: %s", (int)((e.start - now + 59) / 60), e.title);
    notify(ICON_CALENDAR, COL_ACCENT, buf, night);
    break;
  }

  // Rain is coming soon (once per dry-to-wet change).
  static bool rainNotified = false;
  if (w.valid && w.rainInMin >= 0 && w.rainInMin <= RAIN_SOON_MIN)
  {
    if (!rainNotified)
    {
      rainNotified = true;
      if (w.rainInMin == 0)
        strlcpy(buf, "It's starting to rain", sizeof(buf));
      else
        snprintf(buf, sizeof(buf), "Rain in about %d minutes", w.rainInMin);
      notify(ICON_UMBRELLA, COL_RAIN, buf, night);
    }
  }
  else if (w.valid && w.rainInMin < 0)
  {
    rainNotified = false;
  }

  // Bins: the evening before (from 18:00) and the morning of (from 07:00).
  static time_t notifiedBin = 0;
  struct tm lt;
  localtime_r(&now, &lt);
  struct tm t0 = today();
  for (int i = 0; i < calCount; i++)
  {
    const CalEvent &e = calEvents[i];
    if (e.end <= now || !isBinEvent(e.title))
      continue;
    struct tm s;
    localtime_r(&e.start, &s);
    bool isToday = sameDay(dateOf(s.tm_year + 1900, s.tm_mon, s.tm_mday), t0);
    bool isTomorrow = sameDay(dateOf(s.tm_year + 1900, s.tm_mon, s.tm_mday), addDays(t0, 1));
    bool due = (isTomorrow && lt.tm_hour >= 18) || (isToday && lt.tm_hour >= 7);
    time_t key = e.start + (isToday ? 1 : 0); // evening and morning reminders differ
    if (due && notifiedBin != key)
    {
      notifiedBin = key;
      snprintf(buf, sizeof(buf), "%s: %s", isToday ? "Today" : "Tomorrow", e.title);
      notify(ICON_TRASH, COL_OK, buf, night);
    }
    break;
  }

  if (bannerUntil && millis() > bannerUntil)
    hideBanner();
}

// ---- home ----

static void buildHome(lv_obj_t *t)
{
  lblTime = makeLabel(t, &font_clock_120, COL_TEXT, "--:--");
  lv_obj_set_pos(lblTime, 18, 14);
  lblDate = makeLabel(t, &font_text_24, COL_MUTED, "Waiting for time");
  lv_obj_set_pos(lblDate, 26, 134);

  // Settings (Wi-Fi). Turns amber when offline.
  lv_obj_t *gear = lv_btn_create(t);
  lv_obj_set_size(gear, 52, 52);
  lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -12, 14);
  lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(gear, 40, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(gear, 0, 0);
  lv_obj_set_style_radius(gear, LV_RADIUS_CIRCLE, 0);
  lv_obj_add_event_cb(gear, [](lv_event_t *) { openWifi(); }, LV_EVENT_CLICKED, nullptr);
  lblGear = makeLabel(gear, &font_icons_20, COL_DIM, ICON_COG);
  lv_obj_center(lblGear);

  lv_obj_t *wx = makeCard(t, 16, 174, 448, 96);
  homeWxIcon = makeLabel(wx, &font_icons_48, COL_CLOUD, ICON_CLOUD);
  lv_obj_align(homeWxIcon, LV_ALIGN_LEFT_MID, 4, 0);
  homeTemp = makeLabel(wx, &lv_font_montserrat_48, COL_TEXT, "--");
  lv_obj_align(homeTemp, LV_ALIGN_LEFT_MID, 80, 0);
  homeWxDesc = makeLabel(wx, &font_text_20, COL_TEXT, "No weather yet");
  oneLine(homeWxDesc, 230);
  lv_obj_set_style_text_align(homeWxDesc, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(homeWxDesc, LV_ALIGN_TOP_RIGHT, 0, 2);
  homeWxSub = makeLabel(wx, &font_text_16, COL_MUTED, "");
  lv_obj_align(homeWxSub, LV_ALIGN_BOTTOM_RIGHT, 0, -2);

  lv_obj_t *next = makeCard(t, 16, 282, 448, 182);
  makeCaption(next, "UP NEXT");
  for (int i = 0; i < NEXT_ROWS; i++)
  {
    lv_coord_t y = 26 + i * 26;
    nextDot[i] = lv_obj_create(next);
    lv_obj_set_size(nextDot[i], 8, 8);
    lv_obj_set_pos(nextDot[i], 0, y + 7);
    lv_obj_set_style_radius(nextDot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(nextDot[i], 0, 0);
    nextBin[i] = makeLabel(next, &font_icons_20, COL_OK, ICON_TRASH);
    lv_obj_set_pos(nextBin[i], -3, y);
    nextTime[i] = makeLabel(next, &font_text_16, COL_MUTED, "");
    lv_obj_set_pos(nextTime[i], 18, y);
    nextTitle[i] = makeLabel(next, &font_text_18, COL_TEXT, "");
    lv_obj_set_pos(nextTitle[i], 112, y - 1);
    oneLine(nextTitle[i], 306);
    
  }
  nextEmpty = makeLabel(next, &font_text_18, COL_MUTED, "");
  lv_obj_set_pos(nextEmpty, 0, 30);
  lv_obj_set_width(nextEmpty, 420);
  lv_label_set_long_mode(nextEmpty, LV_LABEL_LONG_WRAP);
}

static void updateHomeEvents(const NetStatus &st)
{
  time_t now = time(nullptr);
  struct tm t0 = today();
  time_t horizon = startOfDay(addDays(t0, 7));

  // The next events over the coming week (the feed is sorted by start).
  // Bin collections get a bin icon instead of the calendar colour dot.
  int shown = 0;
  for (int i = 0; i < calCount && shown < NEXT_ROWS; i++)
  {
    const CalEvent &e = calEvents[i];
    if (e.end <= now || e.start >= horizon)
      continue;
    struct tm s;
    localtime_r(&e.start, &s);
    struct tm d = e.start < startOfDay(t0) ? t0 : dateOf(s.tm_year + 1900, s.tm_mon, s.tm_mday);
    char time[16], when[24];
    formatEventTime(e, time, sizeof(time), false);
    if (sameDay(d, t0))
      strlcpy(when, time, sizeof(when));
    else
      snprintf(when, sizeof(when), "%s %s", sameDay(d, addDays(t0, 1)) ? "Tmrw" : WEEKDAYS[d.tm_wday], e.allDay ? "" : time);
    lv_label_set_text(nextTime[shown], when);
    lv_label_set_text(nextTitle[shown], e.title);
    lv_obj_set_style_bg_color(nextDot[shown], lv_color_hex(e.colour), 0);
    bool bin = isBinEvent(e.title);
    bin ? lv_obj_add_flag(nextDot[shown], LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(nextDot[shown], LV_OBJ_FLAG_HIDDEN);
    bin ? lv_obj_clear_flag(nextBin[shown], LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(nextBin[shown], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(nextTime[shown], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(nextTitle[shown], LV_OBJ_FLAG_HIDDEN);
    shown++;
  }
  for (int i = shown; i < NEXT_ROWS; i++)
    for (lv_obj_t *o : {nextDot[i], nextBin[i], nextTime[i], nextTitle[i]})
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);

  if (shown)
    lv_obj_add_flag(nextEmpty, LV_OBJ_FLAG_HIDDEN);
  else
  {
    lv_obj_clear_flag(nextEmpty, LV_OBJ_FLAG_HIDDEN);
    if (st.calendar == CalState::NotSet)
      lv_label_set_text_fmt(nextEmpty, "Connect Google Calendar at http://%s/", st.ip[0] ? st.ip : "<panel ip>");
    else if (st.calendar == CalState::Error && calCount == 0)
      lv_label_set_text(nextEmpty, "Couldn't load the calendar");
    else if (st.calendar == CalState::Ok)
      lv_label_set_text(nextEmpty, "Nothing planned this week");
    else
      lv_label_set_text(nextEmpty, "Loading calendar...");
  }
}

// ---- calendar ----

static void renderCalendar();

static void calSegChanged(lv_event_t *e)
{
  calView = (CalView)lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
  renderCalendar();
}

static void selectView(CalView v)
{
  calView = v;
  lv_btnmatrix_set_btn_ctrl(calSeg, v, LV_BTNMATRIX_CTRL_CHECKED);
  renderCalendar();
}

static void calStep(int dir)
{
  if (calView == VIEW_MONTH)
    calSel = dateOf(calSel.tm_year + 1900, calSel.tm_mon + dir, 1);
  else
    calSel = addDays(calSel, dir * (calView == VIEW_WEEK ? 7 : 1));
  renderCalendar();
}

static void monthCellClicked(lv_event_t *e)
{
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  calSel = monthCellDate[i];
  selectView(VIEW_DAY);
}

static void weekRowClicked(lv_event_t *e)
{
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  calSel = weekRowDate[i];
  selectView(VIEW_DAY);
}

static void buildCalendar(lv_obj_t *t)
{
  lv_obj_t *prev = makeButton(t, LV_SYMBOL_LEFT, 52, 44, [](lv_event_t *) { calStep(-1); });
  lv_obj_set_pos(prev, 16, 12);
  lv_obj_t *next = makeButton(t, LV_SYMBOL_RIGHT, 52, 44, [](lv_event_t *) { calStep(1); });
  lv_obj_set_pos(next, 412, 12);
  calTitle = makeLabel(t, &font_text_24, COL_TEXT, "");
  lv_obj_align(calTitle, LV_ALIGN_TOP_MID, 0, 22);

  static const char *segMap[] = {"Day", "Week", "Month", ""};
  calSeg = lv_btnmatrix_create(t);
  lv_btnmatrix_set_map(calSeg, segMap);
  lv_btnmatrix_set_btn_ctrl_all(calSeg, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(calSeg, true);
  lv_btnmatrix_set_btn_ctrl(calSeg, VIEW_MONTH, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(calSeg, 300, 42);
  lv_obj_align(calSeg, LV_ALIGN_TOP_MID, 0, 64);
  lv_obj_set_style_bg_color(calSeg, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(calSeg, 16, 0);
  lv_obj_set_style_border_width(calSeg, 0, 0);
  lv_obj_set_style_radius(calSeg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(calSeg, 4, 0);
  lv_obj_set_style_pad_gap(calSeg, 4, 0);
  lv_obj_set_style_bg_opa(calSeg, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(calSeg, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(calSeg, LV_RADIUS_CIRCLE, LV_PART_ITEMS);
  lv_obj_set_style_text_color(calSeg, lv_color_hex(COL_MUTED), LV_PART_ITEMS);
  lv_obj_set_style_text_font(calSeg, &font_text_16, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(calSeg, lv_color_hex(COL_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(calSeg, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(calSeg, lv_color_hex(COL_BG_BOTTOM), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(calSeg, calSegChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  // Month: Monday-first grid, 6 weeks.
  monthBox = lv_obj_create(t);
  lv_obj_remove_style_all(monthBox);
  lv_obj_clear_flag(monthBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(monthBox, 16, 116);
  lv_obj_set_size(monthBox, 448, 356);
  static const char *heads[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
  for (int c = 0; c < 7; c++)
  {
    lv_obj_t *h = makeLabel(monthBox, &font_text_14, c >= 5 ? COL_ACCENT : COL_DIM, heads[c]);
    lv_obj_set_width(h, 64);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(h, c * 64, 0);
  }
  for (int i = 0; i < 42; i++)
  {
    lv_obj_t *cell = lv_obj_create(monthBox);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, 60, 52);
    lv_obj_set_pos(cell, (i % 7) * 64 + 2, 22 + (i / 7) * 55);
    lv_obj_set_style_radius(cell, 14, 0);
    lv_obj_set_style_bg_color(cell, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(cell, 40, LV_STATE_PRESSED);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cell, monthCellClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    monthDay[i] = makeLabel(cell, &font_text_18, COL_TEXT, "");
    lv_obj_align(monthDay[i], LV_ALIGN_TOP_MID, 0, 6);
    monthCount[i] = makePill(cell);
    lv_obj_align(monthCount[i], LV_ALIGN_BOTTOM_MID, 0, -5);
    monthCell[i] = cell;
  }

  // Week: one row per day, Monday first.
  weekBox = lv_obj_create(t);
  lv_obj_remove_style_all(weekBox);
  lv_obj_clear_flag(weekBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(weekBox, 16, 116);
  lv_obj_set_size(weekBox, 448, 356);
  for (int i = 0; i < 7; i++)
  {
    lv_obj_t *row = makeCard(weekBox, 0, i * 51, 448, 46);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(row, 50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, weekRowClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    weekName[i] = makeLabel(row, &font_text_18, COL_TEXT, "");
    lv_obj_align(weekName[i], LV_ALIGN_LEFT_MID, 0, 0);
    weekCount[i] = makePill(row);
    lv_obj_align(weekCount[i], LV_ALIGN_LEFT_MID, 84, 0);
    weekFirst[i] = makeLabel(row, &font_text_16, COL_MUTED, "");
    oneLine(weekFirst[i], 290);
    
    lv_obj_align(weekFirst[i], LV_ALIGN_LEFT_MID, 128, 0);
    weekRow[i] = row;
  }

  // Day: scrollable list of event cards.
  dayBox = lv_obj_create(t);
  lv_obj_remove_style_all(dayBox);
  lv_obj_set_pos(dayBox, 16, 116);
  lv_obj_set_size(dayBox, 448, 356);
  lv_obj_set_flex_flow(dayBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(dayBox, 8, 0);
  lv_obj_set_scroll_dir(dayBox, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(dayBox, LV_SCROLLBAR_MODE_OFF);
}

static void setCount(lv_obj_t *pill, int n)
{
  if (n == 0)
  {
    lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(pill, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text_fmt(pill, "%d", n);
}

static void renderMonth(const struct tm &t0)
{
  struct tm first = dateOf(calSel.tm_year + 1900, calSel.tm_mon, 1);
  struct tm start = addDays(first, -mondayIndex(first));
  for (int i = 0; i < 42; i++)
  {
    struct tm d = addDays(start, i);
    monthCellDate[i] = d;
    bool inMonth = d.tm_mon == calSel.tm_mon;
    bool isToday = sameDay(d, t0);
    lv_label_set_text_fmt(monthDay[i], "%d", d.tm_mday);
    lv_obj_set_style_text_color(monthDay[i], lv_color_hex(isToday ? COL_BG_BOTTOM : inMonth ? COL_TEXT : COL_DIM), 0);
    lv_obj_set_style_bg_color(monthCell[i], lv_color_hex(isToday ? COL_ACCENT : 0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(monthCell[i], isToday ? LV_OPA_COVER : inMonth ? 10 : 0, 0);
    setCount(monthCount[i], countOnDay(d));
    lv_obj_set_style_bg_color(monthCount[i], lv_color_hex(isToday ? COL_BG_BOTTOM : COL_ACCENT), 0);
    lv_obj_set_style_text_color(monthCount[i], lv_color_hex(isToday ? COL_ACCENT : COL_BG_BOTTOM), 0);
  }
  lv_label_set_text_fmt(calTitle, "%s %d", MONTHS[calSel.tm_mon], calSel.tm_year + 1900);
}

static void renderWeek(const struct tm &t0)
{
  struct tm start = addDays(calSel, -mondayIndex(calSel));
  for (int i = 0; i < 7; i++)
  {
    struct tm d = addDays(start, i);
    weekRowDate[i] = d;
    bool isToday = sameDay(d, t0);
    lv_label_set_text_fmt(weekName[i], "%s %d", WEEKDAYS[d.tm_wday], d.tm_mday);
    lv_obj_set_style_text_color(weekName[i], lv_color_hex(isToday ? COL_ACCENT : COL_TEXT), 0);
    lv_obj_set_style_border_color(weekRow[i], lv_color_hex(isToday ? COL_ACCENT : 0xFFFFFF), 0);
    lv_obj_set_style_border_opa(weekRow[i], isToday ? LV_OPA_COVER : 28, 0);
    int n = countOnDay(d);
    setCount(weekCount[i], n);
    lv_label_set_text(weekFirst[i], n ? "" : "Free");
    for (int j = 0; j < calCount; j++)
    {
      if (!onDay(calEvents[j], d))
        continue;
      char when[16];
      formatEventTime(calEvents[j], when, sizeof(when), false);
      lv_label_set_text_fmt(weekFirst[i], "%s  %s", when, calEvents[j].title);
      break;
    }
  }
  struct tm end = addDays(start, 6);
  lv_label_set_text_fmt(calTitle, "%d %s - %d %s", start.tm_mday, MONTHS_SHORT[start.tm_mon], end.tm_mday, MONTHS_SHORT[end.tm_mon]);
}

static void renderDay(const struct tm &t0)
{
  lv_obj_clean(dayBox);
  int n = 0;
  for (int i = 0; i < calCount; i++)
  {
    const CalEvent &e = calEvents[i];
    if (!onDay(e, calSel))
      continue;
    lv_obj_t *card = makeCard(dayBox, 0, 0, 448, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(card, 22, 0);
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 5, LV_PCT(100));
    lv_obj_set_style_bg_color(bar, lv_color_hex(e.colour), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, -12, 0);
    char when[24];
    formatEventTime(e, when, sizeof(when), true);
    makeLabel(card, &font_text_14, COL_MUTED, when);
    lv_obj_t *title = makeLabel(card, &font_text_20, COL_TEXT, e.title);
    lv_obj_set_width(title, 400);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(title, 0, 22);
    n++;
  }
  if (n == 0)
  {
    lv_obj_t *icon = makeLabel(dayBox, &font_icons_48, COL_DIM, ICON_CALENDAR);
    lv_obj_set_style_pad_top(icon, 60, 0);
    lv_obj_set_width(icon, 448);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *l = makeLabel(dayBox, &font_text_20, COL_MUTED, "Nothing planned");
    lv_obj_set_width(l, 448);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  bool isToday = sameDay(calSel, t0);
  lv_label_set_text_fmt(calTitle, "%s%s %d %s", isToday ? "Today, " : "", isToday ? WEEKDAYS[calSel.tm_wday] : WEEKDAYS_LONG[calSel.tm_wday],
                        calSel.tm_mday, MONTHS_SHORT[calSel.tm_mon]);
}

static void renderCalendar()
{
  struct tm t0 = today();
  if (calSel.tm_year == 0)
    calSel = t0;
  lv_obj_t *boxes[] = {dayBox, weekBox, monthBox};
  for (int i = 0; i < 3; i++)
    i == calView ? lv_obj_clear_flag(boxes[i], LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(boxes[i], LV_OBJ_FLAG_HIDDEN);
  if (calView == VIEW_MONTH)
    renderMonth(t0);
  else if (calView == VIEW_WEEK)
    renderWeek(t0);
  else
    renderDay(t0);
}

// ---- weather ----

static void buildWeather(lv_obj_t *t)
{
  wxIcon = makeLabel(t, &font_icons_96, COL_CLOUD, ICON_CLOUD);
  lv_obj_set_pos(wxIcon, 24, 24);
  wxTemp = makeLabel(t, &font_num_72, COL_TEXT, "--");
  lv_obj_set_pos(wxTemp, 156, 12);
  wxDesc = makeLabel(t, &font_text_24, COL_TEXT, "No weather yet");
  lv_obj_set_pos(wxDesc, 160, 92);
  wxFeels = makeLabel(t, &font_text_16, COL_MUTED, "Tap to refresh");
  lv_obj_set_pos(wxFeels, 160, 122);
  lv_obj_add_flag(wxTemp, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(wxTemp, 30);
  lv_obj_add_event_cb(wxTemp, [](lv_event_t *) { netRefreshWeather(); }, LV_EVENT_CLICKED, nullptr);

  wxRain = makeLabel(t, &font_text_16, COL_RAIN, "");
  lv_obj_set_pos(wxRain, 24, 150);
  oneLine(wxRain, 432);
  

  // Detail chips.
  static const char *chipIcon[] = {ICON_DROP, ICON_WIND, ICON_SUN, ICON_MOON};
  static const uint32_t chipCol[] = {COL_RAIN, COL_CLOUD, COL_SUN, COL_MOON};
  static const char *chipName[] = {"Humidity", "Wind", "Sunrise", "Sunset"};
  for (int i = 0; i < 4; i++)
  {
    lv_obj_t *c = makeCard(t, 16 + i * 114, 178, 106, 70);
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_t *ic = makeLabel(c, &font_icons_20, chipCol[i], chipIcon[i]);
    lv_obj_set_pos(ic, 0, 0);
    makeLabel(c, &font_text_14, COL_MUTED, chipName[i]);
    lv_obj_align(lv_obj_get_child(c, 1), LV_ALIGN_TOP_LEFT, 26, 2);
    wxChipValue[i] = makeLabel(c, &font_text_20, COL_TEXT, "--");
    lv_obj_align(wxChipValue[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }

  // Next 12 hours, every second hour.
  lv_obj_t *hourly = makeCard(t, 16, 258, 448, 102);
  lv_obj_set_style_pad_ver(hourly, 10, 0);
  for (int i = 0; i < 6; i++)
  {
    lv_coord_t x = i * 70;
    wxHourTime[i] = makeLabel(hourly, &font_text_14, COL_MUTED, "");
    lv_obj_set_width(wxHourTime[i], 60);
    lv_obj_set_style_text_align(wxHourTime[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wxHourTime[i], x, 0);
    wxHourIcon[i] = makeLabel(hourly, &font_icons_28, COL_CLOUD, "");
    lv_obj_set_width(wxHourIcon[i], 60);
    lv_obj_set_style_text_align(wxHourIcon[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wxHourIcon[i], x, 20);
    wxHourTemp[i] = makeLabel(hourly, &font_text_18, COL_TEXT, "");
    lv_obj_set_width(wxHourTemp[i], 60);
    lv_obj_set_style_text_align(wxHourTemp[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wxHourTemp[i], x, 56);
  }

  // Next 5 days.
  lv_obj_t *daily = makeCard(t, 16, 370, 448, 98);
  lv_obj_set_style_pad_ver(daily, 8, 0);
  for (int i = 0; i < WX_DAYS; i++)
  {
    lv_coord_t x = i * 84;
    wxDayName[i] = makeLabel(daily, &font_text_14, COL_MUTED, "");
    lv_obj_set_width(wxDayName[i], 80);
    lv_obj_set_style_text_align(wxDayName[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wxDayName[i], x, 0);
    wxDayIcon[i] = makeLabel(daily, &font_icons_28, COL_CLOUD, "");
    lv_obj_set_width(wxDayIcon[i], 80);
    lv_obj_set_style_text_align(wxDayIcon[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wxDayIcon[i], x, 20);
    wxDayTemp[i] = makeLabel(daily, &font_text_16, COL_TEXT, "");
    lv_obj_set_width(wxDayTemp[i], 80);
    lv_obj_set_style_text_align(wxDayTemp[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wxDayTemp[i], x, 56);
  }
}

static void updateWeatherViews(const Weather &w)
{
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  bool day = isDaytime(lt.tm_hour * 60 + lt.tm_min, w);
  WxLook look = weatherLook(w.code, day);

  char rain[64];
  if (w.rainInMin == 0)
    snprintf(rain, sizeof(rain), "Raining now, %.1f mm in the next 2 hours", w.rainNext2h);
  else if (w.rainInMin > 0)
    snprintf(rain, sizeof(rain), "Rain in about %d min", w.rainInMin);
  else
    strlcpy(rain, "No rain in the next 2 hours", sizeof(rain));

  // Home
  setIcon(homeWxIcon, look);
  lv_label_set_text_fmt(homeTemp, "%d°", roundi(w.temp));
  lv_label_set_text(homeWxDesc, w.rainInMin >= 0 ? (w.rainInMin == 0 ? "Raining" : rain) : look.text);
  lv_label_set_text_fmt(homeWxSub, "H %d°  L %d°  Feels %d°", roundi(w.tmax), roundi(w.tmin), roundi(w.feels));
  lv_obj_align(homeWxDesc, LV_ALIGN_TOP_RIGHT, 0, 2);
  lv_obj_align(homeWxSub, LV_ALIGN_BOTTOM_RIGHT, 0, -2);

  // Weather page
  setIcon(wxIcon, look);
  lv_label_set_text_fmt(wxTemp, "%d°", roundi(w.temp));
  lv_label_set_text(wxDesc, look.text);
  lv_label_set_text_fmt(wxFeels, "Feels %d°   H %d°  L %d°   Updated %02d:%02d", roundi(w.feels), roundi(w.tmax), roundi(w.tmin),
                        w.updatedMin / 60, w.updatedMin % 60);
  lv_label_set_text_fmt(wxRain, LV_SYMBOL_TINT "  %s", rain);
  lv_obj_set_style_text_color(wxRain, lv_color_hex(w.rainInMin >= 0 ? COL_RAIN : COL_MUTED), 0);

  lv_label_set_text_fmt(wxChipValue[0], "%d%%", w.humidity);
  lv_label_set_text_fmt(wxChipValue[1], "%d km/h", roundi(w.wind));
  lv_label_set_text_fmt(wxChipValue[2], "%02d:%02d", w.sunriseMin / 60, w.sunriseMin % 60);
  lv_label_set_text_fmt(wxChipValue[3], "%02d:%02d", w.sunsetMin / 60, w.sunsetMin % 60);

  for (int i = 0; i < 6; i++)
  {
    int h = i * 2;
    if (h >= w.hourCount)
      continue;
    lv_label_set_text_fmt(wxHourTime[i], i == 0 ? "Now" : "%02d:00", w.hour[h]);
    setIcon(wxHourIcon[i], weatherLook(w.hourCode[h], isDaytime(w.hour[h] * 60, w)));
    lv_label_set_text_fmt(wxHourTemp[i], "%d°", roundi(w.hourTemp[h]));
  }
  for (int i = 0; i < w.dayCount && i < WX_DAYS; i++)
  {
    lv_label_set_text(wxDayName[i], i == 0 ? "Today" : WEEKDAYS[w.dayWeekday[i]]);
    setIcon(wxDayIcon[i], weatherLook(w.dayCode[i], true));
    lv_label_set_text_fmt(wxDayTemp[i], "%d° / %d°", roundi(w.dayMax[i]), roundi(w.dayMin[i]));
  }

  // Clock page
  setIcon(clockWx, look);
  lv_label_set_text_fmt(clockTemp, "%d°", roundi(w.temp));
}

// ---- horoscope ----

static const char *ZODIAC_DATES[ZODIAC_COUNT] = {
    "Mar 21 - Apr 19", "Apr 20 - May 20", "May 21 - Jun 20", "Jun 21 - Jul 22", "Jul 23 - Aug 22", "Aug 23 - Sep 22",
    "Sep 23 - Oct 22", "Oct 23 - Nov 21", "Nov 22 - Dec 21", "Dec 22 - Jan 19", "Jan 20 - Feb 18", "Feb 19 - Mar 20"};
// Fire, earth, air, water (signs cycle through the elements in that order).
static const uint32_t ELEMENT_COLOURS[4] = {0xFF8A5B, 0x8BD17C, 0x7CC4F5, 0xA99BFF};

static void buildStars(lv_obj_t *t)
{
  lv_obj_t *title = makeLabel(t, &lv_font_montserrat_32, COL_TEXT, "Horoscope");
  lv_obj_set_pos(title, 22, 18);
  starsDate = makeLabel(t, &font_text_16, COL_MUTED, "");
  lv_obj_set_pos(starsDate, 24, 60);

  starsList = lv_obj_create(t);
  lv_obj_remove_style_all(starsList);
  lv_obj_set_pos(starsList, 16, 92);
  lv_obj_set_size(starsList, 448, 376);
  lv_obj_set_flex_flow(starsList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(starsList, 10, 0);
  lv_obj_set_scroll_dir(starsList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(starsList, LV_SCROLLBAR_MODE_OFF);
}

static void updateStars(const NetStatus &st)
{
  struct tm t0 = today();
  lv_label_set_text_fmt(starsDate, "Today, %s %d %s", WEEKDAYS[t0.tm_wday], t0.tm_mday, MONTHS_SHORT[t0.tm_mon]);
  lv_obj_clean(starsList);
  int n = netGetHoroscopes(horoscopes, ZODIAC_COUNT);
  for (int i = 0; i < n; i++)
  {
    const Horoscope &h = horoscopes[i];
    lv_obj_t *card = makeCard(starsList, 0, 0, 448, LV_SIZE_CONTENT);
    lv_obj_t *name = makeLabel(card, &font_text_24, ELEMENT_COLOURS[h.sign % 4], ZODIAC_SIGNS[h.sign]);
    lv_obj_set_pos(name, 0, 0);
    lv_obj_t *dates = makeLabel(card, &font_text_14, COL_MUTED, ZODIAC_DATES[h.sign]);
    lv_obj_align(dates, LV_ALIGN_TOP_RIGHT, 0, 6);
    lv_obj_t *text = makeLabel(card, &font_text_16, COL_TEXT, h.text);
    lv_obj_set_width(text, 416);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(text, 4, 0);
    lv_obj_set_pos(text, 0, 36);
  }
  if (n == 0)
  {
    Settings s;
    netGetSettings(s);
    lv_obj_t *l = makeLabel(starsList, &font_text_20, COL_MUTED, "");
    lv_obj_set_width(l, 448);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    if (s.zodiacMask)
      lv_label_set_text(l, "Loading today's horoscopes...");
    else
      lv_label_set_text_fmt(l, "Pick your signs on the settings page:\nhttp://%s/", st.ip[0] ? st.ip : "<panel ip>");
  }
}

// ---- clock ----

static void buildClock(lv_obj_t *t)
{
  // Face: a soft radial-looking disc built from a gradient circle.
  lv_obj_t *face = lv_obj_create(t);
  lv_obj_remove_style_all(face);
  lv_obj_set_size(face, 420, 420);
  lv_obj_align(face, LV_ALIGN_CENTER, 0, -8);
  lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(face, lv_color_hex(0x1B3263), 0);
  lv_obj_set_style_bg_grad_color(face, lv_color_hex(0x08101F), 0);
  lv_obj_set_style_bg_grad_dir(face, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(face, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(face, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_border_opa(face, 60, 0);
  lv_obj_set_style_border_width(face, 2, 0);

  meter = lv_meter_create(face);
  lv_obj_remove_style(meter, nullptr, LV_PART_MAIN);
  lv_obj_set_size(meter, 400, 400);
  lv_obj_center(meter);
  lv_obj_set_style_pad_all(meter, 18, 0);
  lv_obj_set_style_text_font(meter, &font_text_24, LV_PART_TICKS);
  lv_obj_set_style_text_color(meter, lv_color_hex(COL_TEXT), LV_PART_TICKS);

  lv_meter_scale_t *minutes = lv_meter_add_scale(meter);
  lv_meter_set_scale_ticks(meter, minutes, 61, 2, 10, lv_color_hex(COL_DIM));
  lv_meter_set_scale_range(meter, minutes, 0, 60, 360, 270);

  lv_meter_scale_t *hours = lv_meter_add_scale(meter);
  lv_meter_set_scale_ticks(meter, hours, 12, 0, 0, lv_color_hex(COL_DIM));
  lv_meter_set_scale_major_ticks(meter, hours, 1, 4, 18, lv_color_hex(COL_ACCENT), 16);
  lv_meter_set_scale_range(meter, hours, 1, 12, 330, 300);

  needleHour = lv_meter_add_needle_line(meter, minutes, 10, lv_color_hex(COL_TEXT), -80);
  needleMin = lv_meter_add_needle_line(meter, minutes, 6, lv_color_hex(COL_CLOUD), -30);
  needleSec = lv_meter_add_needle_line(meter, minutes, 2, lv_color_hex(COL_ACCENT), -14);
  lv_obj_set_style_bg_color(meter, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_size(meter, 16, LV_PART_INDICATOR);
  lv_obj_set_style_radius(meter, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

  clockDate = makeLabel(face, &font_text_20, COL_MUTED, "");
  lv_obj_align(clockDate, LV_ALIGN_CENTER, 0, -70);
  // Icon and temperature use different fonts, so they sit in a row.
  lv_obj_t *row = lv_obj_create(face);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 76);
  clockWx = makeLabel(row, &font_icons_20, COL_SUN, "");
  clockTemp = makeLabel(row, &font_text_20, COL_MUTED, "");
}

// ---- idle (logo) screen ----

static void buildIdle()
{
  scrIdle = makeScreen();
  lv_obj_add_event_cb(scrIdle, wakeClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *logo = lv_img_create(scrIdle);
  lv_img_set_src(logo, &img_logo);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -30);
  idleInfo = makeLabel(scrIdle, &font_text_24, COL_MUTED, "");
  lv_obj_align(idleInfo, LV_ALIGN_BOTTOM_MID, 0, -28);
}

// ---- boot (logo + spinner while Wi-Fi and the clock come up) ----

static void buildBoot()
{
  scrBoot = makeScreen();
  lv_obj_t *logo = lv_img_create(scrBoot);
  lv_img_set_src(logo, &img_logo);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -44);

  lv_obj_t *spin = lv_spinner_create(scrBoot, 1000, 60);
  lv_obj_set_size(spin, 40, 40);
  lv_obj_align(spin, LV_ALIGN_BOTTOM_MID, 0, -72);
  lv_obj_set_style_arc_width(spin, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spin, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spin, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(spin, 30, LV_PART_MAIN);
  lv_obj_set_style_arc_color(spin, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);

  bootStatus = makeLabel(scrBoot, &font_text_18, COL_MUTED, "Starting...");
  lv_obj_align(bootStatus, LV_ALIGN_BOTTOM_MID, 0, -34);
}

// Leaves the boot screen once the time, weather and calendar have loaded
// (the pages are already filled in by then), on a failed connect, or after
// a timeout (the panel still works offline).
static void updateBoot(const NetStatus &st, bool timeKnown)
{
  if (lv_scr_act() != scrBoot)
    return;
  bool calendarDone = st.calendar != CalState::Loading;
  bool ready = st.wifi == WifiState::Connected && timeKnown && st.weather.valid && calendarDone;
  if (st.wifi == WifiState::Connecting)
    lv_label_set_text_fmt(bootStatus, "Connecting to %s...", st.ssid);
  else if (!timeKnown)
    lv_label_set_text(bootStatus, "Getting the time...");
  else if (!st.weather.valid)
    lv_label_set_text(bootStatus, "Loading weather...");
  else if (!calendarDone)
    lv_label_set_text(bootStatus, "Loading calendar...");
  lv_obj_align(bootStatus, LV_ALIGN_BOTTOM_MID, 0, -34);

  if (ready || st.wifi == WifiState::Failed || millis() > BOOT_TIMEOUT_MS)
    fadeTo(scrMain, 500);
}

// ---- page dots ----

static void pageChanged(lv_event_t *)
{
  int active = lv_obj_get_index(lv_tileview_get_tile_act(tiles));
  for (int i = 0; i < PAGE_COUNT; i++)
  {
    lv_obj_set_width(pageDots[i], i == active ? 18 : 6);
    lv_obj_set_style_bg_opa(pageDots[i], i == active ? LV_OPA_COVER : LV_OPA_40, 0);
  }
}

static void buildMain()
{
  scrMain = makeScreen();

  tiles = lv_tileview_create(scrMain);
  lv_obj_set_size(tiles, 480, 480);
  lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tiles, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(tiles, pageChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *page[PAGE_COUNT];
  for (int i = 0; i < PAGE_COUNT; i++)
  {
    lv_dir_t dir = (lv_dir_t)((i > 0 ? LV_DIR_LEFT : 0) | (i < PAGE_COUNT - 1 ? LV_DIR_RIGHT : 0));
    page[i] = lv_tileview_add_tile(tiles, i, 0, dir);
    lv_obj_clear_flag(page[i], LV_OBJ_FLAG_SCROLLABLE);
  }
  buildHome(page[PAGE_HOME]);
  buildCalendar(page[PAGE_CALENDAR]);
  buildWeather(page[PAGE_WEATHER]);
  buildStars(page[PAGE_STARS]);
  buildClock(page[PAGE_CLOCK]);

  lv_obj_t *dots = lv_obj_create(scrMain);
  lv_obj_remove_style_all(dots);
  lv_obj_set_size(dots, LV_SIZE_CONTENT, 6);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(dots, 6, 0);
  lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < PAGE_COUNT; i++)
  {
    lv_obj_t *d = lv_obj_create(dots);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 6, 6);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_40, 0);
    pageDots[i] = d;
  }
  pageChanged(nullptr);
}

// ---- Wi-Fi setup ----

static void openWifi()
{
  lv_scr_load(scrWifi);
  netRequestScan();
}

static void startConnect(const char *pass)
{
  netConnect(chosenSsid, pass);
  awaitingConnect = true;
  lv_scr_load(scrWifi);
}

static void askPassword()
{
  enteringSsid = false;
  lv_label_set_text_fmt(lblPassFor, "Password for %s", chosenSsid);
  lv_textarea_set_text(taPass, "");
  lv_textarea_set_placeholder_text(taPass, "password");
  lv_textarea_set_password_mode(taPass, true);
  lv_scr_load(scrPass);
}

static void networkClicked(lv_event_t *e)
{
  lv_obj_t *btn = lv_event_get_target(e);
  bool open = (bool)(intptr_t)lv_event_get_user_data(e);
  strlcpy(chosenSsid, lv_list_get_btn_text(listNets, btn), sizeof(chosenSsid));
  if (open)
    startConnect("");
  else
    askPassword();
}

// "Other network...": the keyboard screen first asks for the name, then the
// password (blank for an open network).
static void otherNetworkClicked(lv_event_t *)
{
  enteringSsid = true;
  lv_label_set_text(lblPassFor, "Network name");
  lv_textarea_set_text(taPass, "");
  lv_textarea_set_placeholder_text(taPass, "network name");
  lv_textarea_set_password_mode(taPass, false);
  lv_scr_load(scrPass);
}

static void keyboardEvent(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY)
  {
    const char *text = lv_textarea_get_text(taPass);
    if (!enteringSsid)
      startConnect(text);
    else if (text[0])
    {
      strlcpy(chosenSsid, text, sizeof(chosenSsid));
      askPassword();
    }
  }
  else if (code == LV_EVENT_CANCEL)
  {
    enteringSsid = false;
    lv_scr_load(scrWifi);
  }
}

static void buildWifi()
{
  scrWifi = makeScreen();

  lv_obj_t *title = makeLabel(scrWifi, &lv_font_montserrat_32, COL_TEXT, "Wi-Fi");
  lv_obj_set_pos(title, 22, 18);
  lblWifiStatus = makeLabel(scrWifi, &font_text_16, COL_MUTED, "");
  lv_obj_set_pos(lblWifiStatus, 24, 62);
  oneLine(lblWifiStatus, 432);
  

  lv_obj_t *btnScan = makeButton(scrWifi, LV_SYMBOL_REFRESH " Scan", 140, 50, [](lv_event_t *) { netRequestScan(); });
  lv_obj_set_pos(btnScan, 16, 94);
  btnForget = makeButton(scrWifi, "Forget", 140, 50, [](lv_event_t *) { netForget(); });
  lv_obj_set_pos(btnForget, 170, 94);
  lv_obj_t *btnBack = makeButton(scrWifi, LV_SYMBOL_LEFT " Back", 140, 50, [](lv_event_t *) { showHome(); });
  lv_obj_set_pos(btnBack, 324, 94);

  listNets = lv_list_create(scrWifi);
  lv_obj_set_pos(listNets, 16, 156);
  lv_obj_set_size(listNets, 448, 308);
  lv_obj_set_style_bg_color(listNets, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(listNets, 16, 0);
  lv_obj_set_style_border_width(listNets, 0, 0);
  lv_obj_set_style_radius(listNets, 20, 0);
}

static void buildPass()
{
  scrPass = makeScreen();

  lblPassFor = makeLabel(scrPass, &font_text_20, COL_TEXT, "");
  lv_obj_set_pos(lblPassFor, 20, 14);
  oneLine(lblPassFor, 440);
  

  taPass = lv_textarea_create(scrPass);
  lv_obj_set_pos(taPass, 16, 46);
  lv_obj_set_size(taPass, 448, 56);
  lv_textarea_set_one_line(taPass, true);
  lv_textarea_set_password_mode(taPass, true);
  lv_textarea_set_max_length(taPass, 63);
  lv_textarea_set_placeholder_text(taPass, "password");

  lv_obj_t *cb = lv_checkbox_create(scrPass);
  lv_checkbox_set_text(cb, "Show password");
  lv_obj_set_pos(cb, 18, 114);
  lv_obj_add_event_cb(
      cb, [](lv_event_t *e) { lv_textarea_set_password_mode(taPass, !lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)); },
      LV_EVENT_VALUE_CHANGED, nullptr);

  // The keyboard's tick key connects; its close key goes back.
  lv_obj_t *kb = lv_keyboard_create(scrPass);
  lv_obj_set_size(kb, 480, 300);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, taPass);
  lv_obj_add_event_cb(kb, keyboardEvent, LV_EVENT_ALL, nullptr);
}

static void refreshNetworks(const NetStatus &st)
{
  if (st.scanVersion == shownScan)
    return;
  shownScan = st.scanVersion;

  ScanEntry nets[20];
  int n = netGetScan(nets, 20);
  lv_obj_clean(listNets);
  lv_list_add_text(listNets, n ? "Only 2.4 GHz networks show up" : "No networks found. Only 2.4 GHz networks show up; try Scan again.");
  for (int i = 0; i < n; i++)
  {
    lv_obj_t *btn = lv_list_add_btn(listNets, nets[i].open ? LV_SYMBOL_WIFI : LV_SYMBOL_EYE_CLOSE, nets[i].ssid);
    lv_obj_set_height(btn, 52);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(COL_TEXT), 0);
    lv_obj_add_event_cb(btn, networkClicked, LV_EVENT_CLICKED, (void *)(intptr_t)nets[i].open);
    int r = nets[i].rssi;
    lv_obj_t *sig = makeLabel(btn, &font_text_14, r > -60 ? COL_OK : r > -75 ? COL_WARN : 0xFF6B6B,
                              r > -60 ? "strong" : r > -75 ? "ok" : "weak");
    lv_obj_add_flag(sig, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(sig, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  lv_obj_t *other = lv_list_add_btn(listNets, LV_SYMBOL_PLUS, "Other network...");
  lv_obj_set_height(other, 52);
  lv_obj_set_style_bg_opa(other, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color(other, lv_color_hex(COL_MUTED), 0);
  lv_obj_add_event_cb(other, otherNetworkClicked, LV_EVENT_CLICKED, nullptr);
}

static void updateWifi(const NetStatus &st)
{
  switch (st.wifi)
  {
  case WifiState::Connected:
    lv_label_set_text_fmt(lblWifiStatus, "Connected to %s. Settings: http://%s/", st.ssid, st.ip);
    break;
  case WifiState::Connecting:
    lv_label_set_text_fmt(lblWifiStatus, "Connecting to %s...", st.ssid);
    break;
  case WifiState::Failed:
    lv_label_set_text_fmt(lblWifiStatus, "Couldn't join %s. Check the password.", st.ssid);
    break;
  case WifiState::NoCreds:
    lv_label_set_text(lblWifiStatus, "Pick your network");
    break;
  }
  if (st.scanning)
    lv_label_set_text(lblWifiStatus, "Scanning...");
  lv_obj_set_style_text_color(lblGear, lv_color_hex(st.wifi == WifiState::Connected ? COL_DIM : COL_WARN), 0);
  st.wifi == WifiState::NoCreds ? lv_obj_add_flag(btnForget, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(btnForget, LV_OBJ_FLAG_HIDDEN);

  if (awaitingConnect && st.wifi != WifiState::Connecting)
  {
    awaitingConnect = false;
    if (st.wifi == WifiState::Connected)
      showHome();
  }
  if (lv_scr_act() == scrWifi)
    refreshNetworks(st);
}

// ---- night ----

// Night starts at the fixed time or, in sunset mode, at sunset if that is
// earlier. It ends at the fixed end time.
static bool isNight(const struct tm &t, const Weather &w)
{
  Settings s;
  netGetSettings(s);
  int start = s.nightStart;
  if (s.nightAtSunset && w.valid && w.sunsetMin >= 0)
    start = min(start, w.sunsetMin);
  int m = t.tm_hour * 60 + t.tm_min;
  if (start == s.nightEnd)
    return false;
  if (start > s.nightEnd)
    return m >= start || m < s.nightEnd;
  return m >= start && m < s.nightEnd;
}

// ---- public ----

void uiShowPage(int page)
{
  showHome();
  lv_obj_set_tile_id(tiles, page, 0, LV_ANIM_OFF);
  pageChanged(nullptr);
}

void uiShowIdle() { lv_scr_load(scrIdle); }

void uiBegin()
{
  calEvents = (CalEvent *)ps_malloc(sizeof(CalEvent) * MAX_EVENTS);
  horoscopes = (Horoscope *)ps_malloc(sizeof(Horoscope) * ZODIAC_COUNT);
  buildBoot();
  buildMain();
  buildIdle();
  buildWifi();
  buildPass();
  buildBanner();
  lv_scr_load(scrBoot);
}

void uiUpdate()
{
  static int lastMinute = -1;
  static int lastDay = -1;
  static uint32_t seenWeather = 0;
  static bool autoOpened = false;

  NetStatus st;
  netGetStatus(st);

  // First boot (nothing saved): go straight to Wi-Fi setup.
  if (st.wifi == WifiState::NoCreds && !autoOpened)
  {
    autoOpened = true;
    openWifi();
  }

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  bool timeKnown = t.tm_year > 120;
  bool minuteChanged = timeKnown && t.tm_min != lastMinute;
  bool night = timeKnown && isNight(t, st.weather);

  if (st.weatherVersion != seenWeather && st.weather.valid)
  {
    seenWeather = st.weatherVersion;
    lastWeather = st.weather;
    updateWeatherViews(st.weather);
  }

  if (minuteChanged)
  {
    lastMinute = t.tm_min;
    char buf[48];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    lv_label_set_text(lblTime, buf);
    lv_label_set_text_fmt(lblDate, "%s, %d %s", WEEKDAYS_LONG[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
    lv_label_set_text_fmt(clockDate, "%s %d %s", WEEKDAYS[t.tm_wday], t.tm_mday, MONTHS_SHORT[t.tm_mon]);
    if (lastWeather.valid)
    {
      updateWeatherViews(lastWeather); // day/night icons follow the clock
      WxLook look = weatherLook(lastWeather.code, isDaytime(t.tm_hour * 60 + t.tm_min, lastWeather));
      lv_label_set_text_fmt(idleInfo, "%s     %d°  %s", buf, roundi(lastWeather.temp), look.text);
    }
    else
    {
      lv_label_set_text(idleInfo, buf);
    }
  }

  // Calendar views: on new data, state or IP changes, and each minute so
  // past events drop off.
  static uint32_t seenCalendar = UINT32_MAX;
  static CalState lastCalState = CalState::Loading;
  static bool lastHadIp = false;
  bool hasIp = st.wifi == WifiState::Connected && st.ip[0];
  bool dayChanged = timeKnown && t.tm_yday != lastDay;
  if (st.calendarVersion != seenCalendar || st.calendar != lastCalState || hasIp != lastHadIp || minuteChanged)
  {
    seenCalendar = st.calendarVersion;
    lastCalState = st.calendar;
    lastHadIp = hasIp;
    calCount = netGetEvents(calEvents, MAX_EVENTS);
    updateHomeEvents(st);
    if (dayChanged)
    {
      lastDay = t.tm_yday;
      calSel = today();
    }
    renderCalendar();
  }

  static uint32_t seenHoroscopes = UINT32_MAX;
  static bool starsHadIp = false;
  if (st.horoscopeVersion != seenHoroscopes || hasIp != starsHadIp || dayChanged)
  {
    seenHoroscopes = st.horoscopeVersion;
    starsHadIp = hasIp;
    updateStars(st);
  }

  int page = lv_obj_get_index(lv_tileview_get_tile_act(tiles));
  if (timeKnown && page == PAGE_CLOCK && lv_scr_act() == scrMain)
  {
    lv_meter_set_indicator_value(meter, needleHour, (t.tm_hour % 12) * 5 + t.tm_min / 12);
    lv_meter_set_indicator_value(meter, needleMin, t.tm_min);
    lv_meter_set_indicator_value(meter, needleSec, t.tm_sec);
  }

  updateWifi(st);
  updateBoot(st, timeKnown);
  if (timeKnown)
    checkNotifications(lastWeather, night);

  // Daytime idle: show the logo. Only a tap or a notification wakes it.
  if (!night && lv_scr_act() == scrMain && lv_disp_get_inactive_time(nullptr) > IDLE_MS)
    fadeTo(scrIdle, 800);
  powerUpdate(night);
}
