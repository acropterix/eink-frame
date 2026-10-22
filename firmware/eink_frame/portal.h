#pragma once
#include "settings.h"

namespace portal {

enum class Exit { SwitchOff, BatteryCritical };

// Config mode: stays awake with a setup hotspot ("EInkFrame-xxxx") plus the
// home Wi-Fi, serving the config page on port 80, until the config switch is
// turned off. Settings saved on the page are written to `s` and to flash.
Exit run(Settings &s);

}  // namespace portal
