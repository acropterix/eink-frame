#include "net.h"

#include <HTTPClient.h>
#include <MD5Builder.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "epd13in3e.h"
#include "version.h"

namespace {

constexpr uint32_t kWifiTimeoutMs = 10000;
constexpr uint32_t kDownloadStallMs = 10000;

// Survive deep sleep.
RTC_DATA_ATTR char rtcCurrentId[40] = "";
RTC_DATA_ATTR uint8_t rtcBssid[6];
RTC_DATA_ATTR int32_t rtcChannel = 0;
RTC_DATA_ATTR bool rtcApCached = false;

bool waitForWifi(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) return false;
    delay(20);
  }
  return true;
}

void applyStaticIp(const Settings &s) {
  IPAddress ip, gw, mask, dns;
  if (s.useStaticIp && ip.fromString(s.staticIp) && gw.fromString(s.gateway) &&
      mask.fromString(s.subnet)) {
    if (!dns.fromString(s.dns)) dns = gw;
    WiFi.config(ip, gw, mask, dns);
  } else {
    WiFi.config(IPAddress(), IPAddress(), IPAddress());  // DHCP
  }
}

bool tryNetwork(const String &ssid, const String &password) {
  if (ssid.isEmpty()) return false;
  Serial.printf("[wifi] joining '%s'\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  if (waitForWifi(kWifiTimeoutMs)) return true;
  WiFi.disconnect();
  return false;
}

String baseUrl(const Settings &s) {
  return String("http://") + s.serverHost + ":" + s.serverPort;
}

String urlDecode(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '%' && i + 2 < in.length()) {
      char hex[3] = {in[i + 1], in[i + 2], 0};
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else if (c == '+') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace

namespace net {

String frameMd5(const uint8_t *frame) {
  MD5Builder md5;
  md5.begin();
  md5.add(frame, EPD_FRAME_BYTES);
  md5.calculate();
  return md5.toString();
}

String urlEncode(const String &in) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (char c : in) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') {
      out += c;
    } else {
      out += '%';
      out += hex[(uint8_t)c >> 4];
      out += hex[(uint8_t)c & 15];
    }
  }
  return out;
}

}  // namespace net

namespace {

using net::urlEncode;

String commonQuery(const Settings &s, uint16_t batteryMv) {
  return String("token=") + urlEncode(s.frameToken) + "&battery_mv=" + batteryMv + "&rssi=" + WiFi.RSSI() +
         "&fw=" + FW_VERSION + "&ip=" + WiFi.localIP().toString();
}

}  // namespace

namespace net {

bool connect(const Settings &s) {
  if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  applyStaticIp(s);
  uint32_t start = millis();
  bool ok = false;

  if (rtcApCached) {
    // Skip the scan by going straight to the access point we used last time.
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPassword.c_str(), rtcChannel, rtcBssid, true);
    ok = waitForWifi(kWifiTimeoutMs / 2);
    if (!ok) {
      Serial.println("[wifi] cached AP failed, scanning");
      rtcApCached = false;
      WiFi.disconnect();
    }
  }
  if (!ok) ok = tryNetwork(s.wifiSsid, s.wifiPassword);
  if (!ok) ok = tryNetwork(s.wifiSsid2, s.wifiPassword2);
  if (!ok) {
    Serial.println("[wifi] could not connect");
    return false;
  }

