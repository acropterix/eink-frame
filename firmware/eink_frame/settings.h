#pragma once
#include <Arduino.h>

// Settings live in flash (NVS) so they can be changed from the config page
// without reflashing. config.h only provides the values used on first boot.
struct Settings {
  String wifiSsid;
  String wifiPassword;
  String wifiSsid2;       // optional backup network, tried if the first fails
  String wifiPassword2;
  String serverHost;
  uint16_t serverPort;
  String frameToken;
  bool useStaticIp;
  String staticIp;
  String gateway;
  String subnet;
  String dns;
  String setupApPassword; // password for the "EInkFrame-xxxx" setup hotspot
  bool hasBatteryDivider;
  uint32_t cfgVersion;    // last settings version applied from the server
};

// Progress of settings and firmware pushed from the Home Assistant add-on.
struct RemoteState {
  uint32_t trialVersion = 0;   // settings version being tried on this wake (0 = none)
  uint32_t failedVersion = 0;  // last version that failed and was rolled back
  String failReason;           // "wifi", "server" or "token"
  String failedFirmware;       // firmware version that failed to install
  String firmwareError;
};

namespace settings {

void load(Settings &s);
void save(const Settings &s);
void resetToDefaults();

// Last-known-good copy, taken before trying settings pushed from the server.
void saveBackup(const Settings &s);
bool loadBackup(Settings &s);

void loadRemote(RemoteState &r);
void saveRemote(const RemoteState &r);

}  // namespace settings
