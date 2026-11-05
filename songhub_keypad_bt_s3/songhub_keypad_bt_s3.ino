// SongHub Bluetooth keypad - ESP32-S3 board with 2.8" SPI display + FocalTech capacitive touch
// Acts as a Bluetooth LE keyboard for an Android phone / tablet / TV. The buttons are drawn on the touch screen.
// Libraries: "Adafruit GFX Library", "Adafruit ILI9341" (or "Adafruit ST7735 and ST7789 Library" if DISPLAY_DRIVER 1).
// BLE is built into esp32 core 3.x.
// The BLE library's NimBLE backend refuses a second characteristic with the same UUID, but HID needs one input report
// characteristic (UUID 0x2A4D) per report id. We add the media-key one ourselves, which needs access to the service's map.
#define private public
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#undef private
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "config.h"
#include "images.h"
#if DISPLAY_DRIVER == 0
#include <Adafruit_ILI9341.h>
Adafruit_ILI9341 tft(&SPI, TFT_DC, TFT_CS, TFT_RST);
#else
#include <Adafruit_ST7789.h>
Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
#endif

// Screen layout (portrait 240x320):  1 2 3
//                                    4 5 6
//                                    7 8 9
//                                    Cancel 0 Melody
//                                    Play Next Stop
//                                    Tab  Reserve/Enter (2 cells wide; says "Enter" while no number is typed)
// Cancel: with a number typed, tap = erase last digit, hold = clear all.
#define ROWS 6
#define COLS 3
struct Btn { char id; const char* label; uint16_t color; };
static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
static const uint16_t C_BG = rgb(21, 27, 35), C_NUM = rgb(48, 60, 84), C_TEXT = rgb(255, 255, 255),
                      C_DIM = rgb(120, 130, 150), C_OK = rgb(80, 220, 140), C_WARN = rgb(255, 190, 60),
                      C_ACCENT = rgb(110, 150, 255);
static const Btn BTNS[ROWS][COLS] = {
  {{'1', "1", C_NUM}, {'2', "2", C_NUM}, {'3', "3", C_NUM}},
  {{'4', "4", C_NUM}, {'5', "5", C_NUM}, {'6', "6", C_NUM}},
  {{'7', "7", C_NUM}, {'8', "8", C_NUM}, {'9', "9", C_NUM}},
  {{'C', "Cancel", rgb(200, 120, 30)}, {'0', "0", C_NUM}, {'M', "Melody", rgb(130, 70, 190)}},
  {{'P', "Play", rgb(30, 130, 90)}, {'N', "Next", rgb(40, 100, 190)}, {'S', "Stop", rgb(180, 50, 60)}},
  {{'T', "Tab", rgb(90, 90, 110)}, {'R', "Reserve", rgb(30, 150, 100)}, {0, "", 0}},   // Tab = 1 cell, Reserve/Enter = 2 cells
};
#define SW 240
#define SH 320
#define GX0 2      // grid origin; cells are 80x41, buttons 76x37 inside them
#define GY0 74
#define CELL_W 80
#define CELL_H 41
#define BTN_W 76
#define BTN_H 37
#define HDR_H GY0

static const char* MELODY_NAMES[] = {"Off", "20", "40", "60", "80", "Max"};

// ---------- HID: report 1 = keyboard (8 bytes), report 2 = consumer/media key (16-bit usage) ----------
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,        // Keyboard, report id 1
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x08, 0x81, 0x02,                    //   modifier bits
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                    //   reserved byte
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,        //   6 key slots
  0xC0,
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,        // Consumer control, report id 2
  0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
  0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
  0xC0,
};

static BLEHIDDevice* hid;
static BLECharacteristic *kbInput, *ccInput;
static volatile bool linked = false;

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) { linked = true; }
  void onDisconnect(BLEServer*) {
    linked = false;
    BLEDevice::startAdvertising();   // stay discoverable so the phone can reconnect
  }
};

class SecCb : public BLESecurityCallbacks {
  bool onSecurityRequest() { return true; }
  uint32_t onPassKeyRequest() { return 0; }
  void onPassKeyNotify(uint32_t) {}
  bool onConfirmPIN(uint32_t) { return true; }
};

