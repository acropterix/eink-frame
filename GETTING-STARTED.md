# E-Ink Frame: Getting started

Do sections A–D now, while the parts ship. Section E is for when the hardware arrives.

---

## A. Order the remaining parts

### Already ordered
- [x] Waveshare 13.3" E6 panel + HAT+ driver board
- [x] 2 × Seeed XIAO ESP32-S3
- [x] 3.7 V 3000 mAh LiPo (protected)
- [x] Muzilife 11×14 shadow box + ZBEIVAN 11×14 mats (8.5×11 opening)

### Still needed
Check what's already in your parts drawer first. Everything here is cheap and widely sold on Amazon.

| Part | Qty | Used for | Notes |
|---|---|---|---|
| Momentary push button (6×6 mm tactile, or a panel-mount button) | 1 | Next picture | A panel-mount button looks nicer on the frame's side or back. |
| Mini SPST toggle switch (panel mount) | 1 | Config mode | A small on/off switch you can reach from the back. |
| 3 mm LED, **red, orange or yellow** | 1 | Config mode light | Blue and white are too dim at 3.3 V. |
| 1.5 kΩ resistor | 1 | LED | |
| 1 MΩ resistor | 2 | Battery voltage reading | |
| 100 nF ceramic capacitor | 1 | Battery voltage reading | |
| Female–female Dupont jumper wires (10 cm) | ~15 | XIAO ↔ HAT | Check whether the HAT comes with a wire harness first. |
| JST-PH 2.0 2-pin pigtail, female | 1 | Plug-in battery | Lets you unplug the LiPo instead of soldering it straight to the XIAO. Check the polarity. |
| Heat-shrink tubing | — | Insulating joints | |
| USB-C **data** cable | 1 | Flashing | Charge-only cables are a common cause of "board not found". |
| M2 screws + nuts (or M2.5), short | a few | Printed mounting tray | |
| Double-sided foam tape | — | Holding the panel and battery | |
| PETG filament | — | Printed tray and clips | You probably have some already. |

Optional but handy:
- **Resistor/LED assortment kit:** covers the resistors and LED above, and more.
- **Multimeter:** needed for the battery polarity check and the sleep-current test.

**Don't need:** the Raspberry Pi, PiSugar 3 and Lexar microSD card from the earlier plan. Cancel or return them if you ordered them.

---

## B. Prepare your Mac

### 1. Arduino IDE
1. Download **Arduino IDE 2** from arduino.cc/en/software (macOS, Apple Silicon) and install it.
2. **Arduino IDE → Settings → Additional boards manager URLs**, paste:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Tools → Board → Boards Manager**, search **esp32**, install **"esp32 by Espressif Systems"** (version 3.3.x). It's a large download of about 1 GB.
4. Install Rosetta (Apple Silicon only), since one Arduino helper tool needs it:
   ```bash
   softwareupdate --install-rosetta --agree-to-license
   ```

### 2. Prepare the firmware
1. Open `firmware/eink_frame/eink_frame.ino` in Arduino IDE.
2. In the same folder, duplicate `config.example.h` and rename the copy **`config.h`**. Fill in:
   - `WIFI_SSID` / `WIFI_PASSWORD`: your **2.4 GHz** network. The ESP32 can't use 5 GHz.
   - `FRAME_TOKEN`: a long random string. Generate one with:
     ```bash
     openssl rand -hex 16
     ```
   - The `STATIC_*` lines: pick an address **outside your router's DHCP range** (e.g. `192.168.2.60`), with your router as gateway and DNS. Or comment out `USE_STATIC_IP` to use DHCP.
   - `SETUP_AP_PASSWORD`: the password for the frame's setup hotspot (8+ characters).
3. **Tools:**
   - Board: **XIAO_ESP32S3**
   - PSRAM: **OPI PSRAM**
   - USB CDC On Boot: **Enabled**
   - Partition Scheme: leave at the default. It already allows firmware updates over Wi-Fi.
4. Click **Verify (✓)**. It should finish with "Sketch uses about 1,047,000 bytes (31%)". If it compiles now, flashing tomorrow is just plugging in and clicking Upload.

### 3. Optional extras
- **Python 3** to view your pictures exactly as the frame will. Not needed; the add-on shows previews.
- A photo editor if you'd like to crop pictures yourself. The add-on crops automatically.

---

## C. Install the Home Assistant add-on

**About HACS:** HACS installs integrations and dashboard cards, **not add-ons**. Add-ons are installed by Home Assistant's own add-on store, which supports two routes. Use **Option 1** to start; switch to Option 2 later if you'd like updates with one click.

### Option 1: Local add-on (simplest)
1. In Home Assistant: **Settings → Add-ons → Add-on Store**, search **Samba share**, **Install**.
2. In its **Configuration** tab, set a username and password. **Save**, then **Start**.
3. On the Mac: **Finder → Go → Connect to Server (⌘K)** → `smb://192.168.2.10` → sign in → open the **addons** share.
4. Copy the folder **`eink_frame`** into **addons**, so you end up with `addons/eink_frame/config.yaml`.
5. Back in Home Assistant: **Settings → Add-ons → Add-on Store → ⋮ (top right) → Check for updates**, then reload the page (⌘R).
6. A **Local add-ons** section appears with **E-Ink Frame**. Open it and click **Install**. The Pi builds it in a minute or two.
7. **Configuration** tab:
   - **Frame token:** the same string as `FRAME_TOKEN` in `config.h`.
   - Leave the rest as is: hourly changes, quiet hours 22:00–07:00, landscape orientation, colour boost 0.5. **Save.**
