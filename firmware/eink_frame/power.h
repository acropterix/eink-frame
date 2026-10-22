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
// Deep sleep. The button always wakes the frame; the config switch wakes it
// too unless it is already on (e.g. sleeping because the battery is flat).
[[noreturn]] void sleepFor(uint32_t seconds);

}  // namespace power
