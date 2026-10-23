#include "power.h"

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "pins.h"

namespace {
constexpr uint32_t kMinSleepS = 60;
constexpr uint32_t kMaxSleepS = 24 * 3600;
}  // namespace

namespace power {

uint16_t batteryMv(const Settings &s) {
  if (!s.hasBatteryDivider) return 0;
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BATTERY);
  uint32_t mv = sum / 16 * 2;  // the divider halves the voltage
  // A LiPo is always between ~3.0 and 4.2 V. Anything far outside that means
  // the divider isn't fitted (D3 floating) or no battery is connected, so
  // report "unknown" rather than a made-up voltage.
  return (mv < 2500 || mv > 4500) ? 0 : (uint16_t)mv;
}

int batteryPercent(uint16_t mv) {
  // Same curve the add-on uses.
  static const uint16_t kMv[] = {4200, 4100, 4000, 3900, 3800, 3700, 3600, 3500, 3300};
  static const uint8_t kPct[] = {100, 90, 79, 62, 42, 20, 8, 3, 0};
  if (!mv) return -1;
  if (mv >= kMv[0]) return 100;
  for (int i = 1; i < 9; i++) {
    if (mv >= kMv[i]) {
      return kPct[i] + (kPct[i - 1] - kPct[i]) * (int)(mv - kMv[i]) / (int)(kMv[i - 1] - kMv[i]);
    }
  }
  return 0;
}

bool configSwitchOn() {
  pinMode(PIN_CONFIG, INPUT_PULLUP);
  int low = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(PIN_CONFIG) == LOW) low++;
    delay(2);
  }
  return low >= 4;
}

bool buttonDown() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  int low = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(PIN_BUTTON) == LOW) low++;
    delay(2);
  }
  return low >= 4;
}

bool buttonHeldFor(uint32_t ms) {
  while (millis() < ms) {
    if (!buttonDown()) return false;
    delay(20);
  }
  return true;
}

void releaseWakePins() {
  for (gpio_num_t pin : {(gpio_num_t)PIN_BUTTON, (gpio_num_t)PIN_CONFIG}) rtc_gpio_deinit(pin);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_CONFIG, INPUT_PULLUP);
}

void sleepFor(uint32_t seconds) {
  seconds = constrain(seconds, kMinSleepS, kMaxSleepS);
  bool switchOn = configSwitchOn();
  Serial.printf("[sleep] %lu s%s\n", (unsigned long)seconds, switchOn ? " (config switch ignored)" : "");
  Serial.flush();

  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);

  uint64_t mask = 1ULL << PIN_BUTTON;
  if (!switchOn) mask |= 1ULL << PIN_CONFIG;
  for (gpio_num_t pin : {(gpio_num_t)PIN_BUTTON, (gpio_num_t)PIN_CONFIG}) {
    rtc_gpio_init(pin);
    rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);  // keep the pull-ups alive
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

  // Keep the panel's power switch firmly off while we sleep.
  digitalWrite(PIN_EPD_PWR, LOW);
  gpio_hold_en((gpio_num_t)PIN_EPD_PWR);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}

}  // namespace power