8. **Info** tab: turn on **Show in sidebar** and **Watchdog**, then **Start**.
9. **Log** tab: you should see `UI on :8099, frame API on :8200`.

**Updating it later:** copy the new `eink_frame` folder over the old one, then click **Check for updates**. The add-on shows an **Update** button whenever the version in `config.yaml` has gone up.

### Option 2: From GitHub (one-click updates)
1. This project folder is already the repository (`https://github.com/acropterix/eink-frame`): its root holds `repository.yaml` and the `eink_frame/` folder, which is what Home Assistant needs.
2. Home Assistant: **Settings → Add-ons → Add-on Store → ⋮ → Repositories**, then add the repository URL. Private repositories need a URL with an access token.
3. Install **E-Ink Frame** from the new section, then follow steps 7–9 above.

---

## D. Test everything you can without hardware

### 1. Upload pictures
1. Open **E-Ink Frame** in the Home Assistant sidebar. It also works in the HA phone app.
2. Upload a few favourite family photos (**Photo**) and anything the kids have drawn (**Drawing**).
3. Click a thumbnail to see the full-size preview of how it will look on the panel. If photos look flat, raise **Colour boost** to 0.6–0.7 in the add-on's Configuration and restart.
4. **Her birthday picture:** set its date to **10-24** so it shows all day, every year.
5. Let the kids try the **Draw something** pad.

### 2. Pretend to be the frame
From the Mac's Terminal (replace `YOUR_TOKEN`):
```bash
curl -s http://192.168.2.10:8200/frame/health
```
```bash
curl -s -D - -o test.bin "http://192.168.2.10:8200/frame/next?token=YOUR_TOKEN&battery_mv=3950&fw=1.2.0&cfg=0"
```
```bash
ls -l test.bin
```
- You should see `200 OK`, headers including `X-Image-Id` and `X-Sleep-Seconds`, and **`test.bin` at exactly 960000 bytes**.
- In Home Assistant, **Settings → Entities** should now list `sensor.eink_frame_battery` (about 70%), `sensor.eink_frame_last_seen` and `sensor.eink_frame_image`.

Try config mode too:
```bash
curl -s "http://192.168.2.10:8200/frame/heartbeat?token=YOUR_TOKEN&config=1&ip=192.168.2.60"
```
The panel shows the red **config mode** banner and a notification appears. Turn it off again:
```bash
curl -s "http://192.168.2.10:8200/frame/heartbeat?token=YOUR_TOKEN&config=0"
```

### 3. Router
- Make sure your **2.4 GHz** network is on. If your router combines 2.4 and 5 GHz under one name, that's usually fine.
- If you chose a fixed IP, make sure it's outside the DHCP range. Optionally, once the XIAO has connected, add a DHCP reservation for its MAC address.

### 4. Home Assistant automation (optional)
Add the "frame offline" automation from `README.md` (section 6) so you hear about it if the frame stops checking in.

### 5. Plan the build
- Decide where the **button**, **config switch + LED** and **USB-C charging port** go on the frame. The back or bottom edge works well.
- Measure the shadow box's inside depth and the mat opening. When the panel arrives, send me the measurements of the panel, HAT and XIAO, and I'll design the printed tray and clips for your Core One+.

---

## E. When the hardware arrives

Follow this order and test after each step. It makes problems much easier to find.

1. **XIAO alone.** Solder the headers onto one XIAO and keep the other as a spare. Plug it in over USB-C and **Upload**. Open the Serial Monitor (115200 baud) and press RESET. You should see Wi-Fi connect and `[http] status 200`. It ends with a panel `BUSY timeout`, which is expected with no panel attached.
2. **Panel on USB power.** Wire the HAT following `WIRING.md`. Lock the panel's ribbon cable into the HAT and handle the panel by its edges. Press RESET. The panel flashes for about 20 s and shows a picture.
3. **Button, then config switch + LED.**
   - Button: press it while the frame sleeps, and it should wake and show the next picture.
   - Switch: flip it on. The LED lights and the red banner appears in Home Assistant. Open **http://eink-frame.local** and check the battery shows "no divider" or a voltage. Flip it off.
4. **Battery divider, then battery.** ⚠️ **Check the LiPo's polarity with a multimeter** before connecting it to the XIAO's BAT+/BAT− pads. Then check that the battery % in Home Assistant looks sensible.
5. **Sleep current.** With the frame asleep, put the multimeter in series with the battery. Expect well under 0.1 mA. If it's much higher, tell me and we'll switch the HAT's power with a MOSFET.
6. **Assemble** in the shadow box with the printed tray. Keep the USB-C port reachable for charging.
7. **Try a remote update.** In the add-on, send a harmless settings change (e.g. the same server address), press the frame's button twice, and watch it change to **Applied**.

---

## Quick reference

| Item | Value |
|---|---|
| Home Assistant / add-on | `192.168.2.10`, sidebar **E-Ink Frame** |
| Frame API | `http://192.168.2.10:8200` |
| Frame config page (switch on) | `http://eink-frame.local`, or the `EInkFrame-xxxx` hotspot → `http://192.168.4.1` |
| Board settings | XIAO_ESP32S3, **OPI PSRAM**, USB CDC On Boot enabled |
| Re-flash over USB | Hold BOOT, tap RESET, release BOOT, Upload |
| Firmware for remote update | Sketch → Export Compiled Binary → `eink_frame.ino.bin` (bump `FW_VERSION` first) |