// Forget every paired phone (touch the screen while the boot logo is showing).
static void clearBonds() {
#if defined(CONFIG_BLUEDROID_ENABLED)
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return;
  esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * n);
  if (!list) return;
  if (esp_ble_get_bond_device_list(&n, list) == ESP_OK)
    for (int i = 0; i < n; i++) esp_ble_remove_bond_device(list[i].bd_addr);
  free(list);
#elif defined(CONFIG_NIMBLE_ENABLED)
  ble_store_clear();
#endif
}

static void sendKeyReport(uint8_t usage) {
  if (!kbInput || kbInput->getHandle() == 0xFFFF) return;   // not registered: skip rather than assert and reboot
  uint8_t r[8] = {0, 0, usage, 0, 0, 0, 0, 0};
  kbInput->setValue(r, sizeof r);
  kbInput->notify();
}

static void sendConsumerReport(uint16_t usage) {
  if (!ccInput || ccInput->getHandle() == 0xFFFF) return;
  uint8_t r[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  ccInput->setValue(r, sizeof r);
  ccInput->notify();
}

// Press and release one action. Returns false when no phone is connected.
static bool tap(const Action& a) {
  if (!linked) return false;
  if (a.consumer) { sendConsumerReport(a.code); delay(KEY_HOLD_MS); sendConsumerReport(0); }
  else            { sendKeyReport(a.code);      delay(KEY_HOLD_MS); sendKeyReport(0); }
  delay(KEY_GAP_MS);
  return true;
}

static uint8_t digitUsage(char d) { return d == '0' ? 0x27 : 0x1E + (d - '1'); }

// ---------- state ----------
static char entry[MAX_DIGITS + 1] = "";
static int melody = 0;             // our own count of the app's melody step; the app is the real owner
static char toast[24] = "";
static unsigned long toastUntil = 0;
static bool dirty = true;

static unsigned long promptAfter = 0;
static bool toastCenter = false;   // key messages (Reserve, Play/Pause, ...) are shown big in the middle of the header

static void say(const char* t, unsigned long ms = 1500) {
  strncpy(toast, t, sizeof toast - 1);
  toast[sizeof toast - 1] = 0;
  toastUntil = millis() + ms;
  promptAfter = toastUntil + PROMPT_DELAY_MS;   // "Enter song no." waits a moment after a message
  toastCenter = false;
  dirty = true;
}

// RGB LED: green = connected, blue slow blink = waiting to pair; blink(n) flashes it off (3 = failed, in red).
static void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(RGB_LED_PIN, r, g, b); }
static void blink(int n) {
  for (int i = 0; i < n; i++) {
    led(n >= 3 ? 40 : 0, 0, 0); delay(120);
    led(0, linked ? 25 : 0, linked ? 0 : 25); delay(120);
  }
}

// ---------- screen ----------
static void centerText(const char* s, int cx, int cy, const GFXfont* f, uint16_t col) {
  int16_t x1, y1; uint16_t w, h;
  tft.setFont(f);
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  tft.setTextColor(col);
  tft.setCursor(cx - (int)w / 2 - x1, cy - (int)h / 2 - y1);
  tft.print(s);
}

static uint16_t lighten(uint16_t c) {
  uint8_t r = c >> 11, g = (c >> 5) & 63, b = c & 31;
  return ((r + (31 - r) / 2) << 11) | ((g + (63 - g) / 2) << 5) | (b + (31 - b) / 2);
}

static void drawButton(int row, int col, bool pressed) {
  const Btn& b = BTNS[row][col];
  bool wide = b.id == 'R';                             // Reserve/Enter spans two cells
  int w = wide ? BTN_W + CELL_W : BTN_W;
  int x = GX0 + col * CELL_W, y = GY0 + row * CELL_H;
  tft.fillRoundRect(x, y, w, BTN_H, 8, pressed ? lighten(b.color) : b.color);
  bool digit = b.id >= '0' && b.id <= '9';
  const char* label = wide ? (*entry ? "Reserve" : "Enter") : b.label;
  centerText(label, x + w / 2, y + BTN_H / 2, digit ? &FreeSansBold18pt7b : &FreeSansBold9pt7b, C_TEXT);
}

