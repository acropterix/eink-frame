// All URLs are relative so the page works behind Home Assistant ingress.
const COLOURS = [
  ["Black", "#000000"], ["White", "#ffffff"], ["Yellow", "#ffe600"],
  ["Red", "#c80000"], ["Blue", "#003cc8"], ["Green", "#008c3c"],
];

const $ = (sel) => document.querySelector(sel);
let images = [];
let portrait = false;

async function api(path, options = {}) {
  const res = await fetch(path, options);
  if (!res.ok) throw new Error(`${res.status} ${await res.text()}`);
  return res.json();
}

function ago(iso) {
  if (!iso) return "never";
  const mins = Math.round((Date.now() - new Date(iso).getTime()) / 60000);
  if (mins < 1) return "just now";
  if (mins < 60) return `${mins} min ago`;
  const hours = Math.round(mins / 60);
  return hours < 48 ? `${hours} h ago` : `${Math.round(hours / 24)} days ago`;
}

function timeOf(iso) {
  return iso ? new Date(iso).toLocaleString([], { weekday: "short", hour: "numeric", minute: "2-digit" }) : "—";
}

function renderConfigBanner(state) {
  const banner = $("#config-banner");
  banner.hidden = !state.config_mode;
  if (!state.config_mode) return;
  const since = state.config_since_iso ? ago(state.config_since_iso) : "";
  const awake = !since || since === "just now"
    ? "The frame just entered config mode."
    : `The frame has been awake since ${since}.`;
  const staleMins = state.config_last_iso
    ? (Date.now() - new Date(state.config_last_iso).getTime()) / 60000 : 0;
  const text = $("#config-text");
  text.textContent = `${awake} It's draining its battery (roughly a day on a full charge). ` +
    "Turn the config switch off when you're done. ";
  if (state.config_ip) {
    const link = document.createElement("a");
    link.href = `http://${state.config_ip}/`;
    link.target = "_blank";
    link.rel = "noopener";
    link.textContent = `Open the frame's config page (${state.config_ip})`;
    text.append(link);
  }
  if (staleMins > 3) {
    text.append(` · No heartbeat for ${Math.round(staleMins)} min; the frame may have lost Wi-Fi or its battery.`);
  }
}

function renderStatus(state, options) {
  const battery = state.battery_percent != null
    ? `${state.battery_percent}% (${(state.battery_mv / 1000).toFixed(2)} V)` : "unknown";
  $("#status").textContent =
    `Last check-in ${ago(state.last_seen_iso)} · Battery ${battery} · Next wake ${timeOf(state.next_wake_iso)}` +
    ` · Every ${options.interval_minutes} min, quiet ${options.quiet_start}–${options.quiet_end}` +
    (state.refresh_s ? ` · Last panel refresh ${state.refresh_s} s` : "") +
    (state.frame_error ? ` · ⚠️ Frame problem: ${state.frame_error}` : "");
  $("#paused").checked = !!state.paused;
}

