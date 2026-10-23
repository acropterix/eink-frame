// E-Ink Frame firmware for the Seeed XIAO ESP32-S3 + Waveshare 13.3" Spectra 6.
//
// Normal wake: read battery -> join Wi-Fi -> ask the Home Assistant add-on for
// the next picture -> if there is one, download it into PSRAM, turn Wi-Fi off
// and refresh the panel -> deep sleep for as long as the server says.
//
// Remote updates: every check-in response says which settings version and
// firmware the add-on has. Newer settings are downloaded and tried on the
// next wake; if Wi-Fi or the server can't be reached with them, the frame
// restores its previous settings and reports the failure. Newer firmware is
// installed straight away when the battery allows.
//
// Button (D0): wakes the frame early and skips to the next picture.
// Config switch (D9): while on, the frame stays awake and serves a setup page
// (see portal.cpp). Turning it off resumes the normal cycle.
//
// Arduino IDE: board "XIAO_ESP32S3", Tools > PSRAM > "OPI PSRAM".

#include <WiFi.h>
#include <stdarg.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

#include "epd13in3e.h"
#include "net.h"
#include "pins.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "version.h"

// The add-on looks for this tag to read the version of an uploaded firmware.
extern "C" __attribute__((used)) const char kFirmwareTag[] = "EINK-FRAME-FW:" FW_VERSION;

constexpr uint32_t kLowBatterySleepS = 6 * 3600;
constexpr uint16_t kMinFirmwareUpdateMv = 3700;

RTC_DATA_ATTR uint8_t rtcFailures = 0;
// The last thing that went wrong, sent to the add-on on the next check-in so
// Home Assistant can show it (the panel itself just keeps the old picture).
RTC_DATA_ATTR char rtcError[208] = "";
RTC_DATA_ATTR uint32_t rtcRefreshMs = 0;

void noteError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(rtcError, sizeof(rtcError), fmt, args);
  va_end(args);
  Serial.printf("[error] %s\n", rtcError);
}

uint32_t backoffSeconds() {
  uint32_t s = 15 * 60;
  for (uint8_t i = 1; i < rtcFailures && s < 4 * 3600; i++) s *= 2;
  return min(s, (uint32_t)(4 * 3600));
}

void failAndSleep() {
  rtcFailures++;
  power::sleepFor(backoffSeconds());
}

const char *wakeReason() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT1:
      return (esp_sleep_get_ext1_wakeup_status() & (1ULL << PIN_BUTTON)) ? "button" : "config";
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    default: return "boot";
  }
}

// Extra query parameters telling the add-on how remote updates went.
String updateReport(const RemoteState &rs) {
  String q;
  if (rs.failedVersion) q += "&cfg_fail=" + String(rs.failedVersion) + "&cfg_err=" + net::urlEncode(rs.failReason);
  if (rs.failedFirmware.length()) {
    q += "&fw_fail=" + net::urlEncode(rs.failedFirmware) + "&fw_err=" + net::urlEncode(rs.firmwareError);
  }
  if (rtcError[0]) q += "&err=" + net::urlEncode(rtcError);
  if (rtcRefreshMs) q += "&refresh_ms=" + String(rtcRefreshMs);
  return q;
}

// Put back the settings we had before trying a pushed version.
void rollBack(Settings &s, RemoteState &rs, const char *why) {
  Serial.printf("[config] settings v%lu failed (%s), restoring previous settings\n",
                (unsigned long)rs.trialVersion, why);
  Settings previous;
  if (settings::loadBackup(previous)) {
    settings::save(previous);
    s = previous;
  }
  rs.failedVersion = rs.trialVersion;
  rs.failReason = why;
  rs.trialVersion = 0;
  settings::saveRemote(rs);
  net::forgetAp();
  WiFi.disconnect(true);
}