  // Only cache the main network; the backup is found by scanning.
  if (WiFi.SSID() == s.wifiSsid) {
    memcpy(rtcBssid, WiFi.BSSID(), sizeof(rtcBssid));
    rtcChannel = WiFi.channel();
    rtcApCached = true;
  }
  Serial.printf("[wifi] '%s' in %lu ms, IP %s, RSSI %d\n", WiFi.SSID().c_str(),
                (unsigned long)(millis() - start), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

FetchResult fetchNext(const Settings &s, const char *reason, uint16_t batteryMv, const String &report) {
  FetchResult result;
  String url = baseUrl(s) + "/frame/next?" + commonQuery(s, batteryMv) + "&current=" + rtcCurrentId +
               "&reason=" + reason + "&cfg=" + s.cfgVersion + report;

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(15000);
  const char *headers[] = {"X-Sleep-Seconds", "X-Image-Id", "X-Config-Version", "X-Firmware-Version", "X-Frame-MD5"};
  http.collectHeaders(headers, 5);
  if (!http.begin(client, url)) {
    result.status = -1;
    return result;
  }
  result.status = http.GET();
  if (http.hasHeader("X-Sleep-Seconds")) result.sleepS = http.header("X-Sleep-Seconds").toInt();
  result.imageId = http.header("X-Image-Id");
  result.configVersion = http.header("X-Config-Version").toInt();
  result.firmwareVersion = http.header("X-Firmware-Version");
  result.frameMd5 = http.header("X-Frame-MD5");
  Serial.printf("[http] status %d, sleep %lu s, image '%s'\n", result.status,
                (unsigned long)result.sleepS, result.imageId.c_str());

  if (result.status == HTTP_CODE_OK) {
    int len = http.getSize();
    uint8_t *frame = nullptr;
    if (len != EPD_FRAME_BYTES) {
      Serial.printf("[http] unexpected size %d\n", len);
      result.status = -2;
    } else if (!(frame = (uint8_t *)heap_caps_malloc(EPD_FRAME_BYTES, MALLOC_CAP_SPIRAM))) {
      Serial.println("[http] PSRAM allocation failed - is PSRAM enabled?");
      result.status = -3;
    } else {
      WiFiClient *stream = http.getStreamPtr();
      size_t got = 0;
      uint32_t lastData = millis();
      while (got < EPD_FRAME_BYTES && millis() - lastData < kDownloadStallMs) {
        size_t avail = stream->available();
        if (avail) {
          got += stream->readBytes(frame + got, min(avail, EPD_FRAME_BYTES - got));
          lastData = millis();
        } else {
          delay(2);
        }
      }
      if (got == EPD_FRAME_BYTES && result.frameMd5.length() && frameMd5(frame) != result.frameMd5) {
        Serial.println("[http] picture corrupted in download (checksum mismatch)");
        heap_caps_free(frame);
        result.status = -5;
      } else if (got == EPD_FRAME_BYTES) {
        if (result.frameMd5.length()) Serial.println("[http] picture checksum OK: identical to the server's file");
        result.frame = frame;
      } else {
        Serial.printf("[http] download stalled at %u bytes\n", (unsigned)got);
        heap_caps_free(frame);
        result.status = -4;
      }
    }
  }
  http.end();
  return result;
}

void heartbeat(const Settings &s, bool configOn, uint16_t batteryMv) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  String url = baseUrl(s) + "/frame/heartbeat?" + commonQuery(s, batteryMv) +
               "&config=" + (configOn ? "1" : "0");
  if (http.begin(client, url)) {
    int code = http.GET();
    Serial.printf("[http] heartbeat config=%d -> %d\n", configOn, code);
    http.end();
  }
}

bool fetchConfig(const Settings &s, Settings &out) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  String url = baseUrl(s) + "/frame/config?token=" + urlEncode(s.frameToken) + "&from=" + s.cfgVersion;
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[config] fetch failed: %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  out = s;
  int start = 0;
  bool gotVersion = false;
  while (start < (int)body.length()) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    start = end + 1;
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String value = urlDecode(line.substring(eq + 1));
    if (key == "version") { out.cfgVersion = value.toInt(); gotVersion = true; }
    else if (key == "server_host") out.serverHost = value;
    else if (key == "server_port") out.serverPort = value.toInt();
    else if (key == "wifi_ssid") out.wifiSsid = value;
    else if (key == "wifi_password") out.wifiPassword = value;
    else if (key == "wifi_ssid2") out.wifiSsid2 = value;
    else if (key == "wifi_password2") out.wifiPassword2 = value;
    else if (key == "use_static_ip") out.useStaticIp = value == "1";
    else if (key == "static_ip") out.staticIp = value;
    else if (key == "gateway") out.gateway = value;
    else if (key == "subnet") out.subnet = value;
    else if (key == "dns") out.dns = value;
    else Serial.printf("[config] ignoring unknown key '%s'\n", key.c_str());
  }
  Serial.printf("[config] got settings version %lu\n", (unsigned long)out.cfgVersion);
  return gotVersion && out.serverPort > 0;
}

void updateFirmware(const Settings &s, String &error) {
  WiFiClient client;
  String url = baseUrl(s) + "/frame/firmware?token=" + urlEncode(s.frameToken);
  Serial.println("[ota] downloading firmware from the server");
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return ret = httpUpdate.update(client, url);
  // Only reached on failure: success reboots into the new firmware.
  error = ret == HTTP_UPDATE_NO_UPDATES ? String("server had no update") : httpUpdate.getLastErrorString();
  Serial.printf("[ota] failed: %s\n", error.c_str());
}

void forgetAp() { rtcApCached = false; }

const char *currentId() { return rtcCurrentId; }

void setCurrentId(const String &id) { strlcpy(rtcCurrentId, id.c_str(), sizeof(rtcCurrentId)); }

void clearCurrentId() { rtcCurrentId[0] = '\0'; }

}  // namespace net
