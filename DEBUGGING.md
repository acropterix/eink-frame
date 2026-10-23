# Debugging: "the picture never changes"

E-ink keeps its last picture with **no power at all**. A frame that is asleep, offline, out of battery or miswired looks exactly the same: the old picture. Go through these in order. Each step tells you which half of the problem you're in.

> The panel diagrams for step 5 are in [`wiring.html`](wiring.html) → **"When the picture won't change"**.

---

## 0. Is it actually supposed to change yet?

| Check | Where | Result |
|---|---|---|
| Change interval | Add-on → Configuration → `interval_minutes` | Default **60 min**. A change every few minutes won't happen. |
| Quiet hours | same, `quiet_start`/`quiet_end` | Default **22:00–07:00**: no changes overnight. |
| Paused | Add-on page → **Pause** switch | Paused = keeps the current picture (the button still skips). |
| Picture dated today | Gallery → date on a picture | A picture with today's `MM-DD` shows **all day**. |
| Enabled pictures | Gallery | Only one enabled = nothing to rotate to. |

---

## 1. Is the frame checking in? (Home Assistant, no tools)

Open **Settings → Entities** and search `eink_frame`.

| `sensor.eink_frame_last_seen` | `sensor.eink_frame_status` | Meaning → go to |
|---|---|---|
| Not updating for hours | — | Frame isn't waking or can't reach the server: **battery, Wi-Fi, server address** → step 3 |
| Updating every hour | **problem** | Frame runs but hit an error. The attribute **last_problem** says which → table in step 3 |
| Updating every hour | **ok**, `last_refresh_seconds` ≈ 19 | The panel really refreshed. If it still looks the same, the picture sent was the same → step 0 |
| Updating every hour | **ok**, no refresh time | Server keeps saying "keep the current picture" (304) → step 0 |

`sensor.eink_frame_image` only says what the **server sent**, not what the panel shows. The status sensor and the **E-Ink Frame problem** notification need **firmware 1.3.0** and **add-on 1.3.0**.

---

## 2. Press the button

Press the frame's button and watch the panel:

- **Flickers within ~10 s, finishes ~20 s later, new picture:** everything works. The schedule is the "problem" (step 0).
- **Nothing happens at all:** the frame isn't running (flat battery, power) or the panel isn't getting power or commands → step 3.
- **Flickers but ends on the same or a garbled picture:** data wires or a ribbon → step 5.

---

## 3. Read the frame's own log (USB + Serial Monitor)

Plug USB-C into the Mac, open **Tools → Serial Monitor** (115200), press **R** on the XIAO. A healthy run:

```
[boot] EINK-FRAME-FW:1.3.0, wake: boot, battery 0 mV, ...
[wifi] 'HomeWiFi' in 1840 ms, IP 192.168.2.60, RSSI -58
[http] status 200, sleep 3600 s, image 'a1b2c3'
[epd] refresh took 19234 ms
[epd] refreshed
```

| You see | What's wrong | Fix |
|---|---|---|
| Stops after `[boot] … battery critical` | Battery flat | Charge over USB-C |
| No `[wifi]` line / `could not join Wi-Fi` | Wi-Fi name/password, or the 5 GHz-only network | Config mode → settings |
| `[http] status -1` | Server address/port wrong, or the add-on stopped | Check `192.168.2.10:8200` and that the add-on is running |
| `[http] status 401` | Token mismatch | Same token in add-on and frame |
| `[http] status 304` | Server says keep current picture | Step 0 |
| `BUSY wire looks disconnected` | Nothing is driving BUSY | HAT `BUSY` → **D4**: housing fully on, right pin, wire intact |
| `BUSY wire isn't connected: nothing drives it` | Same, caught before the refresh | HAT `BUSY` → **D4** (left side, 5th pin). Beep D4 ↔ 40-pin socket pin 18 |
| `panel never went busy … isn't getting commands` | BUSY is fine, but the controller ignores the commands | DIN → D10, SCLK → D8, CS_M → D1, CS_S → D6, DC → D2, ribbon latches |
| `HAT held BUSY low … at start-up` | Panel never woke up: **power side** | PWR (D5), RST (D7), VCC→3V3, GND; all three ribbon latches; step 5 |
| `HAT held BUSY low … at power on` / `at refresh` | Panel woke but couldn't finish | Ribbons (most likely), then CS_M/CS_S |
| `refresh took ~19000 ms` but the panel didn't change | Commands sent, but the image data never arrived | DIN (D10), SCLK (D8), DC (D2), CS_M (D1) / CS_S (D6) |

---

## 3b. Live wiring test (no multimeter needed)

Best for **intermittent** faults, where one run says one thing and the next says another.

