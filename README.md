# E-Ink Frame

A battery-powered 13.3" colour e-ink picture frame that gets its pictures from Home Assistant.

- **Home Assistant add-on** (`eink_frame/`): an upload page and a 6-colour drawing pad in the HA sidebar. It converts each picture into the panel's format once, at upload, manages the slideshow, and shows the frame's battery in Home Assistant.
- **Firmware** (`firmware/eink_frame`) for a Seeed XIAO ESP32-S3: wake → download → refresh → deep sleep. It has no image logic of its own.
- **Remote updates:** change the frame's Wi-Fi and server settings, or install new firmware, from the Home Assistant panel. If new settings don't work, the frame rolls back to its previous ones automatically.
- **Config mode** (toggle switch + LED on the frame): the frame stays awake and serves its own setup page, where you can change Wi-Fi and server settings, push a picture, or update the firmware without a USB cable.
- **Wiring**: see [wiring.html](wiring.html) (pictures) and [WIRING.md](WIRING.md). **Debugging**: [DEBUGGING.md](DEBUGGING.md).

### Repository layout
```
repository.yaml      Home Assistant add-on repository file (must stay at the root)
eink_frame/          the add-on (Home Assistant installs this folder)
firmware/eink_frame/ Arduino sketch for the XIAO ESP32-S3 (config.h is git-ignored)
*.md, wiring.html    guides
```
Home Assistant searches the whole repository for add-ons, so don't add any other file named `config.yaml`, `config.yml` or `config.json`.

```
Phone / Mac ──upload──▶ HA add-on on 192.168.2.10 ──:8200──▶ XIAO ESP32-S3 ──SPI──▶ HAT+ ──▶ panel
                      (dithers, stores, schedules)       (wakes hourly, ~25 s awake)
```

---

## 1. Install the add-on (Home Assistant OS)

**From this GitHub repository (recommended):** **Settings → Add-ons → Add-on Store → ⋮ → Repositories**, add `https://github.com/acropterix/eink-frame` (private, so the URL needs an access token), then install **E-Ink Frame** and continue at step 6 below. To update later: push, then **⋮ → Check for updates** and click **Update** (it appears whenever the version in `eink_frame/config.yaml` goes up).

**Or as a local add-on:**

