#include "portal.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "epd13in3e.h"
#include "net.h"
#include "portal_html.h"
#include "power.h"
#include "version.h"

namespace {

constexpr uint32_t kHeartbeatMs = 60000;
constexpr uint32_t kSwitchPollMs = 300;
constexpr uint32_t kButtonConfigMs = 30 * 60 * 1000;
constexpr const char *kHostname = "eink-frame";
constexpr const char *kFallbackApPassword = "einkframe";

WebServer server(80);
DNSServer dns;
Settings *cfg = nullptr;
String apSsid;

// Panel jobs run after the page has had its answer: a refresh takes ~25 s,
// and holding the request open that long made phones drop it ("Failed to
// fetch"). The page polls /api/status for the result.
enum class Job { None, Test, Clear, Fetch, Display };
Job pendingJob = Job::None;
bool jobRunning = false;
String jobResult;  // last outcome, shown on the page

uint8_t *uploadBuf = nullptr;
size_t uploadLen = 0;
bool uploadOverflow = false;
bool firmwareOk = false;
String firmwareError;

String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 2);
  for (char c : in) {
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c < 0x20) continue;
    out += c;
  }
  return out;
}

void addField(String &json, const char *key, const String &value, bool quote = true) {
  if (json.length() > 1) json += ',';
  json += '"';
  json += key;
  json += "\":";
  if (quote) json += '"' + jsonEscape(value) + '"';
  else json += value;
}

// One line for the config page about how the panel refresh went.
String panelResult(bool ok, const char *success) {
  if (!ok) return String("Panel problem: ") + epd::lastError();
  return String(success) + " (refresh took " + String(epd::lastRefreshMs() / 1000.0, 1) + " s)";
}

bool showFrame(const uint8_t *frame) {
  epd::begin();
  bool ok = epd::init() && epd::display(frame);
  epd::sleep();
  return ok;
}

void handleRoot() { server.send(200, "text/html", PORTAL_HTML); }

void handleStatus() {
  const Settings &s = *cfg;
  String j = "{";
  uint16_t mv = power::batteryMv(s);
  RemoteState rs;
  settings::loadRemote(rs);
  addField(j, "fw", FW_VERSION);
  addField(j, "battery_mv", String(mv), false);
  addField(j, "battery_pct", String(power::batteryPercent(mv)), false);
  addField(j, "cfg_version", String(s.cfgVersion), false);
  addField(j, "cfg_trial", String(rs.trialVersion), false);
  addField(j, "cfg_failed", String(rs.failedVersion), false);
  addField(j, "cfg_fail_reason", rs.failReason);
  addField(j, "fw_failed", rs.failedFirmware);
  addField(j, "fw_error", rs.firmwareError);
  addField(j, "sta_connected", WiFi.status() == WL_CONNECTED ? "true" : "false", false);
  addField(j, "sta_ssid", WiFi.SSID());
  addField(j, "sta_ip", WiFi.localIP().toString());
  addField(j, "rssi", String(WiFi.RSSI()), false);
  addField(j, "ap_ssid", apSsid);
  addField(j, "ap_ip", WiFi.softAPIP().toString());
  addField(j, "current_id", net::currentId());
  addField(j, "uptime_s", String(millis() / 1000), false);
  addField(j, "ssid", s.wifiSsid);
  addField(j, "ssid2", s.wifiSsid2);
  addField(j, "host", s.serverHost);
  addField(j, "port", String(s.serverPort), false);
  addField(j, "static", s.useStaticIp ? "true" : "false", false);
  addField(j, "ip", s.staticIp);
  addField(j, "gw", s.gateway);
  addField(j, "mask", s.subnet);
  addField(j, "dns", s.dns);
  addField(j, "divider", s.hasBatteryDivider ? "true" : "false", false);
  addField(j, "panel_busy", (pendingJob != Job::None || jobRunning) ? "true" : "false", false);
  addField(j, "panel_result", jobResult);
  j += '}';
  server.send(200, "application/json", j);
}

bool validIp(const String &v) {
  IPAddress ip;
  return ip.fromString(v);
}