static void drawGrid() {
  tft.fillScreen(C_BG);
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (BTNS[r][c].id) drawButton(r, c, false);
}

// ---------- battery ----------
static int batMv = 0, batPct = -1;
static bool batCharging = false;

static int pctFromMv(int mv) {                          // rough single-cell LiPo discharge curve
  static const int mvT[] = {3300, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4150};
  static const int pcT[] = {0, 5, 15, 30, 50, 65, 80, 93, 100};
  if (mv <= mvT[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (mv <= mvT[i]) return pcT[i - 1] + (pcT[i] - pcT[i - 1]) * (mv - mvT[i - 1]) / (mvT[i] - mvT[i - 1]);
  return 100;
}

static void updateBattery() {
  static unsigned long last = 0;
  static int hist[6], n = 0;                            // recent readings, to see whether the voltage is rising
  if (last && millis() - last < BAT_SAMPLE_MS) return;
  last = millis();
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  int mv = (int)(sum / 8 * BAT_DIVIDER);
  if (n == 6) { for (int i = 1; i < 6; i++) hist[i - 1] = hist[i]; n = 5; }
  hist[n++] = mv;
  bool rising = n == 6 && hist[5] - hist[0] >= 15;
  bool nowCharging = mv >= BAT_CHARGING_MV || rising;
  int pct = pctFromMv(batMv ? (batMv * 3 + mv) / 4 : mv);   // light smoothing
  batMv = batMv ? (batMv * 3 + mv) / 4 : mv;
  if (pct != batPct || nowCharging != batCharging) { batPct = pct; batCharging = nowCharging; dirty = true; }
}

// Battery icon with percent, top right of the header.
static void drawBattery() {
  if (batPct < 0) return;
  const int bx = SW - 34, by = 4, bw = 26, bh = 12;
  uint16_t col = batCharging ? C_OK : (batPct <= 15 ? rgb(255, 80, 70) : batPct <= 30 ? C_WARN : C_TEXT);
  tft.drawRect(bx, by, bw, bh, C_TEXT);
  tft.fillRect(bx + bw, by + 3, 3, bh - 6, C_TEXT);     // terminal nub
  int fill = (bw - 4) * batPct / 100;
  if (fill > 0) tft.fillRect(bx + 2, by + 2, fill, bh - 4, col);
  char s[8];
  snprintf(s, sizeof s, "%d%%", batPct);
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(col);
  tft.setCursor(bx - 6 * strlen(s) - 4, by + 2);
  tft.print(s);
  if (batCharging) {                                    // lightning bolt over the icon
    int cx = bx + bw / 2;
    tft.fillTriangle(cx + 2, by + 1, cx - 3, by + 7, cx, by + 7, C_BG);
    tft.fillTriangle(cx, by + 6, cx + 3, by + 6, cx - 2, by + 11, C_BG);
  }
}

static void drawBtIcon(int x0, int y0, uint16_t col) {   // 10x16 Bluetooth rune, drawn 2 px thick
  int cx = x0 + 5;
  for (int d = 0; d < 2; d++) {
    int c = cx + d;
    tft.drawLine(c, y0, c, y0 + 15, col);
    tft.drawLine(c, y0, c + 5, y0 + 4, col);
    tft.drawLine(c + 5, y0 + 4, x0 + d, y0 + 11, col);
    tft.drawLine(c, y0 + 15, c + 5, y0 + 11, col);
    tft.drawLine(c + 5, y0 + 11, x0 + d, y0 + 4, col);
  }
}

// Header (top 72 px): logo background, Bluetooth icon top left, battery top right,
// and in the middle either the typed number, a Reserve message, or the flashing "Enter song no." prompt.
static const int PROMPT_Y = 38, PROMPT_X0 = 40, PROMPT_X1 = 200, PROMPT_ROW0 = 22, PROMPT_ROW1 = 56;

static bool promptShowing() { return !*entry && millis() >= toastUntil && millis() >= promptAfter; }

// Only the flashing prompt is redrawn on each blink (restoring the logo behind it), so the icons never flicker.
static void drawPrompt() {
  if (toastCenter && millis() < toastUntil) return;   // never wipe the Reserve message box
  for (int y = PROMPT_ROW0; y < PROMPT_ROW1; y++)
    tft.drawRGBBitmap(PROMPT_X0, y, HDR_IMG + y * HDR_IMG_W + PROMPT_X0, PROMPT_X1 - PROMPT_X0, 1);
  if (promptShowing() && ((millis() / 500) & 1)) centerText("Enter song no.", SW / 2, PROMPT_Y, &FreeSansBold9pt7b, C_TEXT);
  tft.setFont();
}

static void drawHeader() {
  tft.drawRGBBitmap(0, 0, HDR_IMG, HDR_IMG_W, HDR_IMG_H);   // SongHub logo as the (dimmed) background
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextWrap(false);
  drawBtIcon(8, 4, linked ? C_OK : C_WARN);     // green = connected, amber = waiting to pair (steady)
  if (*entry) centerText(entry, SW / 2, PROMPT_Y, &FreeSansBold24pt7b, C_TEXT);
  else drawPrompt();
  bool showToast = millis() < toastUntil;
  if (showToast && toastCenter) {               // key message (Reserve, Play/Pause, Next...): a framed label in the centre, over the number
    int16_t x1, y1; uint16_t w, h;
    tft.setFont(&FreeSansBold9pt7b);
    tft.getTextBounds(toast, 0, 0, &x1, &y1, &w, &h);
    int pw = w + 28, ph = 34;
    tft.fillRoundRect(SW / 2 - pw / 2, PROMPT_Y - ph / 2, pw, ph, 10, C_BG);
    tft.drawRoundRect(SW / 2 - pw / 2, PROMPT_Y - ph / 2, pw, ph, 10, C_OK);
    centerText(toast, SW / 2, PROMPT_Y, &FreeSansBold9pt7b, C_OK);
  } else if (showToast) {                       // other short messages: small, next to the icon
    tft.setFont();
    tft.setTextColor(C_ACCENT);
    tft.setCursor(26, 8);
    tft.print(toast);
  }
  tft.setFont();
  drawBattery();
  dirty = false;
  static bool wasEmpty = true;                // the wide key reads "Enter" with no number, "Reserve" with one
  if ((*entry == 0) != wasEmpty) { wasEmpty = *entry == 0; drawButton(ROWS - 1, 1, false); }
}

// ---------- touch (FocalTech FT6x36 at I2C 0x38, polled) ----------
static bool readTouch(int& sx, int& sy) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);                                        // TD_STATUS, then point 1: XH XL YH YL
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)5) != 5) return false;
  uint8_t n = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (n == 0 || n > 2) return false;
  int rx = ((xh & 0x0F) << 8) | xl, ry = ((yh & 0x0F) << 8) | yl;
