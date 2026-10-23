// Waveshare 13.3" Spectra 6 driver for the XIAO ESP32-S3.
// Init sequence and register values are taken verbatim from Waveshare's
// ESP32-S3-ePaper-13.3E6 example (epaper_port.c). The panel has two
// controllers: CS_M drives the left 600 columns and CS_S the right 600.

#include "epd13in3e.h"

#include <SPI.h>
#include <stdarg.h>

#include "config.h"
#include "pins.h"

// The panel sits at the end of ~15 cm jumper wires, not a PCB trace, so the
// clock is kept well below Waveshare's 10 MHz: faster edges ring and cross
// talk between neighbouring wires, flipping bits (stray yellow, leftovers of
// the previous picture). Override either in config.h if needed.
#ifndef EPD_SPI_HZ
#define EPD_SPI_HZ 2000000
#endif
// Pause after each row of image data, as Waveshare's driver does (1 ms).
#ifndef EPD_ROW_PAUSE_US
#define EPD_ROW_PAUSE_US 1000
#endif

namespace {

constexpr uint32_t kSpiHz = EPD_SPI_HZ;
constexpr uint32_t kBusyTimeoutMs = 60000;
// A real refresh takes ~19 s. Finishing much faster means BUSY isn't really
// connected (or is stuck high), so the panel most likely didn't change.
constexpr uint32_t kMinRefreshMs = 3000;

char errorText[192] = "";
uint32_t refreshMs = 0;

void fail(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(errorText, sizeof(errorText), fmt, args);
  va_end(args);
  Serial.printf("[epd] %s\n", errorText);
}
constexpr size_t kRowBytes = EPD_WIDTH / 2;    // 600
constexpr size_t kHalfRowBytes = kRowBytes / 2;  // 300 per controller

const uint8_t PSR_V[] = {0xDF, 0x69};
const uint8_t PWR_V[] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
const uint8_t POF_V[] = {0x00};
const uint8_t DRF_V[] = {0x00};
const uint8_t CDI_V[] = {0xF7};
const uint8_t TCON_V[] = {0x03, 0x03};
const uint8_t TRES_V[] = {0x04, 0xB0, 0x03, 0x20};
const uint8_t CMD66_V[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
const uint8_t EN_BUF_V[] = {0x07};
const uint8_t CCSET_V[] = {0x01};
const uint8_t PWS_V[] = {0x22};
const uint8_t AN_TM_V[] = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
const uint8_t AGID_V[] = {0x10};
const uint8_t BTST_P_V[] = {0xE8, 0x28};
const uint8_t BOOST_VDDP_EN_V[] = {0x01};
const uint8_t BTST_N_V[] = {0xE8, 0x28};
const uint8_t BUCK_BOOST_VDDN_V[] = {0x01};
const uint8_t TFT_VCOM_POWER_V[] = {0x02};

enum : uint8_t {
  PSR = 0x00, PWR = 0x01, POF = 0x02, PON = 0x04, BTST_N = 0x05, BTST_P = 0x06,
  DSLP = 0x07, DTM = 0x10, DRF = 0x12, CDI = 0x50, TCON = 0x60, TRES = 0x61,
  AN_TM = 0x74, AGID = 0x86, BUCK_BOOST_VDDN = 0xB0, TFT_VCOM_POWER = 0xB1,
  EN_BUF = 0xB6, BOOST_VDDP_EN = 0xB7, CCSET = 0xE0, PWS = 0xE3, CMD66 = 0xF0,
};

enum Target { MASTER, SLAVE, BOTH };

void select(Target t) {
  digitalWrite(PIN_EPD_CS_M, (t == MASTER || t == BOTH) ? LOW : HIGH);
  digitalWrite(PIN_EPD_CS_S, (t == SLAVE || t == BOTH) ? LOW : HIGH);
}

void deselect() {
  digitalWrite(PIN_EPD_CS_M, HIGH);
  digitalWrite(PIN_EPD_CS_S, HIGH);
}

void command(uint8_t cmd) {
  digitalWrite(PIN_EPD_DC, LOW);
  SPI.transfer(cmd);
  digitalWrite(PIN_EPD_DC, HIGH);
}

void data(const uint8_t *buf, size_t len) {
  digitalWrite(PIN_EPD_DC, HIGH);
  SPI.writeBytes(buf, len);
}

void send(Target t, uint8_t cmd, const uint8_t *buf, size_t len) {
  select(t);
  command(cmd);
  if (len) data(buf, len);
  deselect();
}

// True when nothing drives the BUSY line: it follows our own pull-up and
// pull-down. A line the HAT is really holding low stays low under the pull-up.
bool busyFloating() {
  pinMode(PIN_EPD_BUSY, INPUT_PULLUP);
  delay(5);
  bool high = digitalRead(PIN_EPD_BUSY) == HIGH;
  pinMode(PIN_EPD_BUSY, INPUT_PULLDOWN);
  delay(5);
  bool low = digitalRead(PIN_EPD_BUSY) == LOW;
  pinMode(PIN_EPD_BUSY, INPUT);
  return high && low;
}

// BUSY is LOW while the controller works and HIGH when idle.
bool waitIdle(const char *stage) {
  uint32_t start = millis();
  uint32_t lastNote = start;
  while (digitalRead(PIN_EPD_BUSY) == LOW) {
    if (millis() - lastNote >= 10000) {
      lastNote = millis();
      Serial.printf("[epd] still waiting for BUSY at %s (%lu s of 60)\n", stage,
                    (unsigned long)((lastNote - start) / 1000));
    }
    if (millis() - start > kBusyTimeoutMs) {
      if (busyFloating()) {
        fail("BUSY wire looks disconnected at %s (HAT BUSY -> D4)", stage);
      } else {
        fail("HAT held BUSY low for 60 s at %s: panel not starting. Check PWR->D5, RST->D7, VCC->3V3, "
             "GND, and that all 3 ribbon latches are closed", stage);
      }
      return false;
    }
    delay(10);
  }
  return true;
}

void reset() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_EPD_RST, HIGH);
    delay(30);
    digitalWrite(PIN_EPD_RST, LOW);
    delay(30);
  }
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(30);
}

