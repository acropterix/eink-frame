#include "settings.h"

#include "pins.h"

#include <Preferences.h>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

#ifndef SETUP_AP_PASSWORD
#define SETUP_AP_PASSWORD "einkframe"
#endif

namespace {

constexpr const char *kNamespace = "frame";
constexpr const char *kBackupNamespace = "frame_lkg";
constexpr const char *kRemoteNamespace = "frame_remote";

String ipString(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return String(a) + "." + b + "." + c + "." + d;
}

#ifdef STATIC_IP
#define IP_ARGS(x) ipString(x)
#endif

void defaults(Settings &s) {
  s.wifiSsid = WIFI_SSID;
  s.wifiPassword = WIFI_PASSWORD;
  s.wifiSsid2 = "";
  s.wifiPassword2 = "";
  s.serverHost = SERVER_HOST;
  s.serverPort = SERVER_PORT;
  s.frameToken = FRAME_TOKEN;
#ifdef USE_STATIC_IP
  s.useStaticIp = true;
#else
  s.useStaticIp = false;
#endif
#ifdef STATIC_IP
  s.staticIp = IP_ARGS(STATIC_IP);
  s.gateway = IP_ARGS(STATIC_GATEWAY);
  s.subnet = IP_ARGS(STATIC_SUBNET);
  s.dns = IP_ARGS(STATIC_DNS);
#endif
  s.setupApPassword = SETUP_AP_PASSWORD;
#ifdef BATTERY_MONITOR_BUILTIN
  s.hasBatteryDivider = true;  // the board has its own battery monitor
#else
  s.hasBatteryDivider = HAS_BATTERY_DIVIDER;
#endif
  s.cfgVersion = 0;
}

}  // namespace

namespace settings {

bool loadFrom(const char *ns, Settings &s) {
  Preferences p;
  if (!p.begin(ns, true)) return false;  // nothing saved yet
  s.wifiSsid = p.getString("ssid", s.wifiSsid);
  s.wifiPassword = p.getString("pass", s.wifiPassword);
  s.wifiSsid2 = p.getString("ssid2", s.wifiSsid2);
  s.wifiPassword2 = p.getString("pass2", s.wifiPassword2);
  s.serverHost = p.getString("host", s.serverHost);
  s.serverPort = p.getUShort("port", s.serverPort);
  s.frameToken = p.getString("token", s.frameToken);
  s.useStaticIp = p.getBool("static", s.useStaticIp);
  s.staticIp = p.getString("ip", s.staticIp);
  s.gateway = p.getString("gw", s.gateway);
  s.subnet = p.getString("mask", s.subnet);
  s.dns = p.getString("dns", s.dns);
  s.setupApPassword = p.getString("appass", s.setupApPassword);
  s.hasBatteryDivider = p.getBool("divider", s.hasBatteryDivider);
  s.cfgVersion = p.getUInt("cfgver", s.cfgVersion);
  p.end();
  return true;
}

void saveTo(const char *ns, const Settings &s) {
  Preferences p;
  p.begin(ns, false);
  p.putString("ssid", s.wifiSsid);
  p.putString("pass", s.wifiPassword);
  p.putString("ssid2", s.wifiSsid2);
  p.putString("pass2", s.wifiPassword2);
  p.putString("host", s.serverHost);
  p.putUShort("port", s.serverPort);
  p.putString("token", s.frameToken);
  p.putBool("static", s.useStaticIp);
  p.putString("ip", s.staticIp);
  p.putString("gw", s.gateway);
  p.putString("mask", s.subnet);
  p.putString("dns", s.dns);
  p.putString("appass", s.setupApPassword);
  p.putBool("divider", s.hasBatteryDivider);
  p.putUInt("cfgver", s.cfgVersion);
  p.end();
}

void load(Settings &s) {
  defaults(s);
  loadFrom(kNamespace, s);
}

void save(const Settings &s) { saveTo(kNamespace, s); }

void saveBackup(const Settings &s) { saveTo(kBackupNamespace, s); }

bool loadBackup(Settings &s) {
  defaults(s);
  return loadFrom(kBackupNamespace, s);
}

void loadRemote(RemoteState &r) {
  Preferences p;
  if (!p.begin(kRemoteNamespace, true)) return;
  r.trialVersion = p.getUInt("trial", 0);
  r.failedVersion = p.getUInt("failed", 0);
  r.failReason = p.getString("reason", "");
  r.failedFirmware = p.getString("fwfail", "");
  r.firmwareError = p.getString("fwerr", "");
  p.end();
}

void saveRemote(const RemoteState &r) {
  Preferences p;
  p.begin(kRemoteNamespace, false);
  p.putUInt("trial", r.trialVersion);
  p.putUInt("failed", r.failedVersion);
  p.putString("reason", r.failReason);
  p.putString("fwfail", r.failedFirmware);
  p.putString("fwerr", r.firmwareError);
  p.end();
}

void resetToDefaults() {
  for (const char *ns : {kNamespace, kBackupNamespace, kRemoteNamespace}) {
    Preferences p;
    p.begin(ns, false);
    p.clear();
    p.end();
  }
}

}  // namespace settings
