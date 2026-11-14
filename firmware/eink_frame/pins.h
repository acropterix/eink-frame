#pragma once

// Pin map for the supported boards -> Waveshare 13.3" e-Paper HAT+ (E).
// Pick the board in Arduino IDE (Tools > Board); the right map is chosen here.
// LBL_* are the silkscreen labels, used in error messages and the wiring test.

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
// ---- Adafruit ESP32 Feather V2 (8 MB flash, 2 MB PSRAM) -------------------
// Avoids 12 (boot strapping), 13 (red LED), 14/15 (output PWM during boot)
// and 34/36/37/39 (input only, no pull resistors).
#define BOARD_NAME    "Adafruit ESP32 Feather V2"
#define PIN_BUTTON    26  // A0  push button to GND (RTC pin, wakes from deep sleep)
#define PIN_EPD_PWR   25  // A1  -> PWR (panel power switch on the HAT)
#define PIN_EPD_BUSY  4   // A5  -> BUSY
#define PIN_EPD_SCK   5   // SCK -> CLK
#define PIN_EPD_MOSI  19  // MO  -> DIN
#define PIN_EPD_RST   21  // MI  -> RST
#define PIN_EPD_DC    27  // 27  -> DC
#define PIN_EPD_CS_M  33  // 33  -> CS_M
#define PIN_EPD_CS_S  32  // 32  -> CS_S
#define PIN_BATTERY   35  // built-in battery monitor (200K / 200K divider)
#define PIN_CONFIG    -1  // no config switch: hold the button 3 s instead
#define PIN_BOARD_POWER 2 // NeoPixel + STEMMA QT regulator; kept off to save battery
#define BATTERY_MONITOR_BUILTIN 1

#define LBL_BUTTON "A0"
#define LBL_PWR    "A1"
#define LBL_BUSY   "A5"
#define LBL_SCK    "SCK"
#define LBL_MOSI   "MO"
#define LBL_RST    "MI"
#define LBL_DC     "27"
#define LBL_CS_M   "33"
#define LBL_CS_S   "32"
#define LBL_VCC    "3V"

#else
// ---- Seeed XIAO ESP32-S3 ----------------------------------------------------
#define BOARD_NAME    "Seeed XIAO ESP32-S3"
#define PIN_BUTTON    1   // D0  push button to GND (RTC pin, wakes from deep sleep)
#define PIN_EPD_CS_M  2   // D1  -> CS_M
#define PIN_EPD_DC    3   // D2  -> DC
#define PIN_BATTERY   4   // D3  battery divider midpoint (BAT+ / GND)
#define PIN_EPD_BUSY  5   // D4  -> BUSY
#define PIN_EPD_PWR   6   // D5  -> PWR (panel power switch on the HAT)
#define PIN_EPD_CS_S  43  // D6  -> CS_S
#define PIN_EPD_RST   44  // D7  -> RST
#define PIN_EPD_SCK   7   // D8  -> CLK
#define PIN_CONFIG    8   // D9  optional config toggle switch to GND (RTC pin)
#define PIN_EPD_MOSI  9   // D10 -> DIN
#define PIN_BOARD_POWER -1

#define LBL_BUTTON "D0"
#define LBL_PWR    "D5"
#define LBL_BUSY   "D4"
#define LBL_SCK    "D8"
#define LBL_MOSI   "D10"
#define LBL_RST    "D7"
#define LBL_DC     "D2"
#define LBL_CS_M   "D1"
#define LBL_CS_S   "D6"
#define LBL_VCC    "3V3"
#endif
