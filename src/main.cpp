// Wall panel for the Guition ESP32-S3-4848S040.
// Hardware setup (RGB panel, GT911 touch) and the LVGL loop live here;
// screens are in ui.cpp and networking in net.cpp.
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "net.h"
#include "power.h"
#include "ui.h"

// Display pins from GFX Library's ESP32_4848S040_86BOX_GUITION device
// (the backlight, GPIO38, is driven in power.cpp).
#define PIN_TOUCH_SDA 19
#define PIN_TOUCH_SCL 45
#define GT911_ADDR 0x5D

#define SCREEN_W 480
#define SCREEN_H 480
#define DRAW_BUF_LINES 60

Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */, GFX_NOT_DEFINED /* MISO */);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_W, SCREEN_H, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    bus, GFX_NOT_DEFINED /* RST */, st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

// ---- touch ----

static bool gt911Read(uint16_t reg, uint8_t *buf, uint8_t len)
{
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0)
    return false;
  if (Wire.requestFrom((uint8_t)GT911_ADDR, len) != len)
    return false;
  for (uint8_t i = 0; i < len; i++)
    buf[i] = Wire.read();
  return true;
}

static void gt911Write(uint16_t reg, uint8_t val)
{
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(val);
  Wire.endTransmission();
}

static void touchRead(lv_indev_drv_t *, lv_indev_data_t *data)
{
  static lv_coord_t lastX = 0, lastY = 0;
  static bool pressed = false;
  uint8_t status;
  if (gt911Read(0x814E, &status, 1) && (status & 0x80))
  {
    pressed = (status & 0x0F) > 0;
    if (pressed)
    {
      uint8_t p[4];
      if (gt911Read(0x8150, p, 4))
      {
        lastX = p[0] | (p[1] << 8);
        lastY = p[2] | (p[3] << 8);
      }
    }
    gt911Write(0x814E, 0);
  }
  data->point.x = lastX;
  data->point.y = lastY;
  if (powerOnTouch(pressed))
    data->state = LV_INDEV_STATE_REL;
  else
    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

// ---- display ----

static void dispFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels)
{
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&pixels->full, w, h);
  lv_disp_flush_ready(drv);
}

void setup()
{
  Serial.begin(115200);
  delay(100);
  Serial.println("\nwall_panel");

  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);

  if (!gfx->begin())
    Serial.println("gfx->begin() failed");
  gfx->fillScreen(RGB565_BLACK);
  powerBegin();

  lv_init();
  static lv_disp_draw_buf_t drawBuf;
  size_t bufPixels = SCREEN_W * DRAW_BUF_LINES;
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, bufPixels);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_W;
  dispDrv.ver_res = SCREEN_H;
  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchRead;
  lv_indev_drv_register(&indevDrv);

  netBegin();
  uiBegin();
  Serial.printf("ui up, psram free %u, heap free %u\n", ESP.getFreePsram(), ESP.getFreeHeap());
}

// Serial debug commands: "shot" dumps the framebuffer (read it with
// tools/screenshot.py), "page N" switches page, "idle" shows the logo.
static void serialCommands()
{
  if (!Serial.available())
    return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd == "shot")
  {
    uint16_t *fb = gfx->getFramebuffer();
    Serial.printf("SHOT %d %d\n", SCREEN_W, SCREEN_H);
    Serial.write((const uint8_t *)fb, SCREEN_W * SCREEN_H * 2);
    Serial.print("\nEND\n");
  }
  else if (cmd.startsWith("page "))
  {
    uiShowPage(cmd.substring(5).toInt());
  }
  else if (cmd == "idle")
  {
    uiShowIdle();
  }
}

void loop()
{
  serialCommands();
  lv_timer_handler();
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate > 500)
  {
    lastUpdate = millis();
    uiUpdate();
  }
  delay(5);
}
