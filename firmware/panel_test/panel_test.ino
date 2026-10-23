// Panel test: Waveshare's own 13.3" Spectra 6 driver sequence, unchanged,
// on the XIAO ESP32-S3 pins from WIRING.md. Use it to tell a firmware problem
// from a hardware one: if this shows correct colours, the hardware is fine.
//
// Same as Waveshare's ESP32-S3-ePaper-13.3E6 Arduino example (EPD_13in3e.cpp,
// DEV_Config.cpp): software SPI, DC pin never toggled (commands are framed by
// chip select), panel power on from the start, 1 ms pause per row.
//
// Flash it (same board settings as the frame), open the Serial Monitor at
// 115200 with line ending "Newline". It runs white, then six colour bands.
// Then type: w = white, k = black, b = bands, y = yellow, s = sleep.
// Flash the frame firmware (eink_frame.ino) again afterwards.

// XIAO ESP32-S3 GPIO numbers (silkscreen label in the comment)
#define EPD_SCK_PIN   7   // D8  SCLK
#define EPD_MOSI_PIN  9   // D10 DIN
#define EPD_CS_M_PIN  2   // D1  CS_M
#define EPD_CS_S_PIN  43  // D6  CS_S
#define EPD_DC_PIN    3   // D2  DC
#define EPD_RST_PIN   44  // D7  RST
#define EPD_BUSY_PIN  5   // D4  BUSY
#define EPD_PWR_PIN   6   // D5  PWR

#define EPD_WIDTH  1200
#define EPD_HEIGHT 1600

enum : uint8_t {
  PSR = 0x00, PWR_EPD = 0x01, POF = 0x02, PON = 0x04, BTST_N = 0x05, BTST_P = 0x06,
  DTM = 0x10, DRF = 0x12, CDI = 0x50, TCON = 0x60, TRES = 0x61, AN_TM = 0x74, AGID = 0x86,
  BUCK_BOOST_VDDN = 0xB0, TFT_VCOM_POWER = 0xB1, EN_BUF = 0xB6, BOOST_VDDP_EN = 0xB7,
  CCSET = 0xE0, PWS = 0xE3, CMD66 = 0xF0,
};
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

const uint8_t BLACK = 0x0, WHITE = 0x1, YELLOW = 0x2, RED = 0x3, BLUE = 0x5, GREEN = 0x6;

// ---- Waveshare DEV_Config: bit-banged SPI, mode 0, MSB first
void spiByte(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(EPD_MOSI_PIN, (data & 0x80) ? HIGH : LOW);
    data <<= 1;
    digitalWrite(EPD_SCK_PIN, HIGH);
    digitalWrite(EPD_SCK_PIN, LOW);
  }
}
void spiBytes(const uint8_t *p, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) spiByte(p[i]);
}
void csAll(uint8_t v) {
  digitalWrite(EPD_CS_M_PIN, v);
  digitalWrite(EPD_CS_S_PIN, v);
}
void sendCmd(uint8_t cmd, const uint8_t *buf, uint32_t len) {
  spiByte(cmd);
  spiBytes(buf, len);
}

void devInit() {
  pinMode(EPD_BUSY_PIN, INPUT);
  pinMode(EPD_RST_PIN, OUTPUT);
  pinMode(EPD_DC_PIN, OUTPUT);
  pinMode(EPD_PWR_PIN, OUTPUT);
  pinMode(EPD_SCK_PIN, OUTPUT);
  pinMode(EPD_MOSI_PIN, OUTPUT);
  pinMode(EPD_CS_M_PIN, OUTPUT);
  pinMode(EPD_CS_S_PIN, OUTPUT);
  digitalWrite(EPD_CS_M_PIN, HIGH);
  digitalWrite(EPD_CS_S_PIN, HIGH);
  digitalWrite(EPD_SCK_PIN, LOW);
  digitalWrite(EPD_PWR_PIN, HIGH);
}

// ---- Waveshare EPD_13in3e
void reset() {
  digitalWrite(EPD_RST_PIN, HIGH); delay(30);
  digitalWrite(EPD_RST_PIN, LOW);  delay(30);
  digitalWrite(EPD_RST_PIN, HIGH); delay(30);
  digitalWrite(EPD_RST_PIN, LOW);  delay(30);
  digitalWrite(EPD_RST_PIN, HIGH); delay(30);
}

uint32_t readBusyH() {
  uint32_t start = millis();
  while (!digitalRead(EPD_BUSY_PIN)) {  // LOW: busy, HIGH: idle
    delay(10);
    if (millis() - start > 60000) {
      Serial.println("  BUSY stuck low for 60 s");
      break;
    }
  }
  delay(20);
  return millis() - start;
}

