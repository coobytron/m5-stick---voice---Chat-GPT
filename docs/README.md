# New: Guided breadboard builder

**[Open the guided build](index.html)** (recommended first). It features nine clear stages, highlighted wire endpoints, one wire at a time, zoom, next/back controls, and a persistent wiring reference.

Use [Advanced workshop](workshop.html) only when you need the full exploded diagrams or soldering schematics. The original [rotatable 3D model](assembly.html) remains available.

---

# Updated interactive assembly workshop

**[Open the interactive workshop](workshop.html)** — exploded-view slider, hole-by-hole breadboard map, clickable jumper wiring, circuit schematic, and soldering technique illustrations.

**[Older rotatable 3D study](assembly.html)** — separate geometric 3D study. Its illustrative wire endpoints are **not** the authoritative way to connect the breadboard. Use the workshop's numbered pin-to-hole map for wiring.

## Important physical correction

Your small 30-column solderless breadboard is best used for the XIAO and the record button. Keep the larger Feather Friend beside it. Solder suitable male **headers** to both breakout boards if not already installed. Connect the Feather Friend's header pins with **female-to-male Dupont jumpers** (female to headers, male to breadboard). Your existing male-to-male jumpers are suitable for connections entirely within the breadboard, including rails and switch. **No jumper wires are soldered.** This is a proposed plan, not a tested final circuit.

After prototyping, a separate solderable **perfboard** with female sockets and insulated soldered wiring provides a permanent alternative.

---

# XIAO ESP32-S3 Voice Recorder — Interactive 3D Assembly

[Open the interactive 3D viewer](./index.html)

This folder is a **separate hardware concept** using the **Seeed Studio XIAO ESP32-S3 Sense + Adafruit E-Ink Feather Friend**. The repository's existing firmware targets the M5StickC Plus v1.1 and is **not compatible** with this XIAO design without porting.

## Three interactive stages

1. **Solder headers:** Shows the XIAO over two seven-pin headers aligned in a breadboard.
2. **Breadboard:** Shows the XIAO + Sense expansion board, record button, Feather Friend microSD, color-coded signal wires, and e-ink display.
3. **Perfboard:** Conceptual move onto a permanent solderable perfboard, with a removable XIAO and battery placeholder.

**Controls:** Drag to rotate, pinch or scroll to zoom, right-drag or two-finger pan, reset camera, show/hide wires, and **Export 3D (.glb)** to download the currently selected approximate assembly for Blender.

This is **illustrative geometry**, not dimension-calibrated CAD. Hole locations, pin headers and cable routes need measurement before fabrication. No functional circuit, firmware, or enclosure fit has been validated.

## Proposed electrical connections

| XIAO ESP32-S3 | Feather Friend | Function |
| --- | --- | --- |
| 3V3 | 3V | Regulated 3.3V |
| GND | GND | Common ground |
| D8 / GPIO7 | SCK | Shared SPI clock |
| D9 / GPIO8 | MISO | SPI data from microSD |
| D10 / GPIO9 | MOSI | SPI data to e-ink and microSD |
| D0 / GPIO1 | ECS | E-ink chip select |
| D1 / GPIO2 | D/C | Display data/command |
| D3 / GPIO4 | SDCS | Feather Friend microSD select |
| 3V3 | SRCS | Disable unused SRAM |
| D4 / GPIO5 | Button → GND | Record input; use INPUT_PULLUP |

The **microSD slot on the Feather Friend** is the proposed WAV storage. The XIAO Sense PDM microphone uses internal GPIO41/42. Names like D5, D6, D9, and D10 printed on the Feather Friend may refer to **Feather connector positions**, not matching XIAO pin numbers. Match the signal labels and verify with the manufacturer documentation.

## Soldering workflow

1. With USB and battery disconnected, use a solderless breadboard to hold two 7-pin male headers straight, insert the XIAO, then solder all 14 plated pads to the pins.
2. Solder male headers (or permanent wires) to the Feather Friend. Unsoldered holes will not make dependable electrical connections.
3. Plug the boards into a breadboard and test each subsystem using **USB-C power**. The PowerBoost is unnecessary for this prototype.
4. When working, transfer to a **2.54 mm copper-pad perfboard**. Solder two 7-pin **female sockets** and insulated point-to-point wires, keeping the XIAO removable.
5. Verify electrical continuity, 3.3V/GND isolation and LiPo polarity before powering the assembled design. Only connect a compatible protected 3.7V LiPo after USB tests pass.

**Never solder the solderless breadboard itself.** The final board uses copper pads and solder.

## Publish the model

### GitHub Pages

- Open repository **Settings → Pages**
- Choose **Deploy from a branch**
- Choose **feature/xiao-recorder-3d-assembly** and **/docs**
- Save, then use the GitHub Pages URL GitHub displays

If another Pages site already exists for this repo, publish this branch through a separate static host or avoid replacing the established Pages configuration.

### Local development

From the repo directory, check out the feature branch and run:

    python3 -m http.server 8000 -d docs

Open **http://localhost:8000/** in a browser.

The page has no build system; it loads pinned Three.js modules from jsDelivr, so internet access is required. For a Blender-compatible approximate 3D file, select the assembly stage and click **Export 3D (.glb)**.

## Manufacturer documentation

- [Adafruit eInk Feather Friend / pinouts](https://learn.adafruit.com/adafruit-2-13-eink-display-breakouts-and-featherwings/pinouts)
- [Seeed XIAO ESP32-S3 pin multiplexing](https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/)
- [Seeed XIAO ESP32-S3 Sense microphone guide](https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/)

**This pin plan is untested.** Validate wiring, board revisions, and software SPI/SD initialization before connecting power.