function renderGallery(state) {
  const gallery = $("#gallery");
  gallery.replaceChildren();
  if (!images.length) {
    gallery.textContent = "No pictures yet. Upload one above.";
    return;
  }
  images.forEach((img, index) => {
    const tile = $("#tile").content.firstElementChild.cloneNode(true);
    tile.classList.toggle("portrait", portrait);
    tile.classList.toggle("off", !img.enabled);
    const pic = tile.querySelector("img");
    pic.src = `api/images/${img.id}/thumb.jpg?v=${img.created}`;
    pic.alt = img.name;
    pic.addEventListener("click", () => window.open(`api/images/${img.id}/preview.png`, "_blank"));

    const badges = [];
    if (img.current) badges.push("On the frame");
    if (state.queue_next === img.id) badges.push("Showing next");
    if (img.show_on) badges.push(`Every ${img.show_on}`);
    if (badges.length) {
      const badge = document.createElement("span");
      badge.className = "badge";
      badge.textContent = badges.join(" · ");
      tile.append(badge);
    }

    const name = tile.querySelector(".name");
    name.value = img.name;
    name.addEventListener("change", () => patch(img.id, { name: name.value }));

    const enabled = tile.querySelector(".enabled");
    enabled.checked = img.enabled;
    enabled.addEventListener("change", () => patch(img.id, { enabled: enabled.checked }));

    const showOn = tile.querySelector(".show-on");
    showOn.value = img.show_on || "";
    showOn.addEventListener("change", () => patch(img.id, { show_on: showOn.value }));

    tile.querySelector(".next").addEventListener("click", async () => {
      await api(`api/images/${img.id}/show_next`, { method: "POST" });
      refresh();
    });
    const download = tile.querySelector(".download");
    download.href = `api/images/${img.id}/frame.bin`;
    download.title = "Download the ready-made file for the frame's config page";
    tile.querySelector(".up").disabled = index === 0;
    tile.querySelector(".down").disabled = index === images.length - 1;
    tile.querySelector(".up").addEventListener("click", () => move(index, -1));
    tile.querySelector(".down").addEventListener("click", () => move(index, 1));
    tile.querySelector(".delete").addEventListener("click", async () => {
      if (!confirm(`Delete "${img.name}"?`)) return;
      await api(`api/images/${img.id}`, { method: "DELETE" });
      refresh();
    });
    gallery.append(tile);
  });
}

async function patch(id, body) {
  try {
    await api(`api/images/${id}`, {
      method: "PATCH", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
    });
  } catch (err) {
    alert(err.message);
  }
  refresh();
}

async function move(index, delta) {
  const order = images.map((i) => i.id);
  const [item] = order.splice(index, 1);
  order.splice(index + delta, 0, item);
  await api("api/order", {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ order }),
  });
  refresh();
}

async function refresh() {
  const data = await api("api/images");
  images = data.images;
  const wasPortrait = portrait;
  portrait = data.options.orientation === "portrait";
  if (portrait !== wasPortrait) resizePad();
  renderStatus(data.state, data.options);
  renderConfigBanner(data.state);
  renderGallery(data.state);
  refreshFrameSettings().catch(() => {});
}

// ------------------------------------------------------------- frame settings + firmware
const STATUS_TEXT = { applied: "Applied", waiting: "Waiting for frame", failed: "Failed, frame reverted" };

function cell(row, text, className) {
  const td = document.createElement("td");
  if (className) {
    const pill = document.createElement("span");
    pill.className = `pill ${className}`;
    pill.textContent = text;
    td.append(pill);
  } else {
    td.textContent = text;
  }
  row.append(td);
}

async function refreshFrameSettings() {
  const data = await api("api/frame_settings");
  const f = data.frame;
  let stateText = data.version
    ? `Latest settings version ${data.version}; the frame has applied version ${f.cfg_applied || 0}.`
    : "No settings have been sent to the frame yet.";
  if (f.cfg_failed && f.cfg_failed > (f.cfg_applied || 0)) {
    stateText += ` Version ${f.cfg_failed} failed (${f.cfg_error || "unknown reason"}), so the frame kept its previous settings.`;
  }
  $("#frame-cfg-state").textContent = stateText;

  const table = $("#history");
  table.replaceChildren();
  if (data.changes.length) {
    const head = document.createElement("tr");
    ["Version", "Sent", "Changes", "Status"].forEach((h) => {
      const th = document.createElement("th");
      th.textContent = h;
      head.append(th);
    });
    table.append(head);
    for (const c of data.changes) {
      const row = document.createElement("tr");
      cell(row, String(c.version));
      cell(row, timeOf(c.created_iso));
      cell(row, Object.entries(c.settings).map(([k, v]) => `${k.replaceAll("_", " ")}: ${v === "" ? "(removed)" : v}`).join(", "));
      cell(row, STATUS_TEXT[c.status], c.status);
      table.append(row);
    }
  }

  const fw = data.firmware;
  const running = f.fw ? `The frame is running firmware ${f.fw}.` : "The frame hasn't reported its firmware yet.";
  let fwText = running;
  if (fw) {
    fwText += fw.version === f.fw
      ? ` Uploaded firmware ${fw.version} is installed.`
      : ` Firmware ${fw.version} for ${fw.board || "unknown board"} (${Math.round(fw.size / 1024)} KB) is waiting to be installed at the next check-in.`;
    if (fw.chip != null && f.chip != null && fw.chip !== f.chip) {
      fwText += ` ⚠️ But this file was built for ${fw.board} and the frame is a different board, so it won't be offered. Export it again with the frame's board selected in Arduino IDE.`;
    }
  }
  if (f.fw_failed && fw && f.fw_failed === fw.version) {
    fwText += ` Installing ${f.fw_failed} failed: ${f.fw_error || "unknown error"}. Upload a fixed build to try again.`;
  }
  $("#fw-state").textContent = fwText;
  $("#fw-delete").hidden = !fw;
}