#if TOUCH_SWAP_XY
  sx = ry; sy = rx;
#else
  sx = rx; sy = ry;
#endif
#if TOUCH_FLIP_X
  sx = SW - 1 - sx;
#endif
#if TOUCH_FLIP_Y
  sy = SH - 1 - sy;
#endif
  sx = constrain(sx, 0, SW - 1);
  sy = constrain(sy, 0, SH - 1);
  return true;
}

static bool hitTest(int x, int y, int& row, int& col) {
  if (y < GY0 || x < GX0) return false;
  row = (y - GY0) / CELL_H; col = (x - GX0) / CELL_W;
  if (row >= ROWS) return false;
  if (row == ROWS - 1 && col == 2) col = 1;            // Reserve/Enter spans columns 1-2
  return col < COLS;
}

// ---------- buttons ----------
static void onButton(char k) {
  if (k >= '0' && k <= '9') {
    size_t len = strlen(entry);
    if (len >= MAX_DIGITS) { say("Max digits"); blink(3); return; }
    if (!linked) { say("Not connected"); blink(3); return; }
    sendKeyReport(digitUsage(k)); delay(KEY_HOLD_MS); sendKeyReport(0);
    entry[len] = k; entry[len + 1] = 0;
    dirty = true;
    return;
  }
  bool ok = true;
  const char* label = "";
  switch (k) {
    case 'P': ok = tap(ACT_PLAYPAUSE); label = "Play/Pause"; break;
    case 'S': ok = tap(ACT_STOP);      label = "Stop"; break;
    case 'N': ok = tap(ACT_NEXT);      label = "Next"; break;
    case 'T': ok = tap(ACT_TAB);       label = "Tab"; break;
    case 'M':
      ok = tap(ACT_MELODY);
      if (ok) melody = (melody + 1) % 6;
      label = "Melody";
      break;
    case 'R': {
      // Always send Enter: with no number typed it selects the focused item.
      ok = tap(ACT_RESERVE);
      static char msg[24];
      if (*entry) snprintf(msg, sizeof msg, "Reserved %s", entry);
      else        snprintf(msg, sizeof msg, "Enter");
      label = msg;
      if (ok) entry[0] = 0;
      break;
    }
  }
  if (ok) say(label); else say("Not connected");
  if (ok) { toastCenter = true; toastUntil += RESERVE_EXTRA_MS; promptAfter += RESERVE_EXTRA_MS; }
  blink(ok ? 1 : 3);
}

