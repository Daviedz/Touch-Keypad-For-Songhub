#pragma once
// ---------- Bluetooth ----------
#define BT_DEVICE_NAME "SongHub Keypad S3"   // name shown in the phone's Bluetooth list

// ---------- What each button sends ----------
// The keypad appears to Android as an ordinary Bluetooth keyboard. Each action is either
//   {false, <USB HID keyboard usage>}    a normal key   (usage table: "Keyboard/Keypad Page", 0x07)
//   {true,  <USB HID consumer usage>}    a media key    (usage table: "Consumer Page", 0x0C)
struct Action { bool consumer; uint16_t code; };

#define ACT_PLAYPAUSE  Action{true,  0x00CD}   // KEYCODE_MEDIA_PLAY_PAUSE
#define ACT_STOP       Action{true,  0x00B7}   // KEYCODE_MEDIA_STOP
#define ACT_NEXT       Action{true,  0x00B5}   // KEYCODE_MEDIA_NEXT
#define ACT_RESERVE    Action{false, 0x28}     // Enter
#define ACT_MELODY     Action{false, 0x3B}     // FN+F2  (app steps Off,20,40,60,80,Max,Off itself)
#define ACT_BACKSPACE  Action{false, 0x2A}     // Cancel erases the typed digits with this key
#define ACT_TAB        Action{false, 0x2B}     // Tab key
// Digits 0-9 are always sent as the normal number keys.

#define MAX_DIGITS 6                // longest song number
#define KEY_HOLD_MS 20              // how long a key stays "down" (longer made the app type a digit twice)
#define BACKSPACE_TAP_MS 100        // Cancel tap: Backspace stays down this long
#define CANCEL_HOLD_AFTER_MS 800    // hold Cancel this long to clear the whole number
#define CANCEL_HOLD_MS 1500         // ...by holding Backspace this long; the app auto-repeats it
#define KEY_GAP_MS  80              // pause after each key

#define BOOT_SCREEN_MS 5000         // how long the landscape boot picture stays; Bluetooth is already advertising meanwhile
#define BOOT_ROTATION 1             // boot picture orientation (landscape); use 3 if it shows upside down
#define PROMPT_DELAY_MS 2000        // "Enter song no." waits this long after a message (e.g. Reserved) before flashing again
#define RESERVE_EXTRA_MS 3000       // the Reserved/Enter message stays this much longer than other messages
#define FORGET_WINDOW_MS 3000       // touch the screen during the first seconds of boot to forget paired phones

// ---------- Display (SPI, on the board) ----------
#define TFT_CS   10
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO 13
#define TFT_DC   46
#define TFT_BLK  45                 // backlight
#define TFT_BLK_ON HIGH             // level that turns the backlight on
#define TFT_RST  -1                 // no reset pin listed for this board
#define DISPLAY_DRIVER 0            // 0 = ILI9341 240x320, 1 = ST7789 240x320 (change if the colours/picture are wrong)
#define TFT_ROTATION 0              // 0 = portrait 240x320; 2 = portrait upside down
#define TFT_INVERT true             // this panel shows inverted colours without it

// ---------- Capacitive touch (FocalTech, I2C 0x38) ----------
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_INT 17
#define TOUCH_RST 18
#define TOUCH_ADDR 0x38
// If a tap lands on the wrong button, change these (raw touch is portrait 240x320, screen is portrait too):
#define TOUCH_SWAP_XY 0
#define TOUCH_FLIP_X  0
#define TOUCH_FLIP_Y  0

// ---------- Other on-board parts ----------
#define AUDIO_EN_PIN 1              // audio amplifier enable; kept LOW (no sound used)
// ---------- Battery (schematic: BAT+ -> 200k/200k divider -> BAT_ADC, so the pin sees half the battery voltage) ----------
// The TP4054 CHRG pin is not shown going to the ESP32, so "charging" is guessed from the voltage rising.
#define BAT_ADC_PIN 9               // BAT_ADC net goes to GPIO9 (ESP32-S3R8 pin 14, per the schematic)
#define BAT_DIVIDER 2.0f            // 200k + 200k
#define BAT_EMPTY_MV 3300           // 0 %
#define BAT_FULL_MV  4150           // 100 %
#define BAT_CHARGING_MV 4250        // above this the pack is clearly being fed by the charger
#define BAT_SAMPLE_MS 3000          // how often the battery is measured

#define RGB_LED_PIN  42           // on-board RGB LED: blue blink = waiting to pair, green = connected
