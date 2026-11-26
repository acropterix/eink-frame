"""E-Ink Frame add-on: web UI (via Home Assistant ingress) + frame API.

Port 8099: ingress UI and its JSON API (Home Assistant handles login).
Port 8200: the API the ESP32 frame calls on the local network.
"""

import asyncio
import hashlib
import ipaddress
import json
import logging
import os
import random
import re
import time
import uuid
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo
from pathlib import Path
from urllib.parse import quote

from aiohttp import ClientSession, web

import imaging

DATA_DIR = Path(os.environ.get("DATA_DIR", "/data"))
IMAGES_DIR = DATA_DIR / "images"
DB_PATH = DATA_DIR / "db.json"
FIRMWARE_DIR = DATA_DIR / "firmware"
FIRMWARE_PATH = FIRMWARE_DIR / "firmware.bin"
FIRMWARE_TAG = re.compile(rb"EINK-FRAME-FW:([0-9A-Za-z._-]{1,20})\x00")
OPTIONS_PATH = DATA_DIR / "options.json"
STATIC_DIR = Path(__file__).parent / "static"
UI_PORT = int(os.environ.get("UI_PORT", "8099"))
FRAME_PORT = int(os.environ.get("FRAME_PORT", "8200"))
SUPERVISOR_TOKEN = os.environ.get("SUPERVISOR_TOKEN")
HA_API = "http://supervisor/core/api"

# Home Assistant's time zone. The add-on container itself runs on UTC, so
# quiet hours and "today" for dated pictures must use this instead.
LOCAL_TZ = None


def local_now():
    return datetime.now(LOCAL_TZ) if LOCAL_TZ else datetime.now().astimezone()


async def ha_time_zone():
    """The time zone set in Home Assistant (Settings > System > General), or ""."""
    if not SUPERVISOR_TOKEN:
        return ""
    try:
        async with ClientSession() as session:
            async with session.get(f"{HA_API}/config",
                                   headers={"Authorization": f"Bearer {SUPERVISOR_TOKEN}"}) as r:
                if r.status != 200:
                    log.warning("Home Assistant config API answered %s; can't read its time zone", r.status)
                    return ""
                zone = (await r.json(content_type=None)).get("time_zone", "") or ""
                log.info("Home Assistant's time zone setting: %s", zone or "(none)")
                return zone
    except Exception:  # noqa: BLE001 - fall back rather than not starting
        log.warning("Could not read Home Assistant's time zone", exc_info=True)
        return ""


async def resolve_timezone(opts):
    """The add-on's time_zone option if set, else Home Assistant's own time
    zone. The Supervisor's TZ variable can say UTC when HA itself is set to a
    local zone, so it's only the last fallback."""
    global LOCAL_TZ
    name = (opts.get("time_zone") or "").strip() or await ha_time_zone() or os.environ.get("TZ", "")
    if name:
        try:
            LOCAL_TZ = ZoneInfo(name)
        except Exception:  # noqa: BLE001
            log.warning("Unknown time zone %r, using UTC", name)
    log.info("Time zone %s, local time now %s", name or "UTC", local_now().strftime("%Y-%m-%d %H:%M"))


class HideToken(logging.Filter):
    """Keep the frame token out of the access log (the frame sends it in the URL)."""
    PATTERN = re.compile(r"(token=)[^&\s\"]+")

    def filter(self, record):
        if isinstance(record.msg, str) and "token=" in record.msg:
            record.msg = self.PATTERN.sub(r"\1***", record.msg)
        return True

DEFAULT_OPTIONS = {
    "frame_token": "",
    "interval_minutes": 60,
    "quiet_start": "22:00",
    "quiet_end": "07:00",
    "orientation": "landscape",
    "flip": False,
    "saturation": 0.5,
    "shuffle": False,
    "low_battery_percent": 15,
    "time_zone": "",
}

# LiPo resting voltage (mV) -> approximate charge (%).
LIPO_CURVE = [(4200, 100), (4100, 90), (4000, 79), (3900, 62), (3800, 42),
              (3700, 20), (3600, 8), (3500, 3), (3300, 0)]

log = logging.getLogger("eink_frame")
db_lock = asyncio.Lock()


def load_options():
    opts = dict(DEFAULT_OPTIONS)
    if OPTIONS_PATH.exists():
        opts.update(json.loads(OPTIONS_PATH.read_text()))
    return opts


def load_db():
    if DB_PATH.exists():
        return json.loads(DB_PATH.read_text())
    return {"images": [], "state": {}}