$("#frame-settings").addEventListener("submit", async (event) => {
  event.preventDefault();
  const form = event.target;
  const body = {};
  for (const el of form.elements) {
    if (!el.name) continue;
    if (el.type === "checkbox") body[el.name] = el.checked;
    else if (el.name === "use_static_ip") { if (el.value !== "") body[el.name] = el.value === "1"; }
    else if (el.value.trim() !== "") body[el.name] = el.value.trim();
  }
  const msg = $("#settings-msg");
  msg.className = "msg";
  try {
    const res = await api("api/frame_settings", {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
    });
    msg.textContent = `Saved as version ${res.version}. The frame picks it up at its next check-in (press its button to hurry it).`;
    form.reset();
    refreshFrameSettings();
  } catch (err) {
    msg.className = "msg error";
    msg.textContent = err.message.replace(/^\d+ /, "");
  }
});

$("#fw-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const file = $("#fw-file").files[0];
  const msg = $("#fw-msg");
  msg.className = "msg";
  if (!file) { msg.textContent = "Choose eink_frame.ino.bin first."; return; }
  const form = new FormData();
  form.append("firmware", file, file.name);
  msg.textContent = "Uploading…";
  try {
    const meta = await api("api/firmware", { method: "POST", body: form });
    msg.textContent = `Firmware ${meta.version} uploaded. It installs at the frame's next check-in.`;
    $("#fw-file").value = "";
    refreshFrameSettings();
  } catch (err) {
    msg.className = "msg error";
    msg.textContent = err.message.replace(/^\d+ /, "");
  }
});

$("#fw-delete").addEventListener("click", async () => {
  if (!confirm("Remove the uploaded firmware? The frame will keep what it's running.")) return;
  await api("api/firmware", { method: "DELETE" });
  $("#fw-msg").textContent = "Removed.";
  refreshFrameSettings();
});

// ------------------------------------------------------------- upload
async function uploadForm(formData, msgEl, button) {
  button.disabled = true;
  msgEl.className = "msg";
  msgEl.textContent = "Converting… (a few seconds per picture)";
  try {
    const result = await api("api/images", { method: "POST", body: formData });
    msgEl.textContent = `Added ${result.added.length} picture(s).`;
    if (result.errors.length) {
      msgEl.className = "msg error";
      msgEl.textContent += ` Problems: ${result.errors.join("; ")}`;
    }
    refresh();
    return true;
  } catch (err) {
    msgEl.className = "msg error";
    msgEl.textContent = `Upload failed: ${err.message}`;
    return false;
  } finally {
    button.disabled = false;
  }
}

$("#upload-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const files = $("#files").files;
  if (!files.length) {
    $("#upload-msg").textContent = "Pick at least one picture first.";
    return;
  }
  const form = new FormData();
  form.append("mode", $("#mode").value);
  form.append("fit", $("#fit").value);
  for (const f of files) form.append("files", f, f.name);
  if (await uploadForm(form, $("#upload-msg"), $("#upload-btn"))) {
    $("#files").value = "";
    $("#drop-text").textContent = "Choose photos or drawings (or drop them here)";
  }
});