void handleSettings() {
  Settings next = *cfg;
  next.wifiSsid = server.arg("ssid");
  next.wifiSsid2 = server.arg("ssid2");
  next.serverHost = server.arg("host");
  long port = server.arg("port").toInt();
  next.useStaticIp = server.arg("static") == "1";
  next.staticIp = server.arg("ip");
  next.gateway = server.arg("gw");
  next.subnet = server.arg("mask");
  next.dns = server.arg("dns");
  next.hasBatteryDivider = server.arg("divider") == "1";
  // Secrets are never sent to the page; an empty field means "keep".
  if (server.arg("pass").length()) next.wifiPassword = server.arg("pass");
  if (server.arg("pass2").length()) next.wifiPassword2 = server.arg("pass2");
  if (server.arg("token").length()) next.frameToken = server.arg("token");
  if (server.arg("appass").length()) next.setupApPassword = server.arg("appass");
  if (next.wifiSsid2.isEmpty()) next.wifiPassword2 = "";

  String error;
  if (next.wifiSsid.isEmpty()) error = "Wi-Fi network is required.";
  else if (next.serverHost.isEmpty()) error = "Server address is required.";
  else if (port < 1 || port > 65535) error = "Port must be 1-65535.";
  else if (next.setupApPassword.length() < 8) error = "Hotspot password needs 8+ characters.";
  else if (next.useStaticIp && !(validIp(next.staticIp) && validIp(next.gateway) && validIp(next.subnet)))
    error = "Fixed IP, gateway and subnet must be valid addresses.";
  if (error.length()) {
    server.send(400, "text/plain", error);
    return;
  }
  next.serverPort = (uint16_t)port;
  if (next.wifiSsid != cfg->wifiSsid) net::forgetAp();
  settings::save(next);
  *cfg = next;
  // Settings typed here win over a pushed version that hasn't been tried yet.
  RemoteState rs;
  settings::loadRemote(rs);
  if (rs.trialVersion) {
    rs.trialVersion = 0;
    settings::saveRemote(rs);
  }
  Serial.println("[portal] settings saved");
  server.send(200, "text/plain",
              "Saved. Wi-Fi and server changes take effect when you turn config mode off.");
}

void handleDisplayUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadLen = 0;
    uploadOverflow = false;
    if (!uploadBuf) uploadBuf = (uint8_t *)heap_caps_malloc(EPD_FRAME_BYTES, MALLOC_CAP_SPIRAM);
  } else if (up.status == UPLOAD_FILE_WRITE && uploadBuf) {
    if (uploadLen + up.currentSize > EPD_FRAME_BYTES) {
      uploadOverflow = true;
    } else {
      memcpy(uploadBuf + uploadLen, up.buf, up.currentSize);
      uploadLen += up.currentSize;
    }
  }
}

bool queueJob(Job job, const char *what) {
  if (pendingJob != Job::None || jobRunning) {
    server.send(409, "text/plain", "The panel is still busy with the last request. Wait for it to finish.");
    return false;
  }
  pendingJob = job;
  jobResult = "";
  server.send(202, "text/plain", String(what) + " The panel takes about 25 s; the result appears here.");
  return true;
}

void handleDisplayDone() {
  if (!uploadBuf) {
    server.send(500, "text/plain", "Out of memory - is PSRAM enabled?");
  } else if (uploadOverflow || uploadLen != EPD_FRAME_BYTES) {
    server.send(400, "text/plain", "The file must be exactly 960,000 bytes (a .bin from the add-on).");
    heap_caps_free(uploadBuf);
    uploadBuf = nullptr;
  } else if (!queueJob(Job::Display, "Picture received.")) {
    heap_caps_free(uploadBuf);
    uploadBuf = nullptr;
  }
}

void handleFetch() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/plain", "Not connected to the home Wi-Fi, so the server can't be reached.");
    return;
  }
  queueJob(Job::Fetch, "Getting the next picture.");
}

void handleClear() { queueJob(Job::Clear, "Clearing to white."); }

void handleTest() { queueJob(Job::Test, "Drawing colour bands."); }

// Runs in the portal loop, after the request that started it has been answered.
void runJob(Job job) {
  jobRunning = true;
  // Wi-Fi stays on for the page, so lower its transmit power while the panel
  // draws its refresh current, to keep the supply from dipping.
  wifi_power_t txPower = WiFi.getTxPower();
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  bool ok = false;
  String done;
  if (job == Job::Fetch) {
    FetchResult r = net::fetchNext(*cfg, "button", power::batteryMv(*cfg));
    if (r.status == 200 && r.frame) {
      ok = showFrame(r.frame);
      heap_caps_free(r.frame);
      if (ok) net::setCurrentId(r.imageId);
      jobResult = panelResult(ok, "Done - showing the next picture.");
    } else if (r.status == 304) {
      jobResult = "The server says to keep the current picture.";
    } else if (r.status == 401) {
      jobResult = "The server rejected the frame token.";
    } else {
      jobResult = "Could not get a picture from the server (status " + String(r.status) + ").";
    }
  } else {
    if (job == Job::Display) {
      ok = showFrame(uploadBuf);
      heap_caps_free(uploadBuf);
      uploadBuf = nullptr;
      done = "Done - the picture is on the panel.";
    } else {
      epd::begin();
      ok = epd::init() && (job == Job::Test ? epd::testPattern() : epd::clear(0x1));
      epd::sleep();
      done = job == Job::Test ? "Colour bands drawn. Both halves should show black, white, yellow, red, blue, green"
                              : "Cleared to white.";
    }
    if (ok) net::clearCurrentId();  // the server will replace it at the next check-in
    jobResult = panelResult(ok, done.c_str());
  }
  WiFi.setTxPower(txPower);
  Serial.printf("[portal] %s\n", jobResult.c_str());
  jobRunning = false;
}