bool refresh() {
  send(BOTH, PON, nullptr, 0);
  if (!waitIdle("power on")) return false;
  delay(50);
  uint32_t start = millis();
  send(BOTH, DRF, DRF_V, sizeof(DRF_V));
  if (!waitIdle("refresh")) return false;
  refreshMs = millis() - start;
  delay(50);
  send(BOTH, POF, POF_V, sizeof(POF_V));
  Serial.printf("[epd] refresh took %lu ms\n", (unsigned long)refreshMs);
  if (refreshMs < kMinRefreshMs) {
    fail("panel never went busy (refresh %lu ms): it isn't getting commands. Check DIN->D10, SCLK->D8, "
         "CS_M->D1, CS_S->D6, DC->D2 and the ribbon latches",
         (unsigned long)refreshMs);
    return false;
  }
  return true;
}

}  // namespace

namespace epd {

const char *lastError() { return errorText; }
uint32_t lastRefreshMs() { return refreshMs; }

void begin() {
  errorText[0] = '\0';
  pinMode(PIN_EPD_BUSY, INPUT);  // no pull, as in Waveshare's driver
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_CS_M, OUTPUT);
  pinMode(PIN_EPD_CS_S, OUTPUT);
  pinMode(PIN_EPD_PWR, OUTPUT);
  deselect();
  digitalWrite(PIN_EPD_RST, HIGH);
  digitalWrite(PIN_EPD_PWR, LOW);
  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
  SPI.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
}

bool init() {
  digitalWrite(PIN_EPD_PWR, HIGH);
  delay(10);
  reset();
  // A BUSY line nobody drives just follows our own pull-up/pull-down. Catch
  // that first: otherwise it can read "idle" and every refresh looks instant.
  if (busyFloating()) {
    fail("BUSY wire isn't connected: nothing drives it (HAT BUSY -> D4, left side 5th pin)");
    return false;
  }
  Serial.printf("[epd] BUSY is driven by the HAT (%s after reset)\n",
                digitalRead(PIN_EPD_BUSY) == HIGH ? "high" : "low");
  if (!waitIdle("start-up")) return false;

  send(MASTER, AN_TM, AN_TM_V, sizeof(AN_TM_V));
  send(BOTH, CMD66, CMD66_V, sizeof(CMD66_V));
  send(BOTH, PSR, PSR_V, sizeof(PSR_V));
  send(BOTH, CDI, CDI_V, sizeof(CDI_V));
  send(BOTH, TCON, TCON_V, sizeof(TCON_V));
  send(BOTH, AGID, AGID_V, sizeof(AGID_V));
  send(BOTH, PWS, PWS_V, sizeof(PWS_V));
  send(BOTH, CCSET, CCSET_V, sizeof(CCSET_V));
  send(BOTH, TRES, TRES_V, sizeof(TRES_V));
  send(MASTER, PWR, PWR_V, sizeof(PWR_V));
  send(MASTER, EN_BUF, EN_BUF_V, sizeof(EN_BUF_V));
  send(MASTER, BTST_P, BTST_P_V, sizeof(BTST_P_V));
  send(MASTER, BOOST_VDDP_EN, BOOST_VDDP_EN_V, sizeof(BOOST_VDDP_EN_V));
  send(MASTER, BTST_N, BTST_N_V, sizeof(BTST_N_V));
  send(MASTER, BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V, sizeof(BUCK_BOOST_VDDN_V));
  send(MASTER, TFT_VCOM_POWER, TFT_VCOM_POWER_V, sizeof(TFT_VCOM_POWER_V));
  return true;
}

