#pragma once

// Seeed XIAO ESP32-S3 -> Waveshare 13.3" e-Paper HAT+ (E)
// GPIO numbers (the XIAO silkscreen label is in the comment).
#define PIN_BUTTON    1   // D0  push button to GND (RTC pin, wakes from deep sleep)
#define PIN_EPD_CS_M  2   // D1  -> CS_M
#define PIN_EPD_DC    3   // D2  -> DC
#define PIN_BATTERY   4   // D3  battery divider midpoint (1M / 1M from BAT+ to GND)
#define PIN_EPD_BUSY  5   // D4  -> BUSY
#define PIN_EPD_PWR   6   // D5  -> PWR (panel power switch on the HAT)
#define PIN_EPD_CS_S  43  // D6  -> CS_S
#define PIN_EPD_RST   44  // D7  -> RST
#define PIN_EPD_SCK   7   // D8  -> CLK
#define PIN_CONFIG    8   // D9  config toggle switch to GND (RTC pin; LED is wired to the switch)
#define PIN_EPD_MOSI  9   // D10 -> DIN