// Cancel: tap = erase the last digit (Backspace). Hold = clear the whole number (Backspace held; Android auto-repeats it).
static void cancelTap() {
  if (!*entry) return;
  if (!linked) { say("Not connected"); return; }
  sendKeyReport(ACT_BACKSPACE.code);
  delay(BACKSPACE_TAP_MS);
  sendKeyReport(0);
  delay(KEY_GAP_MS);
  entry[strlen(entry) - 1] = 0;
  dirty = true;                                  // Cancel shows no message, only the number changing
}

static void cancelHold() {
  if (!*entry || !linked) return;
  sendKeyReport(ACT_BACKSPACE.code);
  delay(CANCEL_HOLD_MS);
  sendKeyReport(0);
  delay(KEY_GAP_MS);
  entry[0] = 0;
  dirty = true;
}

// Touch state machine. Digits and function buttons fire on touch-down; Cancel fires on release (tap) or after a hold.
// Sliding to another button cancels the press, so a stray drag never sends anything.
static void pollTouch() {
  static unsigned long lastPoll = 0, lastSeen = 0, downAt = 0;
  static int held = -1;          // button index row*4+col being pressed, -1 none
  static bool locked = false;    // finger slid off its button: ignore until lifted
  static bool holdFired = false;
  if (millis() - lastPoll < 15) return;
  lastPoll = millis();

  int x, y, r = 0, c = 0;
  bool down = readTouch(x, y);
  if (down) lastSeen = millis();
  else if (millis() - lastSeen < 40) down = held >= 0 || locked;   // bridge one-or-two dropped reports
  bool hit = down && hitTest(x, y, r, c);
  int idx = hit ? r * COLS + c : -1;

  if (!down) {                                       // finger lifted
    if (held >= 0) {
      drawButton(held / COLS, held % COLS, false);
      if (BTNS[held / COLS][held % COLS].id == 'C' && !holdFired) cancelTap();
    }
    held = -1; locked = false; holdFired = false;
    return;
  }
  if (locked) return;
  if (held < 0) {                                    // new touch
    if (idx < 0) { locked = true; return; }
    held = idx; downAt = millis(); holdFired = false;
    drawButton(r, c, true);
    char id = BTNS[r][c].id;
    if (id != 'C') onButton(id);
  } else if (idx >= 0 && idx != held) {              // slid onto another button: abandon the press
    drawButton(held / COLS, held % COLS, false);
    held = -1; locked = true;
  } else if (BTNS[held / COLS][held % COLS].id == 'C' && !holdFired && millis() - downAt > CANCEL_HOLD_AFTER_MS) {
    holdFired = true;
    cancelHold();
  }
}

static void touchInit() {
  pinMode(TOUCH_INT, INPUT);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW); delay(10);
  digitalWrite(TOUCH_RST, HIGH); delay(300);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);
}

