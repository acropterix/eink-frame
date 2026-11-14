# Wiring: XIAO ESP32-S3 → Waveshare 13.3" e-Paper HAT+ (E)

> **Start with the picture guide: open [`wiring.html`](wiring.html) in a browser.** It has diagrams for every step, lets you set your own cable colours, and shows the easy way to solder the battery pads. This file is the text reference.

## Parts
- Seeed XIAO ESP32-S3 (headers soldered)
- Waveshare 13.3" E6 panel + HAT+ driver board, with the panel's ribbon cable plugged into the HAT
- 3.7 V protected LiPo, 3000 mAh
- Momentary push button (tap: next picture; hold 3 s: config mode)
- 2 × 100 kΩ resistors (battery voltage reading; optional)
- Female–female jumper wires

## Adafruit ESP32 Feather V2 (current build)
Hold the Feather with USB-C at the top and the components facing you. Pin labels are printed next to every pin.

| HAT signal | Feather pin | (GPIO) |
|---|---|---|
| VCC  | **3V**  | — |
| GND  | **GND** | — |
| BUSY | **A5**  | 4 |
| PWR  | **A1**  | 25 |
| SCLK | **SCK** | 5 |
| DIN  | **MO**  | 19 |
| RST  | **MI**  | 21 |
| DC   | **27**  | 27 |
| CS_M | **33**  | 33 |
| CS_S | **32**  | 32 |
| Button | **A0** → button → **GND** | 26 |
| Battery | JST socket (2.0 mm PH; adapter for a 1.25 mm plug). Reading built in. | 35 |

Pins avoided on purpose: 12 (must be low at power-on), 13 (red LED), 14/15 (pulse during boot), 34/36/37/39 (input only). The firmware turns off the Feather's NeoPixel/STEMMA power during sleep to save battery.

The sections below are for the **Seeed XIAO ESP32-S3**.

## Display: 10 wires
Use the 10-wire cable that came with the HAT: its small white plug goes into the HAT's 10-pin socket (printed left→right: PWR BUSY RST DC CS_S CS_M SCLK DIN GND VCC) and its jumper ends push onto the XIAO pins. The 40-pin socket on the HAT's underside is only a fallback.

| HAT signal | XIAO pin | (GPIO) | 40-pin header pin, if needed |
|---|---|---|---|
| VCC  | **3V3** | — | 1 (3.3V) |
| GND  | **GND** | — | 6 |
| DIN  | **D10** | GPIO9  | 19 |
| CLK  | **D8**  | GPIO7  | 23 |
| CS_M | **D1**  | GPIO2  | 24 |
| CS_S | **D6**  | GPIO43 | 26 |
| DC   | **D2**  | GPIO3  | 22 |
| RST  | **D7**  | GPIO44 | 11 |
| BUSY | **D4**  | GPIO5  | 18 |
| PWR  | **D5**  | GPIO6  | 12 |

The HAT+ (E) has **no voltage switch**: it runs on 3.3 V only, so there is nothing to set.

## Button
```
D0 (GPIO1) ──── [ button ] ──── GND
```
No resistor is needed; the firmware enables the internal pull-up. A tap wakes the frame and skips to the next picture; holding it 3 s opens config mode.

Leave the HAT's GND jumper on the GND pin and solder the button's GND wire to the same pin from the underside. **Keep the HAT's VCC and GND jumpers short and direct**, with nothing else spliced into them: long power leads make the panel show only yellow and black.

## Config mode: no switch needed
Hold the button for **3 s** to enter config mode; hold it 3 s again (or press Restart on the page) to leave. It also ends by itself after 30 minutes. A toggle switch from D9 to GND still works as an alternative, but isn't needed.

## Battery voltage (optional)
Two resistors, no capacitor. Their outer legs share the battery's solder joints, so the only extra work is the middle wire to D3.
```
BAT+ ──[ 100 kΩ ]──┬──[ 100 kΩ ]── BAT− (GND)
                   │
                   └── D3 (GPIO4)
```
- 220 kΩ + 220 kΩ also works. Don't use 1 MΩ without a 100 nF capacitor from D3 to GND: the reading becomes unreliable.
- Draws about 20 µA (roughly 6% of the battery per year).
- After fitting it, tick **Battery divider fitted on D3** on the config page. Without it, leave that unticked; the frame works and Home Assistant shows the battery as unknown.

## Battery
- Solder a **JST-PH pigtail** (not the battery itself) to the **BAT+ / BAT−** pads on the underside of the XIAO, then plug the battery in. Flux, tin pad and wire separately, tape the wire flat on the pad, touch with the iron for 2 s, then hot-glue for strain relief (pictures in `wiring.html`).
- ⚠️ **Check polarity with a multimeter before soldering.** JST-PH battery leads are not wired the same way by every seller, and reversing them will destroy the XIAO.
- Use a **protected** cell (it has a small circuit board under the tape).
- The XIAO charges the battery over USB-C at about 100 mA, so a fully flat 3000 mAh cell takes more than a day to fill. Topping it up every few months is fine.

## Overall layout
```
        ┌──────────── 13.3" Spectra 6 panel ────────────┐
        │                                               │
        └──────────────────────┬────────────────────────┘
                     ribbon cable (fragile, don't crease)
                               │
                      ┌────────┴────────┐
                      │  HAT+ (E) board │
                      └────────┬────────┘
                         10 jumper wires
                               │
   button ── D0 ┌──────────────┴──┐ BAT+/BAT− ── 3000 mAh LiPo
   divider ── D3│ XIAO ESP32-S3   │
                │                 │
                └───────┬─────────┘
                     USB-C (charging and flashing)
```

## Order of assembly
1. Flash the XIAO on its own and check it joins Wi-Fi (see README).
2. Wire the HAT and panel and run it from USB. You should see a refresh about 20 s after reset.
3. Add the divider, then the battery.
4. **Measure the sleep current:** put a multimeter in series with the battery after the frame goes to sleep. Expect well under 0.1 mA. If it is much higher, the HAT's own idle draw is the likely cause; ask me about adding a MOSFET to switch the HAT's VCC.
5. Mount everything in the frame.
