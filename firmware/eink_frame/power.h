#pragma once
#include <Arduino.h>

#include "settings.h"

constexpr uint16_t kCriticalBatteryMv = 3350;

namespace power {

// Battery voltage in mV via the 1M/1M divider on D3, or 0 if not fitted.
uint16_t batteryMv(const Settings &s);
// Approximate LiPo charge (0-100) from its voltage; -1 if unknown.
int batteryPercent(uint16_t mv);
// True while the config toggle switch is on (D9 pulled to GND), debounced.
bool configSwitchOn();
// True while the button (D0) is pressed, debounced.
bool buttonDown();
// True if the button is still down and stays down until `ms` after start-up.
bool buttonHeldFor(uint32_t ms);
// Hand the wake pins back to normal GPIO after deep sleep.
void releaseWakePins();
// Deep sleep. The button always wakes the frame; the config switch wakes it
// too unless it is already on (e.g. sleeping because the battery is flat).
[[noreturn]] void sleepFor(uint32_t seconds);

}  // namespace power
