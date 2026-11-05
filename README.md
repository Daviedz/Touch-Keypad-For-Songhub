# SongHub Bluetooth Keypad - ESP32-S3 touch screen version

Portrait touch-screen port of the SongHub Bluetooth keypad for the ESP32-S3 board with the built-in 2.8" SPI display and FocalTech
capacitive touch. It shows up on the phone as a Bluetooth LE keyboard, exactly like the button versions. No matrix keypad is needed.

> Compiles (esp32 core 3.3.7, 22% of the Huge APP partition). Not yet checked on the screen: touch orientation and the display driver.

## Screen (portrait 240x320)
```
  Bluetooth state / toast / Melody level
  [ typed number ]
   1       2       3
   4       5       6
   7       8       9
 Cancel    0    Melody
  Play    Next    Stop
 Tab   Reserve / Enter
```
- **Tab** sends the Tab key. The wide key next to it always sends Enter: it reads **Enter** while no number is typed and **Reserve** once digits are typed.
- **Battery** (top right): icon + percent, read from the BAT_ADC divider (200k/200k) on `BAT_ADC_PIN`. A bolt/green fill means charging; this is
  guessed from the voltage rising or sitting above `BAT_CHARGING_MV`, because the TP4054 CHRG pin isn't wired to the ESP32 in the schematic.
  `BAT_ADC_PIN` is GPIO9 (confirmed from the schematic).
- Digits 0-9 send the number keys (up to 6). Reserve sends Enter (with a number typed it reserves that song).
- **Cancel**: tap erases the last digit, hold 0.8 s clears the whole number.
- Play/Pause, Next and Stop are media keys; Melody sends FN+F2 (the app steps Off, 20, 40, 60, 80, Max).
- Sliding the finger off a button cancels the press.
- To forget paired phones, touch the screen while the boot logo is showing.
- RGB LED: blue blink = waiting to pair, green = connected, brief flash = key sent, red flashes = failed.

## Board pins (from the board's pin table)
Display SPI: CS 10, MOSI 11, SCK 12, MISO 13, DC 46, backlight 45. Touch (I2C 0x38): SDA 16, SCL 15, INT 17, RST 18.
RGB LED 42. Audio amp enable (GPIO 1) is held low. All set in `config.h`.

## If something looks wrong (`config.h`)
- Colours are inverted in software for this panel (`TFT_INVERT`). Garbled picture: try `DISPLAY_DRIVER 1` (ST7789) instead of 0 (ILI9341).
- Picture upside down: `TFT_ROTATION 2` (then also toggle `TOUCH_FLIP_X` and `TOUCH_FLIP_Y`).
- A tap lands on the wrong button: change `TOUCH_SWAP_XY`, `TOUCH_FLIP_X`, `TOUCH_FLIP_Y`.
- Blank screen: change `TFT_BLK_ON` to `LOW`.

## Build & flash
1. Arduino IDE with **esp32 by Espressif** 3.x. Libraries: **Adafruit GFX**, **Adafruit ILI9341** (or Adafruit ST7735 and ST7789 for `DISPLAY_DRIVER 1`).
2. Board **ESP32S3 Dev Module**, USB CDC On Boot: Enabled, Flash Size 16MB, PSRAM: Disabled (not used), Partition Scheme: Huge APP.
3. Open `songhub_keypad_bt_s3/songhub_keypad_bt_s3.ino`, select the port and upload.

Pair from the phone's Bluetooth settings: **SongHub Keypad S3**, no PIN.

## Note: media-key report
On esp32 core 3.x the NimBLE backend refuses a second HID report characteristic (same UUID 0x2A4D), which made the media
keys crash the board. The sketch adds the media-key report (id 2) by hand next to the library's keyboard report (id 1).
After changing the report layout, **remove "SongHub Keypad S3" from the phone's Bluetooth list and pair again**; Android caches
the key layout and ignores the media keys until it is re-paired.
