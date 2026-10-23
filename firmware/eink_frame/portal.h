#pragma once
#include "settings.h"

namespace portal {

// How long to hold the button to enter or leave config mode.
constexpr uint32_t kLongPressMs = 3000;

enum class Exit { SwitchOff, BatteryCritical };

// Config mode: stays awake with a setup hotspot ("EInkFrame-xxxx") plus the
// home Wi-Fi, serving the config page on port 80, until the config switch is
// turned off. Settings saved on the page are written to `s` and to flash.
// byButton: started with a long press instead of the switch. It then ends
// with another long press, the Restart button, or after 30 minutes.
Exit run(Settings &s, bool byButton = false);

}  // namespace portal