void setup() {
  Serial.begin(115200);
  gpio_hold_dis((gpio_num_t)PIN_EPD_PWR);
  gpio_deep_sleep_hold_dis();
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, LOW);

  Settings s;
  settings::load(s);
  RemoteState rs;
  settings::loadRemote(rs);
  const char *reason = wakeReason();
  uint16_t batteryMv = power::batteryMv(s);
  Serial.printf("\n[boot] %s, wake: %s, battery %u mV, settings v%lu, PSRAM %u bytes\n", kFirmwareTag,
                reason, batteryMv, (unsigned long)s.cfgVersion, (unsigned)ESP.getPsramSize());

  if (batteryMv && batteryMv < kCriticalBatteryMv) {
    Serial.println("[boot] battery critical, not doing anything");
    power::sleepFor(kLowBatterySleepS);
  }

  power::releaseWakePins();
  // Config mode: the switch, or (no switch fitted) holding the button for 3 s.
  bool bySwitch = power::configSwitchOn();
  bool byButton = !bySwitch && power::buttonDown() && power::buttonHeldFor(portal::kLongPressMs);
  if (bySwitch || byButton) {
    if (portal::run(s, byButton) == portal::Exit::BatteryCritical) power::sleepFor(kLowBatterySleepS);
    settings::loadRemote(rs);  // the config page may have cancelled a trial
    reason = "config";         // config mode just ended: do a normal check-in now
    batteryMv = power::batteryMv(s);
  }

  // ---- connect, rolling back pushed settings that don't work
  bool trial = rs.trialVersion != 0;
  if (trial) Serial.printf("[config] trying settings v%lu\n", (unsigned long)rs.trialVersion);

  bool connected = net::connect(s);
  if (!connected && trial) {
    rollBack(s, rs, "wifi");
    trial = false;
    connected = net::connect(s);
  }
  if (!connected) {
    noteError("could not join Wi-Fi '%s'", s.wifiSsid.c_str());
    failAndSleep();
  }

  FetchResult r = net::fetchNext(s, reason, batteryMv, updateReport(rs));
  bool serverOk = r.status == 200 || r.status == 304;
  if (trial && !serverOk) {
    if (r.frame) heap_caps_free(r.frame);
    rollBack(s, rs, r.status == 401 ? "token" : "server");
    trial = false;
    if (!net::connect(s)) failAndSleep();
    r = net::fetchNext(s, reason, batteryMv, updateReport(rs));
    serverOk = r.status == 200 || r.status == 304;
  }
  if (serverOk) rtcError[0] = '\0';  // the add-on has it now
  if (trial && serverOk) {
    Serial.printf("[config] settings v%lu work, keeping them\n", (unsigned long)rs.trialVersion);
    rs.trialVersion = 0;
    settings::saveRemote(rs);
  }

  if (serverOk) {
    // ---- stage newer settings from the server; they're tried on the next wake
    if (r.configVersion > s.cfgVersion && r.configVersion != rs.failedVersion) {
      Settings next;
      if (net::fetchConfig(s, next)) {
        settings::saveBackup(s);
        settings::save(next);
        rs.trialVersion = next.cfgVersion;
        settings::saveRemote(rs);
        if (next.wifiSsid != s.wifiSsid) net::forgetAp();
        Serial.printf("[config] staged settings v%lu for the next wake\n", (unsigned long)next.cfgVersion);
      }
    }

    // ---- install newer firmware (reboots on success)
    bool batteryOk = batteryMv == 0 || batteryMv >= kMinFirmwareUpdateMv;
    if (r.firmwareVersion.length() && r.firmwareVersion != FW_VERSION &&
        r.firmwareVersion != rs.failedFirmware && batteryOk) {
      if (r.frame) {  // the same picture is fetched again after the restart
        heap_caps_free(r.frame);
        r.frame = nullptr;
      }
      String error;
      net::updateFirmware(s, error);
      rs.failedFirmware = r.firmwareVersion;
      rs.firmwareError = error;
      settings::saveRemote(rs);
    }
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (r.status == 200 && r.frame) {
    epd::begin();
    bool ok = epd::init() && epd::display(r.frame);
    epd::sleep();
    heap_caps_free(r.frame);
    if (!ok) {
      noteError("panel: %s", epd::lastError());
      failAndSleep();
    }
    rtcRefreshMs = epd::lastRefreshMs();
    net::setCurrentId(r.imageId);
    Serial.println("[epd] refreshed");
  } else if (r.status == 401) {
    Serial.println("[http] token rejected - check the frame token");
    power::sleepFor(kDefaultSleepS);
  } else if (!serverOk) {
    noteError("server %s:%u answered %d", s.serverHost.c_str(), s.serverPort, r.status);
    failAndSleep();
  }
  rtcFailures = 0;
  power::sleepFor(r.sleepS);
}

void loop() {}