void setup() {
  Serial.begin(115200);
  pinMode(AUDIO_EN_PIN, OUTPUT);
  digitalWrite(AUDIO_EN_PIN, LOW);                   // amplifier off
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, TFT_BLK_ON);
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
#if DISPLAY_DRIVER == 0
  tft.begin(40000000);
#else
  tft.init(240, 320);
  tft.setSPISpeed(40000000);
#endif
  tft.invertDisplay(TFT_INVERT);
  tft.setRotation(TFT_ROTATION);
  touchInit();
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);    // up to ~3.1 V at the pin (battery 4.2 V / 2 = 2.1 V)

  // Boot picture; touching the screen in the first seconds forgets all paired phones.
  tft.setRotation(BOOT_ROTATION);               // boot picture is landscape 320x240
  tft.drawRGBBitmap(0, 0, BOOT_IMG, BOOT_IMG_W, BOOT_IMG_H);
  unsigned long bootStart = millis();
  bool forgetPairings = false;
  int x, y;
  while (millis() - bootStart < FORGET_WINDOW_MS) {
    if (readTouch(x, y)) forgetPairings = true;
    delay(10);
  }

  BLEDevice::init(BT_DEVICE_NAME);
  if (forgetPairings) { clearBonds(); say("Pairings cleared", 3000); }

  BLESecurity* sec = new BLESecurity();
  sec->setCapability(ESP_IO_CAP_NONE);          // "Just Works" pairing, no PIN
  sec->setAuthenticationMode(true, false, true);  // bond, no MITM, secure connections
  BLEDevice::setSecurityCallbacks(new SecCb());

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());

  hid = new BLEHIDDevice(server);
  hid->manufacturer()->setValue("SongHub DIY");
  hid->pnp(0x02, 0x05ac, 0x820a, 0x0110);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t*)REPORT_MAP, sizeof REPORT_MAP);
  kbInput = hid->inputReport(1);                 // keyboard report; the library registers this one
  // Media-key report (id 2): built by hand and put straight into the HID service's characteristic map.
  ccInput = new BLECharacteristic(BLEUUID((uint16_t)0x2a4d), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  BLEDescriptor* ccRef = new BLEDescriptor(BLEUUID((uint16_t)0x2908));   // Report Reference: id 2, type input
  const uint8_t ccRefVal[] = {0x02, 0x01};
  ccRef->setValue((uint8_t*)ccRefVal, sizeof ccRefVal);
  ccInput->addDescriptor(ccRef);
  hid->hidService()->m_characteristicMap.setByUUID(ccInput, ccInput->getUUID());
  hid->setBatteryLevel(100);
  hid->startServices();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  while (millis() - bootStart < BOOT_SCREEN_MS) {   // keep the boot picture up; the LED shows the pairing state meanwhile
    led(0, linked ? 25 : 0, linked ? 0 : ((millis() / 500) & 1) * 25);
    delay(50);
  }
  tft.setRotation(TFT_ROTATION);
  drawGrid();
  dirty = true;
}

void loop() {
  pollTouch();
  updateBattery();

  // A phone that disconnects mid-entry leaves a stale number; start clean on the next connection.
  static bool wasLinked = false;
  if (linked != wasLinked) {
    wasLinked = linked;
    if (!linked) entry[0] = 0;
    say(linked ? "Connected" : "Disconnected");
  }

  // LED: green = connected, slow blue blink = waiting to be paired.
  static int lastLed = -1;
  int want = linked ? 2 : ((millis() / 500) & 1);
  if (want != lastLed) { lastLed = want; led(0, want == 2 ? 25 : 0, want == 1 ? 25 : 0); }

  static int lastPhase = -1;                    // 500 ms flash of "Enter song no." (only that text is redrawn)
  int phase = (millis() / 500) & 1;
  if (phase != lastPhase) { lastPhase = phase; if (!*entry && !dirty && millis() >= toastUntil) drawPrompt(); }
  if (toastUntil && millis() > toastUntil) { toastUntil = 0; dirty = true; }
  static unsigned long lastDraw = 0;
  if (dirty && millis() - lastDraw > 60) { drawHeader(); lastDraw = millis(); }
}
