# E-Ink Frame

Upload photos and drawings from the sidebar panel. Each picture's **.bin** button downloads the ready-made file for the frame's own config page. Each picture is converted once, at upload, into the exact format the 13.3" Spectra 6 panel needs. The frame wakes on its own schedule, downloads the next picture from port 8200 and goes back to sleep.

## Options
- **Frame token**: a shared secret. Use the same value for `FRAME_TOKEN` in the firmware's `config.h`.
- **Change picture every**: minutes between check-ins (default 60).
- **Quiet hours**: no changes between these times; the frame sleeps straight through them.
- **Orientation / Upside down**: how the frame hangs. Changing these re-renders every picture on restart.
- **Colour boost**: 0 dithers against the panel's real, muted inks (most natural), 1 against pure colours (punchier). 0.5 is a good start.
- **Shuffle**: random order instead of the gallery order.
- **Low battery warning**: creates a Home Assistant notification at this level.

## Frame settings and firmware
- **Frame settings:** send Wi-Fi, backup Wi-Fi, server and fixed-IP changes to the frame. Each save becomes a numbered version. The frame tries it on its next wake, rolls back if it can't connect, and reports **Applied** or **Failed** here. Wi-Fi passwords are stored by the add-on but never displayed. The frame token can't be changed remotely.
- **Frame firmware:** upload `eink_frame.ino.bin` (Arduino → Export Compiled Binary). The version is read from the file. The frame installs it at its next check-in if its battery is above 3.7 V.

## Entities
Updated every time the frame checks in:
- `sensor.eink_frame_battery`: battery % (voltage in attributes)
- `sensor.eink_frame_last_seen`: last check-in (Wi-Fi signal, firmware version and next wake in attributes)
- `sensor.eink_frame_image`: name of the picture on the frame
- `binary_sensor.eink_frame_config_mode`: on while the frame's config switch is on (it drains the battery). A notification with a link to the frame's config page is shown while it's on.

These entities are recreated at the frame's next check-in after Home Assistant restarts.

## Frame API
- `GET /frame/next?token=…&current=<id>&battery_mv=…&reason=timer|button|boot`
  - `200` with a 960,000-byte body and `X-Image-Id`: show this picture
  - `304`: keep the current picture
  - Both include `X-Sleep-Seconds`.
- Every `/frame/next` response also carries `X-Config-Version` (newest settings version) and `X-Firmware-Version` (uploaded firmware, if any). The frame reports back with `cfg`, `cfg_fail`, `cfg_err`, `fw`, `fw_fail` and `fw_err` query parameters.
- `GET /frame/config?token=…&from=<applied version>`: settings changed since that version, as `key=value` lines with percent-encoded values
- `GET /frame/firmware?token=…`: the uploaded firmware image (with `x-MD5`)
- `GET /frame/heartbeat?token=…&config=1|0&ip=…&battery_mv=…`: sent every minute in config mode, and once when it ends
- `GET /frame/health`