def save_db(db):
    tmp = DB_PATH.with_suffix(".tmp")
    tmp.write_text(json.dumps(db, indent=1))
    tmp.replace(DB_PATH)


def image_path(image_id, kind):
    names = {"bin": "frame.bin", "preview": "preview.png", "thumb": "thumb.jpg", "original": "original.jpg"}
    return IMAGES_DIR / image_id / names[kind]


def battery_percent(mv):
    if not mv:
        return None
    if mv >= LIPO_CURVE[0][0]:
        return 100
    for (v_hi, p_hi), (v_lo, p_lo) in zip(LIPO_CURVE, LIPO_CURVE[1:]):
        if mv >= v_lo:
            return round(p_lo + (p_hi - p_lo) * (mv - v_lo) / (v_hi - v_lo))
    return 0


def parse_hhmm(value):
    h, m = value.split(":")
    return int(h) * 60 + int(m)


def seconds_until_quiet_ends(opts, now):
    """0 if we are outside quiet hours, else seconds until they end."""
    start, end = parse_hhmm(opts["quiet_start"]), parse_hhmm(opts["quiet_end"])
    if start == end:
        return 0
    minute = now.hour * 60 + now.minute
    inside = start <= minute < end if start < end else (minute >= start or minute < end)
    if not inside:
        return 0
    end_dt = now.replace(hour=end // 60, minute=end % 60, second=0, microsecond=0)
    if end_dt <= now:
        end_dt += timedelta(days=1)
    return int((end_dt - now).total_seconds())


def next_sleep_seconds(opts, now):
    interval = int(opts["interval_minutes"]) * 60
    quiet = seconds_until_quiet_ends(opts, now)
    if quiet:
        return quiet
    # If the normal interval would land inside quiet hours, sleep through them.
    later = now + timedelta(seconds=interval)
    quiet_later = seconds_until_quiet_ends(opts, later)
    return interval + quiet_later


def process_upload(data, mode, fit, opts, image_id):
    img = imaging.open_upload(data)
    original = imaging.shrink_original(img)
    frame, preview = imaging.convert(
        original, mode=mode, fit=fit, orientation=opts["orientation"],
        flip=opts["flip"], saturation=opts["saturation"])
    folder = IMAGES_DIR / image_id
    folder.mkdir(parents=True, exist_ok=True)
    original.save(image_path(image_id, "original"), quality=92)
    image_path(image_id, "bin").write_bytes(frame)
    preview.save(image_path(image_id, "preview"))
    imaging.thumbnail(preview).save(image_path(image_id, "thumb"), quality=85)


def reprocess(entry, opts):
    from PIL import Image
    img = Image.open(image_path(entry["id"], "original")).convert("RGB")
    frame, preview = imaging.convert(
        img, mode=entry["mode"], fit=entry["fit"], orientation=opts["orientation"],
        flip=opts["flip"], saturation=opts["saturation"])
    image_path(entry["id"], "bin").write_bytes(frame)
    preview.save(image_path(entry["id"], "preview"))
    imaging.thumbnail(preview).save(image_path(entry["id"], "thumb"), quality=85)


def render_settings(opts):
    return {k: opts[k] for k in ("orientation", "flip", "saturation")}


async def ensure_render_settings(app):
    """Re-render everything if orientation/flip/saturation changed."""
    opts = app["opts"]
    async with db_lock:
        db = load_db()
        if db.get("render") == render_settings(opts):
            return
        loop = asyncio.get_running_loop()
        for entry in db["images"]:
            try:
                await loop.run_in_executor(None, reprocess, entry, opts)
            except Exception:  # noqa: BLE001 - keep going with the others
                log.exception("Could not re-render %s", entry["id"])
        db["render"] = render_settings(opts)
        save_db(db)
        log.info("Re-rendered %d images for new display settings", len(db["images"]))


# ---------------------------------------------------------------- choosing
def choose_next(db, opts, reason, now):
    """Pick the image to show, or None to keep the current one."""
    state = db["state"]
    enabled = [e for e in db["images"] if e.get("enabled", True)]
    current = state.get("current_id")
    by_id = {e["id"]: e for e in db["images"]}

    queued = state.pop("queue_next", None)
    if queued in by_id:
        return queued

    if state.get("paused") and reason != "button":
        return None
    if not enabled:
        return None

    today = now.strftime("%m-%d")
    special = [e["id"] for e in enabled if e.get("show_on") == today]
    if special and reason != "button":
        return special[0] if current not in special else None

    if opts["shuffle"]:
        choices = [e["id"] for e in enabled if e["id"] != current] or [enabled[0]["id"]]
        return random.choice(choices)

    ids = [e["id"] for e in enabled]
    if current in ids:
        return ids[(ids.index(current) + 1) % len(ids)]
    return ids[0]


# ---------------------------------------------------------------- remote frame settings
# Each save is a numbered change set. The frame tells us the last version it
# applied and fetches everything newer, merged. Wi-Fi passwords are stored
# here so they can be sent to the frame, but never sent back to the UI.
SETTING_FIELDS = ("server_host", "server_port", "wifi_ssid", "wifi_password", "wifi_ssid2",
                  "wifi_password2", "use_static_ip", "static_ip", "gateway", "subnet", "dns")
SECRET_FIELDS = ("wifi_password", "wifi_password2")
FIELD_LABELS = {
    "server_host": "server address", "server_port": "server port", "wifi_ssid": "Wi-Fi network",
    "wifi_password": "Wi-Fi password", "wifi_ssid2": "backup network",
    "wifi_password2": "backup password", "use_static_ip": "fixed IP on/off", "static_ip": "IP address",
    "gateway": "gateway", "subnet": "subnet mask", "dns": "DNS",
}


def frame_config(db):
    return db.setdefault("frame_config", {"version": 0, "changes": []})


def validate_changes(body):
    """Return a clean change set from the UI form, or raise HTTPBadRequest."""
    changes = {}
    for key in SETTING_FIELDS:
        if key not in body or body[key] is None:
            continue
        value = body[key]
        if isinstance(value, str):
            value = value.strip()
        if value == "" and key not in ("wifi_ssid2", "wifi_password2"):
            continue  # blank means "don't change"
        changes[key] = value

    def bad(msg):
        raise web.HTTPBadRequest(text=msg)

    if body.get("remove_backup"):
        changes["wifi_ssid2"] = ""
        changes["wifi_password2"] = ""
    elif changes.get("wifi_ssid2", None) == "":
        changes.pop("wifi_ssid2")
        changes.pop("wifi_password2", None)
    if "server_port" in changes:
        try:
            changes["server_port"] = int(changes["server_port"])
        except (TypeError, ValueError):
            bad("Server port must be a number.")
        if not 1 <= changes["server_port"] <= 65535:
            bad("Server port must be 1-65535.")
    for ssid, pw in (("wifi_ssid", "wifi_password"), ("wifi_ssid2", "wifi_password2")):
        if changes.get(ssid):
            if len(changes[ssid]) > 32:
                bad("Wi-Fi network names are at most 32 characters.")
            if not changes.get(pw) or not 8 <= len(changes[pw]) <= 63:
                bad("When changing a Wi-Fi network, also enter its password (8-63 characters).")
        elif changes.get(pw):
            bad("A Wi-Fi password needs its network name too.")
    if "use_static_ip" in changes:
        changes["use_static_ip"] = bool(changes["use_static_ip"])
    for key in ("static_ip", "gateway", "subnet", "dns"):
        if key in changes:
            try:
                ipaddress.IPv4Address(changes[key])
            except ValueError:
                bad(f"{FIELD_LABELS[key].capitalize()} must be an IPv4 address like 192.168.2.60.")
    if changes.get("use_static_ip") and not all(k in changes for k in ("static_ip", "gateway", "subnet")):
        bad("Turning on a fixed IP needs the IP address, gateway and subnet mask.")
    if not changes:
        bad("Nothing to change: fill in at least one field.")
    return changes


def merged_changes(cfg, after_version):
    merged = {}
    for entry in cfg["changes"]:
        if entry["version"] > after_version:
            merged.update(entry["settings"])
    return merged


def change_status(entry, state):
    applied = int(state.get("cfg_applied") or 0)
    failed = int(state.get("cfg_failed") or 0)
    if entry["version"] <= applied:
        return "applied"
    if entry["version"] == failed:
        return "failed"
    return "waiting"


def public_change(entry, state):
    shown = {k: ("••••••" if k in SECRET_FIELDS and v else v) for k, v in entry["settings"].items()}
    return {"version": entry["version"], "created_iso": entry["created_iso"], "settings": shown,
            "status": change_status(entry, state)}


def firmware_meta(db):
    meta = db.get("firmware")
    return meta if meta and FIRMWARE_PATH.exists() else None


def update_headers(db):
    headers = {}
    version = frame_config(db)["version"]
    if version:
        headers["X-Config-Version"] = str(version)
    meta = firmware_meta(db)
    if meta:
        headers["X-Firmware-Version"] = meta["version"]
    return headers


def record_frame_report(state, query):
    """Store what the frame says about remote settings and firmware updates."""
    for src, dst in (("cfg", "cfg_applied"), ("cfg_fail", "cfg_failed")):
        if src in query:
            try:
                state[dst] = int(query[src])
            except ValueError:
                pass
    for src, dst in (("cfg_err", "cfg_error"), ("fw_fail", "fw_failed"), ("fw_err", "fw_error")):
        if src in query:
            state[dst] = query[src][:120]
    # Problems the frame hit since its last check-in (the panel itself just
    # keeps showing the old picture, so this is the only way to see them).
    if query.get("fw"):
        error = (query.get("err") or "")[:240] or None
        if error and error != state.get("frame_error"):
            state["frame_error_iso"] = local_now().isoformat(timespec="seconds")
            log.warning("Frame reported a problem: %s", error)
        state["frame_error"] = error
    if query.get("busy_blips"):
        try:
            state["busy_blips"] = int(query["busy_blips"])
        except ValueError:
            pass
    if query.get("refresh_ms"):
        try:
            state["refresh_s"] = round(int(query["refresh_ms"]) / 1000, 1)
        except ValueError:
            pass


# ---------------------------------------------------------------- HA bridge
async def push_states(app, state, image_name):
    if not SUPERVISOR_TOKEN:
        return
    headers = {"Authorization": f"Bearer {SUPERVISOR_TOKEN}"}
    pct = state.get("battery_percent")
    entities = {
        "sensor.eink_frame_battery": (
            pct if pct is not None else "unknown",
            {"unit_of_measurement": "%", "device_class": "battery", "state_class": "measurement",
             "friendly_name": "E-Ink Frame battery", "voltage_mv": state.get("battery_mv")}),
        "sensor.eink_frame_last_seen": (
            state.get("last_seen_iso", "unknown"),
            {"device_class": "timestamp", "friendly_name": "E-Ink Frame last check-in",
             "wifi_rssi": state.get("rssi"), "firmware": state.get("fw"),
             "next_wake": state.get("next_wake_iso")}),
        "sensor.eink_frame_status": (
            "problem" if state.get("frame_error") else "ok",
            {"friendly_name": "E-Ink Frame status",
             "icon": "mdi:alert-circle" if state.get("frame_error") else "mdi:check-circle",
             "last_problem": state.get("frame_error"), "problem_since": state.get("frame_error_iso"),
             "last_refresh_seconds": state.get("refresh_s"), "last_wake_reason": state.get("last_reason"),
             "busy_blips_ignored": state.get("busy_blips")}),
        "sensor.eink_frame_image": (
            image_name or "unknown",
            {"friendly_name": "E-Ink Frame image", "icon": "mdi:image-frame",
             "image_count": state.get("image_count")}),
    }
    try:
        session = app["http"]
        for entity_id, (value, attrs) in entities.items():
            await session.post(f"{HA_API}/states/{entity_id}", headers=headers,
                               json={"state": value, "attributes": attrs})
        threshold = app["opts"]["low_battery_percent"]
        if pct is not None and pct <= threshold and not state.get("low_battery_notified"):
            await session.post(
                f"{HA_API}/services/persistent_notification/create", headers=headers,
                json={"notification_id": "eink_frame_battery", "title": "E-Ink Frame",
                      "message": f"Frame battery is at {pct}%. Time to charge it."})
            state["low_battery_notified"] = True
        elif pct is not None and pct > threshold + 10:
            state["low_battery_notified"] = False
        error = state.get("frame_error")
        if error and state.get("error_notified") != error:
            await session.post(
                f"{HA_API}/services/persistent_notification/create", headers=headers,
                json={"notification_id": "eink_frame_problem", "title": "E-Ink Frame problem",
                      "message": f"{error}\n\nThe panel keeps showing the old picture until this is fixed. "
                                 "See DEBUGGING.md."})
            state["error_notified"] = error
        elif not error and state.get("error_notified"):
            await session.post(
                f"{HA_API}/services/persistent_notification/dismiss", headers=headers,
                json={"notification_id": "eink_frame_problem"})
            state["error_notified"] = None
    except Exception:  # noqa: BLE001 - HA being unavailable must not break the frame
        log.warning("Could not update Home Assistant entities", exc_info=True)


async def push_config_mode(app, on, ip, since_iso, notify=True):
    """Mirror config mode into HA: a binary sensor plus a hard-to-miss notification."""
    if not SUPERVISOR_TOKEN:
        return
    headers = {"Authorization": f"Bearer {SUPERVISOR_TOKEN}"}
    session = app["http"]
    try:
        await session.post(
            f"{HA_API}/states/binary_sensor.eink_frame_config_mode", headers=headers,
            json={"state": "on" if on else "off",
                  "attributes": {"friendly_name": "E-Ink Frame config mode", "icon": "mdi:cog-transfer",
                                 "device_class": "problem", "frame_ip": ip, "since": since_iso}})
        if not notify:
            return
        if on:
            link = f"http://{ip}/" if ip and ip != "0.0.0.0" else "the EInkFrame hotspot"
            await session.post(
                f"{HA_API}/services/persistent_notification/create", headers=headers,
                json={"notification_id": "eink_frame_config", "title": "E-Ink Frame config mode is ON",
                      "message": ("The frame is staying awake and draining its battery (roughly a day "
                                  f"on a full charge). Config page: {link}. Turn the config switch off "
                                  "when you're done.")})
        else:
            await session.post(
                f"{HA_API}/services/persistent_notification/dismiss", headers=headers,
                json={"notification_id": "eink_frame_config"})
    except Exception:  # noqa: BLE001
        log.warning("Could not update config-mode entity", exc_info=True)


async def frame_heartbeat(request):
    """Sent every minute while the frame is in config mode, and once when it leaves."""
    app = request.app["main"]
    token = app["opts"].get("frame_token") or ""
    supplied = request.query.get("token") or request.headers.get("X-Frame-Token", "")
    if token and supplied != token:
        raise web.HTTPUnauthorized(text="bad token")

    on = request.query.get("config") == "1"
    now = local_now()
    ip = request.query.get("ip") or request.remote
    async with db_lock:
        db = load_db()
        state = db["state"]
        was_on = bool(state.get("config_mode"))
        if on and not was_on:
            state["config_since_iso"] = now.isoformat(timespec="seconds")
        state["config_mode"] = on
        state["config_ip"] = ip
        state["config_last_iso"] = now.isoformat(timespec="seconds")
        try:
            mv = int(request.query.get("battery_mv", "0"))
        except ValueError:
            mv = 0
        if mv:
            state["battery_mv"] = mv
            state["battery_percent"] = battery_percent(mv)
        state["rssi"] = request.query.get("rssi", state.get("rssi"))
        state["fw"] = request.query.get("fw", state.get("fw"))
        since = state.get("config_since_iso")
        save_db(db)
    await push_config_mode(app, on, ip, since, notify=(on != was_on))
    log.info("Frame heartbeat: config mode %s (%s)", "ON" if on else "off", ip)
    return web.json_response({"ok": True})


# ---------------------------------------------------------------- frame API
async def frame_next(request):
    app = request.app["main"]
    opts = app["opts"]
    token = opts.get("frame_token") or ""
    supplied = request.query.get("token") or request.headers.get("X-Frame-Token", "")
    if token and supplied != token:
        raise web.HTTPUnauthorized(text="bad token")

    reason = request.query.get("reason", "timer")
    now = local_now()
    sleep = next_sleep_seconds(opts, now)

    async with db_lock:
        db = load_db()
        state = db["state"]
        if request.query.get("current"):
            state["current_id"] = request.query["current"]
        config_was_on = bool(state.pop("config_mode", False))
        record_frame_report(state, request.query)
        extra_headers = update_headers(db)
        try:
            mv = int(request.query.get("battery_mv", "0"))
        except ValueError:
            mv = 0
        state.update({
            "last_seen": time.time(),
            "last_seen_iso": now.isoformat(timespec="seconds"),
            "battery_mv": mv or None,
            "battery_percent": battery_percent(mv),
            "rssi": request.query.get("rssi"),
            "fw": request.query.get("fw"),
            "last_reason": reason,
            "next_wake_iso": (now + timedelta(seconds=sleep)).isoformat(timespec="seconds"),
            "image_count": len(db["images"]),
        })

        in_quiet = seconds_until_quiet_ends(opts, now) > 0
        chosen = None if (in_quiet and reason == "timer") else choose_next(db, opts, reason, now)
        if chosen == state.get("current_id"):
            chosen = None
        if chosen:
            state["current_id"] = chosen
        names = {e["id"]: e.get("name") for e in db["images"]}
        save_db(db)
        state_copy = dict(state)

    await push_states(app, state_copy, names.get(state_copy.get("current_id")))
    if config_was_on:
        await push_config_mode(app, False, None, None)
    async with db_lock:  # persist the low-battery flag push_states may have changed
        db = load_db()
        db["state"]["low_battery_notified"] = state_copy.get("low_battery_notified", False)
        db["state"]["error_notified"] = state_copy.get("error_notified")
        save_db(db)

    headers = {"X-Sleep-Seconds": str(sleep), **extra_headers}
    if not chosen:
        log.info("Frame check-in (%s): keep current image, sleep %ss", reason, sleep)
        return web.Response(status=304, headers=headers)

    data = image_path(chosen, "bin").read_bytes()
    headers["X-Image-Id"] = chosen
    headers["X-Frame-MD5"] = hashlib.md5(data).hexdigest()  # lets the frame prove it got every byte
    log.info("Frame check-in (%s): sending %s, sleep %ss", reason, names.get(chosen), sleep)
    return web.Response(body=data, content_type="application/octet-stream", headers=headers)


def check_frame_token(request):
    token = request.app["main"]["opts"].get("frame_token") or ""
    supplied = request.query.get("token") or request.headers.get("X-Frame-Token", "")
    if token and supplied != token:
        raise web.HTTPUnauthorized(text="bad token")


async def frame_config_get(request):
    """Plain key=value lines (percent-encoded values) so the ESP32 needs no JSON parser."""
    check_frame_token(request)
    try:
        after = int(request.query.get("from", "0"))
    except ValueError:
        after = 0
    db = load_db()
    cfg = frame_config(db)
    lines = [f"version={cfg['version']}"]
    for key, value in merged_changes(cfg, after).items():
        if isinstance(value, bool):
            value = "1" if value else "0"
        lines.append(f"{key}={quote(str(value), safe='')}")
    return web.Response(text="\n".join(lines) + "\n", content_type="text/plain")


async def frame_firmware_get(request):
    check_frame_token(request)
    if not FIRMWARE_PATH.exists():
        raise web.HTTPNotFound()
    meta = firmware_meta(load_db()) or {}
    return web.FileResponse(FIRMWARE_PATH, headers={
        "Content-Type": "application/octet-stream",
        "x-MD5": meta.get("md5", ""),
    })


async def frame_health(request):
    return web.json_response({"ok": True, "bytes": imaging.FRAME_BYTES})


# ---------------------------------------------------------------- UI API
async def index(request):
    return web.FileResponse(STATIC_DIR / "index.html")


def public_entry(entry, current_id):
    return {**entry, "current": entry["id"] == current_id}


async def list_images(request):
    db = load_db()
    state = db["state"]
    current = state.get("current_id")
    return web.json_response({
        "images": [public_entry(e, current) for e in db["images"]],
        "state": {k: state.get(k) for k in (
            "last_seen_iso", "battery_mv", "battery_percent", "rssi", "fw", "next_wake_iso",
            "current_id", "queue_next", "paused", "last_reason",
            "config_mode", "config_ip", "config_since_iso", "config_last_iso")},
        "options": {k: request.app["opts"][k] for k in (
            "interval_minutes", "quiet_start", "quiet_end", "orientation", "shuffle")},
    })


async def upload_images(request):
    opts = request.app["opts"]
    reader = await request.multipart()
    mode, fit, files = "photo", "crop", []
    while True:
        part = await reader.next()
        if part is None:
            break
        if part.name == "mode":
            mode = (await part.text()).strip()
        elif part.name == "fit":
            fit = (await part.text()).strip()
        elif part.name == "files":
            files.append((part.filename or "image", await part.read(decode=False)))
    if mode not in imaging.MODES or fit not in imaging.FITS:
        raise web.HTTPBadRequest(text="bad mode or fit")

    loop = asyncio.get_running_loop()
    added, errors = [], []
    for filename, data in files:
        image_id = uuid.uuid4().hex[:12]
        try:
            await loop.run_in_executor(None, process_upload, data, mode, fit, opts, image_id)
        except Exception as err:  # noqa: BLE001 - report per-file problems to the UI
            log.exception("Could not convert %s", filename)
            errors.append(f"{filename}: {err}")
            continue
        added.append({"id": image_id, "name": Path(filename).stem[:60], "mode": mode, "fit": fit,
                      "enabled": True, "show_on": None, "created": int(time.time())})

    async with db_lock:
        db = load_db()
        db["images"].extend(added)
        db["render"] = render_settings(opts)
        save_db(db)
    return web.json_response({"added": added, "errors": errors})


async def update_image(request):
    image_id = request.match_info["image_id"]
    body = await request.json()
    async with db_lock:
        db = load_db()
        entry = next((e for e in db["images"] if e["id"] == image_id), None)
        if not entry:
            raise web.HTTPNotFound()
        if "enabled" in body:
            entry["enabled"] = bool(body["enabled"])
        if "name" in body:
            entry["name"] = str(body["name"])[:60]
        if "show_on" in body:
            value = (body["show_on"] or "").strip()
            if value and not (len(value) == 5 and value[2] == "-" and value.replace("-", "").isdigit()):
                raise web.HTTPBadRequest(text="show_on must be MM-DD")
            entry["show_on"] = value or None
        reprocess_needed = False
        for key, allowed in (("mode", imaging.MODES), ("fit", imaging.FITS)):
            if key in body and body[key] in allowed and body[key] != entry[key]:
                entry[key] = body[key]
                reprocess_needed = True
        if reprocess_needed:
            await asyncio.get_running_loop().run_in_executor(None, reprocess, entry, request.app["opts"])
        save_db(db)
    return web.json_response(entry)


async def delete_image(request):
    image_id = request.match_info["image_id"]
    async with db_lock:
        db = load_db()
        db["images"] = [e for e in db["images"] if e["id"] != image_id]
        if db["state"].get("queue_next") == image_id:
            db["state"].pop("queue_next")
        save_db(db)
    folder = IMAGES_DIR / image_id
    if folder.exists():
        for f in folder.iterdir():
            f.unlink()
        folder.rmdir()
    return web.json_response({"deleted": image_id})


async def show_next(request):
    image_id = request.match_info["image_id"]
    async with db_lock:
        db = load_db()
        if not any(e["id"] == image_id for e in db["images"]):
            raise web.HTTPNotFound()
        db["state"]["queue_next"] = image_id
        save_db(db)
    return web.json_response({"queued": image_id})


async def set_paused(request):
    body = await request.json()
    async with db_lock:
        db = load_db()
        db["state"]["paused"] = bool(body.get("paused"))
        save_db(db)
    return web.json_response({"paused": db["state"]["paused"]})


async def reorder(request):
    body = await request.json()
    order = body.get("order", [])
    async with db_lock:
        db = load_db()
        by_id = {e["id"]: e for e in db["images"]}
        ordered = [by_id.pop(i) for i in order if i in by_id]
        db["images"] = ordered + list(by_id.values())
        save_db(db)
    return web.json_response({"ok": True})


def image_file(kind, content_type):
    async def handler(request):
        path = image_path(request.match_info["image_id"], kind)
        if not path.exists():
            raise web.HTTPNotFound()
        return web.FileResponse(path, headers={"Content-Type": content_type,
                                               "Cache-Control": "max-age=300"})
    return handler


async def download_bin(request):
    """Ready-made 960,000-byte frame for uploading on the frame's config page."""
    image_id = request.match_info["image_id"]
    path = image_path(image_id, "bin")
    if not path.exists():
        raise web.HTTPNotFound()
    name = next((e.get("name") for e in load_db()["images"] if e["id"] == image_id), image_id)
    safe = "".join(c if c.isalnum() or c in "-_ " else "_" for c in (name or image_id)).strip() or image_id
    return web.FileResponse(path, headers={
        "Content-Type": "application/octet-stream",
        "Content-Disposition": f'attachment; filename="{safe}.bin"'})


async def get_frame_settings(request):
    db = load_db()
    state = db["state"]
    cfg = frame_config(db)
    return web.json_response({
        "version": cfg["version"],
        "changes": [public_change(e, state) for e in reversed(cfg["changes"][-20:])],
        "frame": {k: state.get(k) for k in (
            "cfg_applied", "cfg_failed", "cfg_error", "fw", "fw_failed", "fw_error", "last_seen_iso")},
        "firmware": firmware_meta(db),
    })


async def post_frame_settings(request):
    changes = validate_changes(await request.json())
    async with db_lock:
        db = load_db()
        cfg = frame_config(db)
        cfg["version"] += 1
        cfg["changes"].append({"version": cfg["version"], "settings": changes,
                               "created_iso": local_now().isoformat(timespec="seconds")})
        cfg["changes"] = cfg["changes"][-50:]
        save_db(db)
    log.info("Frame settings version %d: %s", cfg["version"], ", ".join(sorted(changes)))
    return web.json_response({"version": cfg["version"]})


def parse_firmware(data):
    if len(data) < 64 * 1024 or data[0] != 0xE9:
        raise web.HTTPBadRequest(text="That isn't an ESP32 firmware image (use Export Compiled Binary's eink_frame.ino.bin).")
    match = FIRMWARE_TAG.search(data)
    if not match:
        raise web.HTTPBadRequest(text="This firmware isn't the E-Ink Frame firmware (no version tag found).")
    return match.group(1).decode()


async def upload_firmware(request):
    reader = await request.multipart()
    data = b""
    while True:
        part = await reader.next()
        if part is None:
            break
        if part.name == "firmware":
            data = await part.read(decode=False)
    version = parse_firmware(data)
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    tmp = FIRMWARE_PATH.with_suffix(".tmp")
    tmp.write_bytes(data)
    tmp.replace(FIRMWARE_PATH)
    meta = {"version": version, "size": len(data), "md5": hashlib.md5(data).hexdigest(),
            "uploaded_iso": local_now().isoformat(timespec="seconds")}
    async with db_lock:
        db = load_db()
        db["firmware"] = meta
        save_db(db)
    log.info("Firmware %s uploaded (%d bytes)", version, len(data))
    return web.json_response(meta)


async def delete_firmware(request):
    async with db_lock:
        db = load_db()
        db.pop("firmware", None)
        save_db(db)
    if FIRMWARE_PATH.exists():
        FIRMWARE_PATH.unlink()
    return web.json_response({"deleted": True})


# ---------------------------------------------------------------- startup
async def on_startup(app):
    app["http"] = ClientSession()
    await ensure_render_settings(app)


async def on_cleanup(app):
    await app["http"].close()


def build_ui_app(opts):
    app = web.Application(client_max_size=40 * 1024 * 1024)
    app["opts"] = opts
    app.router.add_get("/", index)
    app.router.add_static("/static/", STATIC_DIR)
    app.router.add_get("/api/images", list_images)
    app.router.add_post("/api/images", upload_images)
    app.router.add_patch("/api/images/{image_id}", update_image)
    app.router.add_delete("/api/images/{image_id}", delete_image)
    app.router.add_post("/api/images/{image_id}/show_next", show_next)
    app.router.add_get("/api/images/{image_id}/preview.png", image_file("preview", "image/png"))
    app.router.add_get("/api/images/{image_id}/thumb.jpg", image_file("thumb", "image/jpeg"))
    app.router.add_get("/api/images/{image_id}/frame.bin", download_bin)
    app.router.add_post("/api/paused", set_paused)
    app.router.add_get("/api/frame_settings", get_frame_settings)
    app.router.add_post("/api/frame_settings", post_frame_settings)
    app.router.add_post("/api/firmware", upload_firmware)
    app.router.add_delete("/api/firmware", delete_firmware)
    app.router.add_post("/api/order", reorder)
    app.on_startup.append(on_startup)
    app.on_cleanup.append(on_cleanup)
    return app


def build_frame_app(main_app):
    app = web.Application()
    app["main"] = main_app
    app.router.add_get("/frame/next", frame_next)
    app.router.add_get("/frame/heartbeat", frame_heartbeat)
    app.router.add_get("/frame/config", frame_config_get)
    app.router.add_get("/frame/firmware", frame_firmware_get)
    app.router.add_get("/frame/health", frame_health)
    return app


async def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    # staticmethod: a plain lambda on the class would be called with `self` too.
    logging.Formatter.converter = staticmethod(lambda ts: datetime.fromtimestamp(ts, LOCAL_TZ).timetuple())
    logging.getLogger("aiohttp.access").addFilter(HideToken())
    IMAGES_DIR.mkdir(parents=True, exist_ok=True)
    opts = load_options()
    await resolve_timezone(opts)
    ui_app = build_ui_app(opts)
    frame_app = build_frame_app(ui_app)

    # No %t: the line already starts with the local time, and aiohttp's own
    # timestamp would be in the container's UTC.
    access_format = '%a "%r" %s %b "%{User-Agent}i"'
    ui_runner = web.AppRunner(ui_app, access_log_format=access_format)
    frame_runner = web.AppRunner(frame_app, access_log_format=access_format)
    await ui_runner.setup()
    await frame_runner.setup()
    await web.TCPSite(ui_runner, "0.0.0.0", UI_PORT).start()
    await web.TCPSite(frame_runner, "0.0.0.0", FRAME_PORT).start()
    log.info("UI on :%d, frame API on :%d, %d-byte frames", UI_PORT, FRAME_PORT, imaging.FRAME_BYTES)
    if not opts.get("frame_token"):
        log.warning("frame_token is empty: anyone on your network can read the frame API")
    await asyncio.Event().wait()


if __name__ == "__main__":
    asyncio.run(main())