$("#files").addEventListener("change", () => {
  const n = $("#files").files.length;
  $("#drop-text").textContent = n ? `${n} picture(s) selected` : "Choose photos or drawings (or drop them here)";
});
const drop = $("#drop");
["dragenter", "dragover"].forEach((t) => drop.addEventListener(t, (e) => { e.preventDefault(); drop.classList.add("over"); }));
["dragleave", "drop"].forEach((t) => drop.addEventListener(t, () => drop.classList.remove("over")));
drop.addEventListener("drop", (e) => {
  e.preventDefault();
  $("#files").files = e.dataTransfer.files;
  $("#files").dispatchEvent(new Event("change"));
});

$("#paused").addEventListener("change", async () => {
  await api("api/paused", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ paused: $("#paused").checked }),
  });
});

// ------------------------------------------------------------- drawing pad
const pad = $("#pad");
const ctx = pad.getContext("2d");
let colour = COLOURS[0][1];
let drawing = false;
let last = null;
const history = [];

function resetPad() {
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, pad.width, pad.height);
}

function resizePad() {
  pad.width = portrait ? 1200 : 1600;
  pad.height = portrait ? 1600 : 1200;
  history.length = 0;
  resetPad();
}

COLOURS.forEach(([label, hex], i) => {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "swatch" + (i === 0 ? " active" : "");
  b.style.background = hex;
  b.title = label;
  b.setAttribute("aria-label", label);
  b.addEventListener("click", () => {
    colour = hex;
    document.querySelectorAll(".swatch").forEach((s) => s.classList.remove("active"));
    b.classList.add("active");
  });
  $("#swatches").append(b);
});

function point(e) {
  const r = pad.getBoundingClientRect();
  return { x: (e.clientX - r.left) * (pad.width / r.width), y: (e.clientY - r.top) * (pad.height / r.height) };
}

pad.addEventListener("pointerdown", (e) => {
  history.push(ctx.getImageData(0, 0, pad.width, pad.height));
  if (history.length > 20) history.shift();
  drawing = true;
  last = point(e);
  pad.setPointerCapture(e.pointerId);
  stroke(last);
});
pad.addEventListener("pointermove", (e) => { if (drawing) stroke(point(e)); });
["pointerup", "pointercancel"].forEach((t) => pad.addEventListener(t, () => { drawing = false; }));

function stroke(p) {
  const size = Number($("#brush").value) * (pad.width / pad.getBoundingClientRect().width);
  ctx.strokeStyle = colour;
  ctx.lineWidth = size;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.beginPath();
  ctx.moveTo(last.x, last.y);
  ctx.lineTo(p.x, p.y);
  ctx.stroke();
  last = p;
}

$("#undo").addEventListener("click", () => { if (history.length) ctx.putImageData(history.pop(), 0, 0); });
$("#clear").addEventListener("click", () => { history.push(ctx.getImageData(0, 0, pad.width, pad.height)); resetPad(); });
$("#save-drawing").addEventListener("click", () => {
  pad.toBlob(async (blob) => {
    const form = new FormData();
    form.append("mode", "drawing");
    form.append("fit", "contain");
    const stamp = new Date().toLocaleDateString([], { month: "short", day: "numeric" });
    form.append("files", blob, `Drawing ${stamp}.png`);
    if (await uploadForm(form, $("#draw-msg"), $("#save-drawing"))) {
      history.length = 0;
      resetPad();
    }
  }, "image/png");
});

resetPad();
refresh().catch((err) => { $("#status").textContent = `Could not load: ${err.message}`; });
setInterval(() => {
  // Don't redraw the gallery while someone is typing a name or date.
  if ($("#gallery").contains(document.activeElement)) return;
  refresh().catch(() => {});
}, 60000);