bool display(const uint8_t *frame) {
  // Left half of every row to the master controller, right half to the slave.
  for (Target t : {MASTER, SLAVE}) {
    size_t offset = (t == MASTER) ? 0 : kHalfRowBytes;
    select(t);
    command(DTM);
    digitalWrite(PIN_EPD_DC, HIGH);
    for (size_t row = 0; row < EPD_HEIGHT; row++) {
      SPI.writeBytes(frame + row * kRowBytes + offset, kHalfRowBytes);
      delayMicroseconds(EPD_ROW_PAUSE_US);
    }
    deselect();
  }
  return refresh();
}

bool clear(uint8_t colour) {
  uint8_t row[kHalfRowBytes];
  memset(row, (colour << 4) | colour, sizeof(row));
  for (Target t : {MASTER, SLAVE}) {
    select(t);
    command(DTM);
    digitalWrite(PIN_EPD_DC, HIGH);
    for (size_t r = 0; r < EPD_HEIGHT; r++) {
      SPI.writeBytes(row, sizeof(row));
      delayMicroseconds(EPD_ROW_PAUSE_US);
    }
    deselect();
  }
  return refresh();
}

bool testPattern() {
  // Six horizontal colour bands across the full width. The left half comes
  // from the CS_M controller and the right half from CS_S, so a half that
  // doesn't change points at that chip-select wire.
  static const uint8_t kColours[] = {0x0, 0x1, 0x2, 0x3, 0x5, 0x6};
  uint8_t row[kHalfRowBytes];
  for (Target t : {MASTER, SLAVE}) {
    select(t);
    command(DTM);
    digitalWrite(PIN_EPD_DC, HIGH);
    for (size_t r = 0; r < EPD_HEIGHT; r++) {
      uint8_t c = kColours[r * 6 / EPD_HEIGHT];
      memset(row, (c << 4) | c, sizeof(row));
      SPI.writeBytes(row, sizeof(row));
      delayMicroseconds(EPD_ROW_PAUSE_US);
    }
    deselect();
  }
  return refresh();
}

void wiringTest() {
  // Panel powered and reset, then BUSY printed live so a loose wire shows up
  // the moment it's wiggled. 'r' + Enter resets the panel again, 'q' quits.
  begin();
  digitalWrite(PIN_EPD_PWR, HIGH);
  delay(10);
  reset();
  Serial.println("[test] Panel power ON (PWR/D5 high), panel reset. Wiggle one wire or ribbon at a time.");
  Serial.println("[test] A good panel shows 'HIGH = ready' within a second of each reset.");
  Serial.println("[test] Type r + Enter to reset the panel again, q + Enter to stop (stops by itself after 10 min).");
  const char *last = "";
  uint32_t start = millis(), lastPrint = 0;
  while (millis() - start < 10UL * 60 * 1000) {
    const char *state = busyFloating() ? "FLOATING = BUSY wire not connected (D4)"
                        : digitalRead(PIN_EPD_BUSY) == HIGH ? "HIGH = ready"
                                                            : "LOW = busy / panel not starting";
    if (strcmp(state, last) != 0 || millis() - lastPrint >= 3000) {
      Serial.printf("[test] %6.1f s  BUSY %s%s\n", (millis() - start) / 1000.0, state,
                    strcmp(state, last) != 0 && last[0] ? "   <-- changed" : "");
      last = state;
      lastPrint = millis();
    }
    while (Serial.available()) {
      char c = Serial.read();
      if (c == 'q' || c == 'Q') {
        digitalWrite(PIN_EPD_PWR, LOW);
        Serial.println("[test] stopped, panel power off");
        return;
      }
      if (c == 'r' || c == 'R') {
        reset();
        Serial.println("[test] panel reset (RST/D7 pulsed)");
      }
    }
    delay(100);
  }
  digitalWrite(PIN_EPD_PWR, LOW);
  Serial.println("[test] 10 minutes up, panel power off");
}

void sleep() {
  const uint8_t check = 0xA5;
  send(BOTH, DSLP, &check, 1);
  delay(100);
  SPI.endTransaction();
  SPI.end();
  digitalWrite(PIN_EPD_PWR, LOW);
}

}  // namespace epd
