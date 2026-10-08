#include "board.h"
#include "config.h"
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

namespace board {
namespace {

constexpr int     PIN_SDA = 15, PIN_SCL = 16;
constexpr int     PIN_TOUCH_INT = 1;            // GT911 INT, also selects its I2C address at reset
constexpr uint8_t ADDR_STC8 = 0x30, ADDR_IOEX = 0x18, ADDR_GT_A = 0x5D, ADDR_GT_B = 0x14, ADDR_RTC = 0x51;
constexpr uint16_t H_RES = 800, V_RES = 480;

// STC8 commands (V1.3+): 0 = brightest ... 244 = dimmest, 245 = off, 246/247 buzzer, 250 touch on
constexpr uint8_t STC_V13_OFF = 245, STC_V13_BUZZ_ON = 246, STC_V13_BUZZ_OFF = 247, STC_V13_TOUCH_ON = 250;
// STC8 commands (V1.2): 0x10 = on/max, 0x06..0x09 dim steps, 0x05 = off, 0x19 touch on
constexpr uint8_t STC_V12_MAX = 0x10, STC_V12_OFF = 0x05, STC_V12_TOUCH_ON = 0x19;

ProbeReport rep = {};
uint8_t  rev = BOARD_REV;
uint8_t  blPct = 0, blBeforeSleep = UI_DEFAULT_BACKLIGHT;
bool     sleeping = false, clicksOn = false, swallowTouch = false;
uint32_t beepOffAt = 0;
uint8_t  ioexOut = 0;

esp_lcd_panel_handle_t panel = nullptr;
lv_display_t *disp = nullptr;
lv_indev_t   *indev = nullptr;

bool writeByte(uint8_t addr, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}
bool writeReg(uint8_t addr, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

// ---- V1.0 IO expander (TCA9534 / PCA9557 share the register map) ----------
// bit1 = backlight, bit2 = touch reset, bit3/bit4 = audio amp control
void ioexSet(uint8_t bit, bool level) {
  ioexOut = level ? (ioexOut | (1 << bit)) : (ioexOut & ~(1 << bit));
  writeReg(ADDR_IOEX, 0x01, ioexOut);
}

// ---- GT911 ----------------------------------------------------------------
bool gtRead(uint16_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(rep.touchAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)rep.touchAddr, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
bool gtWrite(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(rep.touchAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

void pulseTouchInt() {  // INT low during reset -> GT911 comes up at 0x5D
  pinMode(PIN_TOUCH_INT, OUTPUT);
  digitalWrite(PIN_TOUCH_INT, LOW);
  delay(120);
  pinMode(PIN_TOUCH_INT, INPUT);
  delay(100);
}

void resetTouch() {
  if (rev == 10) {
    ioexSet(2, false);
    pinMode(PIN_TOUCH_INT, OUTPUT);
    digitalWrite(PIN_TOUCH_INT, LOW);
    delay(20);
    ioexSet(2, true);
    delay(100);
    pinMode(PIN_TOUCH_INT, INPUT);
  } else {
    writeByte(ADDR_STC8, rev == 12 ? STC_V12_TOUCH_ON : STC_V13_TOUCH_ON);
    pulseTouchInt();
  }
}

bool probeTouch() {
  if (i2cProbe(ADDR_GT_A)) { rep.touchAddr = ADDR_GT_A; return true; }
  if (i2cProbe(ADDR_GT_B)) { rep.touchAddr = ADDR_GT_B; return true; }
  return false;
}

// ---- LVGL glue --------------------------------------------------------------
void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  // RGB panel: this is a cache-coherent copy into the PSRAM frame buffer
  esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
  lv_display_flush_ready(d);
}

void touchCb(lv_indev_t *, lv_indev_data_t *data) {
  static int16_t x = 0, y = 0;
  static bool pressed = false;
  uint8_t st = 0;
  if (rep.touch && gtRead(0x814E, &st, 1) && (st & 0x80)) {
    uint8_t n = st & 0x0F;
    uint8_t b[4];
    if (n >= 1 && n <= 5 && gtRead(0x8150, b, 4)) {
      x = (int16_t)(b[0] | (b[1] << 8));
      y = (int16_t)(b[2] | (b[3] << 8));
      if (x >= H_RES) x = H_RES - 1;
      if (y >= V_RES) y = V_RES - 1;
      if (!pressed) {
        if (sleeping) { wake(); swallowTouch = true; }       // wake touch never clicks a button
        else if (clicksOn) beep(6);
      }
      pressed = true;
    } else {
      pressed = false;
      swallowTouch = false;
    }
    gtWrite(0x814E, 0);
  }
  data->point.x = x;
  data->point.y = y;
  data->state = (pressed && !swallowTouch) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

uint8_t revision() { return rev; }
const ProbeReport &probe() { return rep; }

bool begin() {
  pinMode(19, OUTPUT);  // as in every Elecrow example for this board
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  delay(50);

  rep.ioExpander = i2cProbe(ADDR_IOEX);
  rep.helperMcu = i2cProbe(ADDR_STC8);
  if (rep.ioExpander && !rep.helperMcu) rev = 10;  // V1.0 boards are recognisable
  else if (rev == 10) rev = 13;                     // asked for V1.0 but board has the STC8

  if (rev == 10) {
    writeReg(ADDR_IOEX, 0x03, 0xE1);  // IO1..IO4 outputs
    ioexOut = 0;
    ioexSet(1, false);                // backlight off for now
    ioexSet(3, false);
    ioexSet(4, true);
  } else {
    // keep the screen dark until the boot animation fades it in
    writeByte(ADDR_STC8, rev == 12 ? STC_V12_OFF : STC_V13_OFF);
  }

  for (int i = 0; i < 8 && !probeTouch(); i++) {
    resetTouch();
    if (rev != 10) rep.helperMcu = i2cProbe(ADDR_STC8);
  }
  rep.touch = probeTouch();
  rep.rtc = i2cProbe(ADDR_RTC);

  // ---- RGB panel ------------------------------------------------------------
  esp_lcd_rgb_panel_config_t c = {};
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = LCD_PCLK_HZ;
  c.timings.h_res = H_RES;
  c.timings.v_res = V_RES;
  c.timings.hsync_pulse_width = 4;
  c.timings.hsync_back_porch = 8;
  c.timings.hsync_front_porch = 8;
  c.timings.vsync_pulse_width = 4;
  c.timings.vsync_back_porch = 8;
  c.timings.vsync_front_porch = 8;
  c.timings.flags.pclk_active_neg = 1;
  c.data_width = 16;
  c.bits_per_pixel = 16;
  c.num_fbs = 1;
  c.bounce_buffer_size_px = H_RES * LCD_BOUNCE_LINES;
  c.hsync_gpio_num = 40;
  c.vsync_gpio_num = 41;
  c.de_gpio_num = 42;
  c.pclk_gpio_num = 39;
  c.disp_gpio_num = -1;
  const int data[16] = {21, 47, 48, 45, 38,       // B0..B4
                        9, 10, 11, 12, 13, 14,    // G0..G5
                        7, 17, 18, 3, 46};        // R0..R4
  for (int i = 0; i < 16; i++) c.data_gpio_nums[i] = data[i];
  c.flags.fb_in_psram = 1;

  if (esp_lcd_new_rgb_panel(&c, &panel) != ESP_OK) return false;
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);

  // clear the frame buffer to black
  void *fb = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) == ESP_OK && fb) memset(fb, 0, H_RES * V_RES * 2);
  return true;
}

void attachLvgl() {
  const size_t bytes = H_RES * LVGL_BUF_LINES * 2;
  void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!b1 || !b2) {  // fall back to PSRAM (slower, but it boots)
    if (b1) heap_caps_free(b1);
    if (b2) heap_caps_free(b2);
    b1 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    b2 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  }
  disp = lv_display_create(H_RES, V_RES);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
}

void setBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  blPct = pct;
  if (rev == 10) {
    ioexSet(1, pct > 0);
  } else if (rev == 12) {
    if (pct == 0) { writeByte(ADDR_STC8, STC_V12_OFF); return; }
    writeByte(ADDR_STC8, STC_V12_MAX);              // must be "on" before dimming
    if (pct < 88) writeByte(ADDR_STC8, pct >= 63 ? 0x09 : pct >= 38 ? 0x08 : pct >= 13 ? 0x07 : 0x06);
  } else {
    writeByte(ADDR_STC8, pct == 0 ? STC_V13_OFF : (uint8_t)((100 - pct) * 244 / 100));
  }
}
uint8_t backlight() { return blPct; }

void sleep() {
  if (sleeping) return;
  blBeforeSleep = blPct ? blPct : UI_DEFAULT_BACKLIGHT;
  sleeping = true;
  setBacklight(0);
}
bool asleep() { return sleeping; }
void wake() {
  if (!sleeping) return;
  sleeping = false;
  setBacklight(blBeforeSleep);
  if (disp) lv_display_trigger_activity(disp);
}

void beep(uint16_t ms) {
  if (rev < 13) return;
  writeByte(ADDR_STC8, STC_V13_BUZZ_ON);
  beepOffAt = millis() + ms;
  if (!beepOffAt) beepOffAt = 1;
}
void setClicks(bool on) { clicksOn = on; }
bool clicks() { return clicksOn; }

void loop() {
  if (beepOffAt && (int32_t)(millis() - beepOffAt) >= 0) {
    writeByte(ADDR_STC8, STC_V13_BUZZ_OFF);
    beepOffAt = 0;
  }
}

}  // namespace board