1. **Get access to the add-ons folder.** In Home Assistant: **Settings → Add-ons → Add-on Store**, install and start **Samba share** (set a username and password in its Configuration tab first).
2. On the Mac: **Finder → Go → Connect to Server…** → `smb://192.168.2.10` → open the **addons** share.
3. Copy the whole **`eink_frame`** folder into **addons**, so you have `addons/eink_frame/config.yaml`.
4. In Home Assistant: **Settings → Add-ons → Add-on Store → ⋮ (top right) → Check for updates**, then reload the page. **E-Ink Frame** appears under **Local add-ons**.
5. Open it and click **Install**. It builds on the Pi in a couple of minutes; all its Python packages are prebuilt for the Pi 4.
6. **Configuration** tab: set **Frame token** to a long random string (you'll put the same one in the firmware), and check orientation and quiet hours. **Save.**
7. **Info** tab: turn on **Show in sidebar**, then **Start**. Check the **Log** tab for `UI on :8099, frame API on :8200`.

**Test it from the Mac:**
```bash
curl http://192.168.2.10:8200/frame/health
```
Upload a picture from the sidebar panel, then:
```bash
curl -o test.bin "http://192.168.2.10:8200/frame/next?token=YOUR_TOKEN"
```
`test.bin` should be exactly **960000** bytes.

Pictures live in the add-on's `/data` folder and are included in Home Assistant backups. 500 pictures take about 1 GB.

## 2. Flash the firmware

1. Install **Arduino IDE 2**. In **Settings**, add this Additional Boards Manager URL:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. **Boards Manager** → install **esp32 by Espressif Systems** (3.x).
3. Open `firmware/eink_frame/eink_frame.ino`.
4. Copy `config.example.h` to **`config.h`** in the same folder and fill in your Wi-Fi details, `FRAME_TOKEN`, and a free fixed IP address (reserve it in your router). `SERVER_HOST` is already `192.168.2.10`. These values are only used on first boot; after that, change them from the config page.
5. **Tools:**
   - Board: **XIAO_ESP32S3**
   - PSRAM: **OPI PSRAM** ← required
   - USB CDC On Boot: **Enabled** (to see the log)
6. **Upload**, then open the Serial Monitor at 115200 baud.

**Updating firmware later:** upload it in the Home Assistant panel (section 5), or on the frame's config page (section 4): in Arduino IDE choose **Sketch → Export Compiled Binary** and upload `eink_frame.ino.bin` there. Over USB instead: the frame spends almost all its time in deep sleep, when the USB port disappears, so hold the XIAO's **BOOT** button, tap **RESET**, release **BOOT**, then upload.

## 3. Bring-up order
1. **XIAO alone on USB.** After reset, the log should show Wi-Fi connecting and `[http] status 200` or `304`. With no panel attached, a 200 ends in a BUSY timeout, which is expected.
2. **Add the HAT and panel** (see [WIRING.md](WIRING.md)), still on USB. Press RESET: the panel flashes for about 20 s and shows the picture.
3. **Add the battery divider and battery.** The battery level appears in Home Assistant at the next check-in.
4. **Measure the sleep current** (see WIRING.md), then mount everything in the frame.

## 4. Config mode

Flip the **config switch** on. The LED lights and the frame wakes within a second.

**No switch fitted?** Hold the button for **3 seconds** (a short press still means "next picture"). Leave config mode by holding it 3 s again or pressing **Restart** on the page; it also ends by itself after 30 minutes.

- **Where to find the page:**
  - On your home Wi-Fi: **http://eink-frame.local** or its IP address. The Home Assistant panel shows a red banner with a direct link.
  - If the frame can't join your Wi-Fi (new router, wrong password), join its **`EInkFrame-xxxx`** hotspot from a phone (password `einkframe` unless you changed it) and open **http://192.168.4.1**. Most phones pop the page up on their own.
- **On the page:**
  - **Status:** battery percentage and voltage (with a low-battery warning and how long config mode can run on what's left), firmware version, which pushed settings version is active or failed, Wi-Fi, and the server address.
  - **Show a picture now:** upload a `.bin` file from the add-on's **.bin** button on any picture, or get the next one from the server, or clear the panel to white.
  - **Settings:** Wi-Fi network and password, a backup Wi-Fi network, server address and port, frame token, fixed IP, setup hotspot password, and whether the battery divider is fitted. They're saved to the frame's flash and take effect when you switch config mode off. Password fields are never shown; leave one blank to keep it.
  - **Firmware update:** upload a new `.bin` from Arduino's **Export Compiled Binary**.
- **Home Assistant while it's on:**
  - A **sticky red banner** in the E-Ink Frame panel.
  - A **notification** with the link.
  - `binary_sensor.eink_frame_config_mode` turns on.
  - The frame checks in every minute. If those check-ins stop, the banner says so.
- **Battery:** config mode keeps Wi-Fi running, which drains a full battery in **roughly a day**. If the battery gets critically low, the frame leaves config mode and shuts down, even with the switch still on. The LED stays lit as a reminder.
- **Switch it off:** the frame tells the server, does a normal check-in with the new settings, and goes back to sleep.

The config page has no login of its own; it relies on the physical switch and the hotspot password. Only leave config mode on while you're using it.

## 5. Changing settings and firmware from Home Assistant

You don't need config mode for routine changes. In the **E-Ink Frame** panel:

### Frame settings
1. Fill in only the fields you want to change (Wi-Fi, backup Wi-Fi, server address or port, fixed IP), then click **Send to frame**. Changing a Wi-Fi network always needs its password.
2. **At its next check-in** (within the hour, or press the frame's button), the frame downloads the change and saves it. Its old settings are kept as a backup.
3. **On the following wake**, it tries the new settings:
   - It connects and reaches the server: they're kept, and the change shows **Applied**.
   - Wi-Fi fails, or the server can't be reached or rejects the token: it **restores the previous settings**, checks in with those, and the change shows **Failed, frame reverted**, with the reason (`wifi`, `server` or `token`).
4. A failed change isn't retried. Send a corrected one; it's merged with any changes the frame hasn't applied yet.

Notes:
- Wi-Fi passwords are stored by the add-on so it can send them to the frame. They're never shown in the panel again.
- The **frame token** can't be changed remotely (a wrong token would lock the frame out). Change it in config mode, then in the add-on's Configuration.
- Settings typed on the frame's own config page take priority over a pushed change that hasn't been tried yet.
- **Safe way to move the frame to a new router:** first send the new network as the **backup** Wi-Fi network. Once the old router is gone, the frame falls back to it automatically.

### Firmware
1. In Arduino IDE: **Sketch → Export Compiled Binary**, then upload `eink_frame.ino.bin` in the **Frame firmware** card.
2. The add-on checks that it's an ESP32 image and reads its version (from `version.h`). Bump `FW_VERSION` for every build you upload.
3. At the next check-in, if the battery is above **3.7 V** (or the battery isn't being measured), the frame downloads it, installs it and restarts. The panel shows which version the frame is running.
4. If installing fails, the frame keeps its current firmware, reports the error, and won't retry that version.

A build that crashes on start-up can't fix itself. Recover with USB (hold BOOT, tap RESET, upload). Test new builds over USB before uploading them here.

## 6. Using it
- **Sidebar → E-Ink Frame:** upload photos ("Photo" = smooth dithering) or drawings ("Drawing" = crisp, flat colours), or draw directly on the pad with the six colours the panel can show.
- **Show next:** that picture appears at the next check-in. Press the frame's button to see it immediately.
- **Special date (MM-DD):** shows that picture all day, every year (e.g. `10-24`).
- **Pause slideshow:** keeps the current picture; the button still skips ahead.
- **Button on the frame:** wakes it and moves to the next picture.

### Home Assistant entities
- `sensor.eink_frame_battery`
- `sensor.eink_frame_last_seen`
- `sensor.eink_frame_image`
- `binary_sensor.eink_frame_config_mode`

A notification is created automatically when the battery drops below the warning level. Example automation to alert you if the frame stops checking in:
```yaml
alias: E-Ink Frame offline
triggers:
  - trigger: template
    value_template: >
      {{ states('sensor.eink_frame_last_seen') not in ['unknown', 'unavailable'] and
         now() - as_datetime(states('sensor.eink_frame_last_seen')) > timedelta(hours=26) }}
actions:
  - action: persistent_notification.create
    data:
      title: E-Ink Frame
      message: The frame hasn't checked in for over a day.
```

## 7. Battery life (estimate, 3000 mAh)
Each picture change costs about 1 mAh, mostly the ~20 s refresh. Check-ins with no change (a 304 response) cost far less.

| Schedule | Changes per day | Approximate life |
|---|---|---|
| Hourly, 24/7 | 24 | ~3–4 months |
| Hourly, quiet 22:00–07:00 (default) | 15 | ~5–6 months |
| Every 20 min, quiet hours on | 45 | ~2 months |

These are estimates; check the battery sensor after the first week.

## 8. Tuning and troubleshooting
| Symptom | Fix |
|---|---|
| Colours look washed out | Raise **Colour boost** (e.g. 0.7) and restart the add-on; it re-renders everything. |
| Picture is sideways or upside down | Change **Orientation** or **Upside down** and restart the add-on. |
| `[http] token rejected` | `FRAME_TOKEN` in `config.h` doesn't match the add-on option. |
| `PSRAM allocation failed` | Tools → PSRAM → **OPI PSRAM**, then re-upload. |
| `BUSY timeout` with the panel connected | Check the BUSY, RST and PWR wires and that the ribbon cable is fully locked into the HAT. |
| Wi-Fi fails | The ESP32 only supports 2.4 GHz networks. Check the SSID and password and whether the static IP is free. |
| No battery level | Divider not fitted, or the "battery divider fitted" setting is off on the config page. |
| Pushed settings show "Failed (server)" but the server was fine | The Pi was probably restarting during that check-in. Send the same change again. |
| Firmware upload rejected: "no version tag found" | The file isn't built from this sketch (it must contain `kFirmwareTag`), or it's the wrong `.bin`. Use `eink_frame.ino.bin`, not the bootloader or partitions file. |
| Firmware stays "waiting to be installed" | The battery is below 3.7 V, or the version matches what's already running. Charge it, or bump `FW_VERSION`. |
| Can't find the config page | Join the `EInkFrame-xxxx` hotspot and open http://192.168.4.1. `.local` names don't work on some Android phones. |
| Changed Wi-Fi settings and now it won't connect | Flip config mode on and use the hotspot to fix them. Or add the new network as the **backup** network first, then swap. |
| Arduino compile fails with `ctags: bad CPU type in executable` | Apple Silicon Mac without Rosetta. Run `softwareupdate --install-rosetta`, then compile again. |