void handleReboot() {
  server.send(200, "text/plain", "Restarting…");
  delay(300);
  ESP.restart();
}

void handleFirmwareUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    firmwareOk = false;
    firmwareError = "";
    Serial.printf("[ota] %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) firmwareError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE && firmwareError.isEmpty()) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) firmwareError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_END && firmwareError.isEmpty()) {
    if (Update.end(true)) firmwareOk = true;
    else firmwareError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    firmwareError = "Upload aborted";
  }
}

void handleFirmwareDone() {
  if (firmwareOk) {
    server.send(200, "text/plain", "Firmware updated. Restarting…");
    delay(500);
    ESP.restart();
  }
  server.send(500, "text/plain", "Update failed: " + firmwareError);
}

void handleNotFound() {
  // Send phones that join the hotspot straight to the config page.
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
  server.send(302, "text/plain", "");
}

}  // namespace

namespace portal {

Exit run(Settings &s, bool byButton) {
  cfg = &s;
  Serial.printf("[portal] config mode ON (%s)\n", byButton ? "button held" : "switch");

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  apSsid = String("EInkFrame-") + suffix;
  const char *apPass = s.setupApPassword.length() >= 8 ? s.setupApPassword.c_str() : kFallbackApPassword;
  WiFi.softAP(apSsid.c_str(), apPass);
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("[portal] hotspot '%s' at %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());

  net::connect(s);  // may fail; the hotspot still works
  if (MDNS.begin(kHostname)) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/display", HTTP_POST, handleDisplayDone, handleDisplayUpload);
  server.on("/api/fetch", HTTP_POST, handleFetch);
  server.on("/api/clear", HTTP_POST, handleClear);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/firmware", HTTP_POST, handleFirmwareDone, handleFirmwareUpload);
  server.onNotFound(handleNotFound);
  server.begin();

  uint16_t mv = power::batteryMv(s);
  net::heartbeat(s, true, mv);
  uint32_t lastBeat = millis(), lastPoll = millis();
  int offReadings = 0;
  uint32_t started = millis(), heldSince = 0;
  bool releasedOnce = false;  // ignore the long press that started config mode
  Exit exit = Exit::SwitchOff;

  while (true) {
    dns.processNextRequest();
    server.handleClient();
    if (pendingJob != Job::None) {
      Job job = pendingJob;
      pendingJob = Job::None;
      delay(50);  // let the 202 reply finish sending
      runJob(job);
    }
    delay(2);

    if (millis() - lastPoll >= kSwitchPollMs) {
      lastPoll = millis();
      if (byButton) {
        bool down = power::buttonDown();
        if (!down) releasedOnce = true;
        heldSince = (down && releasedOnce) ? (heldSince ? heldSince : millis()) : 0;
        if (heldSince && millis() - heldSince >= kLongPressMs) break;
        if (millis() - started >= kButtonConfigMs) {
          Serial.println("[portal] 30 minutes up, leaving config mode");
          break;
        }
      } else {
        offReadings = power::configSwitchOn() ? 0 : offReadings + 1;
        if (offReadings >= 2) break;
      }
    }
    if (millis() - lastBeat >= kHeartbeatMs) {
      lastBeat = millis();
      mv = power::batteryMv(s);
      if (mv && mv < kCriticalBatteryMv) {
        Serial.println("[portal] battery critical, leaving config mode");
        exit = Exit::BatteryCritical;
        break;
      }
      if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
      net::heartbeat(s, true, mv);
    }
  }

  net::heartbeat(s, false, power::batteryMv(s));
  server.stop();
  MDNS.end();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[portal] config mode OFF");
  return exit;
}

}  // namespace portal
