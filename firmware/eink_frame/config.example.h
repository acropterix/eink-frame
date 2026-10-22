#pragma once
// Copy this file to config.h and fill it in. config.h is not shared.
// These are only the first-boot defaults: after that, change any of them from
// the frame's config page (flip the config switch on). They are stored in flash.

#define WIFI_SSID      "your-wifi-name"
#define WIFI_PASSWORD  "your-wifi-password"

// Home Assistant Pi running the E-Ink Frame add-on.
#define SERVER_HOST    "192.168.2.10"
#define SERVER_PORT    8200
// Must match "Frame token" in the add-on configuration.
#define FRAME_TOKEN    "change-me"

// Optional fixed IP for faster Wi-Fi connects (saves battery). Reserve this
// address in your router first. Comment out USE_STATIC_IP to use DHCP.
#define USE_STATIC_IP
#define STATIC_IP      192, 168, 2, 60
#define STATIC_GATEWAY 192, 168, 2, 1
#define STATIC_SUBNET  255, 255, 255, 0
#define STATIC_DNS     192, 168, 2, 1

// Set to 0 if you have not fitted the 1M/1M battery divider on D3.
#define HAS_BATTERY_DIVIDER 1

// Password for the "EInkFrame-xxxx" setup hotspot used in config mode (8+ chars).
#define SETUP_AP_PASSWORD "einkframe"