void turnOnDisplay() {
  csAll(0); spiByte(PON); csAll(1);
  Serial.printf("  power on: BUSY for %lu ms\n", (unsigned long)readBusyH());
  delay(50);
  csAll(0); sendCmd(DRF, DRF_V, sizeof(DRF_V)); csAll(1);
  uint32_t t = readBusyH();
  Serial.printf("  refresh:  BUSY for %lu ms %s\n", (unsigned long)t,
                t < 3000 ? "<-- too short, panel didn't really refresh" : "(ok)");
  csAll(0); sendCmd(POF, POF_V, sizeof(POF_V)); csAll(1);
}

void epdInit() {
  reset();
  digitalWrite(EPD_CS_M_PIN, LOW); sendCmd(AN_TM, AN_TM_V, sizeof(AN_TM_V)); csAll(1);
  csAll(0); sendCmd(CMD66, CMD66_V, sizeof(CMD66_V)); csAll(1);
  csAll(0); sendCmd(PSR, PSR_V, sizeof(PSR_V)); csAll(1);
  csAll(0); sendCmd(CDI, CDI_V, sizeof(CDI_V)); csAll(1);
  csAll(0); sendCmd(TCON, TCON_V, sizeof(TCON_V)); csAll(1);
  csAll(0); sendCmd(AGID, AGID_V, sizeof(AGID_V)); csAll(1);
  csAll(0); sendCmd(PWS, PWS_V, sizeof(PWS_V)); csAll(1);
  csAll(0); sendCmd(CCSET, CCSET_V, sizeof(CCSET_V)); csAll(1);
  csAll(0); sendCmd(TRES, TRES_V, sizeof(TRES_V)); csAll(1);
  digitalWrite(EPD_CS_M_PIN, LOW); sendCmd(PWR_EPD, PWR_V, sizeof(PWR_V)); csAll(1);
  digitalWrite(EPD_CS_M_PIN, LOW); sendCmd(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V)); csAll(1);
  digitalWrite(EPD_CS_M_PIN, LOW); sendCmd(BTST_P, BTST_P_V, sizeof(BTST_P_V)); csAll(1);
  digitalWrite(EPD_CS_M_PIN, LOW); sendCmd(BOOST_VDDP_EN, BOOST_VDDP_EN_V, sizeof(BOOST_VDDP_EN_V)); csAll(1);
  digitalWrite(EPD_CS_M_PIN, LOW); sendCmd(BTST_N, BTST_N_V, sizeof(BTST_N_V)); csAll(1);
  digitalWrite(EPD_CS_M_PIN, LOW); sendCmd(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V, sizeof(BUCK_BOOST_VDDN_V)); csAll(1);
  digitalWrite(EPD_CS_M_PIN, LOW); sendCmd(TFT_VCOM_POWER, TFT_VCOM_POWER_V, sizeof(TFT_VCOM_POWER_V)); csAll(1);
}

// Fill each controller's half (300 bytes per row) with a colour chosen per row.
void sendHalf(uint8_t csPin, uint8_t (*colourForRow)(uint32_t)) {
  uint8_t buf[EPD_WIDTH / 4];
  digitalWrite(csPin, LOW);
  spiByte(DTM);
  for (uint32_t row = 0; row < EPD_HEIGHT; row++) {
    uint8_t c = colourForRow(row);
    memset(buf, (c << 4) | c, sizeof(buf));
    spiBytes(buf, sizeof(buf));
    delay(1);
  }
  csAll(1);
}

uint8_t solidColour;
uint8_t solid(uint32_t) { return solidColour; }
uint8_t bands(uint32_t row) {
  static const uint8_t order[] = {BLACK, WHITE, YELLOW, RED, BLUE, GREEN};
  return order[row * 6 / EPD_HEIGHT];
}

void show(const char *what, uint8_t (*colourForRow)(uint32_t)) {
  Serial.printf("[panel_test] %s\n", what);
  digitalWrite(EPD_PWR_PIN, HIGH);  // back on if 's' turned it off
  delay(10);
  epdInit();
  uint32_t t = millis();
  sendHalf(EPD_CS_M_PIN, colourForRow);
  sendHalf(EPD_CS_S_PIN, colourForRow);
  Serial.printf("  data sent in %lu ms\n", (unsigned long)(millis() - t));
  turnOnDisplay();
}

void showSolid(const char *what, uint8_t colour) {
  solidColour = colour;
  show(what, solid);
}

void epdSleep() {
  csAll(0); spiByte(0x07); spiByte(0xA5); csAll(1);
  delay(100);
  digitalWrite(EPD_PWR_PIN, LOW);
  digitalWrite(EPD_RST_PIN, LOW);
  Serial.println("[panel_test] panel asleep, power off");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n[panel_test] Waveshare reference driver on XIAO pins");
  devInit();
  delay(100);
  showSolid("white", WHITE);
  delay(2000);
  show("six bands: black, white, yellow, red, blue, green", bands);
  Serial.println("[panel_test] Type w / k / y / b / s + Enter");
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'w') showSolid("white", WHITE);
  else if (c == 'k') showSolid("black", BLACK);
  else if (c == 'y') showSolid("yellow", YELLOW);
  else if (c == 'b') show("six bands", bands);
  else if (c == 's') epdSleep();
}
