#pragma once
#include <Arduino.h>

#include "settings.h"

constexpr uint32_t kDefaultSleepS = 3600;

struct FetchResult {
  int status = 0;            // HTTP status, or negative for local errors
  uint32_t sleepS = kDefaultSleepS;
  String imageId;
  uint8_t *frame = nullptr;  // PSRAM buffer when status == 200; caller frees
  uint32_t configVersion = 0;  // newest settings version on the server
  String firmwareVersion;      // firmware version offered by the server ("" = none)
  String frameMd5;             // checksum of the picture as the server sent it ("" = older add-on)
};

namespace net {

// Join Wi-Fi: the access point used last time first, then the main network,
// then the backup network. Keeps the current WiFi mode (STA or AP+STA).
bool connect(const Settings &s);
// MD5 of a downloaded frame buffer, as lowercase hex.
String frameMd5(const uint8_t *frame);
// Ask the add-on for the next picture. `report` is appended to the query
// (settings/firmware update results).
FetchResult fetchNext(const Settings &s, const char *reason, uint16_t batteryMv, const String &report = "");
// Download settings changed on the server since s.cfgVersion and apply them
// to `out` (a copy of s). Returns false if nothing could be fetched.
bool fetchConfig(const Settings &s, Settings &out);
// Download and install firmware from the add-on. Only returns on failure
// (success reboots), with `error` set.
void updateFirmware(const Settings &s, String &error);
// Forget the cached access point (after Wi-Fi settings change).
void forgetAp();
String urlEncode(const String &in);
// Tell the add-on whether config mode is on (so it can warn about battery).
void heartbeat(const Settings &s, bool configOn, uint16_t batteryMv);

// ID of the picture currently on the panel (kept across deep sleep).
const char *currentId();
void setCurrentId(const String &id);
void clearCurrentId();

}  // namespace net