1. Frame on USB, **Serial Monitor** open at 115200, line ending **Newline**.
2. Press **R** on the XIAO. When it prints `Type test + Enter within 5 s`, type `test` and press Enter.
3. The panel is powered and reset, and the BUSY line is printed live:

| Prints | Means |
|---|---|
| `BUSY HIGH = ready` | Panel started. Wiring to it is fine. |
| `BUSY LOW = busy / panel not starting` | BUSY wire OK, but the panel isn't starting: PWR (D5), RST (D7), VCC, GND, ribbons |
| `BUSY FLOATING = BUSY wire not connected` | BUSY wire open between the panel and D4 |

4. **Wiggle one thing at a time** and watch for `<-- changed`: the BUSY jumper at D4, then the white plug in the HAT, then each ribbon latch. Whatever makes it change is the loose connection.
5. Type `r` + Enter to reset the panel again (a working panel goes `HIGH` within a second). Type `q` + Enter to finish.

**Fixing a loose jumper:** push the black housing fully down on the pin. If the metal crimp inside wiggles or was re-crimped, replace that wire. A male–female jumper from the HAT's 40-pin socket (BUSY = pin **18**) to **D4** works as a substitute.

---

## 4. The colour test (config mode)

1. Flip the **config switch** on (LED lights). **No switch fitted?** Press and **keep holding the button for 3 s**. Home Assistant shows the red config-mode banner within a few seconds.
2. On your phone or Mac open **http://eink-frame.local** (or join the `EInkFrame-xxxx` hotspot → `http://192.168.4.1`).
3. Press **Colour test**. Wait ~25 s for the message.

| Panel shows | Message | Problem |
|---|---|---|
| 6 bands across the **whole** width | "refresh took ~19 s" | Wiring is good. The problem is config or software → steps 0–1 |
| Bands on the **left** half only | — | **CS_S** wire (HAT `CS_S` → D6) |
| Bands on the **right** half only | — | **CS_M** wire (HAT `CS_M` → D1) |
| Noise, stripes, wrong colours | ~19 s | **DIN** (D10), **SCLK** (D8) or **DC** (D2) loose or swapped |
| No change, flickers | ~19 s | DIN/SCLK not arriving, or a ribbon is partly in |
| No change, no flicker | "BUSY wire looks disconnected" | **BUSY** wire (→ D4) |
| No change, no flicker | "HAT held BUSY low … start-up" | Power side: VCC, GND, PWR, RST, or a ribbon not latched |
| No change | "finished in N ms" | **BUSY** wire (→ D4) |

Use **Clear to white** to check the fix, then press **Get next picture from server**. Flip the switch off when done (no switch: hold the button 3 s again, press **Restart**, or wait 30 min).

---

## 5. Checking the ribbons and wires

It worked once, then stopped after re-wiring and mounting. That points at **something disturbed**, not a design problem. There are four connection points, so check each one:

```
 panel ──①── Adapter (B) ──②── white ribbon ──③── HAT ──④── 10 wires ── XIAO
```

**①②③ Ribbon connectors** (unplug USB and battery first):
1. Flip the black latch **up** (it hinges; don't pull it off).
2. Pull the ribbon out and look at it. The contacts should be clean and straight. A crease right at the connector can crack traces.
3. Push it back **straight and fully in**, contacts facing the **same way as before**. The edge of the stiffener should sit level with the connector, not skewed.
4. Press the latch **down** until it's flat. The ribbon shouldn't move when tugged lightly.
5. In the frame, make sure no ribbon is pulled tight or bent sharply at a connector. Leave a loose loop.

**④ The 10 jumper wires.** These changed when you shortened them, so they're the first suspect.
- Each black housing pushed fully onto its pin, none sitting on two pins or one pin off.
- Go **by the printed label on the HAT**, not the colour (table in `wiring.html`).
- If you cut and re-joined any wire: give it a gentle tug and look for a cold or cracked joint.

**Continuity check with a multimeter (unpowered, beep mode).** The HAT's 40-pin socket on the underside carries the same 10 signals, so it makes handy test points. Beep each XIAO pin against its socket pin:

| XIAO pin | 40-pin socket pin | Signal |
|---|---|---|
| 3V3 | 1 | VCC |
| GND | 6 | GND |
| D10 | 19 | DIN |
| D8 | 23 | SCLK |
| D1 | 24 | CS_M |
| D6 | 26 | CS_S |
| D2 | 22 | DC |
| D7 | 11 | RST |
| D4 | 18 | BUSY |
| D5 | 12 | PWR |

Each should beep. Also check that **no** two neighbouring XIAO pins beep to each other (a solder bridge), except GND to GND.

---

## 6. Still stuck?

Send me:
- the Serial Monitor output from one press of **R**, and
- the attributes of `sensor.eink_frame_status`.
