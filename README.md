# SongHub Bluetooth Keypad - ESP32-S3 touch screen version

Portrait touch-screen keypad for the ESP32-S3 board with the built-in 2.8" SPI display and FocalTech capacitive touch. It shows up on the
phone as a Bluetooth LE keyboard. No matrix keypad is needed. Built with esp32 core 3.3.7 (28% of the Huge APP partition).

## Screen (portrait 240x320)
```
 [BT icon]  [ SongHub logo background ]  [battery]
            flashing "Enter song no." / typed number
   1       2       3
   4       5       6
   7       8       9
 Cancel    0    Melody
  Play    Next    Stop
 Tab   Reserve / Enter
```
- Digits 0-9 send the number keys (up to 6). The number is shown in the middle of the logo header; with no number the prompt "Enter song no." flashes.
- **Reserve / Enter** always sends Enter. It reads **Enter** while no number is typed and **Reserve** once digits are typed.
- **Tab** sends the Tab key.
- **Cancel**: tap erases the last digit, hold 0.8 s clears the whole number. No message is shown.
- Play/Pause, Next and Stop are media keys; Melody sends FN+F2 (the app steps Off, 20, 40, 60, 80, Max). Each press shows a framed label
  in the middle of the header for a few seconds; the prompt returns 2 s later.
- Sliding the finger off a button cancels the press.
- **Bluetooth icon** (top left): green = connected, amber = waiting to pair.
- **Battery** (top right): icon + percent from the BAT_ADC divider (200k/200k) on GPIO9. Green fill with a bolt = charging. Charging is guessed
  from the voltage rising or sitting above `BAT_CHARGING_MV`, because the TP4054 CHRG pin isn't wired to the ESP32.
- **Boot**: landscape SongHub picture for 5 s. Touch the screen in the first 3 s to forget all paired phones.
- RGB LED: blue blink = waiting to pair, green = connected, brief flash = key sent, red flashes = failed.

## Board pins (from the board's schematic)
Display SPI: CS 10, MOSI 11, SCK 12, MISO 13, DC 46, backlight 45. Touch (I2C 0x38): SDA 16, SCL 15, INT 17, RST 18.
RGB LED 42. Battery ADC GPIO9. Audio amp enable (GPIO 1) is held low. All set in `config.h`, along with the timings
(`BOOT_SCREEN_MS`, `PROMPT_DELAY_MS`, `RESERVE_EXTRA_MS`, ...).

## If something looks wrong (`config.h`)
- Colours are inverted in software for this panel (`TFT_INVERT`). Garbled picture: try `DISPLAY_DRIVER 1` (ST7789) instead of 0 (ILI9341).
- Picture upside down: `TFT_ROTATION 2` (then also toggle `TOUCH_FLIP_X` and `TOUCH_FLIP_Y`). Boot picture upside down: `BOOT_ROTATION 3`.
- A tap lands on the wrong button: change `TOUCH_SWAP_XY`, `TOUCH_FLIP_X`, `TOUCH_FLIP_Y`.
- Blank screen: change `TFT_BLK_ON` to `LOW`.

## Build & flash
1. Arduino IDE with **esp32 by Espressif** 3.x. Libraries: **Adafruit GFX**, **Adafruit ILI9341** (or Adafruit ST7735 and ST7789 for `DISPLAY_DRIVER 1`).
2. Board **ESP32S3 Dev Module**, USB CDC On Boot: Enabled, Flash Size 16MB, PSRAM: Disabled, Partition Scheme: Huge APP.
3. Open `songhub_keypad_bt_s3/songhub_keypad_bt_s3.ino`, select the port and upload.

Or flash the prebuilt `firmware/songhub_keypad_bt_s3.merged.bin` at address `0x0` (Espressif Flash Download Tool or `esptool.py write_flash 0x0 ...`).

Pair from the phone's Bluetooth settings: **SongHub Keypad S3**, no PIN.

## Note: media-key report
On esp32 core 3.x the NimBLE backend refuses a second HID report characteristic (same UUID 0x2A4D). The sketch adds the media-key report
(id 2) by hand next to the library's keyboard report (id 1). After changing the report layout, **remove "SongHub Keypad S3" from the phone's
Bluetooth list and pair again**; Android caches the key layout and ignores the media keys until it is re-paired.
