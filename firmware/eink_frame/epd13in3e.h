#pragma once
#include <Arduino.h>

// Waveshare 13.3" E Ink Spectra 6 (E6) panel, 1200 x 1600, 4 bits per pixel.
// Ported from Waveshare's ESP32-S3-ePaper-13.3E6 driver.
#define EPD_WIDTH        1200
#define EPD_HEIGHT       1600
#define EPD_FRAME_BYTES  (EPD_WIDTH * EPD_HEIGHT / 2)  // 960,000

namespace epd {

// Configure pins and SPI. Call once after wake.
void begin();
// Power the panel and send the init sequence. Returns false on BUSY timeout.
bool init();
// Send a full 960,000-byte frame (row-major, 600 bytes per row, left pixel in
// the high nibble) and refresh. Takes about 20 s. Returns false on timeout.
bool display(const uint8_t *frame);
// Fill the whole panel with one colour code (0 black, 1 white, 2 yellow,
// 3 red, 5 blue, 6 green) and refresh.
bool clear(uint8_t colour);
// Six colour bands (black, white, yellow, red, blue, green) for wiring tests.
bool testPattern();
// Interactive wiring check over the Serial Monitor: powers the panel and
// prints the BUSY line live (ready / busy / not connected) until 'q'.
void wiringTest();
// Put the controller to sleep and cut panel power.
void sleep();
// Why the last init/display/clear failed, in words (empty after success).
const char *lastError();
// How long the last full refresh took. A real one takes about 19 s.
uint32_t lastRefreshMs();

}  // namespace epd
